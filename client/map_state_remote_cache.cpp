#include "map_state_remote_cache.h"

#include <mutex>
#include <string>
#include <unordered_set>

namespace
{
	using CargoConnection = MapStateRuntime::Detail::CargoConnection;
	using CargoKind = MapStateRuntime::Detail::CargoKind;
	using CargoMarker = MapStateRuntime::Detail::CargoMarker;
	using CargoSnapshot = MapStateRuntime::Detail::CargoSnapshot;
	using PlayerMarker = MapStateRuntime::Detail::PlayerMarker;
	using RuptureCycleSnapshot = MapStateRuntime::Detail::RuptureCycleSnapshot;
	using TeleporterMarker = MapStateRuntime::Detail::TeleporterMarker;

	std::mutex g_remoteCacheMutex;
	bool g_hasRuptureCycleSnapshot = false;
	RuptureCycleSnapshot g_ruptureCycleSnapshot{};
	int64_t g_lastReceivedAtUnixMs = 0;

	struct SnapshotAssembly final
	{
		uint64_t SnapshotId = 0;
		uint64_t Generation = 0;
		uint32_t ContentFlags = 0;
		uint16_t ExpectedPlayersChunkCount = 0;
		uint16_t ExpectedTeleportersChunkCount = 0;
		uint16_t ExpectedCargoMarkersChunkCount = 0;
		uint16_t ExpectedCargoConnectionsChunkCount = 0;
		uint16_t ExpectedPlayersCount = 0;
		uint16_t ExpectedTeleportersCount = 0;
		uint16_t ExpectedCargoMarkersCount = 0;
		uint16_t ExpectedCargoConnectionsCount = 0;
		std::string WorldName;
		CargoSnapshot Snapshot{};
		std::unordered_set<uint16_t> PlayersChunksReceived;
		std::unordered_set<uint16_t> TeleportersChunksReceived;
		std::unordered_set<uint16_t> CargoMarkersChunksReceived;
		std::unordered_set<uint16_t> CargoConnectionsChunksReceived;

		void Reset()
		{
			*this = {};
		}
	};

	SnapshotAssembly g_snapshotAssembly{};
	bool g_hasCargoSnapshot = false;
	CargoSnapshot g_activeCargoSnapshot{};

	template <size_t N>
	std::string ReadFixedString(const char (&buffer)[N])
	{
		size_t length = 0;
		while (length < N && buffer[length] != '\0')
		{
			++length;
		}

		return std::string(buffer, length);
	}

	SDK::FVector MakeVector(float x, float y, float z)
	{
		SDK::FVector value{};
		value.X = x;
		value.Y = y;
		value.Z = z;
		return value;
	}

	void ApplySnapshotMetadata(CargoSnapshot& snapshot, uint64_t generation, const std::string& worldName)
	{
		snapshot.Generation = generation;
		snapshot.WorldName = worldName;
		snapshot.Reason = "DedicatedServerSnapshot";
	}

	void UpdateCounts(CargoSnapshot& snapshot)
	{
		snapshot.SenderCount = 0;
		snapshot.ReceiverCount = 0;
		for (const CargoMarker& marker : snapshot.Markers)
		{
			if (marker.Kind == CargoKind::Sender)
			{
				++snapshot.SenderCount;
			}
			else
			{
				++snapshot.ReceiverCount;
			}
		}

		snapshot.ConnectionCount = static_cast<int>(snapshot.Connections.size());
		snapshot.TeleporterCount = static_cast<int>(snapshot.Teleporters.size());
		snapshot.PlayerCount = static_cast<int>(snapshot.Players.size());
	}

	bool HasAllExpectedChunks(const SnapshotAssembly& assembly)
	{
		if (assembly.ExpectedPlayersChunkCount != assembly.PlayersChunksReceived.size())
		{
			return false;
		}
		if (assembly.ExpectedTeleportersChunkCount != assembly.TeleportersChunksReceived.size())
		{
			return false;
		}
		if (assembly.ExpectedCargoMarkersChunkCount != assembly.CargoMarkersChunksReceived.size())
		{
			return false;
		}
		if (assembly.ExpectedCargoConnectionsChunkCount != assembly.CargoConnectionsChunksReceived.size())
		{
			return false;
		}

		return true;
	}
}

namespace MapExtensionClient
{
	namespace RemoteCache
	{
		void Reset()
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			g_hasRuptureCycleSnapshot = false;
			g_ruptureCycleSnapshot = {};
			g_lastReceivedAtUnixMs = 0;
			g_snapshotAssembly.Reset();
			g_hasCargoSnapshot = false;
			g_activeCargoSnapshot = {};
		}

