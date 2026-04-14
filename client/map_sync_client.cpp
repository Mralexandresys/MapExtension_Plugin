#include "map_sync_client.h"

#include "map_state_remote_cache.h"
#include "../plugin_helpers.h"
#include "../shared/map_sync_protocol.h"

#include "Chimera_classes.hpp"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"
#include "plugin_network_helpers.h"

#include <chrono>

	namespace
	{
		// Minimum interval between two outbound snapshot requests sent to the
		// server. Raised from 1500 ms to 5000 ms to reduce the number of
		// IPluginNetworkChannel packets processed on the main thread in
		// dedicated-server sessions.
		constexpr int64_t kMinRequestIntervalMs = 5000;
		// Minimum interval between two accepted server-side snapshots. A fresh
		// server snapshot replaces the local runtime scan, so polling more
		// frequently than the server refresh rate wastes CPU. 15 s is ample
		// given the 5 s request gate above.
		constexpr int64_t kRefreshIntervalMs = 15000;

	PluginNetworkMessageCallback g_ruptureReceiveHandle = nullptr;
	PluginNetworkMessageCallback g_snapshotBeginReceiveHandle = nullptr;
	PluginNetworkMessageCallback g_snapshotEndReceiveHandle = nullptr;
	PluginNetworkMessageCallback g_playersChunkReceiveHandle = nullptr;
	PluginNetworkMessageCallback g_teleportersChunkReceiveHandle = nullptr;
	PluginNetworkMessageCallback g_cargoMarkersChunkReceiveHandle = nullptr;
	PluginNetworkMessageCallback g_cargoConnectionsChunkReceiveHandle = nullptr;
	uint64_t g_requestSequence = 0;
	int64_t g_lastRequestAtUnixMs = 0;
	bool g_protocolMismatchLogged = false;

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

		SDK::ACrPlayerControllerBase* TryGetLocalCrPlayerController(SDK::UWorld* world)
		{
			std::string worldName;
			if (!TryGetWorldNameSafe(world, worldName))
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

	template <typename TPacket>
	bool ValidateProtocol(const TPacket& packet)
	{
		if (packet.protocol_version == MapSyncProtocol::kProtocolVersion)
		{
			return true;
		}

		if (!g_protocolMismatchLogged)
		{
			g_protocolMismatchLogged = true;
			LOG_WARN(
				"Ignoring packet with protocol_version=%u (expected=%u)",
				packet.protocol_version,
				MapSyncProtocol::kProtocolVersion);
		}
		return false;
	}

	void UnregisterHandler(PluginNetworkMessageCallback& handle, const char* typeTag)
	{
		IPluginHooks* hooks = GetHooks();
		const IPluginSelf* self = GetPluginSelf();
		if (!handle || !hooks || !hooks->Network || !self || hooks->Network->IsServer())
		{
			handle = nullptr;
			return;
		}

		hooks->Network->UnregisterMessageHandler(self, typeTag, handle);
		handle = nullptr;
	}
}

namespace MapExtensionClient
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
				LOG_WARN("Network channel unavailable on client build; dedicated snapshot sync disabled.");
				return true;
			}

			if (hooks->Network->IsServer())
			{
				return true;
			}

			g_ruptureReceiveHandle = Network::OnReceive<MapSyncProtocol::ServerRuptureStatePacket>(
				hooks,
				self,
				[](const MapSyncProtocol::ServerRuptureStatePacket& packet)
				{
					if (!ValidateProtocol(packet))
					{
						return;
					}

					RemoteCache::StoreRuptureState(packet, GetCurrentUnixTimeMilliseconds());
				});

			g_snapshotBeginReceiveHandle = Network::OnReceive<MapSyncProtocol::ServerSnapshotBeginPacket>(
				hooks,
				self,
				[](const MapSyncProtocol::ServerSnapshotBeginPacket& packet)
				{
					if (!ValidateProtocol(packet))
					{
						return;
					}
					RemoteCache::BeginSnapshot(packet);
				});

			g_snapshotEndReceiveHandle = Network::OnReceive<MapSyncProtocol::ServerSnapshotEndPacket>(
				hooks,
				self,
				[](const MapSyncProtocol::ServerSnapshotEndPacket& packet)
				{
					if (!ValidateProtocol(packet))
					{
						return;
					}
					RemoteCache::FinalizeSnapshot(packet);
				});

			g_playersChunkReceiveHandle = Network::OnReceive<MapSyncProtocol::ServerPlayersChunkPacket>(
				hooks,
				self,
				[](const MapSyncProtocol::ServerPlayersChunkPacket& packet)
				{
					if (!ValidateProtocol(packet))
					{
						return;
					}
					RemoteCache::StorePlayersChunk(packet);
				});

			g_teleportersChunkReceiveHandle = Network::OnReceive<MapSyncProtocol::ServerTeleportersChunkPacket>(
				hooks,
				self,
				[](const MapSyncProtocol::ServerTeleportersChunkPacket& packet)
				{
					if (!ValidateProtocol(packet))
					{
						return;
					}
					RemoteCache::StoreTeleportersChunk(packet);
				});

			g_cargoMarkersChunkReceiveHandle = Network::OnReceive<MapSyncProtocol::ServerCargoMarkersChunkPacket>(
				hooks,
				self,
				[](const MapSyncProtocol::ServerCargoMarkersChunkPacket& packet)
				{
					if (!ValidateProtocol(packet))
					{
						return;
					}
					RemoteCache::StoreCargoMarkersChunk(packet);
				});

			g_cargoConnectionsChunkReceiveHandle = Network::OnReceive<MapSyncProtocol::ServerCargoConnectionsChunkPacket>(
				hooks,
				self,
				[](const MapSyncProtocol::ServerCargoConnectionsChunkPacket& packet)
				{
					if (!ValidateProtocol(packet))
					{
						return;
					}
					RemoteCache::StoreCargoConnectionsChunk(packet);
				});

			return true;
		}

		void Shutdown()
		{
			UnregisterHandler(g_ruptureReceiveHandle, typeid(MapSyncProtocol::ServerRuptureStatePacket).name());
			UnregisterHandler(g_snapshotBeginReceiveHandle, typeid(MapSyncProtocol::ServerSnapshotBeginPacket).name());
			UnregisterHandler(g_snapshotEndReceiveHandle, typeid(MapSyncProtocol::ServerSnapshotEndPacket).name());
			UnregisterHandler(g_playersChunkReceiveHandle, typeid(MapSyncProtocol::ServerPlayersChunkPacket).name());
			UnregisterHandler(g_teleportersChunkReceiveHandle, typeid(MapSyncProtocol::ServerTeleportersChunkPacket).name());
			UnregisterHandler(g_cargoMarkersChunkReceiveHandle, typeid(MapSyncProtocol::ServerCargoMarkersChunkPacket).name());
			UnregisterHandler(g_cargoConnectionsChunkReceiveHandle, typeid(MapSyncProtocol::ServerCargoConnectionsChunkPacket).name());

			g_requestSequence = 0;
			g_lastRequestAtUnixMs = 0;
			g_protocolMismatchLogged = false;
			RemoteCache::Reset();
		}

		void ResetRuntimeState()
		{
			g_lastRequestAtUnixMs = 0;
			RemoteCache::Reset();
		}

		bool RequestSnapshotIfNeeded(SDK::UWorld* world, const char* reason)
		{
			IPluginHooks* hooks = GetHooks();
			const IPluginSelf* self = GetPluginSelf();
			if (!hooks || !hooks->Network || !self || hooks->Network->IsServer())
			{
				return false;
			}

			SDK::ACrGameStateBase* gameState = TryGetGameState(world);
			if (!gameState || !gameState->bIsDedicatedServer)
			{
				return false;
			}

			if (!TryGetLocalCrPlayerController(world))
			{
				return false;
			}

			const int64_t nowUnixMs = GetCurrentUnixTimeMilliseconds();
			const int64_t lastReceivedAtUnixMs = RemoteCache::GetLastReceivedAtUnixMs();
			if (lastReceivedAtUnixMs > 0 && nowUnixMs - lastReceivedAtUnixMs < kRefreshIntervalMs)
			{
				return false;
			}

			if (g_lastRequestAtUnixMs > 0 && nowUnixMs - g_lastRequestAtUnixMs < kMinRequestIntervalMs)
			{
				return false;
			}

			MapSyncProtocol::ClientSnapshotRequestPacket packet{};
			packet.request_sequence = ++g_requestSequence;
			packet.request_flags = MapSyncProtocol::kRequestFlagAll;

			Network::SendPacketToServer(hooks, self, packet);
			g_lastRequestAtUnixMs = nowUnixMs;

			LOG_DEBUG(
				"Requested dedicated snapshot via network (%s, request_sequence=%llu)",
				reason ? reason : "unknown",
				static_cast<unsigned long long>(packet.request_sequence));
			return true;
		}

		bool TryCopyRemoteRuptureCycleSnapshot(MapStateRuntime::Detail::RuptureCycleSnapshot& outSnapshot)
		{
			return RemoteCache::TryCopyRuptureCycleSnapshot(outSnapshot);
		}

		bool TryCopyRemoteCargoSnapshot(MapStateRuntime::Detail::CargoSnapshot& outSnapshot)
		{
			return RemoteCache::TryCopyCargoSnapshot(outSnapshot);
		}
	}
}
