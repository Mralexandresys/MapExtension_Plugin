#include "map_state_full_scan.h"

#include "map_state_capture.h"
#include "map_state_types.h"
#include "plugin_helpers.h"

#include "Chimera_classes.hpp"
#include "Engine_classes.hpp"
#include "Engine_structs.hpp"
#include "MassLOD_classes.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace
{
	using MapStateFullScan::Phase;
	using MapStateFullScan::Mode;
	using MapStateFullScan::Status;

	// Keep player/Mass observation centers close enough that their high-detail
	// ranges overlap as well as the 25,000-unit World Partition source. Extend
	// one full step beyond the calibrated map bounds so edge cells are covered.
	constexpr float kStreamingRadius = 25000.0f;
	constexpr float kGridStep = 20000.0f;
	constexpr float kGridBoundaryPadding = 20000.0f;
	// Give World Partition and PCG several game-thread ticks after each move.
	// IsStreamingCompleted() can briefly report the previous source position.
	constexpr float kMinimumStreamingWaitSeconds = 2.0f;
	constexpr float kStreamingTimeoutSeconds = 60.0f;
	constexpr float kMinimumPcgWaitSeconds = 6.0f;
	constexpr float kPcgObservationQuietSeconds = 4.0f;
	constexpr float kPcgObservationTimeoutSeconds = 30.0f;
	constexpr float kCaptureTimeoutSeconds = 20.0f;
	constexpr float kRestorationMinimumWaitSeconds = 3.0f;
	constexpr float kRestorationTimeoutSeconds = 60.0f;
	constexpr size_t kWaveTimerStopWavesOffset = 0x83;

	struct RuntimeState
	{
		SDK::UWorld* World = nullptr;
		SDK::AActor* SourceActor = nullptr;
		SDK::UWorldPartitionStreamingSourceComponent* SourceComponent = nullptr;
		std::vector<SDK::FVector> Grid;
		size_t GridIndex = 0;
		size_t TimedOutZones = 0;
		int StartingPlantCount = 0;
		uint64_t StartingObservationEventCount = 0;
		int LiveTrackedPlantObservations = 0;
		Mode CurrentMode = Mode::MassSource;
		SDK::UMassLODSubsystem* MassLodSubsystem = nullptr;
		bool OriginalGatherStreamingSources = false;
		bool OriginalAllowNonPlayerViewers = false;
		bool MassSettingsChanged = false;
		SDK::APlayerController* PlayerController = nullptr;
		SDK::ACharacter* PlayerCharacter = nullptr;
		SDK::FVector OriginalPlayerLocation{};
		SDK::FRotator OriginalPlayerRotation{};
		bool OriginalPlayerCollision = true;
		bool PlayerMoveInputWasIgnored = false;
		SDK::EMovementMode OriginalMovementMode = SDK::EMovementMode::MOVE_Walking;
		uint8_t OriginalCustomMovementMode = 0;
		bool PlayerStateCaptured = false;
		SDK::UCrSettingsShared* SharedSettings = nullptr;
		bool OriginalAutoSaveEnabled = true;
		bool AutoSaveChanged = false;
		SDK::UCrEnviroWaveSubsystem* WaveSubsystem = nullptr;
		SDK::UCrEnviroWaveTimerSubsystem* WaveTimerSubsystem = nullptr;
		bool ActiveWavePausedForScan = false;
		bool WaveTimerDisabledForScan = false;
		float OriginalWaveTimerRemainingSeconds = 0.0f;
		int OriginalWaveTimerPhase = 0;
		float PhaseElapsedSeconds = 0.0f;
		float ObservationQuietSeconds = 0.0f;
		uint64_t ZoneObservationBaselineRevision = 0;
		uint64_t ZoneObservationBaselineEventCount = 0;
		uint64_t LastObservationEventCount = 0;
		int ZoneObservedPlantBaselineCount = 0;
		bool ZoneSawObservationEvent = false;
		int64_t CaptureBaselineUnixMs = 0;
		Phase CurrentPhase = Phase::Idle;
		Phase PendingFinalPhase = Phase::Idle;
		std::string PendingFinalMessage;
		std::string Message;
	};

	RuntimeState g_runtime{};
	std::atomic<bool> g_startRequested = false;
	std::atomic<bool> g_cancelRequested = false;
	std::mutex g_statusMutex;
	Status g_status{};

	bool IsActivePhase(Phase phase)
	{
		switch (phase)
		{
		case Phase::CreatingSource:
		case Phase::Moving:
		case Phase::WaitingForStreaming:
		case Phase::SettlingPcg:
		case Phase::Capturing:
		case Phase::Restoring:
			return true;
		default:
			return false;
		}
	}

	void PublishStatus()
	{
		const MapStateRuntime::Detail::PoiScanInfo poiInfo =
			MapStateRuntime::Detail::GetPoiScanInfo();

		std::lock_guard<std::mutex> lock(g_statusMutex);
		g_status.CurrentPhase = g_runtime.CurrentPhase;
		g_status.CurrentMode = g_runtime.CurrentMode;
		g_status.CurrentZone = g_runtime.Grid.empty()
			? 0
			: std::min(g_runtime.GridIndex + 1, g_runtime.Grid.size());
		g_status.TotalZones = g_runtime.Grid.size();
		g_status.TimedOutZones = g_runtime.TimedOutZones;
		g_status.StartingPlantCount = g_runtime.StartingPlantCount;
		g_status.CurrentPlantCount = poiInfo.PlantCount;
		g_status.LiveTrackedPlantObservations = g_runtime.LiveTrackedPlantObservations;
		g_status.Message = g_runtime.Message;
	}

	SDK::FTransform MakeTransform(const SDK::FVector& location)
	{
		SDK::FTransform transform{};
		transform.Rotation.W = 1.0;
		transform.Translation = location;
		transform.Scale3D.X = 1.0;
		transform.Scale3D.Y = 1.0;
		transform.Scale3D.Z = 1.0;
		return transform;
	}

	std::vector<SDK::FVector> BuildScanGrid(float z)
	{
		using namespace MapStateRuntime::Detail;

		const float minX = std::min(kMapSrcX1, kMapSrcX2) - kGridBoundaryPadding;
		const float maxX = std::max(kMapSrcX1, kMapSrcX2) + kGridBoundaryPadding;
		const float minY = std::min(kMapSrcY1, kMapSrcY2) - kGridBoundaryPadding;
		const float maxY = std::max(kMapSrcY1, kMapSrcY2) + kGridBoundaryPadding;
		const int columns = std::max(2, static_cast<int>(std::ceil((maxX - minX) / kGridStep)) + 1);
		const int rows = std::max(2, static_cast<int>(std::ceil((maxY - minY) / kGridStep)) + 1);

		std::vector<SDK::FVector> grid;
		grid.reserve(static_cast<size_t>(columns * rows));
		for (int row = 0; row < rows; ++row)
		{
			const float y = minY + (maxY - minY) * static_cast<float>(row) / static_cast<float>(rows - 1);
			for (int columnOffset = 0; columnOffset < columns; ++columnOffset)
			{
				const int column = (row % 2 == 0)
					? columnOffset
					: columns - 1 - columnOffset;
				SDK::FVector location{};
				location.X = minX + (maxX - minX) * static_cast<float>(column) / static_cast<float>(columns - 1);
				location.Y = y;
				location.Z = z;
				grid.push_back(location);
			}
		}
		LOG_INFO(
			"Full-map plant scan grid bounds x=[%.0f,%.0f] y=[%.0f,%.0f] "
			"step<=%.0f zones=%zu",
			static_cast<double>(minX),
			static_cast<double>(maxX),
			static_cast<double>(minY),
			static_cast<double>(maxY),
			static_cast<double>(kGridStep),
			grid.size());
		return grid;
	}

	template <typename TSubsystem>
	TSubsystem* ResolveWorldSubsystem(SDK::UWorld* world)
	{
		if (!world)
		{
			return nullptr;
		}
		SDK::USubsystem* subsystem = SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(
			world,
			TSubsystem::StaticClass());
		if (!subsystem || !subsystem->IsA(TSubsystem::StaticClass()))
		{
			return nullptr;
		}
		return static_cast<TSubsystem*>(subsystem);
	}

	bool IsWaveTimerStopped(const SDK::UCrEnviroWaveTimerSubsystem* subsystem)
	{
		static_assert(sizeof(SDK::UCrEnviroWaveTimerSubsystem) > kWaveTimerStopWavesOffset);
		if (!subsystem)
		{
			return true;
		}
		// bStopWaves is not reflected in the generated SDK, but its offset is
		// validated against the current class size and documented by the game dump.
		const auto* bytes = reinterpret_cast<const uint8_t*>(subsystem);
		return bytes[kWaveTimerStopWavesOffset] != 0;
	}

	bool LockRuptureCycle(SDK::UWorld* world)
	{
		g_runtime.WaveSubsystem = ResolveWorldSubsystem<SDK::UCrEnviroWaveSubsystem>(world);
		if (g_runtime.WaveSubsystem && g_runtime.WaveSubsystem->IsWaveInProgress())
		{
			if (g_runtime.WaveSubsystem->IsWavePaused())
			{
				LOG_INFO("Full-map plant scan found an already paused rupture wave");
				return true;
			}
			g_runtime.WaveSubsystem->PauseCurrentWave();
			if (!g_runtime.WaveSubsystem->IsWavePaused())
			{
				LOG_WARN("Full-map plant scan could not pause the active rupture wave");
				return false;
			}
			g_runtime.ActiveWavePausedForScan = true;
			LOG_INFO("Full-map plant scan paused the active rupture wave");
			return true;
		}

		g_runtime.WaveTimerSubsystem =
			ResolveWorldSubsystem<SDK::UCrEnviroWaveTimerSubsystem>(world);
		if (!g_runtime.WaveTimerSubsystem)
		{
			LOG_WARN("Full-map plant scan could not resolve the rupture timer subsystem");
			return false;
		}
		if (IsWaveTimerStopped(g_runtime.WaveTimerSubsystem))
		{
			LOG_INFO("Full-map plant scan found an already disabled rupture timer");
			return true;
		}

		if (SDK::ACrWaveTimerActor* timerActor = g_runtime.WaveTimerSubsystem->TimerActor)
		{
			const double nowSeconds = SDK::UGameplayStatics::GetTimeSeconds(world);
			g_runtime.OriginalWaveTimerRemainingSeconds = static_cast<float>(
				std::max(0.0, static_cast<double>(timerActor->NextTime) - nowSeconds));
			g_runtime.OriginalWaveTimerPhase = timerActor->NextPhase;
		}
		g_runtime.WaveTimerSubsystem->WavesActive(false);
		g_runtime.WaveTimerDisabledForScan = true;
		if (!IsWaveTimerStopped(g_runtime.WaveTimerSubsystem))
		{
			LOG_WARN("Full-map plant scan could not disable the rupture timer");
			return false;
		}
		LOG_INFO(
			"Full-map plant scan locked the rupture timer with %.1f second(s) remaining",
			static_cast<double>(g_runtime.OriginalWaveTimerRemainingSeconds));
		return true;
	}

	void RestoreRuptureCycle()
	{
		if (g_runtime.ActiveWavePausedForScan && g_runtime.WaveSubsystem)
		{
			if (g_runtime.WaveSubsystem->IsWavePaused())
			{
				g_runtime.WaveSubsystem->ResumeCurrentWave();
			}
			LOG_INFO("Full-map plant scan resumed the rupture wave");
		}
		g_runtime.ActiveWavePausedForScan = false;
		g_runtime.WaveSubsystem = nullptr;

		if (g_runtime.WaveTimerDisabledForScan && g_runtime.WaveTimerSubsystem)
		{
			g_runtime.WaveTimerSubsystem->WavesActive(true);
			SDK::ACrWaveTimerActor* timerActor = g_runtime.WaveTimerSubsystem->TimerActor;
			if (timerActor && !timerActor->IsActorBeingDestroyed())
			{
				const double nowSeconds = SDK::UGameplayStatics::GetTimeSeconds(g_runtime.World);
				timerActor->NextPhase = g_runtime.OriginalWaveTimerPhase;
				timerActor->NextTime = static_cast<float>(
					nowSeconds + g_runtime.OriginalWaveTimerRemainingSeconds);
				timerActor->OnRep_Phase();
				timerActor->OnRep_Time();
			}
			LOG_INFO(
				"Full-map plant scan restored the rupture timer with %.1f second(s) remaining",
				static_cast<double>(g_runtime.OriginalWaveTimerRemainingSeconds));
		}
		g_runtime.WaveTimerDisabledForScan = false;
		g_runtime.WaveTimerSubsystem = nullptr;
	}

	void RestorePlayer()
	{
		if (g_runtime.AutoSaveChanged && g_runtime.SharedSettings)
		{
			g_runtime.SharedSettings->bEnableAutoSave =
				g_runtime.OriginalAutoSaveEnabled;
			g_runtime.AutoSaveChanged = false;
			g_runtime.SharedSettings = nullptr;
		}
		if (!g_runtime.PlayerStateCaptured || !g_runtime.PlayerCharacter)
		{
			return;
		}
		if (!g_runtime.PlayerCharacter->IsActorBeingDestroyed())
		{
			g_runtime.PlayerCharacter->K2_SetActorLocationAndRotation(
				g_runtime.OriginalPlayerLocation,
				g_runtime.OriginalPlayerRotation,
				false,
				nullptr,
				true);
			g_runtime.PlayerCharacter->SetActorEnableCollision(
				g_runtime.OriginalPlayerCollision);
			if (g_runtime.PlayerCharacter->CharacterMovement)
			{
				g_runtime.PlayerCharacter->CharacterMovement->SetMovementMode(
					g_runtime.OriginalMovementMode,
					g_runtime.OriginalCustomMovementMode);
			}
		}
		if (g_runtime.PlayerController && !g_runtime.PlayerMoveInputWasIgnored)
		{
			g_runtime.PlayerController->SetIgnoreMoveInput(false);
		}
		LOG_INFO("Full-map plant scan restored the local player");
		g_runtime.PlayerStateCaptured = false;
		g_runtime.PlayerCharacter = nullptr;
		g_runtime.PlayerController = nullptr;
	}

	void RestoreMassLodSettings()
	{
		if (!g_runtime.MassSettingsChanged || !g_runtime.MassLodSubsystem)
		{
			return;
		}
		g_runtime.MassLodSubsystem->bGatherStreamingSources =
			g_runtime.OriginalGatherStreamingSources;
		g_runtime.MassLodSubsystem->bAllowNonPlayerViwerActors =
			g_runtime.OriginalAllowNonPlayerViewers;
		LOG_INFO("Full-map plant scan restored Mass LOD settings");
		g_runtime.MassSettingsChanged = false;
		g_runtime.MassLodSubsystem = nullptr;
	}

	void DestroySource()
	{
		RestoreRuptureCycle();
		RestorePlayer();
		RestoreMassLodSettings();
		if (g_runtime.SourceComponent)
		{
			g_runtime.SourceComponent->DisableStreamingSource();
			g_runtime.SourceComponent = nullptr;
		}
		if (g_runtime.SourceActor)
		{
			g_runtime.SourceActor->K2_DestroyActor();
			g_runtime.SourceActor = nullptr;
		}
	}

	bool EnableMassLodSource(SDK::UWorld* world)
		{
			auto* subsystem = static_cast<SDK::UMassLODSubsystem*>(
				SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(
					world,
					SDK::UMassLODSubsystem::StaticClass()));
			if (!subsystem)
			{
				LOG_WARN("Full-map plant scan could not resolve MassLODSubsystem");
				return false;
			}
			g_runtime.MassLodSubsystem = subsystem;
			g_runtime.OriginalGatherStreamingSources =
				subsystem->bGatherStreamingSources;
			g_runtime.OriginalAllowNonPlayerViewers =
				subsystem->bAllowNonPlayerViwerActors;
			subsystem->bGatherStreamingSources = true;
			subsystem->bAllowNonPlayerViwerActors = true;
			g_runtime.MassSettingsChanged = true;
			LOG_INFO(
				"Full-map plant scan enabled Mass LOD non-player viewers "
				"(viewers=%d registered_actor_viewers=%d)",
				subsystem->Viewers.Num(),
				subsystem->RegisteredActorViewers.Num());
			return true;
		}

		bool BeginPlayerFallback()
		{
			SDK::APlayerController* controller =
				SDK::UGameplayStatics::GetPlayerController(g_runtime.World, 0);
			SDK::ACharacter* character =
				SDK::UGameplayStatics::GetPlayerCharacter(g_runtime.World, 0);
			if (!controller || !character)
			{
				return false;
			}

			g_runtime.PlayerController = controller;
			g_runtime.PlayerCharacter = character;
			g_runtime.OriginalPlayerLocation = character->K2_GetActorLocation();
			g_runtime.OriginalPlayerRotation = character->K2_GetActorRotation();
			g_runtime.OriginalPlayerCollision = character->GetActorEnableCollision();
			g_runtime.PlayerMoveInputWasIgnored = controller->IsMoveInputIgnored();
			if (character->CharacterMovement)
			{
				g_runtime.OriginalMovementMode =
					character->CharacterMovement->MovementMode;
				g_runtime.OriginalCustomMovementMode =
					character->CharacterMovement->CustomMovementMode;
				character->CharacterMovement->StopMovementImmediately();
				character->CharacterMovement->DisableMovement();
			}
			if (!g_runtime.PlayerMoveInputWasIgnored)
			{
				controller->SetIgnoreMoveInput(true);
			}
			character->SetActorEnableCollision(false);
			if (controller->Player
				&& controller->Player->IsA(SDK::UCrLocalPlayer::StaticClass()))
			{
				auto* localPlayer =
					static_cast<SDK::UCrLocalPlayer*>(controller->Player);
				SDK::UCrSettingsShared* sharedSettings =
					localPlayer->GetSharedSettings();
				if (sharedSettings)
				{
					g_runtime.SharedSettings = sharedSettings;
					g_runtime.OriginalAutoSaveEnabled =
						sharedSettings->bEnableAutoSave;
					sharedSettings->bEnableAutoSave = false;
					g_runtime.AutoSaveChanged = true;
				}
			}
			g_runtime.PlayerStateCaptured = true;
			g_runtime.CurrentMode = Mode::PlayerFallback;
			g_runtime.GridIndex = 0;
			g_runtime.LiveTrackedPlantObservations = 0;
			g_runtime.TimedOutZones = 0;
			g_runtime.PhaseElapsedSeconds = 0.0f;
			g_runtime.CurrentPhase = Phase::Moving;
			g_runtime.Message =
				"Parcours joueur actif : position originale sauvegardee";
			LOG_WARN(
				"Full-map plant scan is using the local player as the "
				"authoritative World Partition/Mass viewer");
			PublishStatus();
			return true;
		}
	void CompleteFinish(Phase phase, const char* message)
	{
		DestroySource();
		g_runtime.CurrentPhase = phase;
		g_runtime.Message = message ? message : "";
		g_runtime.PhaseElapsedSeconds = 0.0f;
		PublishStatus();
	}

	void Finish(Phase phase, const char* message)
	{
		if (g_runtime.CurrentPhase == Phase::Restoring)
		{
			g_runtime.PendingFinalPhase = phase;
			g_runtime.PendingFinalMessage = message ? message : "";
			return;
		}
		if (g_runtime.PlayerStateCaptured && g_runtime.SourceActor)
		{
			g_runtime.PendingFinalPhase = phase;
			g_runtime.PendingFinalMessage = message ? message : "";
			g_runtime.SourceActor->K2_SetActorLocation(
				g_runtime.OriginalPlayerLocation,
				false,
				nullptr,
				true);
			g_runtime.CurrentPhase = Phase::Restoring;
			g_runtime.Message = "Rechargement de la zone d'origine du joueur";
			g_runtime.PhaseElapsedSeconds = 0.0f;
			PublishStatus();
			return;
		}
		CompleteFinish(phase, message);
	}

	bool IsDedicatedSession(SDK::UWorld* world)
	{
		SDK::AGameStateBase* baseState = SDK::UGameplayStatics::GetGameState(world);
		SDK::ACrGameStateBase* gameState = static_cast<SDK::ACrGameStateBase*>(baseState);
		return gameState && gameState->bIsDedicatedServer;
	}

	bool CreateStreamingSource(SDK::UWorld* world)
	{
		SDK::FVector initialLocation{};
		if (!g_runtime.Grid.empty())
		{
			initialLocation = g_runtime.Grid.front();
		}
		const SDK::FTransform transform = MakeTransform(initialLocation);
		SDK::AActor* actor = SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(
			world,
			// TargetPoint provides a native root component, so moving the actor
			// reliably updates the streaming source's owner location.
			SDK::ATargetPoint::StaticClass(),
			transform,
			SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			nullptr,
			SDK::ESpawnActorScaleMethod::OverrideRootScale);
		if (!actor)
		{
			return false;
		}

		actor = SDK::UGameplayStatics::FinishSpawningActor(
			actor,
			transform,
			SDK::ESpawnActorScaleMethod::OverrideRootScale);
		if (!actor)
		{
			return false;
		}
		actor->SetActorHiddenInGame(true);
		actor->SetActorEnableCollision(false);
		actor->SetReplicates(false);

		SDK::UActorComponent* componentBase = actor->AddComponentByClass(
			SDK::UWorldPartitionStreamingSourceComponent::StaticClass(),
			false,
			MakeTransform(SDK::FVector{}),
			false);
		auto* component =
			static_cast<SDK::UWorldPartitionStreamingSourceComponent*>(componentBase);
		if (!component)
		{
			actor->K2_DestroyActor();
			return false;
		}

		SDK::FStreamingSourceShape shape{};
		shape.bUseGridLoadingRange = false;
		shape.LoadingRangeScale = 1.0f;
		shape.Radius = kStreamingRadius;
		component->Shapes.Add(shape);
		component->TargetBehavior = SDK::EStreamingSourceTargetBehavior::Include;
		component->Priority = SDK::EStreamingSourcePriority::Normal;
		component->TargetState = SDK::EStreamingSourceTargetState::Activated;
		component->EnableStreamingSource();

		g_runtime.SourceActor = actor;
		g_runtime.SourceComponent = component;
		return true;
	}

	void Start(SDK::UWorld* world)
	{
		if (!world)
		{
			Finish(Phase::Failed, "Aucun monde charge");
			return;
		}
		if (IsDedicatedSession(world))
		{
			Finish(Phase::Failed, "Le scan complet est disponible uniquement en solo/local");
			return;
		}

		const MapStateRuntime::Detail::PoiScanInfo poiInfo =
			MapStateRuntime::Detail::GetPoiScanInfo();
		if (poiInfo.WorldName.empty())
		{
			Finish(Phase::Failed, "Le monde Chimera n'est pas encore pret");
			return;
		}

		g_runtime = RuntimeState{};
		g_runtime.World = world;
		g_runtime.StartingPlantCount = poiInfo.PlantCount;
		g_runtime.StartingObservationEventCount = poiInfo.ObservationEventCount;
		g_runtime.Grid = BuildScanGrid(0.0f);
		g_runtime.CurrentMode = Mode::PlayerFallback;
		if (!LockRuptureCycle(world))
		{
			Finish(Phase::Failed, "Impossible de verrouiller le timer de rupture");
			return;
		}
		EnableMassLodSource(world);
		g_runtime.CurrentPhase = Phase::CreatingSource;
		g_runtime.Message = "Creation de la source World Partition";
		PublishStatus();
	}

	void AdvanceZone()
	{
		++g_runtime.GridIndex;
		g_runtime.PhaseElapsedSeconds = 0.0f;
		if (g_runtime.GridIndex >= g_runtime.Grid.size())
		{
			const bool sawPlantBeginPlay =
				MapStateRuntime::Detail::GetPoiScanInfo().ObservationEventCount
					> g_runtime.StartingObservationEventCount;
			if (!sawPlantBeginPlay)
			{
				Finish(
					g_runtime.StartingPlantCount == 0 ? Phase::Failed : Phase::CompletedPartial,
					"Scan termine sans nouvel evenement plante; couverture non prouvee");
			}
			else if (g_runtime.TimedOutZones == 0)
			{
				Finish(Phase::Completed, "Scan complet termine");
			}
			else
			{
				Finish(Phase::CompletedPartial, "Scan termine avec des zones expirees");
			}
			return;
		}
		g_runtime.CurrentPhase = Phase::Moving;
		g_runtime.Message = "Deplacement vers la zone suivante";
		PublishStatus();
	}
}