		void StoreRuptureState(const MapSyncProtocol::ServerRuptureStatePacket& packet, int64_t receivedAtUnixMs)
		{
			RuptureCycleSnapshot snapshot{};
			snapshot.Available =
				(packet.state_flags & MapSyncProtocol::kServerRuptureStateAvailable) != 0;
			snapshot.HasElapsed =
				(packet.state_flags & MapSyncProtocol::kServerRuptureStateHasElapsed) != 0;
			snapshot.HasObservedAtUnixMs = true;
			snapshot.ObservedAtUnixMs =
				receivedAtUnixMs > 0 ? receivedAtUnixMs : packet.observed_at_unix_ms;
			snapshot.ElapsedSeconds = packet.elapsed_seconds;
			snapshot.Wave = ReadFixedString(packet.wave);
			snapshot.Stage = ReadFixedString(packet.stage);
			snapshot.Step = ReadFixedString(packet.step);

			if (snapshot.Wave.empty()) snapshot.Wave = "None";
			if (snapshot.Stage.empty()) snapshot.Stage = "None";
			if (snapshot.Step.empty()) snapshot.Step = "None";

			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			g_hasRuptureCycleSnapshot = true;
			g_ruptureCycleSnapshot = snapshot;
			g_lastReceivedAtUnixMs = snapshot.ObservedAtUnixMs;
			if (g_hasCargoSnapshot)
			{
				g_activeCargoSnapshot.RuptureCycle = snapshot;
			}
		}

		void BeginSnapshot(const MapSyncProtocol::ServerSnapshotBeginPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			g_snapshotAssembly.Reset();
			g_snapshotAssembly.SnapshotId = packet.snapshot_id;
			g_snapshotAssembly.Generation = packet.generation;
			g_snapshotAssembly.ContentFlags = packet.content_flags;
			g_snapshotAssembly.ExpectedPlayersChunkCount = packet.players_chunk_count;
			g_snapshotAssembly.ExpectedTeleportersChunkCount = packet.teleporters_chunk_count;
			g_snapshotAssembly.ExpectedCargoMarkersChunkCount = packet.cargo_markers_chunk_count;
			g_snapshotAssembly.ExpectedCargoConnectionsChunkCount = packet.cargo_connections_chunk_count;
			g_snapshotAssembly.ExpectedPlayersCount = packet.players_count;
			g_snapshotAssembly.ExpectedTeleportersCount = packet.teleporters_count;
			g_snapshotAssembly.ExpectedCargoMarkersCount = packet.cargo_markers_count;
			g_snapshotAssembly.ExpectedCargoConnectionsCount = packet.cargo_connections_count;
			g_snapshotAssembly.WorldName = ReadFixedString(packet.world_name);
			ApplySnapshotMetadata(
				g_snapshotAssembly.Snapshot,
				packet.generation != 0 ? packet.generation : packet.snapshot_id,
				g_snapshotAssembly.WorldName);
		}

