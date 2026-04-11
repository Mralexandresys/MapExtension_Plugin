#include "map_state_capture.h"

#include "plugin_config.h"
#include "plugin_helpers.h"

#if !defined(MODLOADER_SERVER_BUILD)
#include "client/map_sync_client.h"
#endif

#include "AuItems_classes.hpp"
#include "BP_PackageReceiver_classes.hpp"
#include "BP_PackageSender_classes.hpp"
#include "BP_Teleporter_classes.hpp"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

using CargoConnection = MapStateRuntime::Detail::CargoConnection;
using CargoKind = MapStateRuntime::Detail::CargoKind;
using CargoMarker = MapStateRuntime::Detail::CargoMarker;
using CargoSnapshot = MapStateRuntime::Detail::CargoSnapshot;
using RuptureCycleSnapshot = MapStateRuntime::Detail::RuptureCycleSnapshot;
using PlayerMarker = MapStateRuntime::Detail::PlayerMarker;
using ReceiverLinkInfo = MapStateRuntime::Detail::ReceiverLinkInfo;
using TeleporterMarker = MapStateRuntime::Detail::TeleporterMarker;

namespace
{
	constexpr int kMaxLoggedMarkersPerSnapshot = 8;
	constexpr int kMaxLoggedActorsPerKind = 4;
	constexpr int64_t kCargoActorRefreshMinIntervalMs = 750;
	constexpr int64_t kConnectionRetentionMs = 15000;
	struct TrackedChimeraWorldState final
	{
		SDK::UWorld* World = nullptr;
		std::string WorldName;
		bool Ready = false;
	};

	void ClearChimeraWorldState(const char* reason);

	bool g_runtimePlanLogged = false;
	bool g_engineTickSeen = false;
	int g_anyWorldBeginPlayCount = 0;
	int g_saveLoadedCount = 0;
	int g_experienceLoadedCount = 0;
	SDK::UWorld* g_lastChimeraWorld = nullptr;
	std::string g_lastWorldName;
	bool g_chimeraWorldReady = false;
	float g_engineTickAccumulatorSeconds = 0.0f;
	int64_t g_lastCargoActorRefreshAtUnixMs = 0;
	std::mutex g_runtimeStateMutex;
	std::mutex g_snapshotMutex;
	CargoSnapshot g_snapshot{};
	std::unordered_set<uint32_t> g_requestedCustomNameEntityIds;
	std::string g_lastRuptureCycleStateChangeKey;
	bool g_lastRuptureCycleStateChangeKeyValid = false;
	MapStateRuntime::Detail::RuptureCycleSnapshot g_lastPersistentRuptureCycleSnapshot{};
	bool g_lastPersistentRuptureCycleSnapshotValid = false;
	std::string g_lastRuptureCycleObservedSignature;
	bool g_lastRuptureCycleObservedSignatureValid = false;
	int64_t g_lastRuptureCycleObservedAtUnixMs = 0;
	std::atomic<bool> g_httpCargoRefreshRequested = false;
	std::atomic<int64_t> g_lastHttpCargoRefreshRequestAtUnixMs = 0;

	bool ShouldLogLifecycle()
	{
		return MapExtensionPluginConfig::Config::VerboseLifecycleLogs();
	}

	bool ShouldLogCargoSnapshots()
	{
		return MapExtensionPluginConfig::Config::LogCargoSnapshots();
	}

	bool ShouldLogActorScanFallback()
	{
		return MapExtensionPluginConfig::Config::LogActorScanFallback();
	}

	float GetRefreshIntervalSeconds()
	{
		int refreshMs = MapExtensionPluginConfig::Config::RefreshIntervalMs();
		if (refreshMs < 100)
		{
			refreshMs = 100;
		}
		return static_cast<float>(refreshMs) / 1000.0f;
	}

	const char* ConsumeRequestedCargoRefreshReason()
	{
		if (!g_httpCargoRefreshRequested.exchange(false))
		{
			return nullptr;
		}

		return "HttpRequest(/cargo)";
	}

	TrackedChimeraWorldState CopyTrackedChimeraWorldState()
	{
		std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
		TrackedChimeraWorldState state;
		state.World = g_lastChimeraWorld;
		state.WorldName = g_lastWorldName;
		state.Ready = g_chimeraWorldReady;
		return state;
	}

	bool HasPackageTransportReplicatorSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_snapshotMutex);
		return g_snapshot.HasPackageTransportReplicator;
	}

	bool IsChimeraWorldName(const std::string& worldName)
	{
		return worldName.find("ChimeraMain") != std::string::npos;
	}

	const char* EnviroWaveToString(SDK::EEnviroWave wave)
	{
		switch (wave)
		{
		case SDK::EEnviroWave::None:
			return "None";
		case SDK::EEnviroWave::Heat:
			return "Heat";
		case SDK::EEnviroWave::Cold:
			return "Cold";
		default:
			return "Unknown";
		}
	}

	const char* EnviroWaveStageToString(SDK::EEnviroWaveStage stage)
	{
		switch (stage)
		{
		case SDK::EEnviroWaveStage::None:
			return "None";
		case SDK::EEnviroWaveStage::PreWave:
			return "PreWave";
		case SDK::EEnviroWaveStage::Moving:
			return "Moving";
		case SDK::EEnviroWaveStage::Fadeout:
			return "Fadeout";
		case SDK::EEnviroWaveStage::Growback:
			return "Growback";
		default:
			return "Unknown";
		}
	}

	const char* PreWaveSubstageToString(SDK::EEnviroWavePreWaveSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWavePreWaveSubstage::None:
			return "None";
		case SDK::EEnviroWavePreWaveSubstage::BeforeExplosion:
			return "BeforeExplosion";
		case SDK::EEnviroWavePreWaveSubstage::AfterExplosion:
			return "AfterExplosion";
		default:
			return "Unknown";
		}
	}

	const char* FadeoutSubstageToString(SDK::EEnviroWaveFadeoutSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWaveFadeoutSubstage::None:
			return "None";
		case SDK::EEnviroWaveFadeoutSubstage::FireWave:
			return "FireWave";
		case SDK::EEnviroWaveFadeoutSubstage::Burning:
			return "Burning";
		case SDK::EEnviroWaveFadeoutSubstage::Fading:
			return "Fading";
		default:
			return "Unknown";
		}
	}

	const char* GrowbackSubstageToString(SDK::EEnviroWaveGrowbackSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWaveGrowbackSubstage::None:
			return "None";
		case SDK::EEnviroWaveGrowbackSubstage::MoonPhase:
			return "MoonPhase";
		case SDK::EEnviroWaveGrowbackSubstage::RegrowthStart:
			return "RegrowthStart";
		case SDK::EEnviroWaveGrowbackSubstage::Regrowth:
			return "Regrowth";
		default:
			return "Unknown";
		}
	}

	struct RuptureCycleLocalState final
	{
		SDK::EEnviroWave Wave = SDK::EEnviroWave::None;
		SDK::EEnviroWaveStage Stage = SDK::EEnviroWaveStage::None;
		SDK::EEnviroWavePreWaveSubstage PreWaveSubstage = SDK::EEnviroWavePreWaveSubstage::None;
		SDK::EEnviroWaveFadeoutSubstage FadeoutSubstage = SDK::EEnviroWaveFadeoutSubstage::None;
		SDK::EEnviroWaveGrowbackSubstage GrowbackSubstage = SDK::EEnviroWaveGrowbackSubstage::None;
		double ElapsedSeconds = 0.0;
		bool HasElapsed = false;
	};

	bool TryReadLocalRuptureCycleState(
		SDK::UCrEnviroWaveSubsystem* waveSubsystem,
		RuptureCycleLocalState& outState)
	{
		if (!waveSubsystem)
		{
			return false;
		}

		__try
		{
			outState.Wave = waveSubsystem->GetCurrentType();
			outState.Stage = waveSubsystem->GetCurrentStage();
			outState.PreWaveSubstage = waveSubsystem->CurrentPreWaveSubstage;
			outState.FadeoutSubstage = waveSubsystem->CurrentFadeoutSubstage;
			outState.GrowbackSubstage = waveSubsystem->CurrentGrowbackSubstage;
			outState.ElapsedSeconds = waveSubsystem->GetTimeSinceLastWaveStarted();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	struct BuildingCustomNameLookup final
	{
		SDK::UCrBuildingCustomNameSubsystem* Subsystem = nullptr;
		std::unordered_map<uint32_t, std::string> ByEntityId;
	};

	template <typename TSubsystem>
	TSubsystem* TryGetWorldSubsystem(SDK::UWorld* world, SDK::UClass* subsystemClass)
	{
		if (!world || !subsystemClass)
		{
			return nullptr;
		}

		auto* subsystem = SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, subsystemClass);
		if (!subsystem || !subsystem->IsA(subsystemClass))
		{
			return nullptr;
		}

		return static_cast<TSubsystem*>(subsystem);
	}

	const char* EnviroWaveStepToString(
		SDK::EEnviroWaveStage stage,
		SDK::EEnviroWavePreWaveSubstage preWaveSubstage,
		SDK::EEnviroWaveFadeoutSubstage fadeoutSubstage,
		SDK::EEnviroWaveGrowbackSubstage growbackSubstage)
	{
		if (stage == SDK::EEnviroWaveStage::PreWave && preWaveSubstage != SDK::EEnviroWavePreWaveSubstage::None)
		{
			return PreWaveSubstageToString(preWaveSubstage);
		}
		if (stage == SDK::EEnviroWaveStage::Fadeout && fadeoutSubstage != SDK::EEnviroWaveFadeoutSubstage::None)
		{
			return FadeoutSubstageToString(fadeoutSubstage);
		}
		if (stage == SDK::EEnviroWaveStage::Growback && growbackSubstage != SDK::EEnviroWaveGrowbackSubstage::None)
		{
			return GrowbackSubstageToString(growbackSubstage);
		}
		return "None";
	}

	bool TryProbeWorldNameRaw(SDK::UWorld* world)
	{
		if (!world)
		{
			return false;
		}

		__try
		{
			volatile auto nameIndex = world->Name.ComparisonIndex;
			(void)nameIndex;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool TryValidateTrackedWorldPointerByName(SDK::UWorld* world, const std::string& expectedName)
	{
		if (!world || expectedName.empty())
		{
			return false;
		}
		if (!TryProbeWorldNameRaw(world))
		{
			return false;
		}

		try
		{
			std::string currentName = world->GetName();
			return currentName == expectedName;
		}
		catch (...)
		{
			return false;
		}
	}

	bool TryIsObjectOfClass(SDK::UObject* obj, SDK::UClass* expectedClass)
	{
		if (!obj || !expectedClass)
		{
			return false;
		}

		__try
		{
			return obj->IsA(expectedClass);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool TryIsActorObject(SDK::UObject* obj, SDK::UClass* actorClass)
	{
		return TryIsObjectOfClass(obj, actorClass);
	}

	template <typename TObjectClass>
	SDK::UClass* TryGetStaticClass()
	{
		__try
		{
			return TObjectClass::StaticClass();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return nullptr;
		}
	}

	bool TryCopyValidatedTrackedChimeraWorldState(
		TrackedChimeraWorldState& outState,
		const char* reason,
		bool requireReady,
		bool& outInvalidPointer)
	{
		outInvalidPointer = false;
		outState = CopyTrackedChimeraWorldState();
		if (!outState.World)
		{
			return false;
		}
		if (requireReady && !outState.Ready)
		{
			return false;
		}

		// Validate the tracked world pointer using name-based probing.
		// This is more robust than IsA(UWorld::StaticClass()) in this runtime
		// because UWorld::StaticClass() can return nullptr or stale pointers.
		if (!outState.WorldName.empty())
		{
			if (!TryValidateTrackedWorldPointerByName(outState.World, outState.WorldName))
			{
				LOG_WARN(
					"Detected invalid tracked ChimeraMain world pointer via name probe (%s)",
					reason ? reason : "unknown");
				outState = {};
				outInvalidPointer = true;
				return false;
			}
		}
		else
		{
			// Fallback: if no world name is stored, use the legacy IsA check
			SDK::UClass* worldClass = TryGetStaticClass<SDK::UWorld>();
			if (!TryIsObjectOfClass(static_cast<SDK::UObject*>(outState.World), worldClass))
			{
				LOG_WARN(
					"Detected invalid tracked ChimeraMain world pointer (%s)",
					reason ? reason : "unknown");
				outState = {};
				outInvalidPointer = true;
				return false;
			}
		}

		return true;
	}

	bool TryObjectHasOuterInChain(SDK::UObject* obj, SDK::UObject* targetOuter, int maxDepth = 12)
	{
		if (!obj || !targetOuter || maxDepth <= 0)
		{
			return false;
		}

		__try
		{
			SDK::UObject* current = obj;
			for (int depth = 0; current && depth < maxDepth; ++depth)
			{
				if (current == targetOuter)
				{
					return true;
				}
				current = current->Outer;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}

		return false;
	}

	SDK::ACrGameStateBase* TryGetGameState(SDK::UWorld* world);
	SDK::ACrPlayerControllerBase* TryGetLocalCrPlayerController(SDK::UWorld* world);

	bool CaptureLocalRuptureCycleState(SDK::UWorld* world, RuptureCycleLocalState& outState)
	{
		SDK::UClass* subsystemClass = TryGetStaticClass<SDK::UCrEnviroWaveSubsystem>();
		if (!subsystemClass)
		{
			return false;
		}

		SDK::UCrEnviroWaveSubsystem* waveSubsystem =
			TryGetWorldSubsystem<SDK::UCrEnviroWaveSubsystem>(world, subsystemClass);
		if (!waveSubsystem)
		{
			return false;
		}

		if (!TryReadLocalRuptureCycleState(waveSubsystem, outState))
		{
			return false;
		}

		outState.HasElapsed = std::isfinite(outState.ElapsedSeconds);
		return true;
	}

	MapStateRuntime::Detail::RuptureCycleSnapshot ToRuptureCycleSnapshot(const RuptureCycleLocalState& state)
	{
		MapStateRuntime::Detail::RuptureCycleSnapshot snapshot{};
		snapshot.Available = true;
		snapshot.Wave = EnviroWaveToString(state.Wave);
		snapshot.Stage = EnviroWaveStageToString(state.Stage);
		snapshot.Step = EnviroWaveStepToString(
			state.Stage,
			state.PreWaveSubstage,
			state.FadeoutSubstage,
			state.GrowbackSubstage);
		snapshot.ElapsedSeconds = state.ElapsedSeconds;
		snapshot.HasElapsed = state.HasElapsed;
		return snapshot;
	}

	MapStateRuntime::Detail::RuptureCycleSnapshot CapturePreferredRuptureCycleSnapshot(
		SDK::UWorld* world)
	{
		RuptureCycleLocalState localState{};
		if (CaptureLocalRuptureCycleState(world, localState))
		{
			return ToRuptureCycleSnapshot(localState);
		}

#if !defined(MODLOADER_SERVER_BUILD)
		// Local subsystem unavailable (typical on a dedicated-server client).
		// Fall back to the remote cache populated by the network sync layer.
		MapStateRuntime::Detail::RuptureCycleSnapshot remoteSnapshot{};
		if (MapExtensionClient::Sync::TryCopyRemoteRuptureCycleSnapshot(remoteSnapshot))
		{
			return remoteSnapshot;
		}
#endif

		return {};
	}

	// Key covering only the discrete state fields (wave/stage/step). Used to suppress
	// repeated log entries when only ElapsedSeconds advances (e.g. local solo runtime).
	std::string BuildRuptureCycleStateChangeKey(const MapStateRuntime::Detail::RuptureCycleSnapshot& snapshot)
	{
		std::ostringstream oss;
		oss
			<< snapshot.Available << '|'
			<< snapshot.Wave << '|'
			<< snapshot.Stage << '|'
			<< snapshot.Step;
		return oss.str();
	}

	int64_t GetCurrentUnixTimeMilliseconds()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	void ApplyRuptureCycleObservationTimestamp(MapStateRuntime::Detail::RuptureCycleSnapshot& snapshot)
	{
		snapshot.HasObservedAtUnixMs = false;
		snapshot.ObservedAtUnixMs = 0;

		if (!snapshot.Available || !snapshot.HasElapsed)
		{
			return;
		}

		const std::string signature = BuildRuptureCycleStateChangeKey(snapshot);
		if (!g_lastRuptureCycleObservedSignatureValid || signature != g_lastRuptureCycleObservedSignature)
		{
			g_lastRuptureCycleObservedSignature = signature;
			g_lastRuptureCycleObservedSignatureValid = true;
			g_lastRuptureCycleObservedAtUnixMs = GetCurrentUnixTimeMilliseconds();
		}

		snapshot.HasObservedAtUnixMs = g_lastRuptureCycleObservedAtUnixMs > 0;
		snapshot.ObservedAtUnixMs = g_lastRuptureCycleObservedAtUnixMs;
	}

	void StorePersistentRuptureCycleSnapshot(const MapStateRuntime::Detail::RuptureCycleSnapshot& snapshot)
	{
		if (!snapshot.Available)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(g_snapshotMutex);
		g_lastPersistentRuptureCycleSnapshot = snapshot;
		g_lastPersistentRuptureCycleSnapshotValid = true;
	}

	bool TryGetPersistentRuptureCycleSnapshot(MapStateRuntime::Detail::RuptureCycleSnapshot& outSnapshot)
	{
		std::lock_guard<std::mutex> lock(g_snapshotMutex);
		if (!g_lastPersistentRuptureCycleSnapshotValid)
		{
			return false;
		}

		outSnapshot = g_lastPersistentRuptureCycleSnapshot;
		return outSnapshot.Available;
	}

	void ResetRuptureCycleState()
	{
		g_lastRuptureCycleStateChangeKey.clear();
		g_lastRuptureCycleStateChangeKeyValid = false;
		g_lastPersistentRuptureCycleSnapshot = {};
		g_lastPersistentRuptureCycleSnapshotValid = false;
		g_lastRuptureCycleObservedSignature.clear();
		g_lastRuptureCycleObservedSignatureValid = false;
		g_lastRuptureCycleObservedAtUnixMs = 0;
	}

	void LogRuptureCycleIfNeeded(
		const MapStateRuntime::Detail::RuptureCycleSnapshot& snapshot,
		uint64_t generation,
		const char* reason)
	{
		if (!MapExtensionPluginConfig::Config::LogRuptureCycleChat() || !snapshot.Available)
		{
			return;
		}

		// Suppress repeated log entries when only ElapsedSeconds changes (solo runtime).
		const std::string stateKey = BuildRuptureCycleStateChangeKey(snapshot);
		if (g_lastRuptureCycleStateChangeKeyValid && stateKey == g_lastRuptureCycleStateChangeKey)
		{
			return;
		}

		g_lastRuptureCycleStateChangeKey = stateKey;
		g_lastRuptureCycleStateChangeKeyValid = true;

		const std::string elapsedText = snapshot.HasElapsed ? std::to_string(snapshot.ElapsedSeconds) : "--";

		LOG_INFO(
			"RuptureCycle #%llu from '%s': wave=%s stage=%s step=%s elapsed=%s",
			static_cast<unsigned long long>(generation),
			reason ? reason : "unknown",
			snapshot.Wave.c_str(),
			snapshot.Stage.c_str(),
			snapshot.Step.c_str(),
			elapsedText.c_str());
	}

	void MarkChimeraWorldReady(const char* reason)
	{
		bool markedReady = false;
		{
			std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
			if (g_chimeraWorldReady)
			{
				return;
			}

			g_chimeraWorldReady = true;
			markedReady = true;
		}

		if (!markedReady)
		{
			return;
		}

		if (ShouldLogLifecycle())
		{
			LOG_INFO(
				"ChimeraMain world marked ready by %s",
				reason ? reason : "unknown");
		}
	}

	void ClearChimeraWorldState(const char* reason)
	{
		bool hadState = false;
		{
			std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
			hadState = g_lastChimeraWorld || !g_lastWorldName.empty() || g_chimeraWorldReady;
			if (hadState)
			{
				g_lastChimeraWorld = nullptr;
				g_lastWorldName.clear();
				g_chimeraWorldReady = false;
				g_lastCargoActorRefreshAtUnixMs = 0;
				g_requestedCustomNameEntityIds.clear();
			}
		}

		if (!hadState)
		{
			return;
		}

		g_engineTickAccumulatorSeconds = 0.0f;
		g_engineTickSeen = false;
		ResetRuptureCycleState();

#if !defined(MODLOADER_SERVER_BUILD)
		MapExtensionClient::Sync::ResetRuntimeState();
#endif

		if (ShouldLogLifecycle())
		{
			LOG_INFO(
				"Cleared ChimeraMain world state due to %s",
				reason ? reason : "unknown");
		}
	}


	std::string CargoKindToString(CargoKind kind)
	{
		return kind == CargoKind::Sender ? "sender" : "receiver";
	}

	bool IsCargoRealtimeActorImpl(SDK::AActor* actor)
	{
		return actor
			&& (
				actor->IsA(SDK::ABP_PackageReceiver_C::StaticClass())
				|| actor->IsA(SDK::ABP_PackageSender_C::StaticClass())
				|| actor->IsA(SDK::ACrItemReceiverBuilding::StaticClass())
				|| actor->IsA(SDK::ACrItemSenderBuilding::StaticClass()));
	}

	bool IsAuxiliaryRealtimeActorImpl(SDK::AActor* actor)
	{
		return actor
			&& (
				actor->IsA(SDK::ACrTeleporter::StaticClass())
				|| actor->IsA(SDK::ABP_Teleporter_C::StaticClass())
				|| actor->IsA(SDK::ACrMegamachineTeleporterDevice::StaticClass())
				|| actor->IsA(SDK::ACrCharacterPlayerBase::StaticClass()));
	}

	std::string NormalizeCargoBuildingName(CargoKind kind, const std::string& value)
	{
		if (value.empty())
		{
			return {};
		}

		if (kind == CargoKind::Sender)
		{
			if (value == "Package Sender" || value == "Cargo Sender")
			{
				return "Cargo Dispatcher";
			}

			return value;
		}

		if (value == "Package Receiver")
		{
			return "Cargo Receiver";
		}

		return value;
	}

	std::string GetConfiguredCargoBuildingName(CargoKind kind)
	{
		if (SDK::UCrBuilidngsDeveloperSettings* settings = SDK::UCrBuilidngsDeveloperSettings::GetDefaultObj())
		{
			const std::string configuredName = kind == CargoKind::Sender
				? settings->SenderDefaultName.ToString()
				: settings->ReceiverDefaultName.ToString();
			if (!configuredName.empty())
			{
				return NormalizeCargoBuildingName(kind, configuredName);
			}
		}

		return kind == CargoKind::Sender ? "Cargo Dispatcher" : "Cargo Receiver";
	}

	std::string GetItemDisplayName(const SDK::UAuItemDataBase* item)
	{
		if (!item)
		{
			return {};
		}

		const std::string itemName = item->ItemName.ToString();
		if (!itemName.empty())
		{
			return itemName;
		}

		if (!item->UniqueItemName.IsNone())
		{
			const std::string uniqueName = item->UniqueItemName.ToString();
			if (!uniqueName.empty())
			{
				return uniqueName;
			}
		}

		return item->GetName();
	}

	void AppendResourceName(CargoMarker& marker, const std::string& resourceName)
	{
		if (resourceName.empty())
		{
			return;
		}

		if (marker.ResourceSummary.empty())
		{
			marker.ResourceSummary = resourceName;
			return;
		}

		if (marker.ResourceSummary.find(resourceName) != std::string::npos)
		{
			return;
		}

		marker.ResourceSummary += ", ";
		marker.ResourceSummary += resourceName;
	}

	std::string ComposeMarkerDisplayName(const CargoMarker& marker)
	{
		if (marker.Kind == CargoKind::Sender && !marker.ResourceSummary.empty())
		{
			return marker.DisplayName + " - " + marker.ResourceSummary;
		}

		return marker.DisplayName;
	}

	std::string FormatVector(const SDK::FVector& value)
	{
		std::ostringstream oss;
		oss.setf(std::ios::fixed);
		oss.precision(1);
		oss << "(X=" << value.X << " Y=" << value.Y << " Z=" << value.Z << ")";
		return oss.str();
	}

	std::string FormatVector2f(const SDK::FVector2f& value)
	{
		std::ostringstream oss;
		oss.setf(std::ios::fixed);
		oss.precision(1);
		oss << "(X=" << value.X << " Y=" << value.Y << ")";
		return oss.str();
	}

	std::string BuildReplicationHelperKey(const SDK::FCrMassEntityReplicationHelper& helper)
	{
		std::ostringstream oss;
		oss << helper.NetID.Value << ':' << helper.Entity.ID;
		return oss.str();
	}

	std::string BuildLocationKey(CargoKind kind, const SDK::FVector& location)
	{
		std::ostringstream oss;
		oss << static_cast<int>(kind) << ':'
			<< static_cast<int>(std::lround(location.X)) << ':'
			<< static_cast<int>(std::lround(location.Y)) << ':'
			<< static_cast<int>(std::lround(location.Z));
		return oss.str();
	}

	std::string BuildLocationKey(const char* prefix, const SDK::FVector& location)
	{
		std::ostringstream oss;
		oss << (prefix ? prefix : "marker") << ':'
			<< static_cast<int>(std::lround(location.X)) << ':'
			<< static_cast<int>(std::lround(location.Y)) << ':'
			<< static_cast<int>(std::lround(location.Z));
		return oss.str();
	}

	SDK::FVector MakeVector(double x, double y, double z)
	{
		SDK::FVector value{};
		value.X = static_cast<float>(x);
		value.Y = static_cast<float>(y);
		value.Z = static_cast<float>(z);
		return value;
	}

	SDK::ACrGameStateBase* TryGetGameState(SDK::UWorld* world)
	{
		if (!world)
		{
			return nullptr;
		}
		if (!TryIsObjectOfClass(
				static_cast<SDK::UObject*>(world),
				TryGetStaticClass<SDK::UWorld>()))
		{
			return nullptr;
		}

		SDK::AGameStateBase* gameStateBase = SDK::UGameplayStatics::GetGameState(world);
		if (!gameStateBase || !gameStateBase->IsA(SDK::ACrGameStateBase::StaticClass()))
		{
			return nullptr;
		}

		return static_cast<SDK::ACrGameStateBase*>(gameStateBase);
	}

	void LogRuntimePlanIfNeededImpl()
	{
		if (g_runtimePlanLogged || !MapExtensionPluginConfig::Config::LogRuntimePlanOnce())
		{
			return;
		}

		g_runtimePlanLogged = true;
		LOG_INFO("Runtime plan: capture cargo dispatcher/receiver network data from runtime sources, then publish snapshots over local HTTP.");
		LOG_INFO("Runtime plan: use package transport replication first, actor scans as fallback, and keep the web UI separate from Unreal widgets.");
		LOG_INFO(
			"Runtime plan: refresh the cached snapshot every %d ms while ChimeraMain is running.",
			MapExtensionPluginConfig::Config::RefreshIntervalMs());
		LOG_INFO("Runtime plan: also refresh immediately when relevant actors begin play in ChimeraMain.");
	}

	std::string HexEncode(const std::string& value)
	{
		static const char* kHex = "0123456789abcdef";
		std::string out;
		out.reserve(value.size() * 2);
		for (unsigned char ch : value)
		{
			out.push_back(kHex[(ch >> 4) & 0xF]);
			out.push_back(kHex[ch & 0xF]);
		}
		return out;
	}

	std::string SanitizePrintableKey(const std::string& value)
	{
		std::string out;
		out.reserve(value.size());
		for (unsigned char ch : value)
		{
			if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
			{
				out.push_back(static_cast<char>(ch));
			}
			else if (ch == '_' || ch == '-' || ch == '.')
			{
				out.push_back(static_cast<char>(ch));
			}
			else
			{
				out.push_back('_');
			}
		}
		return out;
	}

	std::string MakePublicKey(CargoKind kind, const std::string& internalKey)
	{
		bool isMostlyPrintable = !internalKey.empty();
		for (unsigned char ch : internalKey)
		{
			if (ch < 32 || ch > 126)
			{
				isMostlyPrintable = false;
				break;
			}
		}

		const std::string prefix = kind == CargoKind::Sender ? "sender_" : "receiver_";
		if (isMostlyPrintable)
		{
			return prefix + SanitizePrintableKey(internalKey);
		}

		return prefix + HexEncode(internalKey);
	}

	std::string MakeTaggedPublicKey(const char* prefix, const std::string& internalKey)
	{
		const std::string safePrefix = prefix ? prefix : "marker";
		bool isMostlyPrintable = !internalKey.empty();
		for (unsigned char ch : internalKey)
		{
			if (ch < 32 || ch > 126)
			{
				isMostlyPrintable = false;
				break;
			}
		}

		if (isMostlyPrintable)
		{
			return safePrefix + "_" + SanitizePrintableKey(internalKey);
		}

		return safePrefix + "_" + HexEncode(internalKey);
	}

	void LogSnapshotSummary(const CargoSnapshot& snapshot)
	{
		if (!ShouldLogCargoSnapshots())
		{
			return;
		}

		LOG_INFO(
			"Cargo snapshot #%llu from '%s': markers=%zu, senders=%d, receivers=%d, connections=%zu, teleporters=%zu, players=%zu, replicator=%s, teleporter_replicator=%s, actor_fallback=%s",
			static_cast<unsigned long long>(snapshot.Generation),
			snapshot.Reason.c_str(),
			snapshot.Markers.size(),
			snapshot.SenderCount,
			snapshot.ReceiverCount,
			snapshot.Connections.size(),
			snapshot.Teleporters.size(),
			snapshot.Players.size(),
			snapshot.HasPackageTransportReplicator ? "yes" : "no",
			snapshot.HasTeleportReplicator ? "yes" : "no",
			snapshot.UsedActorFallback ? "yes" : "no");

		const size_t maxLog = std::min(snapshot.Markers.size(), static_cast<size_t>(kMaxLoggedMarkersPerSnapshot));
		for (size_t index = 0; index < maxLog; ++index)
		{
			const CargoMarker& marker = snapshot.Markers[index];
			LOG_INFO(
				"  Cargo %s #%zu key=%s label='%s' resource='%s' source=%s world=%s map=%s",
				CargoKindToString(marker.Kind).c_str(),
				index + 1,
				marker.PublicKey.c_str(),
				ComposeMarkerDisplayName(marker).c_str(),
				marker.ResourceSummary.c_str(),
				marker.Source.c_str(),
				FormatVector(marker.WorldLocation).c_str(),
				FormatVector2f(marker.MapLocation).c_str());
		}
	}

	bool IsRealtimeRefreshReason(const char* reason)
	{
		return reason != nullptr
			&& (
				std::strcmp(reason, "EngineTick") == 0
				|| std::strncmp(reason, "ActorBeginPlay", std::strlen("ActorBeginPlay")) == 0);
	}

	bool IsRelevantRealtimeActorImpl(SDK::AActor* actor)
	{
		return IsCargoRealtimeActorImpl(actor) || IsAuxiliaryRealtimeActorImpl(actor);
	}

	bool IsRelevantRuptureActorImpl(SDK::AActor* actor)
	{
		return actor
			&& (
				actor->IsA(SDK::ACrWaveTimerActor::StaticClass())
				|| actor->IsA(SDK::ACrGatherableSpawnersRepActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWaveVisualsReplicationActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWaveVisualsActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWavePlayerFXActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWaveRegion::StaticClass()));
	}

	CargoMarker* AddOrUpdateMarker(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& markerIndexes,
		CargoKind kind,
		const SDK::FVector& worldLocation,
		std::string internalKey,
		const char* source,
		std::string displayName,
		std::string resourceName)
	{
		const auto existing = markerIndexes.find(internalKey);
		if (existing != markerIndexes.end())
		{
			CargoMarker& marker = snapshot.Markers[existing->second];
			if (marker.DisplayName.empty() && !displayName.empty())
			{
				marker.DisplayName = NormalizeCargoBuildingName(kind, displayName);
			}
			AppendResourceName(marker, resourceName);
			return &marker;
		}

		CargoMarker marker{};
		marker.Kind = kind;
		marker.WorldLocation = worldLocation;
		marker.MapLocation = MapStateRuntime::Detail::WorldToMap(worldLocation);
		marker.DisplayName = !displayName.empty()
			? NormalizeCargoBuildingName(kind, displayName)
			: GetConfiguredCargoBuildingName(kind);
		AppendResourceName(marker, resourceName);
		marker.Source = source ? source : "unknown";
		marker.InternalKey = std::move(internalKey);
		marker.PublicKey = MakePublicKey(kind, marker.InternalKey);
		snapshot.Markers.push_back(std::move(marker));
		markerIndexes.emplace(snapshot.Markers.back().InternalKey, snapshot.Markers.size() - 1);
		return &snapshot.Markers.back();
	}

	TeleporterMarker* AddOrUpdateTeleporter(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& teleporterIndexes,
		const SDK::FVector& worldLocation,
		std::string internalKey,
		const char* source,
		std::string displayName)
	{
		const auto existing = teleporterIndexes.find(internalKey);
		if (existing != teleporterIndexes.end())
		{
			TeleporterMarker& marker = snapshot.Teleporters[existing->second];
			if (marker.DisplayName.empty() && !displayName.empty())
			{
				marker.DisplayName = std::move(displayName);
			}
			if (marker.Source.empty() && source)
			{
				marker.Source = source;
			}
			return &marker;
		}

		TeleporterMarker marker{};
		marker.WorldLocation = worldLocation;
		marker.MapLocation = MapStateRuntime::Detail::WorldToMap(worldLocation);
		marker.DisplayName = !displayName.empty() ? std::move(displayName) : "Teleporter";
		marker.Source = source ? source : "unknown";
		marker.InternalKey = std::move(internalKey);
		marker.PublicKey = MakeTaggedPublicKey("teleporter", marker.InternalKey);
		snapshot.Teleporters.push_back(std::move(marker));
		teleporterIndexes.emplace(snapshot.Teleporters.back().InternalKey, snapshot.Teleporters.size() - 1);
		return &snapshot.Teleporters.back();
	}

	PlayerMarker* AddOrUpdatePlayer(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& playerIndexes,
		const SDK::FVector& worldLocation,
		std::string internalKey,
		const char* source,
		std::string displayName)
	{
		const auto existing = playerIndexes.find(internalKey);
		if (existing != playerIndexes.end())
		{
			PlayerMarker& marker = snapshot.Players[existing->second];
			if (marker.DisplayName.empty() && !displayName.empty())
			{
				marker.DisplayName = std::move(displayName);
			}
			if (marker.Source.empty() && source)
			{
				marker.Source = source;
			}
			return &marker;
		}

		PlayerMarker marker{};
		marker.WorldLocation = worldLocation;
		marker.MapLocation = MapStateRuntime::Detail::WorldToMap(worldLocation);
		marker.DisplayName = !displayName.empty() ? std::move(displayName) : "Player";
		marker.Source = source ? source : "unknown";
		marker.InternalKey = std::move(internalKey);
		marker.PublicKey = MakeTaggedPublicKey("player", marker.InternalKey);
		snapshot.Players.push_back(std::move(marker));
		playerIndexes.emplace(snapshot.Players.back().InternalKey, snapshot.Players.size() - 1);
		return &snapshot.Players.back();
	}

	std::string GetInteractionDisplayName(SDK::AActor* actor)
	{
		if (!actor)
		{
			return {};
		}

		const SDK::TArray<SDK::UActorComponent*> components =
			actor->K2_GetComponentsByClass(SDK::UCrInteractionComponent::StaticClass());
		for (int index = 0; index < components.Num(); ++index)
		{
			SDK::UCrInteractionComponent* interactionComponent =
				static_cast<SDK::UCrInteractionComponent*>(components[index]);
			if (!interactionComponent)
			{
				continue;
			}

			const std::string displayName = interactionComponent->GetInteractionDisplayName().ToString();
			if (!displayName.empty())
			{
				return displayName;
			}
		}

		return {};
	}

	SDK::UCrBuildingCustomNameSubsystem* TryGetBuildingCustomNameSubsystem(SDK::UWorld* world)
	{
		return TryGetWorldSubsystem<SDK::UCrBuildingCustomNameSubsystem>(
			world,
			SDK::UCrBuildingCustomNameSubsystem::StaticClass());
	}

	uint32_t GetPersistentEntityIdValue(const SDK::FCrMassPersistentEntityID& entityId);
	uint32_t GetReplicationHelperEntityIdValue(const SDK::FCrMassEntityReplicationHelper& helper);

	BuildingCustomNameLookup BuildBuildingCustomNameLookup(SDK::UWorld* world)
	{
		BuildingCustomNameLookup lookup{};
		lookup.Subsystem = TryGetBuildingCustomNameSubsystem(world);
		if (!lookup.Subsystem)
		{
			return lookup;
		}

		for (const auto& entry : lookup.Subsystem->CustomNames)
		{
			const uint32_t entityId = GetPersistentEntityIdValue(entry.Key());
			if (entityId == 0)
			{
				continue;
			}

			std::string displayName = entry.Value().ToString();
			if (displayName.empty())
			{
				continue;
			}

			lookup.ByEntityId.emplace(entityId, std::move(displayName));
		}

		return lookup;
	}

	std::string GetBuildingCustomName(SDK::UWorld* world, SDK::AActor* buildingActor)
	{
		if (!world || !buildingActor)
		{
			return {};
		}

		SDK::UCrBuildingCustomNameSubsystem* customNameSubsystem =
			TryGetBuildingCustomNameSubsystem(world);
		if (!customNameSubsystem)
		{
			return {};
		}

		return customNameSubsystem->GetBuildingCustomName(buildingActor).ToString();
	}

	uint32_t GetPersistentEntityIdValue(const SDK::FCrMassPersistentEntityID& entityId)
	{
		return entityId.ID;
	}

	uint32_t GetReplicationHelperEntityIdValue(const SDK::FCrMassEntityReplicationHelper& helper)
	{
		return GetPersistentEntityIdValue(helper.Entity);
	}

	SDK::ACrPlayerControllerBase* TryGetLocalCrPlayerController(SDK::UWorld* world)
	{
		if (!world)
		{
			return nullptr;
		}
		if (!TryIsObjectOfClass(
				static_cast<SDK::UObject*>(world),
				TryGetStaticClass<SDK::UWorld>()))
		{
			return nullptr;
		}

		SDK::APlayerController* playerController = SDK::UGameplayStatics::GetPlayerController(world, 0);
		if (!playerController || !playerController->IsA(SDK::ACrPlayerControllerBase::StaticClass()))
		{
			return nullptr;
		}

		return static_cast<SDK::ACrPlayerControllerBase*>(playerController);
	}

	std::string FindCachedBuildingCustomName(
		const BuildingCustomNameLookup& customNameLookup,
		const SDK::FCrMassEntityReplicationHelper& helper)
	{
		const uint32_t targetId = GetReplicationHelperEntityIdValue(helper);
		if (targetId == 0)
		{
			return {};
		}

		const auto entry = customNameLookup.ByEntityId.find(targetId);
		if (entry == customNameLookup.ByEntityId.end())
		{
			return {};
		}

		return entry->second;
	}

	void RequestBuildingCustomNameIfNeeded(
		SDK::UWorld* world,
		const SDK::FCrMassEntityReplicationHelper& helper)
	{
		const uint32_t entityId = GetReplicationHelperEntityIdValue(helper);
		if (!world || entityId == 0)
		{
			return;
		}

		{
			std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
			if (!g_requestedCustomNameEntityIds.insert(entityId).second)
			{
				return;
			}
		}

		SDK::ACrPlayerControllerBase* playerController = TryGetLocalCrPlayerController(world);
		if (!playerController)
		{
			std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
			g_requestedCustomNameEntityIds.erase(entityId);
			return;
		}

		playerController->ServerGetBuildingCustomName(helper);
	}

	std::string GetBuildingCustomName(
		SDK::UWorld* world,
		const SDK::FCrMassEntityReplicationHelper& helper,
		const BuildingCustomNameLookup& customNameLookup)
	{
		if (!world)
		{
			return {};
		}

		const std::string cachedName = FindCachedBuildingCustomName(customNameLookup, helper);
		if (!cachedName.empty())
		{
			return cachedName;
		}

		RequestBuildingCustomNameIfNeeded(world, helper);
		return {};
	}

	std::string GetCargoActorDisplayName(
		SDK::UWorld* world,
		SDK::AActor* actor,
		CargoKind kind)
	{
		const std::string customName = GetBuildingCustomName(world, actor);
		if (!customName.empty())
		{
			return customName;
		}

		const std::string interactionName = GetInteractionDisplayName(actor);
		if (!interactionName.empty())
		{
			return interactionName;
		}

		return GetConfiguredCargoBuildingName(kind);
	}

	CargoMarker* FindMarkerByLocation(
		CargoSnapshot& snapshot,
		CargoKind kind,
		const SDK::FVector& worldLocation)
	{
		const std::string locationKey = BuildLocationKey(kind, worldLocation);
		for (CargoMarker& marker : snapshot.Markers)
		{
			if (marker.Kind != kind)
			{
				continue;
			}

			if (BuildLocationKey(kind, marker.WorldLocation) == locationKey)
			{
				return &marker;
			}
		}

		return nullptr;
	}

	const CargoMarker* FindMarkerByInternalKey(
		const CargoSnapshot& snapshot,
		CargoKind kind,
		const std::string& internalKey)
	{
		for (const CargoMarker& marker : snapshot.Markers)
		{
			if (marker.Kind == kind && marker.InternalKey == internalKey)
			{
				return &marker;
			}
		}

		return nullptr;
	}

	const CargoConnection* FindConnectionByPublicKeys(
		const CargoSnapshot& snapshot,
		const std::string& senderKey,
		const std::string& receiverKey,
		const std::string& itemDisplayName)
	{
		for (const CargoConnection& connection : snapshot.Connections)
		{
			if (connection.SenderKey == senderKey
				&& connection.ReceiverKey == receiverKey
				&& connection.ItemDisplayName == itemDisplayName)
			{
				return &connection;
			}
		}

		return nullptr;
	}

	std::string BuildConnectionSignature(const CargoConnection& connection)
	{
		std::string signature = connection.SenderKey;
		signature.push_back('\x1F');
		signature += connection.ReceiverKey;
		signature.push_back('\x1F');
		signature += connection.ItemDisplayName;
		return signature;
	}

	void MergeRetainedConnections(
		CargoSnapshot& snapshot,
		const CargoSnapshot& previousSnapshot,
		int64_t nowUnixMs)
	{
		if (!snapshot.HasPackageTransportReplicator || !previousSnapshot.HasPackageTransportReplicator)
		{
			return;
		}

		std::unordered_set<std::string> signatures;
		signatures.reserve(snapshot.Connections.size());
		for (const CargoConnection& connection : snapshot.Connections)
		{
			signatures.insert(BuildConnectionSignature(connection));
		}

		size_t retainedCount = 0;
		for (const CargoConnection& previousConnection : previousSnapshot.Connections)
		{
			const std::string signature = BuildConnectionSignature(previousConnection);
			if (signatures.find(signature) != signatures.end())
			{
				continue;
			}
			if (previousConnection.LastObservedAtUnixMs <= 0
				|| nowUnixMs - previousConnection.LastObservedAtUnixMs > kConnectionRetentionMs)
			{
				continue;
			}

			snapshot.Connections.push_back(previousConnection);
			signatures.insert(signature);
			++retainedCount;
		}

		if (retainedCount > 0 && ShouldLogLifecycle())
		{
			LOG_INFO(
				"Cargo refresh '%s': retained %zu recent connection(s) missing from current replicator snapshot",
				snapshot.Reason.c_str(),
				retainedCount);
		}
	}

	std::string GetTeleporterDisplayName(SDK::ACrTeleporter* teleporter)
	{
		if (!teleporter)
		{
			return {};
		}

		const std::string interactionName = GetInteractionDisplayName(teleporter);
		if (!interactionName.empty())
		{
			return interactionName;
		}

		return teleporter->GetName();
	}

	std::string GetTeleporterDisplayName(SDK::ACrMegamachineTeleporterDevice* teleporter)
	{
		if (!teleporter)
		{
			return {};
		}

		const std::string interactionText = teleporter->InteractionText.ToString();
		if (!interactionText.empty())
		{
			return interactionText;
		}

		const std::string interactionName = GetInteractionDisplayName(teleporter);
		if (!interactionName.empty())
		{
			return interactionName;
		}

		return teleporter->GetName();
	}

	std::string GetPlayerDisplayName(SDK::APawn* playerPawn)
	{
		if (!playerPawn)
		{
			return {};
		}

		if (SDK::AController* controller = playerPawn->GetController())
		{
			if (SDK::APlayerState* playerState = controller->PlayerState)
			{
				const std::string playerName = playerState->GetPlayerName().ToString();
				if (!playerName.empty())
				{
					return playerName;
				}
			}
		}

		return playerPawn->GetName();
	}

	std::string GetPlayerInternalKey(SDK::APawn* playerPawn)
	{
		if (!playerPawn)
		{
			return {};
		}

		if (SDK::AController* controller = playerPawn->GetController())
		{
			if (SDK::APlayerState* playerState = controller->PlayerState)
			{
				const std::string playerStateName = playerState->GetName();
				if (!playerStateName.empty())
				{
					return "player_state:" + playerStateName;
				}
			}

			const std::string controllerName = controller->GetName();
			if (!controllerName.empty())
			{
				return "controller:" + controllerName;
			}
		}

		const std::string pawnName = playerPawn->GetName();
		if (!pawnName.empty())
		{
			return "pawn:" + pawnName;
		}

		return {};
	}

	void CapturePlayerPawn(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& playerIndexes,
		SDK::APawn* playerPawn,
		const char* source)
	{
		if (!playerPawn)
		{
			return;
		}

		const SDK::FVector worldLocation = playerPawn->K2_GetActorLocation();
		std::string internalKey = GetPlayerInternalKey(playerPawn);
		if (internalKey.empty())
		{
			internalKey = BuildLocationKey("player", worldLocation);
		}

		AddOrUpdatePlayer(
			snapshot,
			playerIndexes,
			worldLocation,
			internalKey,
			source,
			GetPlayerDisplayName(playerPawn));
	}

	void CaptureFromReplicator(
		SDK::UWorld* world,
		CargoSnapshot& snapshot,
		SDK::ACrGameStateBase* gameState,
		const CargoSnapshot* previousSnapshot,
		int64_t observedAtUnixMs,
		const BuildingCustomNameLookup& customNameLookup,
		std::unordered_map<std::string, size_t>& markerIndexes)
	{
		if (!gameState)
		{
			return;
		}

		SDK::ACrPackageTransportReplicator* replicator = gameState->PackageTransportReplicator;
		snapshot.HasPackageTransportReplicator = replicator != nullptr;
		if (!replicator)
		{
			return;
		}

		snapshot.UsedReplicator = true;
		std::unordered_map<std::string, ReceiverLinkInfo> receiverLookup;
		struct SenderReplicationInfo
		{
			SDK::FVector WorldLocation{};
			std::string DisplayName;
		};
		std::unordered_map<std::string, SenderReplicationInfo> senderLookup;
		std::unordered_set<std::string> connectionKeys;

		const SDK::FCrSenderReceiversContainer& senderReceivers = replicator->SenderReceiversContainer;
		for (int index = 0; index < senderReceivers.AllSenderReceiversData.Num(); ++index)
		{
			const SDK::FCrSenderReceiverData& senderReceiver = senderReceivers.AllSenderReceiversData[index];
			const std::string entityKey = BuildReplicationHelperKey(senderReceiver.Entity);
			const SDK::FVector worldLocation = MakeVector(
				senderReceiver.Location.X,
				senderReceiver.Location.Y,
				senderReceiver.Location.Z);
			const std::string displayName = GetBuildingCustomName(world, senderReceiver.Entity, customNameLookup);

			if (senderReceiver.bIsSender)
			{
				senderLookup.emplace(
					entityKey,
					SenderReplicationInfo{
						worldLocation,
						displayName
					});
				continue;
			}

			CargoMarker* marker = AddOrUpdateMarker(
				snapshot,
				markerIndexes,
				CargoKind::Receiver,
				worldLocation,
				entityKey,
				"package_transport_replicator.receiver",
				displayName,
				{});
			if (!marker)
			{
				continue;
			}

			const auto markerIndex = markerIndexes.find(marker->InternalKey);
			if (markerIndex == markerIndexes.end())
			{
				continue;
			}

			receiverLookup.emplace(
				marker->InternalKey,
				ReceiverLinkInfo{
					markerIndex->second,
					marker->WorldLocation,
					marker->MapLocation,
					marker->PublicKey
				});
		}

		const SDK::FCrPackageTransportConnectionsContainer& connections = replicator->ConnectionsContainer;
		snapshot.ConnectionCount = connections.ConnectionsData.Num();
		for (int index = 0; index < connections.ConnectionsData.Num(); ++index)
		{
			const SDK::FCrPackageTransportConnectionData& connection = connections.ConnectionsData[index];
			const std::string senderKey = BuildReplicationHelperKey(connection.Sender);
			const std::string receiverKey = BuildReplicationHelperKey(connection.Receiver);
			const std::string previousReceiverKey = BuildReplicationHelperKey(connection.PrevReceiver);
			const std::string resourceName = GetItemDisplayName(connection.Item);
			const std::string senderPublicKey = MakePublicKey(CargoKind::Sender, senderKey);
			const std::string receiverPublicKey = MakePublicKey(CargoKind::Receiver, receiverKey);
			const std::string previousReceiverPublicKey = previousReceiverKey.empty()
				? std::string{}
				: MakePublicKey(CargoKind::Receiver, previousReceiverKey);
			const auto senderIt = senderLookup.find(senderKey);
			const CargoMarker* previousSenderMarker = nullptr;
			if (senderIt == senderLookup.end() && previousSnapshot)
			{
				previousSenderMarker = FindMarkerByInternalKey(*previousSnapshot, CargoKind::Sender, senderKey);
			}

			CargoMarker* senderMarker = nullptr;
			if (senderIt != senderLookup.end() || previousSenderMarker)
			{
				const SDK::FVector senderLocation = senderIt != senderLookup.end()
					? senderIt->second.WorldLocation
					: previousSenderMarker->WorldLocation;
				std::string senderDisplayName = senderIt != senderLookup.end()
					? senderIt->second.DisplayName
					: previousSenderMarker->DisplayName;
				if (senderDisplayName.empty())
				{
					senderDisplayName = GetBuildingCustomName(world, connection.Sender, customNameLookup);
				}
				senderMarker = AddOrUpdateMarker(
					snapshot,
					markerIndexes,
					CargoKind::Sender,
					senderLocation,
					senderKey,
					"package_transport_replicator.connection",
					senderDisplayName,
					resourceName);
			}

			auto receiverIt = receiverLookup.find(receiverKey);
			const CargoMarker* previousReceiverMarker = nullptr;
			if (receiverIt == receiverLookup.end() && previousSnapshot)
			{
				previousReceiverMarker = FindMarkerByInternalKey(*previousSnapshot, CargoKind::Receiver, receiverKey);
				if (!previousReceiverMarker && !previousReceiverKey.empty())
				{
					previousReceiverMarker = FindMarkerByInternalKey(*previousSnapshot, CargoKind::Receiver, previousReceiverKey);
				}
			}

			const CargoConnection* previousConnection = nullptr;
			if (previousSnapshot)
			{
				previousConnection = FindConnectionByPublicKeys(
					*previousSnapshot,
					senderPublicKey,
					receiverPublicKey,
					resourceName);
				if (!previousConnection && !previousReceiverPublicKey.empty())
				{
					previousConnection = FindConnectionByPublicKeys(
						*previousSnapshot,
						senderPublicKey,
						previousReceiverPublicKey,
						resourceName);
				}
			}

			std::string resolvedSenderPublicKey = senderPublicKey;
			std::string resolvedSenderLabel;
			SDK::FVector resolvedSenderWorldLocation{};
			SDK::FVector2f resolvedSenderMapLocation{};
			bool hasResolvedSender = false;
			if (senderMarker)
			{
				resolvedSenderPublicKey = senderMarker->PublicKey;
				resolvedSenderLabel = ComposeMarkerDisplayName(*senderMarker);
				resolvedSenderWorldLocation = senderMarker->WorldLocation;
				resolvedSenderMapLocation = senderMarker->MapLocation;
				hasResolvedSender = true;
			}
			else if (previousConnection)
			{
				resolvedSenderPublicKey = previousConnection->SenderKey;
				resolvedSenderLabel = previousConnection->SenderLabel;
				resolvedSenderWorldLocation = previousConnection->SenderWorldLocation;
				resolvedSenderMapLocation = previousConnection->SenderMapLocation;
				hasResolvedSender = true;
			}

			if (!hasResolvedSender)
			{
				continue;
			}

			std::string resolvedReceiverPublicKey = receiverPublicKey;
			std::string resolvedReceiverLabel;
			SDK::FVector resolvedReceiverWorldLocation{};
			SDK::FVector2f resolvedReceiverMapLocation{};
			bool hasResolvedReceiver = false;
			if (receiverIt != receiverLookup.end())
			{
				CargoMarker& receiverMarker = snapshot.Markers[receiverIt->second.MarkerIndex];
				AppendResourceName(receiverMarker, resourceName);
				resolvedReceiverPublicKey = receiverIt->second.PublicKey;
				resolvedReceiverLabel = receiverMarker.DisplayName;
				resolvedReceiverWorldLocation = receiverMarker.WorldLocation;
				resolvedReceiverMapLocation = receiverMarker.MapLocation;
				hasResolvedReceiver = true;
			}
			else if (previousReceiverMarker)
			{
				resolvedReceiverPublicKey = previousReceiverMarker->PublicKey;
				resolvedReceiverLabel = previousReceiverMarker->DisplayName;
				resolvedReceiverWorldLocation = previousReceiverMarker->WorldLocation;
				resolvedReceiverMapLocation = previousReceiverMarker->MapLocation;
				hasResolvedReceiver = true;
			}
			else if (previousConnection)
			{
				resolvedReceiverPublicKey = previousConnection->ReceiverKey;
				resolvedReceiverLabel = previousConnection->ReceiverLabel;
				resolvedReceiverWorldLocation = previousConnection->ReceiverWorldLocation;
				resolvedReceiverMapLocation = previousConnection->ReceiverMapLocation;
				hasResolvedReceiver = true;
			}

			if (!hasResolvedReceiver)
			{
				continue;
			}

			std::string dedupeKey = resolvedSenderPublicKey;
			dedupeKey.push_back('\x1F');
			dedupeKey += resolvedReceiverPublicKey;
			dedupeKey.push_back('\x1F');
			dedupeKey += resourceName;
			if (!connectionKeys.insert(dedupeKey).second)
			{
				continue;
			}

			CargoConnection route{};
			route.SenderKey = resolvedSenderPublicKey;
			route.ReceiverKey = resolvedReceiverPublicKey;
			route.SenderLabel = resolvedSenderLabel;
			route.ReceiverLabel = resolvedReceiverLabel;
			route.ItemDisplayName = resourceName;
			route.RequestedAmount = connection.RequestedAmount;
			route.SenderWorldLocation = resolvedSenderWorldLocation;
			route.ReceiverWorldLocation = resolvedReceiverWorldLocation;
			route.SenderMapLocation = resolvedSenderMapLocation;
			route.ReceiverMapLocation = resolvedReceiverMapLocation;
			route.LastObservedAtUnixMs = observedAtUnixMs;
			snapshot.Connections.push_back(std::move(route));
		}
	}

	template <typename TActorClass>
	int CaptureActorsOfClass(
		SDK::UWorld* world,
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& markerIndexes,
		CargoKind kind,
		const char* source,
		bool allowCreateIfMissing = true)
	{
		if (!world)
		{
			return 0;
		}

		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, TActorClass::StaticClass(), &actors);

		const int count = actors.Num();
		const int maxLog = ShouldLogActorScanFallback() ? std::min(count, kMaxLoggedActorsPerKind) : 0;
		std::string actorClassName = "(unknown)";
		if (SDK::UClass* actorClass = TActorClass::StaticClass())
		{
			actorClassName = actorClass->GetName();
		}

		for (int index = 0; index < count; ++index)
		{
			SDK::AActor* actor = actors[index];
			if (!actor)
			{
				continue;
			}

			const SDK::FVector worldLocation = actor->K2_GetActorLocation();
			const std::string displayName = GetCargoActorDisplayName(world, actor, kind);
			CargoMarker* existingMarker = FindMarkerByLocation(snapshot, kind, worldLocation);
			if (existingMarker)
			{
				if (!displayName.empty())
				{
					existingMarker->DisplayName = displayName;
				}
			}
			else if (allowCreateIfMissing)
			{
				AddOrUpdateMarker(
					snapshot,
					markerIndexes,
					kind,
					worldLocation,
					BuildLocationKey(kind, worldLocation),
					source,
					displayName,
					{});
			}

			if (index < maxLog)
			{
				LOG_INFO(
					"  Actor fallback %s #%d class=%s actor=%s custom_name='%s' world=%s",
					CargoKindToString(kind).c_str(),
					index + 1,
					actorClassName.c_str(),
					actor->GetName().c_str(),
					displayName.c_str(),
					FormatVector(worldLocation).c_str());
			}
		}

		return count;
	}

	void CaptureActorFallback(SDK::UWorld* world, CargoSnapshot& snapshot, std::unordered_map<std::string, size_t>& markerIndexes)
	{
		const size_t before = snapshot.Markers.size();
		snapshot.ActorReceiverCount = CaptureActorsOfClass<SDK::ABP_PackageReceiver_C>(
			world,
			snapshot,
			markerIndexes,
			CargoKind::Receiver,
			"actor_scan.receiver");
		if (snapshot.ActorReceiverCount == 0)
		{
			snapshot.ActorReceiverCount = CaptureActorsOfClass<SDK::ACrItemReceiverBuilding>(
				world,
				snapshot,
				markerIndexes,
				CargoKind::Receiver,
				"actor_scan.receiver_base",
				false);
		}
		snapshot.ActorSenderCount = CaptureActorsOfClass<SDK::ABP_PackageSender_C>(
			world,
			snapshot,
			markerIndexes,
			CargoKind::Sender,
			"actor_scan.sender");
		if (snapshot.ActorSenderCount == 0)
		{
			snapshot.ActorSenderCount = CaptureActorsOfClass<SDK::ACrItemSenderBuilding>(
				world,
				snapshot,
				markerIndexes,
				CargoKind::Sender,
				"actor_scan.sender_base",
				false);
		}

		if (snapshot.Markers.size() > before)
		{
			snapshot.UsedActorFallback = true;
		}
	}

	void CaptureTeleporters(
		SDK::UWorld* world,
		CargoSnapshot& snapshot,
		SDK::ACrGameStateBase* gameState,
		const BuildingCustomNameLookup& customNameLookup)
	{
		std::unordered_map<std::string, size_t> teleporterIndexes;
		std::unordered_map<std::string, size_t> teleporterIndexesByLocation;

		if (gameState && gameState->TeleportReplicator)
		{
			snapshot.HasTeleportReplicator = true;
			snapshot.UsedTeleporterReplicator = true;

			const SDK::FCrTeleportersContainer& teleporters = gameState->TeleportReplicator->TeleportersContainer;
			for (int index = 0; index < teleporters.TeleportersData.Num(); ++index)
			{
				const SDK::FCrTeleporterData& teleporter = teleporters.TeleportersData[index];
				const SDK::FVector worldLocation =
					MakeVector(teleporter.Location.X, teleporter.Location.Y, teleporter.Location.Z);
				const std::string internalKey = BuildReplicationHelperKey(teleporter.Teleporter);

				AddOrUpdateTeleporter(
					snapshot,
					teleporterIndexes,
					worldLocation,
					internalKey,
					"teleport_replicator",
					GetBuildingCustomName(world, teleporter.Teleporter, customNameLookup));
				teleporterIndexesByLocation.emplace(
					BuildLocationKey("teleporter", worldLocation),
					snapshot.Teleporters.size() - 1);
			}
		}

		if (!world)
		{
			snapshot.TeleporterCount = static_cast<int>(snapshot.Teleporters.size());
			return;
		}

		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrTeleporter::StaticClass(), &actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ACrTeleporter* teleporter = static_cast<SDK::ACrTeleporter*>(actors[index]);
			if (!teleporter)
			{
				continue;
			}

			const SDK::FVector worldLocation = teleporter->K2_GetActorLocation();
			std::string displayName = GetBuildingCustomName(world, teleporter);
			if (displayName.empty())
			{
				displayName = GetTeleporterDisplayName(teleporter);
			}
			const std::string locationKey = BuildLocationKey("teleporter", worldLocation);
			const auto existingByLocation = teleporterIndexesByLocation.find(locationKey);
			if (existingByLocation != teleporterIndexesByLocation.end())
			{
				TeleporterMarker& marker = snapshot.Teleporters[existingByLocation->second];
				if (!displayName.empty())
				{
					marker.DisplayName = displayName;
				}
				marker.Source = "teleport_replicator+actor";
				continue;
			}

			AddOrUpdateTeleporter(
				snapshot,
				teleporterIndexes,
				worldLocation,
				locationKey,
				"actor_scan.teleporter",
				displayName);
		}

		actors.Clear();
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ABP_Teleporter_C::StaticClass(), &actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ABP_Teleporter_C* teleporter = static_cast<SDK::ABP_Teleporter_C*>(actors[index]);
			if (!teleporter)
			{
				continue;
			}

			const SDK::FVector worldLocation = teleporter->K2_GetActorLocation();
			std::string displayName = GetBuildingCustomName(world, teleporter);
			if (displayName.empty())
			{
				displayName = GetTeleporterDisplayName(static_cast<SDK::ACrTeleporter*>(teleporter));
			}
			const std::string locationKey = BuildLocationKey("teleporter", worldLocation);
			const auto existingByLocation = teleporterIndexesByLocation.find(locationKey);
			if (existingByLocation != teleporterIndexesByLocation.end())
			{
				TeleporterMarker& marker = snapshot.Teleporters[existingByLocation->second];
				if (!displayName.empty())
				{
					marker.DisplayName = displayName;
				}
				if (marker.Source == "teleport_replicator")
				{
					marker.Source = "teleport_replicator+bp_actor";
				}
				else if (marker.Source.empty())
				{
					marker.Source = "bp_actor_scan.teleporter";
				}
				continue;
			}

			AddOrUpdateTeleporter(
				snapshot,
				teleporterIndexes,
				worldLocation,
				locationKey,
				"bp_actor_scan.teleporter",
				displayName);
			teleporterIndexesByLocation.emplace(locationKey, snapshot.Teleporters.size() - 1);
		}

		actors.Clear();
		SDK::UGameplayStatics::GetAllActorsOfClass(
			world,
			SDK::ACrMegamachineTeleporterDevice::StaticClass(),
			&actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ACrMegamachineTeleporterDevice* teleporter =
				static_cast<SDK::ACrMegamachineTeleporterDevice*>(actors[index]);
			if (!teleporter)
			{
				continue;
			}

			const SDK::FVector worldLocation = teleporter->K2_GetActorLocation();
			std::string displayName = GetBuildingCustomName(world, teleporter);
			if (displayName.empty())
			{
				displayName = GetTeleporterDisplayName(teleporter);
			}
			const std::string locationKey = BuildLocationKey("teleporter", worldLocation);
			const auto existingByLocation = teleporterIndexesByLocation.find(locationKey);
			if (existingByLocation != teleporterIndexesByLocation.end())
			{
				TeleporterMarker& marker = snapshot.Teleporters[existingByLocation->second];
				if (!displayName.empty())
				{
					marker.DisplayName = displayName;
				}
				if (marker.Source == "teleport_replicator")
				{
					marker.Source = "teleport_replicator+device_actor";
				}
				else if (marker.Source.empty())
				{
					marker.Source = "actor_scan.teleporter_device";
				}
				continue;
			}

			AddOrUpdateTeleporter(
				snapshot,
				teleporterIndexes,
				worldLocation,
				locationKey,
				"actor_scan.teleporter_device",
				displayName);
			teleporterIndexesByLocation.emplace(locationKey, snapshot.Teleporters.size() - 1);
		}

		snapshot.TeleporterCount = static_cast<int>(snapshot.Teleporters.size());
	}

	void CapturePlayers(SDK::UWorld* world, CargoSnapshot& snapshot)
	{
		if (!world)
		{
			snapshot.PlayerCount = 0;
			return;
		}

		std::unordered_map<std::string, size_t> playerIndexes;
		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrCharacterPlayerBase::StaticClass(), &actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ACrCharacterPlayerBase* player = static_cast<SDK::ACrCharacterPlayerBase*>(actors[index]);
			if (!player)
			{
				continue;
			}

			CapturePlayerPawn(
				snapshot,
				playerIndexes,
				static_cast<SDK::APawn*>(player),
				"actor_scan.player");
		}

		const int localPlayerCount = std::max(
			1,
			SDK::UGameplayStatics::GetNumLocalPlayerControllers(world));
		for (int playerIndex = 0; playerIndex < localPlayerCount; ++playerIndex)
		{
			SDK::APawn* playerPawn = SDK::UGameplayStatics::GetPlayerPawn(world, playerIndex);
			if (playerPawn)
			{
				CapturePlayerPawn(
					snapshot,
					playerIndexes,
					playerPawn,
					"local_player_pawn");
				continue;
			}

			SDK::APlayerController* controller =
				SDK::UGameplayStatics::GetPlayerController(world, playerIndex);
			if (!controller)
			{
				continue;
			}

			CapturePlayerPawn(
				snapshot,
				playerIndexes,
				controller->K2_GetPawn(),
				"local_player_controller");
		}

		snapshot.PlayerCount = static_cast<int>(snapshot.Players.size());
	}

	CargoSnapshot CopySnapshotImpl()
	{
		std::lock_guard<std::mutex> lock(g_snapshotMutex);
		return g_snapshot;
	}

	bool RefreshCargoSnapshotImpl(SDK::UWorld* world, const char* reason)
	{
		const bool isRealtimeRefresh = IsRealtimeRefreshReason(reason);
		const TrackedChimeraWorldState trackedState = CopyTrackedChimeraWorldState();
		CargoSnapshot nextSnapshot{};
		CargoSnapshot previousSnapshot{};
		const int64_t observedAtUnixMs = GetCurrentUnixTimeMilliseconds();
		{
			std::lock_guard<std::mutex> lock(g_snapshotMutex);
			nextSnapshot.Generation = g_snapshot.Generation + 1;
			previousSnapshot = g_snapshot;
		}
		nextSnapshot.Reason = reason ? reason : "unknown";
		nextSnapshot.WorldName = trackedState.WorldName;

		std::unordered_map<std::string, size_t> markerIndexes;
		SDK::ACrGameStateBase* gameState = TryGetGameState(world);
		const bool needsCustomNameLookup =
			gameState
			&& (gameState->PackageTransportReplicator != nullptr || gameState->TeleportReplicator != nullptr);
		const BuildingCustomNameLookup customNameLookup =
			needsCustomNameLookup ? BuildBuildingCustomNameLookup(world) : BuildingCustomNameLookup{};
		CaptureFromReplicator(
			world,
			nextSnapshot,
			gameState,
			&previousSnapshot,
			observedAtUnixMs,
			customNameLookup,
			markerIndexes);
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Cargo refresh '%s': replicator stage completed", nextSnapshot.Reason.c_str());
		}
		if (!isRealtimeRefresh || !nextSnapshot.HasPackageTransportReplicator)
		{
			CaptureActorFallback(world, nextSnapshot, markerIndexes);
			if (ShouldLogLifecycle())
			{
				LOG_INFO("Cargo refresh '%s': actor fallback stage completed", nextSnapshot.Reason.c_str());
			}
		}
		CaptureTeleporters(world, nextSnapshot, gameState, customNameLookup);
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Cargo refresh '%s': teleporter stage completed", nextSnapshot.Reason.c_str());
		}
		CapturePlayers(world, nextSnapshot);
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Cargo refresh '%s': player stage completed", nextSnapshot.Reason.c_str());
		}
		MergeRetainedConnections(nextSnapshot, previousSnapshot, observedAtUnixMs);

		nextSnapshot.RuptureCycle = CapturePreferredRuptureCycleSnapshot(world);
		ApplyRuptureCycleObservationTimestamp(nextSnapshot.RuptureCycle);
		StorePersistentRuptureCycleSnapshot(nextSnapshot.RuptureCycle);

		nextSnapshot.SenderCount = 0;
		nextSnapshot.ReceiverCount = 0;
		for (const CargoMarker& marker : nextSnapshot.Markers)
		{
			if (marker.Kind == CargoKind::Sender)
			{
				++nextSnapshot.SenderCount;
			}
			else
			{
				++nextSnapshot.ReceiverCount;
			}
		}

		if (nextSnapshot.WorldName.empty() && world)
		{
			nextSnapshot.WorldName = world->GetName();
		}

		if (nextSnapshot.Markers.empty() && ShouldLogCargoSnapshots())
		{
			LOG_WARN(
				"Cargo snapshot #%llu from '%s' found no cargo markers (replicator=%s, actor_receivers=%d, actor_senders=%d)",
				static_cast<unsigned long long>(nextSnapshot.Generation),
				nextSnapshot.Reason.c_str(),
				nextSnapshot.HasPackageTransportReplicator ? "yes" : "no",
				nextSnapshot.ActorReceiverCount,
				nextSnapshot.ActorSenderCount);
		}

