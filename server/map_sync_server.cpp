#include "map_sync_server.h"

#include "../map_state_capture.h"
#include "../plugin_helpers.h"
#include "../shared/map_sync_protocol.h"

#include "Chimera_classes.hpp"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"
#include "plugin_network_helpers.h"

#include <chrono>
#include <cstring>
#include <string>

namespace
{
	using CargoConnection = MapStateRuntime::Detail::CargoConnection;
	using CargoMarker = MapStateRuntime::Detail::CargoMarker;
	using CargoSnapshot = MapStateRuntime::Detail::CargoSnapshot;
	using PlayerMarker = MapStateRuntime::Detail::PlayerMarker;
	using RuptureCycleSnapshot = MapStateRuntime::Detail::RuptureCycleSnapshot;
	using TeleporterMarker = MapStateRuntime::Detail::TeleporterMarker;

	PluginNetworkServerMessageCallback g_snapshotRequestHandle = nullptr;
	SDK::UWorld* g_trackedWorld = nullptr;
	bool g_worldReady = false;
	uint64_t g_snapshotId = 0;
	uint64_t g_sequence = 0;

	int64_t GetCurrentUnixTimeMilliseconds()
	{
		using namespace std::chrono;
		const auto now = system_clock::now();
		return duration_cast<milliseconds>(now.time_since_epoch()).count();
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

	bool TryGetWorldNameSafe(SDK::UWorld* world, std::string& outName)
	{
		outName.clear();
		if (!TryProbeWorldNameRaw(world))
		{
			return false;
		}

		try
		{
			outName = world->GetName();
			return !outName.empty();
		}
		catch (...)
		{
			outName.clear();
			return false;
		}
	}

	bool IsChimeraWorldName(const char* worldName)
	{
		return worldName != nullptr && std::strstr(worldName, "ChimeraMain") != nullptr;
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

	bool TryIsObjectOfClass(SDK::UObject* object, SDK::UClass* expectedClass)
	{
		if (!object || !expectedClass)
		{
			return false;
		}

		__try
		{
			return object->IsA(expectedClass);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	struct RuptureCycleState final
	{
		SDK::EEnviroWave Wave = SDK::EEnviroWave::None;
		SDK::EEnviroWaveStage Stage = SDK::EEnviroWaveStage::None;
		SDK::EEnviroWavePreWaveSubstage PreWaveSubstage = SDK::EEnviroWavePreWaveSubstage::None;
		SDK::EEnviroWaveFadeoutSubstage FadeoutSubstage = SDK::EEnviroWaveFadeoutSubstage::None;
		SDK::EEnviroWaveGrowbackSubstage GrowbackSubstage = SDK::EEnviroWaveGrowbackSubstage::None;
		double ElapsedSeconds = 0.0;
		bool HasElapsed = false;
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

	SDK::ACrGameStateBase* TryGetGameState(SDK::UWorld* world)
	{
		std::string worldName;
		if (!TryGetWorldNameSafe(world, worldName))
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

	bool TryReadWaveSubsystemFields(SDK::UCrEnviroWaveSubsystem* waveSubsystem, RuptureCycleState& outState)
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
			outState.HasElapsed = true;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
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

	const char* EnviroWaveStepToString(const RuptureCycleState& state)
	{
		if (state.Stage == SDK::EEnviroWaveStage::PreWave
			&& state.PreWaveSubstage != SDK::EEnviroWavePreWaveSubstage::None)
		{
			return PreWaveSubstageToString(state.PreWaveSubstage);
		}
		if (state.Stage == SDK::EEnviroWaveStage::Fadeout
			&& state.FadeoutSubstage != SDK::EEnviroWaveFadeoutSubstage::None)
		{
			return FadeoutSubstageToString(state.FadeoutSubstage);
		}
		if (state.Stage == SDK::EEnviroWaveStage::Growback
			&& state.GrowbackSubstage != SDK::EEnviroWaveGrowbackSubstage::None)
		{
			return GrowbackSubstageToString(state.GrowbackSubstage);
		}
		return "None";
	}

	void ResetRuntimeState()
	{
		g_trackedWorld = nullptr;
		g_worldReady = false;
	}

	bool CaptureRuptureCycleState(SDK::UWorld* world, RuptureCycleState& outState)
	{
		SDK::ACrGameStateBase* gameState = TryGetGameState(world);
		if (!gameState || !gameState->bIsDedicatedServer)
		{
			return false;
		}

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

		return TryReadWaveSubsystemFields(waveSubsystem, outState);
	}

	bool TryBootstrapCurrentWorld(const char* reason)
	{
		SDK::UWorld* world = SDK::UWorld::GetWorld();
		if (!world)
		{
			return false;
		}

		std::string worldName;
		if (!TryGetWorldNameSafe(world, worldName))
		{
			return false;
		}
		if (!IsChimeraWorldName(worldName.c_str()))
		{
			return false;
		}

		if (g_trackedWorld != world)
		{
			g_trackedWorld = world;
			g_worldReady = false;
			LOG_DEBUG(
				"Server snapshot sync bootstrapped ChimeraMain world=%p via %s",
				static_cast<void*>(world),
				reason ? reason : "unknown");
		}

		RuptureCycleState probeState{};
		if (!g_worldReady && CaptureRuptureCycleState(world, probeState))
		{
			g_worldReady = true;
		}

		return g_trackedWorld != nullptr;
	}

	void FillRupturePacket(MapSyncProtocol::ServerRuptureStatePacket& packet, const RuptureCycleSnapshot& snapshot)
	{
		packet.protocol_version = MapSyncProtocol::kProtocolVersion;
		packet.snapshot_id = g_snapshotId;
		packet.sequence = ++g_sequence;
		packet.observed_at_unix_ms = snapshot.HasObservedAtUnixMs
			? snapshot.ObservedAtUnixMs
			: GetCurrentUnixTimeMilliseconds();
		if (snapshot.Available)
		{
			packet.state_flags |= MapSyncProtocol::kServerRuptureStateAvailable;
		}
		if (snapshot.HasElapsed)
		{
			packet.state_flags |= MapSyncProtocol::kServerRuptureStateHasElapsed;
			packet.elapsed_seconds = snapshot.ElapsedSeconds;
		}
		if (packet.observed_at_unix_ms > 0)
		{
			packet.state_flags |= MapSyncProtocol::kServerRuptureStateHasObservedAt;
		}
		MapSyncProtocol::CopyCStringTruncated(packet.wave, sizeof(packet.wave), snapshot.Wave.c_str());
		MapSyncProtocol::CopyCStringTruncated(packet.stage, sizeof(packet.stage), snapshot.Stage.c_str());
		MapSyncProtocol::CopyCStringTruncated(packet.step, sizeof(packet.step), snapshot.Step.c_str());
	}

	void FillPlayerEntry(MapSyncProtocol::ServerPlayerEntry& entry, const PlayerMarker& marker)
	{
		entry.world_x = static_cast<float>(marker.WorldLocation.X);
		entry.world_y = static_cast<float>(marker.WorldLocation.Y);
		entry.world_z = static_cast<float>(marker.WorldLocation.Z);
		MapSyncProtocol::CopyCStringTruncated(entry.label, sizeof(entry.label), marker.DisplayName.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.source, sizeof(entry.source), marker.Source.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.unique_key, sizeof(entry.unique_key), marker.PublicKey.c_str());
	}

	void FillTeleporterEntry(MapSyncProtocol::ServerTeleporterEntry& entry, const TeleporterMarker& marker)
	{
		entry.world_x = static_cast<float>(marker.WorldLocation.X);
		entry.world_y = static_cast<float>(marker.WorldLocation.Y);
		entry.world_z = static_cast<float>(marker.WorldLocation.Z);
		MapSyncProtocol::CopyCStringTruncated(entry.label, sizeof(entry.label), marker.DisplayName.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.source, sizeof(entry.source), marker.Source.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.unique_key, sizeof(entry.unique_key), marker.PublicKey.c_str());
	}

	void FillCargoMarkerEntry(MapSyncProtocol::ServerCargoMarkerEntry& entry, const CargoMarker& marker)
	{
		entry.world_x = static_cast<float>(marker.WorldLocation.X);
		entry.world_y = static_cast<float>(marker.WorldLocation.Y);
		entry.world_z = static_cast<float>(marker.WorldLocation.Z);
		entry.kind = marker.Kind == MapStateRuntime::Detail::CargoKind::Sender
			? MapSyncProtocol::kCargoMarkerSender
			: MapSyncProtocol::kCargoMarkerReceiver;
		MapSyncProtocol::CopyCStringTruncated(entry.display_name, sizeof(entry.display_name), marker.DisplayName.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.resource, sizeof(entry.resource), marker.ResourceSummary.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.source, sizeof(entry.source), marker.Source.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.unique_key, sizeof(entry.unique_key), marker.PublicKey.c_str());
	}

	void FillCargoConnectionEntry(MapSyncProtocol::ServerCargoConnectionEntry& entry, const CargoConnection& connection)
	{
		entry.sender_world_x = static_cast<float>(connection.SenderWorldLocation.X);
		entry.sender_world_y = static_cast<float>(connection.SenderWorldLocation.Y);
		entry.sender_world_z = static_cast<float>(connection.SenderWorldLocation.Z);
		entry.receiver_world_x = static_cast<float>(connection.ReceiverWorldLocation.X);
		entry.receiver_world_y = static_cast<float>(connection.ReceiverWorldLocation.Y);
		entry.receiver_world_z = static_cast<float>(connection.ReceiverWorldLocation.Z);
		entry.requested_amount = connection.RequestedAmount;
		entry.observed_at_unix_ms = connection.LastObservedAtUnixMs;
		MapSyncProtocol::CopyCStringTruncated(entry.sender_key, sizeof(entry.sender_key), connection.SenderKey.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.receiver_key, sizeof(entry.receiver_key), connection.ReceiverKey.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.sender_label, sizeof(entry.sender_label), connection.SenderLabel.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.receiver_label, sizeof(entry.receiver_label), connection.ReceiverLabel.c_str());
		MapSyncProtocol::CopyCStringTruncated(entry.item_name, sizeof(entry.item_name), connection.ItemDisplayName.c_str());
	}

	template <typename TPacket, typename TItem, typename TCollection, size_t Capacity, typename FillFn>
	void SendChunkedCollection(
		void* senderPlayerController,
		uint64_t snapshotId,
		const TCollection& collection,
		FillFn fillFn)
	{
		IPluginHooks* hooks = GetHooks();
		const IPluginSelf* self = GetPluginSelf();
		if (!hooks || !hooks->Network || !self || !senderPlayerController)
		{
			return;
		}

		const size_t totalCount = collection.size();
		const uint16_t chunkCount = totalCount == 0
			? 0
			: static_cast<uint16_t>((totalCount + Capacity - 1) / Capacity);

		for (uint16_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
		{
			TPacket packet{};
			packet.protocol_version = MapSyncProtocol::kProtocolVersion;
			packet.snapshot_id = snapshotId;
			packet.chunk_index = chunkIndex;
			packet.chunk_count = chunkCount;
			const size_t startIndex = static_cast<size_t>(chunkIndex) * Capacity;
			const size_t remaining = totalCount - startIndex;
			packet.item_count = static_cast<uint16_t>(remaining > Capacity ? Capacity : remaining);

			for (uint16_t itemIndex = 0; itemIndex < packet.item_count; ++itemIndex)
			{
				fillFn(packet.items[itemIndex], collection[startIndex + itemIndex]);
			}

			Network::SendPacketToPlayer(hooks, self, senderPlayerController, packet);
		}
	}

	void SendFullSnapshotToPlayer(void* senderPlayerController, const char* reason)
	{
		IPluginHooks* hooks = GetHooks();
		const IPluginSelf* self = GetPluginSelf();
		if (!hooks || !hooks->Network || !self || !senderPlayerController)
		{
			return;
		}

		TryBootstrapCurrentWorld(reason);
		if (!g_trackedWorld)
		{
			return;
		}

		MapStateRuntime::Detail::RefreshCargoSnapshot(g_trackedWorld, reason ? reason : "ServerSnapshotRequest");
		CargoSnapshot snapshot = MapStateRuntime::Detail::CopySnapshot();
		const uint64_t snapshotId = ++g_snapshotId;

		MapSyncProtocol::ServerRuptureStatePacket rupturePacket{};
		FillRupturePacket(rupturePacket, snapshot.RuptureCycle);
		rupturePacket.snapshot_id = snapshotId;
		Network::SendPacketToPlayer(hooks, self, senderPlayerController, rupturePacket);

		MapSyncProtocol::ServerSnapshotBeginPacket beginPacket{};
		beginPacket.protocol_version = MapSyncProtocol::kProtocolVersion;
		beginPacket.content_flags = MapSyncProtocol::kSnapshotHasRupture;
		beginPacket.snapshot_id = snapshotId;
		beginPacket.generation = snapshot.Generation != 0 ? snapshot.Generation : snapshotId;
		beginPacket.players_count = static_cast<uint16_t>(snapshot.Players.size());
		beginPacket.teleporters_count = static_cast<uint16_t>(snapshot.Teleporters.size());
		beginPacket.cargo_markers_count = static_cast<uint16_t>(snapshot.Markers.size());
		beginPacket.cargo_connections_count = static_cast<uint16_t>(snapshot.Connections.size());
		beginPacket.players_chunk_count = static_cast<uint16_t>((snapshot.Players.size() + MapSyncProtocol::kPlayerChunkCapacity - 1) / MapSyncProtocol::kPlayerChunkCapacity);
		beginPacket.teleporters_chunk_count = static_cast<uint16_t>((snapshot.Teleporters.size() + MapSyncProtocol::kTeleporterChunkCapacity - 1) / MapSyncProtocol::kTeleporterChunkCapacity);
		beginPacket.cargo_markers_chunk_count = static_cast<uint16_t>((snapshot.Markers.size() + MapSyncProtocol::kCargoMarkerChunkCapacity - 1) / MapSyncProtocol::kCargoMarkerChunkCapacity);
		beginPacket.cargo_connections_chunk_count = static_cast<uint16_t>((snapshot.Connections.size() + MapSyncProtocol::kCargoConnectionChunkCapacity - 1) / MapSyncProtocol::kCargoConnectionChunkCapacity);
		MapSyncProtocol::CopyCStringTruncated(beginPacket.world_name, sizeof(beginPacket.world_name), snapshot.WorldName.c_str());
		if (!snapshot.Players.empty()) beginPacket.content_flags |= MapSyncProtocol::kSnapshotHasPlayers;
		if (!snapshot.Teleporters.empty()) beginPacket.content_flags |= MapSyncProtocol::kSnapshotHasTeleporters;
		if (!snapshot.Markers.empty()) beginPacket.content_flags |= MapSyncProtocol::kSnapshotHasCargoMarkers;
		if (!snapshot.Connections.empty()) beginPacket.content_flags |= MapSyncProtocol::kSnapshotHasCargoConnections;
		Network::SendPacketToPlayer(hooks, self, senderPlayerController, beginPacket);

		SendChunkedCollection<MapSyncProtocol::ServerPlayersChunkPacket, MapSyncProtocol::ServerPlayerEntry, decltype(snapshot.Players), MapSyncProtocol::kPlayerChunkCapacity>(
			senderPlayerController,
			snapshotId,
			snapshot.Players,
			[](MapSyncProtocol::ServerPlayerEntry& entry, const PlayerMarker& marker)
			{
				FillPlayerEntry(entry, marker);
			});

		SendChunkedCollection<MapSyncProtocol::ServerTeleportersChunkPacket, MapSyncProtocol::ServerTeleporterEntry, decltype(snapshot.Teleporters), MapSyncProtocol::kTeleporterChunkCapacity>(
			senderPlayerController,
			snapshotId,
			snapshot.Teleporters,
			[](MapSyncProtocol::ServerTeleporterEntry& entry, const TeleporterMarker& marker)
			{
				FillTeleporterEntry(entry, marker);
			});

		SendChunkedCollection<MapSyncProtocol::ServerCargoMarkersChunkPacket, MapSyncProtocol::ServerCargoMarkerEntry, decltype(snapshot.Markers), MapSyncProtocol::kCargoMarkerChunkCapacity>(
			senderPlayerController,
			snapshotId,
			snapshot.Markers,
			[](MapSyncProtocol::ServerCargoMarkerEntry& entry, const CargoMarker& marker)
			{
				FillCargoMarkerEntry(entry, marker);
			});

		SendChunkedCollection<MapSyncProtocol::ServerCargoConnectionsChunkPacket, MapSyncProtocol::ServerCargoConnectionEntry, decltype(snapshot.Connections), MapSyncProtocol::kCargoConnectionChunkCapacity>(
			senderPlayerController,
			snapshotId,
			snapshot.Connections,
			[](MapSyncProtocol::ServerCargoConnectionEntry& entry, const CargoConnection& connection)
			{
				FillCargoConnectionEntry(entry, connection);
			});

		MapSyncProtocol::ServerSnapshotEndPacket endPacket{};
		endPacket.protocol_version = MapSyncProtocol::kProtocolVersion;
		endPacket.success = 1;
		endPacket.snapshot_id = snapshotId;
		endPacket.generation = beginPacket.generation;
		endPacket.players_count = beginPacket.players_count;
		endPacket.teleporters_count = beginPacket.teleporters_count;
		endPacket.cargo_markers_count = beginPacket.cargo_markers_count;
		endPacket.cargo_connections_count = beginPacket.cargo_connections_count;
		Network::SendPacketToPlayer(hooks, self, senderPlayerController, endPacket);
	}

	void OnSnapshotRequest(void* senderPlayerController, const MapSyncProtocol::ClientSnapshotRequestPacket& packet)
	{
		if (packet.protocol_version != MapSyncProtocol::kProtocolVersion)
		{
			LOG_WARN(
				"Ignoring client snapshot request with protocol_version=%u (expected=%u)",
				packet.protocol_version,
				MapSyncProtocol::kProtocolVersion);
			return;
		}

		SendFullSnapshotToPlayer(senderPlayerController, "ClientSnapshotRequest");
	}
}

namespace MapExtensionServer
{
	namespace Sync
	{
		bool Initialize()
		{
			IPluginHooks* hooks = GetHooks();
			const IPluginSelf* self = GetPluginSelf();
			if (!hooks || !self)
			{
				return false;
			}

			if (!hooks->Network)
			{
				LOG_WARN("Network channel unavailable on server build; dedicated snapshot sync disabled.");
				return true;
			}

			if (!hooks->Network->IsServer())
			{
				return true;
			}

			g_snapshotRequestHandle = Network::OnServerReceive<MapSyncProtocol::ClientSnapshotRequestPacket>(
				hooks,
				self,
				[](void* senderPlayerController, const MapSyncProtocol::ClientSnapshotRequestPacket& packet)
				{
					OnSnapshotRequest(senderPlayerController, packet);
				});

			TryBootstrapCurrentWorld("ServerInitialize");
			return true;
		}

		void Shutdown()
		{
			IPluginHooks* hooks = GetHooks();
			const IPluginSelf* self = GetPluginSelf();
			if (g_snapshotRequestHandle && hooks && hooks->Network && self && hooks->Network->IsServer())
			{
				hooks->Network->UnregisterServerMessageHandler(
					self,
					typeid(MapSyncProtocol::ClientSnapshotRequestPacket).name(),
					g_snapshotRequestHandle);
			}

			g_snapshotRequestHandle = nullptr;
			g_snapshotId = 0;
			g_sequence = 0;
			ResetRuntimeState();
		}

		void OnEngineInit()
		{
			TryBootstrapCurrentWorld("EngineInit");
		}

		void OnEngineShutdown()
		{
			ResetRuntimeState();
		}

		void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName)
		{
			if (IsChimeraWorldName(worldName))
			{
				g_trackedWorld = world;
				g_worldReady = false;
				return;
			}
		}

		void OnBeforeWorldEndPlay(SDK::UWorld* world, const char* worldName)
		{
			if (!g_trackedWorld || g_trackedWorld != world)
			{
				return;
			}

			if (IsChimeraWorldName(worldName))
			{
				g_worldReady = false;
			}
		}

		void OnAfterWorldEndPlay(SDK::UWorld* world, const char* worldName)
		{
			if (g_trackedWorld == world && IsChimeraWorldName(worldName))
			{
				ResetRuntimeState();
			}
		}

		void OnExperienceLoadComplete()
		{
			TryBootstrapCurrentWorld("ExperienceLoadComplete");
		}

		void OnPlayerJoined(void* playerController)
		{
			(void)playerController;
			TryBootstrapCurrentWorld("PlayerJoined");
		}
	}
}