namespace MapStateFullScan
{
	void RequestStart()
	{
		g_startRequested.store(true);
	}

	void RequestCancel()
	{
		g_cancelRequested.store(true);
	}

	Status CopyStatus()
	{
		std::lock_guard<std::mutex> lock(g_statusMutex);
		return g_status;
	}

	bool IsActive()
	{
		std::lock_guard<std::mutex> lock(g_statusMutex);
		return IsActivePhase(g_status.CurrentPhase);
	}

	void Tick(SDK::UWorld* world, float deltaSeconds)
	{
		if (g_cancelRequested.exchange(false) && IsActivePhase(g_runtime.CurrentPhase))
		{
			Finish(Phase::Cancelled, "Scan annule; les zones deja capturees restent sauvegardees");
			return;
		}

		if (g_startRequested.exchange(false))
		{
			if (!IsActivePhase(g_runtime.CurrentPhase))
			{
				Start(world);
			}
		}

		if (!IsActivePhase(g_runtime.CurrentPhase))
		{
			return;
		}
		if (!world || world != g_runtime.World)
		{
			// The ending-world callback owns cleanup while its pointer is valid.
			g_runtime.SourceActor = nullptr;
			g_runtime.SourceComponent = nullptr;
			g_runtime.CurrentPhase = Phase::Failed;
			g_runtime.Message = "Le monde a change pendant le scan";
			PublishStatus();
			return;
		}

		const float safeDelta = (deltaSeconds > 0.0f && deltaSeconds < 5.0f)
			? deltaSeconds
			: 0.0f;
		g_runtime.PhaseElapsedSeconds += safeDelta;

		switch (g_runtime.CurrentPhase)
		{
		case Phase::CreatingSource:
			if (!CreateStreamingSource(world))
			{
				Finish(Phase::Failed, "Impossible de creer la source World Partition");
				break;
			}
			if (!BeginPlayerFallback())
			{
				Finish(Phase::Failed, "Le joueur local est indisponible pour le scan");
			}
			break;

		case Phase::Moving:
			if (!g_runtime.SourceActor || g_runtime.GridIndex >= g_runtime.Grid.size())
			{
				Finish(Phase::Failed, "Etat de parcours invalide");
				break;
			}
			{
				const MapStateRuntime::Detail::PoiScanInfo poiInfo =
					MapStateRuntime::Detail::GetPoiScanInfo();
				g_runtime.ZoneObservationBaselineRevision = poiInfo.ObservationRevision;
				g_runtime.ZoneObservationBaselineEventCount = poiInfo.ObservationEventCount;
				g_runtime.LastObservationEventCount = poiInfo.ObservationEventCount;
				g_runtime.ZoneObservedPlantBaselineCount = poiInfo.ObservedPlantCount;
				g_runtime.ZoneSawObservationEvent = false;
				g_runtime.ObservationQuietSeconds = 0.0f;
			}
			g_runtime.SourceActor->K2_SetActorLocation(
				g_runtime.Grid[g_runtime.GridIndex],
				false,
				nullptr,
				true);
			if (g_runtime.CurrentMode == Mode::PlayerFallback)
			{
				if (!g_runtime.PlayerCharacter
					|| g_runtime.PlayerCharacter->IsActorBeingDestroyed())
				{
					Finish(Phase::Failed, "Le joueur local est devenu indisponible");
					break;
				}
				SDK::FVector playerLocation = g_runtime.Grid[g_runtime.GridIndex];
				playerLocation.Z = g_runtime.OriginalPlayerLocation.Z;
				g_runtime.PlayerCharacter->K2_SetActorLocation(
					playerLocation,
					false,
					nullptr,
					true);
			}
			g_runtime.CurrentPhase = Phase::WaitingForStreaming;
			g_runtime.Message = "Chargement de la zone";
			g_runtime.PhaseElapsedSeconds = 0.0f;
			PublishStatus();
			break;

		case Phase::WaitingForStreaming:
			if (g_runtime.PhaseElapsedSeconds >= kMinimumStreamingWaitSeconds
				&& g_runtime.SourceComponent
				&& g_runtime.SourceComponent->IsStreamingCompleted())
			{
				g_runtime.CurrentPhase = Phase::SettlingPcg;
				g_runtime.Message = "Generation PCG";
				g_runtime.PhaseElapsedSeconds = 0.0f;
				PublishStatus();
			}
			else if (g_runtime.PhaseElapsedSeconds >= kStreamingTimeoutSeconds)
			{
				++g_runtime.TimedOutZones;
				LOG_WARN(
					"Full-map plant scan timed out at zone %zu/%zu",
					g_runtime.GridIndex + 1,
					g_runtime.Grid.size());
				AdvanceZone();
			}
			break;

		case Phase::SettlingPcg:
			{
				const MapStateRuntime::Detail::PoiScanInfo poiInfo =
					MapStateRuntime::Detail::GetPoiScanInfo();
				if (poiInfo.ObservationEventCount != g_runtime.LastObservationEventCount)
				{
					g_runtime.LastObservationEventCount = poiInfo.ObservationEventCount;
					g_runtime.ZoneSawObservationEvent =
						poiInfo.ObservationEventCount > g_runtime.ZoneObservationBaselineEventCount;
					g_runtime.ObservationQuietSeconds = 0.0f;
				}
				else
				{
					g_runtime.ObservationQuietSeconds += safeDelta;
				}

				const bool observationsSettled =
					g_runtime.ZoneSawObservationEvent
					&& g_runtime.PhaseElapsedSeconds >= kMinimumPcgWaitSeconds
					&& g_runtime.ObservationQuietSeconds >= kPcgObservationQuietSeconds;
				const bool observationTimedOut =
					g_runtime.PhaseElapsedSeconds >= kPcgObservationTimeoutSeconds;
				if (observationsSettled || observationTimedOut)
				{
					if (observationTimedOut)
					{
						++g_runtime.TimedOutZones;
						LOG_WARN(
							"Full-map plant scan PCG observation timeout at zone %zu/%zu",
							g_runtime.GridIndex + 1,
							g_runtime.Grid.size());
					}
					g_runtime.CaptureBaselineUnixMs = poiInfo.LastScanAtUnixMs;
					MapStateRuntime::Detail::ForcePoiRescan();
					g_runtime.CurrentPhase = Phase::Capturing;
					g_runtime.Message = "Capture et sauvegarde des plantes";
					g_runtime.PhaseElapsedSeconds = 0.0f;
					PublishStatus();
				}
			}
			break;

		case Phase::Capturing:
			// ForcePoiRescan resets the timestamp to zero before scheduling the
			// refresh. Require a strictly newer timestamp so that reset cannot
			// be mistaken for a completed capture when refresh is deferred.
			if (MapStateRuntime::Detail::GetPoiScanInfo().LastScanAtUnixMs
				> g_runtime.CaptureBaselineUnixMs)
			{
				const MapStateRuntime::Detail::PoiScanInfo poiInfo =
					MapStateRuntime::Detail::GetPoiScanInfo();
				const uint64_t zoneBeginPlayEvents =
					poiInfo.ObservationEventCount >= g_runtime.ZoneObservationBaselineEventCount
						? poiInfo.ObservationEventCount - g_runtime.ZoneObservationBaselineEventCount
						: 0;
				const int zoneNewUniquePlants = std::max(
					0,
					poiInfo.ObservedPlantCount - g_runtime.ZoneObservedPlantBaselineCount);
				g_runtime.LiveTrackedPlantObservations +=
					static_cast<int>(zoneBeginPlayEvents);
				const SDK::FVector& target = g_runtime.Grid[g_runtime.GridIndex];
				LOG_INFO(
					"Full-map plant scan zone %zu/%zu mode=player "
					"target=(%.0f,%.0f,%.0f) revision=%llu->%llu "
					"begin_play_events=%llu new_unique_plants=%d "
					"gatherables=%d live_tracked_plants=%d catalog=%d",
					g_runtime.GridIndex + 1,
					g_runtime.Grid.size(),
					static_cast<double>(target.X),
					static_cast<double>(target.Y),
					static_cast<double>(target.Z),
					static_cast<unsigned long long>(g_runtime.ZoneObservationBaselineRevision),
					static_cast<unsigned long long>(poiInfo.ObservationRevision),
					static_cast<unsigned long long>(zoneBeginPlayEvents),
					zoneNewUniquePlants,
					poiInfo.LastGatherableActorCount,
					poiInfo.LastLiveTrackedPlantActorCount,
					poiInfo.PlantCount);
				AdvanceZone();
			}
			else if (g_runtime.PhaseElapsedSeconds >= kCaptureTimeoutSeconds)
			{
				++g_runtime.TimedOutZones;
				LOG_WARN(
					"Full-map plant scan capture timed out at zone %zu/%zu",
					g_runtime.GridIndex + 1,
					g_runtime.Grid.size());
				AdvanceZone();
			}
			break;

		case Phase::Restoring:
			if ((g_runtime.PhaseElapsedSeconds >= kRestorationMinimumWaitSeconds
					&& g_runtime.SourceComponent
					&& g_runtime.SourceComponent->IsStreamingCompleted())
				|| g_runtime.PhaseElapsedSeconds >= kRestorationTimeoutSeconds)
			{
				if (g_runtime.PhaseElapsedSeconds >= kRestorationTimeoutSeconds)
				{
					LOG_WARN("Full-map plant scan timed out reloading the player's original zone");
				}
				const Phase finalPhase = g_runtime.PendingFinalPhase;
				const std::string finalMessage = g_runtime.PendingFinalMessage;
				CompleteFinish(finalPhase, finalMessage.c_str());
			}
			break;

		default:
			break;
		}
	}

	void Shutdown(SDK::UWorld* world)
	{
		if (!world || world == g_runtime.World)
		{
			// Player/autosave/Mass restoration must not depend on the temporary
			// streaming actor still existing.
			DestroySource();
		}
		else
		{
			g_runtime.SourceActor = nullptr;
			g_runtime.SourceComponent = nullptr;
		}
		if (IsActivePhase(g_runtime.CurrentPhase))
		{
			g_runtime.CurrentPhase = Phase::Cancelled;
			g_runtime.Message = "Scan interrompu par la fermeture du monde";
		}
		g_startRequested.store(false);
		g_cancelRequested.store(false);
		PublishStatus();
	}
}