#if !defined(MODLOADER_SERVER_BUILD)
		// On a dedicated-server client, prefer the authoritative snapshot received
		// from the server plugin over the local runtime scan. The local scan on a
		// dedicated client often yields incomplete data because the replication
		// state is partial.
		MapStateRuntime::Detail::CargoSnapshot remoteCargoSnapshot{};
		if (MapExtensionClient::Sync::TryCopyRemoteCargoSnapshot(remoteCargoSnapshot))
		{
			// Preserve the generation counter and reason from the local refresh
			// so the HTTP endpoint and log output remain consistent.
			remoteCargoSnapshot.Generation = nextSnapshot.Generation;
			remoteCargoSnapshot.Reason = nextSnapshot.Reason;
			// Use remote rupture cycle if available, else keep whatever the local
			// or remote-fallback already resolved.
			if (!remoteCargoSnapshot.RuptureCycle.Available && nextSnapshot.RuptureCycle.Available)
			{
				remoteCargoSnapshot.RuptureCycle = nextSnapshot.RuptureCycle;
			}
			ApplyRuptureCycleObservationTimestamp(remoteCargoSnapshot.RuptureCycle);
			StorePersistentRuptureCycleSnapshot(remoteCargoSnapshot.RuptureCycle);

			if (ShouldLogLifecycle())
			{
				LOG_INFO(
					"Using remote server snapshot: markers=%zu, connections=%zu, teleporters=%zu, players=%zu (local had markers=%zu)",
					remoteCargoSnapshot.Markers.size(),
					remoteCargoSnapshot.Connections.size(),
					remoteCargoSnapshot.Teleporters.size(),
					remoteCargoSnapshot.Players.size(),
					nextSnapshot.Markers.size());
			}

			nextSnapshot = remoteCargoSnapshot;
		}
