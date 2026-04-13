#pragma once

#include "../map_state_types.h"
#include "../shared/map_sync_protocol.h"

#include <cstdint>

namespace MapExtensionClient
{
	namespace RemoteCache
	{
		void Reset();

		void StoreRuptureState(const MapSyncProtocol::ServerRuptureStatePacket& packet, int64_t receivedAtUnixMs);
		void BeginSnapshot(const MapSyncProtocol::ServerSnapshotBeginPacket& packet);
		void StorePlayersChunk(const MapSyncProtocol::ServerPlayersChunkPacket& packet);
		void StoreTeleportersChunk(const MapSyncProtocol::ServerTeleportersChunkPacket& packet);
		void StoreCargoMarkersChunk(const MapSyncProtocol::ServerCargoMarkersChunkPacket& packet);
		void StoreCargoConnectionsChunk(const MapSyncProtocol::ServerCargoConnectionsChunkPacket& packet);
		void FinalizeSnapshot(const MapSyncProtocol::ServerSnapshotEndPacket& packet);

		bool TryCopyRuptureCycleSnapshot(MapStateRuntime::Detail::RuptureCycleSnapshot& outSnapshot);
		bool TryCopyCargoSnapshot(MapStateRuntime::Detail::CargoSnapshot& outSnapshot);
		bool HasRuptureCycleSnapshot();
		bool HasCargoSnapshot();
		int64_t GetLastReceivedAtUnixMs();
	}
}