		void StorePlayersChunk(const MapSyncProtocol::ServerPlayersChunkPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (packet.snapshot_id == 0 || packet.snapshot_id != g_snapshotAssembly.SnapshotId)
			{
				return;
			}
			if (!g_snapshotAssembly.PlayersChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount =
				packet.item_count > MapSyncProtocol::kPlayerChunkCapacity
					? static_cast<uint16_t>(MapSyncProtocol::kPlayerChunkCapacity)
					: packet.item_count;
			for (uint16_t index = 0; index < itemCount; ++index)
			{
				const auto& item = packet.items[index];
				PlayerMarker marker{};
				marker.WorldLocation = MakeVector(item.world_x, item.world_y, item.world_z);
				marker.MapLocation = MapStateRuntime::Detail::WorldToMap(marker.WorldLocation);
				marker.DisplayName = ReadFixedString(item.label);
				marker.Source = ReadFixedString(item.source);
				marker.PublicKey = ReadFixedString(item.unique_key);
				marker.InternalKey = marker.PublicKey;
				g_snapshotAssembly.Snapshot.Players.push_back(std::move(marker));
			}
		}

		void StoreTeleportersChunk(const MapSyncProtocol::ServerTeleportersChunkPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (packet.snapshot_id == 0 || packet.snapshot_id != g_snapshotAssembly.SnapshotId)
			{
				return;
			}
			if (!g_snapshotAssembly.TeleportersChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount =
				packet.item_count > MapSyncProtocol::kTeleporterChunkCapacity
					? static_cast<uint16_t>(MapSyncProtocol::kTeleporterChunkCapacity)
					: packet.item_count;
			for (uint16_t index = 0; index < itemCount; ++index)
			{
				const auto& item = packet.items[index];
				TeleporterMarker marker{};
				marker.WorldLocation = MakeVector(item.world_x, item.world_y, item.world_z);
				marker.MapLocation = MapStateRuntime::Detail::WorldToMap(marker.WorldLocation);
				marker.DisplayName = ReadFixedString(item.label);
				marker.Source = ReadFixedString(item.source);
				marker.PublicKey = ReadFixedString(item.unique_key);
				marker.InternalKey = marker.PublicKey;
				g_snapshotAssembly.Snapshot.Teleporters.push_back(std::move(marker));
			}
		}

		void StoreCargoMarkersChunk(const MapSyncProtocol::ServerCargoMarkersChunkPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (packet.snapshot_id == 0 || packet.snapshot_id != g_snapshotAssembly.SnapshotId)
			{
				return;
			}
			if (!g_snapshotAssembly.CargoMarkersChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount =
				packet.item_count > MapSyncProtocol::kCargoMarkerChunkCapacity
					? static_cast<uint16_t>(MapSyncProtocol::kCargoMarkerChunkCapacity)
					: packet.item_count;
			for (uint16_t index = 0; index < itemCount; ++index)
			{
				const auto& item = packet.items[index];
				CargoMarker marker{};
				marker.Kind = item.kind == MapSyncProtocol::kCargoMarkerSender
					? CargoKind::Sender
					: CargoKind::Receiver;
				marker.WorldLocation = MakeVector(item.world_x, item.world_y, item.world_z);
				marker.MapLocation = MapStateRuntime::Detail::WorldToMap(marker.WorldLocation);
				marker.DisplayName = ReadFixedString(item.display_name);
				marker.ResourceSummary = ReadFixedString(item.resource);
				marker.Source = ReadFixedString(item.source);
				marker.PublicKey = ReadFixedString(item.unique_key);
				marker.InternalKey = marker.PublicKey;
				g_snapshotAssembly.Snapshot.Markers.push_back(std::move(marker));
			}
		}

		void StoreCargoConnectionsChunk(const MapSyncProtocol::ServerCargoConnectionsChunkPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (packet.snapshot_id == 0 || packet.snapshot_id != g_snapshotAssembly.SnapshotId)
			{
				return;
			}
			if (!g_snapshotAssembly.CargoConnectionsChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount =
				packet.item_count > MapSyncProtocol::kCargoConnectionChunkCapacity
					? static_cast<uint16_t>(MapSyncProtocol::kCargoConnectionChunkCapacity)
					: packet.item_count;
			for (uint16_t index = 0; index < itemCount; ++index)
			{
				const auto& item = packet.items[index];
				CargoConnection connection{};
				connection.SenderKey = ReadFixedString(item.sender_key);
				connection.ReceiverKey = ReadFixedString(item.receiver_key);
				connection.SenderLabel = ReadFixedString(item.sender_label);
				connection.ReceiverLabel = ReadFixedString(item.receiver_label);
				connection.ItemDisplayName = ReadFixedString(item.item_name);
				connection.RequestedAmount = item.requested_amount;
				connection.LastObservedAtUnixMs = item.observed_at_unix_ms;
				connection.SenderWorldLocation = MakeVector(item.sender_world_x, item.sender_world_y, item.sender_world_z);
				connection.ReceiverWorldLocation = MakeVector(item.receiver_world_x, item.receiver_world_y, item.receiver_world_z);
				connection.SenderMapLocation = MapStateRuntime::Detail::WorldToMap(connection.SenderWorldLocation);
				connection.ReceiverMapLocation = MapStateRuntime::Detail::WorldToMap(connection.ReceiverWorldLocation);
				g_snapshotAssembly.Snapshot.Connections.push_back(std::move(connection));
			}
		}

		void FinalizeSnapshot(const MapSyncProtocol::ServerSnapshotEndPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (!packet.success || packet.snapshot_id == 0 || packet.snapshot_id != g_snapshotAssembly.SnapshotId)
			{
				return;
			}
			if (!HasAllExpectedChunks(g_snapshotAssembly))
			{
				return;
			}

			CargoSnapshot snapshot = g_snapshotAssembly.Snapshot;
			if (g_hasRuptureCycleSnapshot)
			{
				snapshot.RuptureCycle = g_ruptureCycleSnapshot;
			}
			UpdateCounts(snapshot);
			g_activeCargoSnapshot = std::move(snapshot);
			g_hasCargoSnapshot = true;
			if (g_lastReceivedAtUnixMs == 0)
			{
				g_lastReceivedAtUnixMs = g_activeCargoSnapshot.RuptureCycle.ObservedAtUnixMs;
			}
		}

		bool TryCopyRuptureCycleSnapshot(RuptureCycleSnapshot& outSnapshot)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (!g_hasRuptureCycleSnapshot)
			{
				return false;
			}

			outSnapshot = g_ruptureCycleSnapshot;
			return true;
		}

		bool TryCopyCargoSnapshot(CargoSnapshot& outSnapshot)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (!g_hasCargoSnapshot)
			{
				return false;
			}

			outSnapshot = g_activeCargoSnapshot;
			return true;
		}

		bool HasRuptureCycleSnapshot()
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			return g_hasRuptureCycleSnapshot;
		}

		bool HasCargoSnapshot()
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			return g_hasCargoSnapshot;
		}

		int64_t GetLastReceivedAtUnixMs()
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			return g_lastReceivedAtUnixMs;
		}
	}
}