#endif

		{
			std::lock_guard<std::mutex> lock(g_snapshotMutex);
			g_snapshot = nextSnapshot;
		}

		LogRuptureCycleIfNeeded(
			nextSnapshot.RuptureCycle,
			nextSnapshot.Generation,
			nextSnapshot.Reason.c_str());

		if (!isRealtimeRefresh)
		{
			LogSnapshotSummary(nextSnapshot);
		}
		else if (
			nextSnapshot.Markers.size() != previousSnapshot.Markers.size()
			|| nextSnapshot.Connections.size() != previousSnapshot.Connections.size()
			|| nextSnapshot.Teleporters.size() != previousSnapshot.Teleporters.size()
			|| nextSnapshot.Players.size() != previousSnapshot.Players.size())
		{
			LOG_INFO(
				"Realtime cargo snapshot #%llu changed: markers=%zu, connections=%zu, teleporters=%zu, players=%zu",
				static_cast<unsigned long long>(nextSnapshot.Generation),
				nextSnapshot.Markers.size(),
				nextSnapshot.Connections.size(),
				nextSnapshot.Teleporters.size(),
				nextSnapshot.Players.size());
		}
		return !nextSnapshot.Markers.empty()
			|| !nextSnapshot.Teleporters.empty()
			|| !nextSnapshot.Players.empty();
	}

	void TryRefreshCurrentWorldImpl(const char* reason)
	{
		TrackedChimeraWorldState trackedState{};
		bool invalidPointer = false;
		if (!TryCopyValidatedTrackedChimeraWorldState(trackedState, reason, true, invalidPointer))
		{
			if (invalidPointer)
			{
				ClearChimeraWorldState("Invalid Chimera world pointer");
				return;
			}

			if (ShouldLogLifecycle())
			{
				LOG_INFO(
					"Skipping snapshot refresh '%s': ChimeraMain world is not loaded yet",
					reason ? reason : "unknown");
			}
			return;
		}

		RefreshCargoSnapshotImpl(trackedState.World, reason);
	}
}

