#include "map_state_remote_cache.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	using CargoConnection = MapStateRuntime::Detail::CargoConnection;
	using CargoKind = MapStateRuntime::Detail::CargoKind;
	using CargoMarker = MapStateRuntime::Detail::CargoMarker;
	using CargoSnapshot = MapStateRuntime::Detail::CargoSnapshot;
	using PlayerMarker = MapStateRuntime::Detail::PlayerMarker;
	using PoiKind = MapStateRuntime::Detail::PoiKind;
	using PoiMarker = MapStateRuntime::Detail::PoiMarker;
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
		uint64_t PoiRevision = 0;
		uint32_t ContentFlags = 0;
		uint16_t ExpectedPlayersChunkCount = 0;
		uint16_t ExpectedTeleportersChunkCount = 0;
		uint16_t ExpectedCargoMarkersChunkCount = 0;
		uint16_t ExpectedCargoConnectionsChunkCount = 0;
		uint16_t ExpectedPoisChunkCount = 0;
		uint16_t ExpectedPlayersCount = 0;
		uint16_t ExpectedTeleportersCount = 0;
		uint16_t ExpectedCargoMarkersCount = 0;
		uint16_t ExpectedCargoConnectionsCount = 0;
		uint16_t ExpectedPoisCount = 0;
		uint16_t PoiPage = 0;
		uint16_t PoiPageCount = 1;
		uint16_t PoisTotalCount = 0;
		std::string WorldName;
		CargoSnapshot Snapshot{};
		std::unordered_set<uint16_t> PlayersChunksReceived;
		std::unordered_set<uint16_t> TeleportersChunksReceived;
		std::unordered_set<uint16_t> CargoMarkersChunksReceived;
		std::unordered_set<uint16_t> CargoConnectionsChunksReceived;
		std::unordered_set<uint16_t> PoisChunksReceived;

		void Reset()
		{
			*this = {};
		}
	};

	SnapshotAssembly g_snapshotAssembly{};
	bool g_hasCargoSnapshot = false;
	CargoSnapshot g_activeCargoSnapshot{};

	// Protocol v5 POI pagination: pages already fetched are retained here and
	// merged into every finalized snapshot, keyed by page index. The store is
	// tied to one exact catalog revision so removed or changed POIs cannot leak
	// in from pages fetched before the catalog changed.
	std::map<uint16_t, std::vector<PoiMarker>> g_poiPages;
	uint64_t g_poiRevision = 0;
	uint16_t g_poiPageCount = 1;
	uint16_t g_poiTotalCount = 0;
	std::string g_poiPagesWorldName;
	uint16_t g_nextPoiPageToRequest = 0;

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

		snapshot.AbandonedBaseCount = 0;
		snapshot.PlantResourceCount = 0;
		snapshot.IgnitiumCount = 0;
		snapshot.StarTearsCount = 0;
		for (const PoiMarker& poi : snapshot.Pois)
		{
			switch (poi.Kind)
			{
			case PoiKind::AbandonedBase:
				++snapshot.AbandonedBaseCount;
				break;
			case PoiKind::PlantResource:
				++snapshot.PlantResourceCount;
				break;
			case PoiKind::Ignitium:
				++snapshot.IgnitiumCount;
				break;
			case PoiKind::StarTears:
				++snapshot.StarTearsCount;
				break;
			}
		}
	}

	template <size_t Capacity>
	bool HasCoherentChunkLayout(uint16_t itemCount, uint16_t chunkCount)
	{
		static_assert(Capacity > 0);
		const size_t expectedChunkCount = itemCount == 0
			? 0
			: ((static_cast<size_t>(itemCount) - 1) / Capacity) + 1;
		return expectedChunkCount == chunkCount;
	}

	bool HasExpectedContentFlag(uint32_t contentFlags, uint32_t flag, uint16_t itemCount)
	{
		return ((contentFlags & flag) != 0) == (itemCount != 0);
	}

	bool IsValidSnapshotBegin(const MapSyncProtocol::ServerSnapshotBeginPacket& packet)
	{
		if (packet.snapshot_id == 0 || packet.generation == 0 || packet.poi_revision == 0)
		{
			return false;
		}
		if ((packet.content_flags & ~MapSyncProtocol::kSnapshotContentFlagsAll) != 0
			|| (packet.content_flags & MapSyncProtocol::kSnapshotHasRupture) == 0)
		{
			return false;
		}
		if (!HasCoherentChunkLayout<MapSyncProtocol::kPlayerChunkCapacity>(
				packet.players_count,
				packet.players_chunk_count)
			|| !HasCoherentChunkLayout<MapSyncProtocol::kTeleporterChunkCapacity>(
				packet.teleporters_count,
				packet.teleporters_chunk_count)
			|| !HasCoherentChunkLayout<MapSyncProtocol::kCargoMarkerChunkCapacity>(
				packet.cargo_markers_count,
				packet.cargo_markers_chunk_count)
			|| !HasCoherentChunkLayout<MapSyncProtocol::kCargoConnectionChunkCapacity>(
				packet.cargo_connections_count,
				packet.cargo_connections_chunk_count)
			|| !HasCoherentChunkLayout<MapSyncProtocol::kPoiChunkCapacity>(
				packet.pois_count,
				packet.pois_chunk_count))
		{
			return false;
		}

		// Protocol v5 POI pagination coherence: the advertised page must exist,
		// and the page item count must match the slice of the advertised total.
		const uint32_t expectedPoiPageCount = packet.pois_total_count == 0
			? 1u
			: (static_cast<uint32_t>(packet.pois_total_count) + MapSyncProtocol::kPoiPageCapacity - 1)
				/ MapSyncProtocol::kPoiPageCapacity;
		if (packet.poi_page_count != expectedPoiPageCount
			|| packet.poi_page >= packet.poi_page_count)
		{
			return false;
		}
		const uint32_t poiPageStart =
			static_cast<uint32_t>(packet.poi_page) * MapSyncProtocol::kPoiPageCapacity;
		const uint32_t poiPageRemaining = packet.pois_total_count > poiPageStart
			? packet.pois_total_count - poiPageStart
			: 0u;
		const uint32_t expectedPoiPageItems = poiPageRemaining > MapSyncProtocol::kPoiPageCapacity
			? MapSyncProtocol::kPoiPageCapacity
			: poiPageRemaining;
		if (packet.pois_count != expectedPoiPageItems)
		{
			return false;
		}

		return HasExpectedContentFlag(
				packet.content_flags,
				MapSyncProtocol::kSnapshotHasPlayers,
				packet.players_count)
			&& HasExpectedContentFlag(
				packet.content_flags,
				MapSyncProtocol::kSnapshotHasTeleporters,
				packet.teleporters_count)
			&& HasExpectedContentFlag(
				packet.content_flags,
				MapSyncProtocol::kSnapshotHasCargoMarkers,
				packet.cargo_markers_count)
			&& HasExpectedContentFlag(
				packet.content_flags,
				MapSyncProtocol::kSnapshotHasCargoConnections,
				packet.cargo_connections_count)
			&& HasExpectedContentFlag(
				packet.content_flags,
				MapSyncProtocol::kSnapshotHasPois,
				packet.pois_count);
	}

	template <size_t Capacity, typename TPacket>
	bool IsExpectedChunk(
		const SnapshotAssembly& assembly,
		const TPacket& packet,
		uint16_t expectedChunkCount,
		uint16_t expectedItemCount)
	{
		static_assert(Capacity > 0);
		if (assembly.SnapshotId == 0
			|| packet.snapshot_id == 0
			|| packet.snapshot_id != assembly.SnapshotId
			|| expectedChunkCount == 0
			|| packet.chunk_count != expectedChunkCount
			|| packet.chunk_index >= expectedChunkCount)
		{
			return false;
		}

		const size_t startIndex = static_cast<size_t>(packet.chunk_index) * Capacity;
		if (startIndex >= expectedItemCount)
		{
			return false;
		}

		const size_t remaining = static_cast<size_t>(expectedItemCount) - startIndex;
		const size_t expectedPacketItemCount = remaining > Capacity ? Capacity : remaining;
		return packet.item_count == expectedPacketItemCount;
	}

	template <typename TSet>
	bool HasAllExpectedChunkIndexes(uint16_t expectedChunkCount, const TSet& receivedChunks)
	{
		if (receivedChunks.size() != expectedChunkCount)
		{
			return false;
		}

		for (uint32_t index = 0; index < expectedChunkCount; ++index)
		{
			if (receivedChunks.find(static_cast<uint16_t>(index)) == receivedChunks.end())
			{
				return false;
			}
		}
		return true;
	}

	bool HasAllExpectedChunks(const SnapshotAssembly& assembly)
	{
		return HasAllExpectedChunkIndexes(
				assembly.ExpectedPlayersChunkCount,
				assembly.PlayersChunksReceived)
			&& HasAllExpectedChunkIndexes(
				assembly.ExpectedTeleportersChunkCount,
				assembly.TeleportersChunksReceived)
			&& HasAllExpectedChunkIndexes(
				assembly.ExpectedCargoMarkersChunkCount,
				assembly.CargoMarkersChunksReceived)
			&& HasAllExpectedChunkIndexes(
				assembly.ExpectedCargoConnectionsChunkCount,
				assembly.CargoConnectionsChunksReceived)
			&& HasAllExpectedChunkIndexes(
				assembly.ExpectedPoisChunkCount,
				assembly.PoisChunksReceived);
	}

	bool HasExpectedItemCounts(const SnapshotAssembly& assembly)
	{
		return assembly.Snapshot.Players.size() == assembly.ExpectedPlayersCount
			&& assembly.Snapshot.Teleporters.size() == assembly.ExpectedTeleportersCount
			&& assembly.Snapshot.Markers.size() == assembly.ExpectedCargoMarkersCount
			&& assembly.Snapshot.Connections.size() == assembly.ExpectedCargoConnectionsCount
			&& assembly.Snapshot.Pois.size() == assembly.ExpectedPoisCount;
	}

	bool HasMatchingEndCounters(
		const SnapshotAssembly& assembly,
		const MapSyncProtocol::ServerSnapshotEndPacket& packet)
	{
		return packet.players_count == assembly.ExpectedPlayersCount
			&& packet.teleporters_count == assembly.ExpectedTeleportersCount
			&& packet.cargo_markers_count == assembly.ExpectedCargoMarkersCount
			&& packet.cargo_connections_count == assembly.ExpectedCargoConnectionsCount
			&& packet.pois_count == assembly.ExpectedPoisCount
			&& packet.poi_page == assembly.PoiPage
			&& packet.poi_page_count == assembly.PoiPageCount
			&& packet.pois_total_count == assembly.PoisTotalCount
			&& packet.poi_revision == assembly.PoiRevision;
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
			g_poiPages.clear();
			g_poiRevision = 0;
			g_poiPageCount = 1;
			g_poiTotalCount = 0;
			g_poiPagesWorldName.clear();
			g_nextPoiPageToRequest = 0;
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
			if (!IsValidSnapshotBegin(packet))
			{
				return;
			}

			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			g_snapshotAssembly.Reset();
			g_snapshotAssembly.SnapshotId = packet.snapshot_id;
			g_snapshotAssembly.Generation = packet.generation;
			g_snapshotAssembly.PoiRevision = packet.poi_revision;
			g_snapshotAssembly.ContentFlags = packet.content_flags;
			g_snapshotAssembly.ExpectedPlayersChunkCount = packet.players_chunk_count;
			g_snapshotAssembly.ExpectedTeleportersChunkCount = packet.teleporters_chunk_count;
			g_snapshotAssembly.ExpectedCargoMarkersChunkCount = packet.cargo_markers_chunk_count;
			g_snapshotAssembly.ExpectedCargoConnectionsChunkCount = packet.cargo_connections_chunk_count;
			g_snapshotAssembly.ExpectedPoisChunkCount = packet.pois_chunk_count;
			g_snapshotAssembly.ExpectedPlayersCount = packet.players_count;
			g_snapshotAssembly.ExpectedTeleportersCount = packet.teleporters_count;
			g_snapshotAssembly.ExpectedCargoMarkersCount = packet.cargo_markers_count;
			g_snapshotAssembly.ExpectedCargoConnectionsCount = packet.cargo_connections_count;
			g_snapshotAssembly.ExpectedPoisCount = packet.pois_count;
			g_snapshotAssembly.PoiPage = packet.poi_page;
			g_snapshotAssembly.PoiPageCount = packet.poi_page_count;
			g_snapshotAssembly.PoisTotalCount = packet.pois_total_count;
			g_snapshotAssembly.WorldName = ReadFixedString(packet.world_name);
			ApplySnapshotMetadata(
				g_snapshotAssembly.Snapshot,
				packet.generation,
				g_snapshotAssembly.WorldName);
			g_snapshotAssembly.Snapshot.PoiRevision = packet.poi_revision;
		}

		void StorePlayersChunk(const MapSyncProtocol::ServerPlayersChunkPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (!IsExpectedChunk<MapSyncProtocol::kPlayerChunkCapacity>(
					g_snapshotAssembly,
					packet,
					g_snapshotAssembly.ExpectedPlayersChunkCount,
					g_snapshotAssembly.ExpectedPlayersCount)
				|| !g_snapshotAssembly.PlayersChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount = packet.item_count;
			for (uint16_t index = 0; index < itemCount; ++index)
			{
				const auto& item = packet.items[index];
				PlayerMarker marker{};
				marker.IsSelf = (item.flags & MapSyncProtocol::kPlayerEntrySelf) != 0;
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
			if (!IsExpectedChunk<MapSyncProtocol::kTeleporterChunkCapacity>(
					g_snapshotAssembly,
					packet,
					g_snapshotAssembly.ExpectedTeleportersChunkCount,
					g_snapshotAssembly.ExpectedTeleportersCount)
				|| !g_snapshotAssembly.TeleportersChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount = packet.item_count;
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
			if (!IsExpectedChunk<MapSyncProtocol::kCargoMarkerChunkCapacity>(
					g_snapshotAssembly,
					packet,
					g_snapshotAssembly.ExpectedCargoMarkersChunkCount,
					g_snapshotAssembly.ExpectedCargoMarkersCount)
				|| !g_snapshotAssembly.CargoMarkersChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount = packet.item_count;
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
			if (!IsExpectedChunk<MapSyncProtocol::kCargoConnectionChunkCapacity>(
					g_snapshotAssembly,
					packet,
					g_snapshotAssembly.ExpectedCargoConnectionsChunkCount,
					g_snapshotAssembly.ExpectedCargoConnectionsCount)
				|| !g_snapshotAssembly.CargoConnectionsChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount = packet.item_count;
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

		void StorePoisChunk(const MapSyncProtocol::ServerPoisChunkPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (!IsExpectedChunk<MapSyncProtocol::kPoiChunkCapacity>(
					g_snapshotAssembly,
					packet,
					g_snapshotAssembly.ExpectedPoisChunkCount,
					g_snapshotAssembly.ExpectedPoisCount)
				|| !g_snapshotAssembly.PoisChunksReceived.emplace(packet.chunk_index).second)
			{
				return;
			}

			const uint16_t itemCount = packet.item_count;
			for (uint16_t index = 0; index < itemCount; ++index)
			{
				const auto& item = packet.items[index];
				PoiMarker marker{};
				switch (item.kind)
				{
				case MapSyncProtocol::kPoiAbandonedBase:
					marker.Kind = PoiKind::AbandonedBase;
					break;
				case MapSyncProtocol::kPoiPlantResource:
					marker.Kind = PoiKind::PlantResource;
					break;
				case MapSyncProtocol::kPoiIgnitium:
					marker.Kind = PoiKind::Ignitium;
					break;
				case MapSyncProtocol::kPoiStarTears:
					marker.Kind = PoiKind::StarTears;
					break;
				default:
					return;
				}
				marker.Depleted = (item.flags & MapSyncProtocol::kPoiEntryDepleted) != 0;
				marker.WorldLocation = MakeVector(item.world_x, item.world_y, item.world_z);
				marker.MapLocation = MapStateRuntime::Detail::WorldToMap(marker.WorldLocation);
				marker.DisplayName = ReadFixedString(item.label);
				marker.ResourceName = ReadFixedString(item.resource);
				marker.Source = ReadFixedString(item.source);
				marker.PublicKey = ReadFixedString(item.unique_key);
				marker.InternalKey = marker.PublicKey;
				g_snapshotAssembly.Snapshot.Pois.push_back(std::move(marker));
			}
		}

		void FinalizeSnapshot(const MapSyncProtocol::ServerSnapshotEndPacket& packet)
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			if (packet.success != 1u
				|| packet.snapshot_id == 0
				|| packet.snapshot_id != g_snapshotAssembly.SnapshotId
				|| packet.generation == 0
				|| packet.generation != g_snapshotAssembly.Generation
				|| g_snapshotAssembly.Snapshot.Generation != g_snapshotAssembly.Generation)
			{
				return;
			}
			if (!HasMatchingEndCounters(g_snapshotAssembly, packet)
				|| !HasAllExpectedChunks(g_snapshotAssembly)
				|| !HasExpectedItemCounts(g_snapshotAssembly))
			{
				return;
			}

			CargoSnapshot snapshot = g_snapshotAssembly.Snapshot;
			if (g_hasRuptureCycleSnapshot)
			{
				snapshot.RuptureCycle = g_ruptureCycleSnapshot;
			}

			// Merge only pages sliced from the exact same canonical POI catalog.
			// A count-only check is insufficient: a removal and an insertion can
			// preserve both total and page count while changing every later slice.
			if (g_poiPagesWorldName != g_snapshotAssembly.WorldName
				|| g_poiRevision != g_snapshotAssembly.PoiRevision
				|| g_poiPageCount != g_snapshotAssembly.PoiPageCount
				|| g_poiTotalCount != g_snapshotAssembly.PoisTotalCount)
			{
				g_poiPages.clear();
				g_poiPagesWorldName = g_snapshotAssembly.WorldName;
				g_poiRevision = g_snapshotAssembly.PoiRevision;
				g_poiPageCount = g_snapshotAssembly.PoiPageCount;
				g_poiTotalCount = g_snapshotAssembly.PoisTotalCount;
			}
			g_poiPages[g_snapshotAssembly.PoiPage] = std::move(snapshot.Pois);
			snapshot.Pois.clear();
			for (const auto& pageEntry : g_poiPages)
			{
				snapshot.Pois.insert(
					snapshot.Pois.end(),
					pageEntry.second.begin(),
					pageEntry.second.end());
			}

			std::unordered_set<std::string> mergedPoiKeys;
			bool mergedPoisAreValid = snapshot.Pois.size() <= g_poiTotalCount;
			for (const PoiMarker& marker : snapshot.Pois)
			{
				if (marker.PublicKey.empty() || !mergedPoiKeys.emplace(marker.PublicKey).second)
				{
					mergedPoisAreValid = false;
					break;
				}
			}
			const bool hasEveryPoiPage = g_poiPages.size() == g_poiPageCount;
			if (!mergedPoisAreValid
				|| (hasEveryPoiPage && snapshot.Pois.size() != g_poiTotalCount))
			{
				g_poiPages.clear();
				g_nextPoiPageToRequest = 0;
				g_snapshotAssembly.Reset();
				return;
			}

			g_nextPoiPageToRequest = static_cast<uint16_t>(
				(g_snapshotAssembly.PoiPage + 1) % g_snapshotAssembly.PoiPageCount);

			UpdateCounts(snapshot);
			g_activeCargoSnapshot = std::move(snapshot);
			g_hasCargoSnapshot = true;
			if (g_lastReceivedAtUnixMs == 0)
			{
				g_lastReceivedAtUnixMs = g_activeCargoSnapshot.RuptureCycle.ObservedAtUnixMs;
			}
			g_snapshotAssembly.Reset();
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

		uint16_t GetNextPoiPageToRequest()
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			return g_nextPoiPageToRequest;
		}

		bool HasPendingPoiPages()
		{
			std::lock_guard<std::mutex> lock(g_remoteCacheMutex);
			return g_hasCargoSnapshot && g_poiPages.size() < g_poiPageCount;
		}
	}
}
