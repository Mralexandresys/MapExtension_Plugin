#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace MapSyncProtocol
{
	constexpr uint32_t kProtocolVersion = 6;

	constexpr size_t kWorldNameCapacity = 64;
	constexpr size_t kKeyCapacity = 64;
	constexpr size_t kLabelCapacity = 48;
	constexpr size_t kResourceCapacity = 48;
	constexpr size_t kSourceCapacity = 32;
	constexpr size_t kItemNameCapacity = 48;
	constexpr size_t kPlayerChunkCapacity = 6;
	constexpr size_t kTeleporterChunkCapacity = 6;
	constexpr size_t kCargoMarkerChunkCapacity = 6;
	constexpr size_t kCargoConnectionChunkCapacity = 4;
	constexpr size_t kPoiChunkCapacity = 4;
	constexpr size_t kPreferredPoiPacketSizeLimit = 1024;
	// POIs are paginated in protocol v6: each snapshot response carries at most
	// one page of POIs and the client requests the remaining pages across
	// subsequent snapshot requests. A stable POI revision prevents pages from
	// different catalog contents from being merged. This bounds the packet burst
	// emitted for a single request on worlds with thousands of gatherable actors.
	constexpr size_t kPoiChunksPerPage = 16;
	constexpr size_t kPoiPageCapacity = kPoiChunkCapacity * kPoiChunksPerPage;

	enum ServerRuptureStateFlags : uint32_t
	{
		kServerRuptureStateAvailable = 1u << 0,
		kServerRuptureStateHasElapsed = 1u << 1,
		kServerRuptureStateHasObservedAt = 1u << 2
	};

	enum SnapshotContentFlags : uint32_t
	{
		kSnapshotHasRupture = 1u << 0,
		kSnapshotHasPlayers = 1u << 1,
		kSnapshotHasTeleporters = 1u << 2,
		kSnapshotHasCargoMarkers = 1u << 3,
		kSnapshotHasCargoConnections = 1u << 4,
		kSnapshotHasPois = 1u << 5
	};

	constexpr uint32_t kSnapshotContentFlagsAll =
		kSnapshotHasRupture
		| kSnapshotHasPlayers
		| kSnapshotHasTeleporters
		| kSnapshotHasCargoMarkers
		| kSnapshotHasCargoConnections
		| kSnapshotHasPois;

	enum CargoMarkerKind : uint8_t
	{
		kCargoMarkerSender = 0,
		kCargoMarkerReceiver = 1
	};

	enum PoiMarkerKind : uint8_t
	{
		kPoiAbandonedBase = 0,
		kPoiPlantResource = 1,
		kPoiIgnitium = 2,
		kPoiStarTears = 3
	};

	enum PoiEntryFlags : uint8_t
	{
		kPoiEntryDepleted = 1u << 0,
		kPoiEntryUnavailable = 1u << 1,
		kPoiEntryUnknown = 1u << 2
	};

	enum PlayerEntryFlags : uint8_t
	{
		// Set by the server on the entry matching the requesting player, so the
		// receiving client can highlight "me" on the map.
		kPlayerEntrySelf = 1u << 0
	};

	struct ClientSnapshotRequestPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		// Reserved: the server always answers with a full snapshot.
		uint32_t request_flags = 0;
		uint64_t request_sequence = 0;
		// POI page requested for this snapshot (protocol v6 pagination).
		uint16_t poi_page = 0;
		uint8_t reserved[14] = {};
	};

	struct ServerSnapshotBeginPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t content_flags = 0;
		uint64_t snapshot_id = 0;
		uint64_t generation = 0;
		uint64_t poi_revision = 0;
		uint16_t players_count = 0;
		uint16_t teleporters_count = 0;
		uint16_t cargo_markers_count = 0;
		uint16_t cargo_connections_count = 0;
		uint16_t players_chunk_count = 0;
		uint16_t teleporters_chunk_count = 0;
		uint16_t cargo_markers_chunk_count = 0;
		uint16_t cargo_connections_chunk_count = 0;
		// POI pagination (protocol v6): pois_count/pois_chunk_count describe the
		// page carried by this snapshot; pois_total_count is the full POI count
		// across all poi_page_count pages. poi_revision identifies the exact
		// canonical catalog from which the page was sliced.
		uint16_t pois_count = 0;
		uint16_t pois_chunk_count = 0;
		uint16_t poi_page = 0;
		uint16_t poi_page_count = 0;
		uint16_t pois_total_count = 0;
		uint16_t reserved_pois = 0;
		char world_name[kWorldNameCapacity] = {};
	};

	struct ServerSnapshotEndPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t success = 0;
		uint64_t snapshot_id = 0;
		uint64_t generation = 0;
		uint64_t poi_revision = 0;
		uint16_t players_count = 0;
		uint16_t teleporters_count = 0;
		uint16_t cargo_markers_count = 0;
		uint16_t cargo_connections_count = 0;
		uint16_t pois_count = 0;
		uint16_t poi_page = 0;
		uint16_t poi_page_count = 0;
		uint16_t pois_total_count = 0;
		uint8_t reserved[8] = {};
	};

	struct ServerRuptureStatePacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t state_flags = 0;
		uint64_t snapshot_id = 0;
		uint64_t sequence = 0;
		double elapsed_seconds = 0.0;
		int64_t observed_at_unix_ms = 0;
		char wave[kLabelCapacity] = {};
		char stage[kLabelCapacity] = {};
		char step[kLabelCapacity] = {};
	};

	struct ServerPlayerEntry
	{
		float world_x = 0.0f;
		float world_y = 0.0f;
		float world_z = 0.0f;
		uint8_t flags = 0;
		uint8_t reserved[3] = {};
		char label[kLabelCapacity] = {};
		char source[kSourceCapacity] = {};
		char unique_key[kKeyCapacity] = {};
	};

	struct ServerTeleporterEntry
	{
		float world_x = 0.0f;
		float world_y = 0.0f;
		float world_z = 0.0f;
		char label[kLabelCapacity] = {};
		char source[kSourceCapacity] = {};
		char unique_key[kKeyCapacity] = {};
	};

	struct ServerCargoMarkerEntry
	{
		float world_x = 0.0f;
		float world_y = 0.0f;
		float world_z = 0.0f;
		uint8_t kind = kCargoMarkerReceiver;
		uint8_t reserved[3] = {};
		char display_name[kLabelCapacity] = {};
		char resource[kResourceCapacity] = {};
		char source[kSourceCapacity] = {};
		char unique_key[kKeyCapacity] = {};
	};

	struct ServerCargoConnectionEntry
	{
		float sender_world_x = 0.0f;
		float sender_world_y = 0.0f;
		float sender_world_z = 0.0f;
		float receiver_world_x = 0.0f;
		float receiver_world_y = 0.0f;
		float receiver_world_z = 0.0f;
		int32_t requested_amount = 0;
		int64_t observed_at_unix_ms = 0;
		char sender_key[kKeyCapacity] = {};
		char receiver_key[kKeyCapacity] = {};
		char sender_label[kLabelCapacity] = {};
		char receiver_label[kLabelCapacity] = {};
		char item_name[kItemNameCapacity] = {};
	};

	struct ServerPoiEntry
	{
		float world_x = 0.0f;
		float world_y = 0.0f;
		float world_z = 0.0f;
		uint8_t kind = kPoiAbandonedBase;
		uint8_t flags = 0;
		uint8_t reserved[2] = {};
		char label[kLabelCapacity] = {};
		char resource[kResourceCapacity] = {};
		char source[kSourceCapacity] = {};
		char unique_key[kKeyCapacity] = {};
	};

	// One page of a chunked snapshot section. Every section streams the same
	// envelope, only the entry type and the per-packet capacity change.
	template <typename TEntry, size_t Capacity>
	struct ServerChunkPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t reserved = 0;
		uint64_t snapshot_id = 0;
		uint16_t chunk_index = 0;
		uint16_t chunk_count = 0;
		uint16_t item_count = 0;
		uint16_t reserved2 = 0;
		TEntry items[Capacity] = {};
	};

	using ServerPlayersChunkPacket = ServerChunkPacket<ServerPlayerEntry, kPlayerChunkCapacity>;
	using ServerTeleportersChunkPacket = ServerChunkPacket<ServerTeleporterEntry, kTeleporterChunkCapacity>;
	using ServerCargoMarkersChunkPacket = ServerChunkPacket<ServerCargoMarkerEntry, kCargoMarkerChunkCapacity>;
	using ServerCargoConnectionsChunkPacket = ServerChunkPacket<ServerCargoConnectionEntry, kCargoConnectionChunkCapacity>;
	using ServerPoisChunkPacket = ServerChunkPacket<ServerPoiEntry, kPoiChunkCapacity>;

	static_assert(std::is_trivially_copyable_v<ClientSnapshotRequestPacket>);
	static_assert(std::is_trivially_copyable_v<ServerSnapshotBeginPacket>);
	static_assert(std::is_trivially_copyable_v<ServerSnapshotEndPacket>);
	static_assert(std::is_trivially_copyable_v<ServerRuptureStatePacket>);
	static_assert(std::is_trivially_copyable_v<ServerPlayerEntry>);
	static_assert(std::is_trivially_copyable_v<ServerTeleporterEntry>);
	static_assert(std::is_trivially_copyable_v<ServerCargoMarkerEntry>);
	static_assert(std::is_trivially_copyable_v<ServerCargoConnectionEntry>);
	static_assert(std::is_trivially_copyable_v<ServerPoiEntry>);
	static_assert(std::is_trivially_copyable_v<ServerPlayersChunkPacket>);
	static_assert(std::is_trivially_copyable_v<ServerTeleportersChunkPacket>);
	static_assert(std::is_trivially_copyable_v<ServerCargoMarkersChunkPacket>);
	static_assert(std::is_trivially_copyable_v<ServerCargoConnectionsChunkPacket>);
	static_assert(std::is_trivially_copyable_v<ServerPoisChunkPacket>);
	static_assert(
		sizeof(ServerPoisChunkPacket) <= kPreferredPoiPacketSizeLimit,
		"POI chunk packets must remain within the SDK's recommended 1 KiB payload size");

	inline void CopyCStringTruncated(char* destination, size_t capacity, const char* source)
	{
		if (!destination || capacity == 0)
		{
			return;
		}

		// snprintf always NUL-terminates and truncates to the buffer size.
		std::snprintf(destination, capacity, "%s", source ? source : "");
	}
}