namespace MapStateRuntime
{
	namespace Detail
	{
		CargoSnapshot CopySnapshot()
		{
			return CopySnapshotImpl();
		}

		void LogRuntimePlanIfNeeded()
		{
			LogRuntimePlanIfNeededImpl();
		}

	bool IsRelevantRealtimeActor(SDK::AActor* actor)
	{
		return IsRelevantRealtimeActorImpl(actor);
	}

	bool RefreshCargoSnapshot(SDK::UWorld* world, const char* reason)
	{
		return RefreshCargoSnapshotImpl(world, reason);
	}

	void RequestCargoSnapshotRefresh(const char* reason)
	{
		const int64_t nowUnixMs = GetCurrentUnixTimeMilliseconds();
		const int64_t lastRequestAtUnixMs = g_lastHttpCargoRefreshRequestAtUnixMs.load();
		if (lastRequestAtUnixMs > 0 && nowUnixMs - lastRequestAtUnixMs < 250)
		{
			return;
		}

		g_lastHttpCargoRefreshRequestAtUnixMs.store(nowUnixMs);
		g_httpCargoRefreshRequested.store(true);
		if (ShouldLogLifecycle())
		{
			LOG_INFO(
				"Scheduled cargo snapshot refresh from %s",
				reason ? reason : "unknown");
		}
	}

