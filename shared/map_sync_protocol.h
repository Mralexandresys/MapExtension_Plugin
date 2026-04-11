#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace MapSyncProtocol
{
	constexpr uint32_t kProtocolVersion = 1;
	constexpr uint32_t kRequestFlagRuptureCycle = 1u << 0;
	constexpr uint32_t kRequestFlagPlayers = 1u << 1;
	constexpr uint32_t kRequestFlagTeleporters = 1u << 2;
	constexpr uint32_t kRequestFlagCargoMarkers = 1u << 3;
	constexpr uint32_t kRequestFlagCargoConnections = 1u << 4;
	constexpr uint32_t kRequestFlagAll =
		kRequestFlagRuptureCycle
		| kRequestFlagPlayers
		| kRequestFlagTeleporters
		| kRequestFlagCargoMarkers
		| kRequestFlagCargoConnections;

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
		kSnapshotHasCargoConnections = 1u << 4
	};

	enum CargoMarkerKind : uint8_t
	{
		kCargoMarkerSender = 0,
		kCargoMarkerReceiver = 1
	};

	struct ClientSnapshotRequestPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t request_flags = kRequestFlagAll;
		uint64_t request_sequence = 0;
		uint8_t reserved[16] = {};
	};

	struct ServerSnapshotBeginPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t content_flags = 0;
		uint64_t snapshot_id = 0;
		uint64_t generation = 0;
		uint16_t players_count = 0;
		uint16_t teleporters_count = 0;
		uint16_t cargo_markers_count = 0;
		uint16_t cargo_connections_count = 0;
		uint16_t players_chunk_count = 0;
		uint16_t teleporters_chunk_count = 0;
		uint16_t cargo_markers_chunk_count = 0;
		uint16_t cargo_connections_chunk_count = 0;
		char world_name[kWorldNameCapacity] = {};
	};

	struct ServerSnapshotEndPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t success = 0;
		uint64_t snapshot_id = 0;
		uint64_t generation = 0;
		uint16_t players_count = 0;
		uint16_t teleporters_count = 0;
		uint16_t cargo_markers_count = 0;
		uint16_t cargo_connections_count = 0;
		uint8_t reserved[16] = {};
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

	struct ServerPlayersChunkPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t reserved = 0;
		uint64_t snapshot_id = 0;
		uint16_t chunk_index = 0;
		uint16_t chunk_count = 0;
		uint16_t item_count = 0;
		uint16_t reserved2 = 0;
		ServerPlayerEntry items[kPlayerChunkCapacity] = {};
	};

	struct ServerTeleportersChunkPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t reserved = 0;
		uint64_t snapshot_id = 0;
		uint16_t chunk_index = 0;
		uint16_t chunk_count = 0;
		uint16_t item_count = 0;
		uint16_t reserved2 = 0;
		ServerTeleporterEntry items[kTeleporterChunkCapacity] = {};
	};

	struct ServerCargoMarkersChunkPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t reserved = 0;
		uint64_t snapshot_id = 0;
		uint16_t chunk_index = 0;
		uint16_t chunk_count = 0;
		uint16_t item_count = 0;
		uint16_t reserved2 = 0;
		ServerCargoMarkerEntry items[kCargoMarkerChunkCapacity] = {};
	};

	struct ServerCargoConnectionsChunkPacket
	{
		uint32_t protocol_version = kProtocolVersion;
		uint32_t reserved = 0;
		uint64_t snapshot_id = 0;
		uint16_t chunk_index = 0;
		uint16_t chunk_count = 0;
		uint16_t item_count = 0;
		uint16_t reserved2 = 0;
		ServerCargoConnectionEntry items[kCargoConnectionChunkCapacity] = {};
	};

	static_assert(std::is_trivially_copyable_v<ClientSnapshotRequestPacket>);
	static_assert(std::is_trivially_copyable_v<ServerSnapshotBeginPacket>);
	static_assert(std::is_trivially_copyable_v<ServerSnapshotEndPacket>);
	static_assert(std::is_trivially_copyable_v<ServerRuptureStatePacket>);
	static_assert(std::is_trivially_copyable_v<ServerPlayerEntry>);
	static_assert(std::is_trivially_copyable_v<ServerTeleporterEntry>);
	static_assert(std::is_trivially_copyable_v<ServerCargoMarkerEntry>);
	static_assert(std::is_trivially_copyable_v<ServerCargoConnectionEntry>);
	static_assert(std::is_trivially_copyable_v<ServerPlayersChunkPacket>);
	static_assert(std::is_trivially_copyable_v<ServerTeleportersChunkPacket>);
	static_assert(std::is_trivially_copyable_v<ServerCargoMarkersChunkPacket>);
	static_assert(std::is_trivially_copyable_v<ServerCargoConnectionsChunkPacket>);

	inline void CopyCStringTruncated(char* destination, size_t capacity, const char* source)
	{
		if (!destination || capacity == 0)
		{
			return;
		}

		destination[0] = '\0';
		if (!source)
		{
			return;
		}

		size_t index = 0;
		while (index + 1 < capacity && source[index] != '\0')
		{
			destination[index] = source[index];
			++index;
		}
		destination[index] = '\0';
	}
}