	void TryRefreshCurrentWorld(const char* reason)
	{
		TryRefreshCurrentWorldImpl(reason);
	}
}
}

namespace MapStateRuntime
{
	void OnEngineInit()
	{
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Engine init received");
		}
		Detail::LogRuntimePlanIfNeeded();
		Detail::TryRefreshCurrentWorld("EngineInit");
	}

	void OnEngineShutdown()
	{
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Engine shutdown received");
		}
		g_engineTickAccumulatorSeconds = 0.0f;
		g_engineTickSeen = false;
		{
			std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
			g_lastChimeraWorld = nullptr;
			g_lastWorldName.clear();
			g_chimeraWorldReady = false;
			g_lastCargoActorRefreshAtUnixMs = 0;
			g_requestedCustomNameEntityIds.clear();
		}
		ResetRuptureCycleState();
	}

	void OnEngineTick(float deltaSeconds)
	{
		TrackedChimeraWorldState trackedState{};
		bool invalidPointer = false;
		if (!TryCopyValidatedTrackedChimeraWorldState(
			trackedState,
			"EngineTick",
			true,
			invalidPointer))
		{
			if (invalidPointer)
			{
				ClearChimeraWorldState("Invalid Chimera world pointer");
			}
			return;
		}

		if (!g_engineTickSeen)
		{
			g_engineTickSeen = true;
			if (ShouldLogLifecycle())
			{
				LOG_INFO(
					"EngineTick callback is alive; realtime cargo refresh interval=%d ms",
					MapExtensionPluginConfig::Config::RefreshIntervalMs());
			}
		}

#if !defined(MODLOADER_SERVER_BUILD)
		// On a dedicated-server client, periodically request an authoritative
		// snapshot from the server plugin. The sync client handles rate-limiting
		// and dedicated-session detection internally.
		MapExtensionClient::Sync::RequestSnapshotIfNeeded(trackedState.World, "EngineTick");
#endif

		const float refreshIntervalSeconds = GetRefreshIntervalSeconds();
		if (const char* requestedReason = ConsumeRequestedCargoRefreshReason())
		{
			Detail::RefreshCargoSnapshot(trackedState.World, requestedReason);
			g_engineTickAccumulatorSeconds = 0.0f;
			return;
		}

		if (deltaSeconds <= 0.0f || deltaSeconds > 5.0f)
		{
			g_engineTickAccumulatorSeconds = refreshIntervalSeconds;
		}
		else
		{
			g_engineTickAccumulatorSeconds += deltaSeconds;
		}

		if (g_engineTickAccumulatorSeconds < refreshIntervalSeconds)
		{
			return;
		}

		g_engineTickAccumulatorSeconds = 0.0f;
		Detail::RefreshCargoSnapshot(trackedState.World, "EngineTick");
	}

	void OnActorBeginPlay(void* actor)
	{
		TrackedChimeraWorldState trackedState{};
		bool invalidPointer = false;
		if (!actor
			|| !TryCopyValidatedTrackedChimeraWorldState(
				trackedState,
				"ActorBeginPlay",
				true,
				invalidPointer))
		{
			if (invalidPointer)
			{
				ClearChimeraWorldState("Invalid Chimera world pointer");
			}
			return;
		}

		SDK::AActor* beginPlayActor = static_cast<SDK::AActor*>(actor);
		if (!TryObjectHasOuterInChain(
			static_cast<SDK::UObject*>(beginPlayActor),
			static_cast<SDK::UObject*>(trackedState.World)))
		{
			return;
		}
		const bool cargoActor = IsCargoRealtimeActorImpl(beginPlayActor);
		const bool auxiliaryActor = IsAuxiliaryRealtimeActorImpl(beginPlayActor);
		const bool ruptureActor = IsRelevantRuptureActorImpl(beginPlayActor);
		if (!cargoActor && !auxiliaryActor && !ruptureActor)
		{
			return;
		}

		if (!cargoActor && !auxiliaryActor)
		{
			// Rupture visuals can spawn in bursts while the player moves. Avoid a full cargo scan here.
			return;
		}

		// Once the transport replicator is active, EngineTick already refreshes the
		// authoritative cargo snapshot. Refreshing again on every cargo-network actor
		// callback only adds hitching during spawn bursts. Keep teleporter/player
		// begin-play refreshes active because they are not covered by that replicator.
		if (cargoActor && HasPackageTransportReplicatorSnapshot())
		{
			return;
		}

		// When EngineTick is running, auxiliary actor callbacks (teleporters, players)
		// are already captured by the 500 ms tick scan. Skip the redundant refresh.
		if (!cargoActor && auxiliaryActor && g_engineTickSeen)
		{
			return;
		}

		const int64_t nowUnixMs = GetCurrentUnixTimeMilliseconds();
		{
			std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
			if (g_lastCargoActorRefreshAtUnixMs > 0
				&& nowUnixMs - g_lastCargoActorRefreshAtUnixMs < kCargoActorRefreshMinIntervalMs)
			{
				return;
			}
			g_lastCargoActorRefreshAtUnixMs = nowUnixMs;
		}

		if (ShouldLogLifecycle())
		{
			LOG_INFO("ActorBeginPlay refresh trigger: class=%s actor=%s source=%s",
				beginPlayActor->Class ? beginPlayActor->Class->GetName().c_str() : "(null)",
				beginPlayActor->GetName().c_str(),
				ruptureActor ? "rupture" : (auxiliaryActor ? "auxiliary" : "cargo"));
		}

		g_engineTickAccumulatorSeconds = 0.0f;
		Detail::RefreshCargoSnapshot(
			trackedState.World,
			ruptureActor ? "ActorBeginPlay(Rupture)" : "ActorBeginPlay");
	}

	void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName)
	{
		++g_anyWorldBeginPlayCount;
		const char* safeName = worldName ? worldName : "(null)";
		if (ShouldLogLifecycle())
		{
			LOG_INFO("AnyWorldBeginPlay #%d: world=%p name=%s", g_anyWorldBeginPlayCount, static_cast<void*>(world), safeName);
		}

		if (std::strstr(safeName, "ChimeraMain") != nullptr)
		{
			{
				std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
				g_lastChimeraWorld = world;
				g_lastWorldName = safeName;
				g_chimeraWorldReady = false;
				g_lastCargoActorRefreshAtUnixMs = 0;
				g_requestedCustomNameEntityIds.clear();
			}
			g_engineTickAccumulatorSeconds = 0.0f;
			ResetRuptureCycleState();
			if (ShouldLogLifecycle())
			{
				LOG_INFO("ChimeraMain detected: deferring cargo snapshot until the world is ready");
			}
		}
		else
		{
			TrackedChimeraWorldState trackedState{};
			bool invalidPointer = false;
			if (TryCopyValidatedTrackedChimeraWorldState(
				trackedState,
				"Non-Chimera world begin play",
				false,
				invalidPointer))
			{
				// A new world has begun play while ChimeraMain was tracked.
				// This is a reliable signal that ChimeraMain is being torn down.
				// Clear the stale pointer immediately to prevent ExperienceLoadComplete
				// or EngineTick from calling into a freed UWorld (crash on return to menu).
				if (ShouldLogLifecycle())
				{
					LOG_INFO(
						"Non-Chimera world supersedes ChimeraMain: clearing tracked state (new world=%p name=%s)",
						static_cast<void*>(world),
						safeName);
				}
				ClearChimeraWorldState("Non-Chimera world superseded ChimeraMain");
			}
			else if (invalidPointer)
			{
				ClearChimeraWorldState("Invalid Chimera world pointer");
			}
			else
			{
				ClearChimeraWorldState("Non-Chimera world begin play");
			}
		}
	}


	void OnSaveLoaded()
	{
		TrackedChimeraWorldState trackedState{};
		bool invalidPointer = false;
		++g_saveLoadedCount;
		if (ShouldLogLifecycle())
		{
			LOG_INFO("SaveLoaded #%d: refreshing cargo snapshot from save state", g_saveLoadedCount);
		}
		if (!TryCopyValidatedTrackedChimeraWorldState(
			trackedState,
			"SaveLoaded",
			false,
			invalidPointer))
		{
			if (invalidPointer)
			{
				ClearChimeraWorldState("Invalid Chimera world pointer");
			}
			return;
		}

		if (trackedState.World)
		{
			MarkChimeraWorldReady("SaveLoaded");
		}
		Detail::TryRefreshCurrentWorld("SaveLoaded");
	}

	void OnExperienceLoadComplete()
	{
		TrackedChimeraWorldState trackedState{};
		bool invalidPointer = false;
		++g_experienceLoadedCount;
		if (ShouldLogLifecycle())
		{
			LOG_INFO("ExperienceLoadComplete #%d: refreshing cargo snapshot from runtime replication", g_experienceLoadedCount);
		}
		if (!TryCopyValidatedTrackedChimeraWorldState(
			trackedState,
			"ExperienceLoadComplete",
			false,
			invalidPointer))
		{
			if (invalidPointer)
			{
				ClearChimeraWorldState("Invalid Chimera world pointer");
			}
			return;
		}

		if (trackedState.World)
		{
			MarkChimeraWorldReady("ExperienceLoadComplete");
		}
		Detail::TryRefreshCurrentWorld("ExperienceLoadComplete");
	}

	void OnBeforeWorldEndPlay(SDK::UWorld* world, const char* worldName)
	{
		const char* safeName = worldName ? worldName : "(null)";
		if (ShouldLogLifecycle())
		{
			LOG_INFO("BeforeWorldEndPlay: world=%p name=%s", static_cast<void*>(world), safeName);
		}

		// If the ending world is our tracked ChimeraMain, mark it as not ready
		// while the world pointer is still valid.
		std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
		if (g_lastChimeraWorld && g_lastChimeraWorld == world)
		{
			g_chimeraWorldReady = false;
		}
	}

	void OnAfterWorldEndPlay(SDK::UWorld* world, const char* worldName)
	{
		const char* safeName = worldName ? worldName : "(null)";
		if (ShouldLogLifecycle())
		{
			LOG_INFO("AfterWorldEndPlay: world=%p name=%s", static_cast<void*>(world), safeName);
		}

		// If the ended world is our tracked ChimeraMain, clear everything.
		// The world pointer may be partially torn down at this point.
		bool wasTracked = false;
		{
			std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
			if (g_lastChimeraWorld && g_lastChimeraWorld == world)
			{
				wasTracked = true;
			}
		}

		if (wasTracked)
		{
			ClearChimeraWorldState("AfterWorldEndPlay");
		}
	}

	void OnPlayerJoined(void* playerController)
	{
		(void)playerController;
		if (ShouldLogLifecycle())
		{
			LOG_INFO("PlayerJoined: controller=%p", playerController);
		}
		// On server, this is a good time to try bootstrapping the world
		// if we haven't yet. On client, it's mostly informational.
		Detail::TryRefreshCurrentWorld("PlayerJoined");
	}
}
