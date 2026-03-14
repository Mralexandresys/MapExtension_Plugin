#include "map_state_json.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace MapStateRuntime
{
namespace Detail
{
	namespace
	{
		using nlohmann::json;

		double RoundJsonNumber(double value, int precision = 1)
		{
			double scale = 1.0;
			for (int index = 0; index < precision; ++index)
			{
				scale *= 10.0;
			}
			return std::round(value * scale) / scale;
		}

		std::string CargoKindToString(CargoKind kind)
		{
			return kind == CargoKind::Sender ? "sender" : "receiver";
		}

		std::string ComposeMarkerDisplayName(const CargoMarker& marker)
		{
			if (marker.Kind == CargoKind::Sender && !marker.ResourceSummary.empty())
			{
				return marker.DisplayName + " - " + marker.ResourceSummary;
			}

			return marker.DisplayName;
		}

		json ToJson(const SDK::FVector& value)
		{
			return json{
				{"x", RoundJsonNumber(value.X)},
				{"y", RoundJsonNumber(value.Y)},
				{"z", RoundJsonNumber(value.Z)}
			};
		}

		json ToJson(const SDK::FVector2f& value)
		{
			return json{
				{"x", RoundJsonNumber(value.X)},
				{"y", RoundJsonNumber(value.Y)}
			};
		}

		json ToJson(const CargoMarker& marker)
		{
			return json{
				{"kind", CargoKindToString(marker.Kind)},
				{"display_name", marker.DisplayName},
				{"label", ComposeMarkerDisplayName(marker)},
				{"resource", marker.ResourceSummary},
				{"source", marker.Source},
				{"unique_key", marker.PublicKey},
				{"world", ToJson(marker.WorldLocation)},
				{"map", ToJson(marker.MapLocation)}
			};
		}

		json ToJson(const TeleporterMarker& teleporter)
		{
			return json{
				{"label", teleporter.DisplayName},
				{"source", teleporter.Source},
				{"unique_key", teleporter.PublicKey},
				{"world", ToJson(teleporter.WorldLocation)},
				{"map", ToJson(teleporter.MapLocation)}
			};
		}

		json ToJson(const PlayerMarker& player)
		{
			return json{
				{"label", player.DisplayName},
				{"source", player.Source},
				{"unique_key", player.PublicKey},
				{"world", ToJson(player.WorldLocation)},
				{"map", ToJson(player.MapLocation)}
			};
		}

		json ToJson(const CargoConnection& connection)
		{
			return json{
				{"sender_key", connection.SenderKey},
				{"receiver_key", connection.ReceiverKey},
				{"sender_label", connection.SenderLabel},
				{"receiver_label", connection.ReceiverLabel},
				{"item", connection.ItemDisplayName},
				{"requested_amount", connection.RequestedAmount},
				{"sender", json{
					{"world", ToJson(connection.SenderWorldLocation)},
					{"map", ToJson(connection.SenderMapLocation)}
				}},
				{"receiver", json{
					{"world", ToJson(connection.ReceiverWorldLocation)},
					{"map", ToJson(connection.ReceiverMapLocation)}
				}}
			};
		}

		json ToJson(const RuptureCycleSnapshot& snapshot)
		{
			return json{
				{"available", snapshot.Available},
				{"chat_hud_found", snapshot.ChatHudFound},
				{"prefix_found", snapshot.PrefixFound},
				{"parsed", snapshot.Parsed},
				{"sequence", snapshot.HasSequence ? json(snapshot.Sequence) : json(nullptr)},
				{"wave", snapshot.Wave},
				{"stage", snapshot.Stage},
				{"step", snapshot.Step},
				{"elapsed_seconds", snapshot.HasElapsed ? json(RoundJsonNumber(snapshot.ElapsedSeconds, 3)) : json(nullptr)},
				{"observed_at_unix_ms", snapshot.HasObservedAtUnixMs ? json(snapshot.ObservedAtUnixMs) : json(nullptr)},
				{"raw_line", snapshot.RawLine},
				{"raw_payload", snapshot.RawPayload}
			};
		}
	}

	std::string BuildHealthJson(const CargoSnapshot& snapshot, int httpPort)
	{
		const json payload = {
			{"ok", true},
			{"plugin", "MapExtension_Plugin"},
			{"port", httpPort},
			{"world", snapshot.WorldName},
			{"snapshot_generation", snapshot.Generation},
			{"marker_count", snapshot.Markers.size()},
			{"connection_count", snapshot.Connections.size()},
			{"teleporter_count", snapshot.Teleporters.size()},
			{"player_count", snapshot.Players.size()}
		};
		return payload.dump();
	}

	std::string BuildCargoJson(const CargoSnapshot& snapshot)
	{
		json markers = json::array();
		for (const CargoMarker& marker : snapshot.Markers)
		{
			markers.push_back(ToJson(marker));
		}

		json teleporters = json::array();
		for (const TeleporterMarker& teleporter : snapshot.Teleporters)
		{
			teleporters.push_back(ToJson(teleporter));
		}

		json players = json::array();
		for (const PlayerMarker& player : snapshot.Players)
		{
			players.push_back(ToJson(player));
		}

		json connections = json::array();
		for (const CargoConnection& connection : snapshot.Connections)
		{
			connections.push_back(ToJson(connection));
		}

		const json payload = {
			{"generation", snapshot.Generation},
			{"world", snapshot.WorldName},
			{"reason", snapshot.Reason},
			{"counts", {
				{"markers", snapshot.Markers.size()},
				{"senders", snapshot.SenderCount},
				{"receivers", snapshot.ReceiverCount},
				{"connections", snapshot.Connections.size()},
				{"teleporters", snapshot.Teleporters.size()},
				{"players", snapshot.Players.size()}
			}},
			{"map", {
				{"src_x1", RoundJsonNumber(kMapSrcX1)},
				{"src_y1", RoundJsonNumber(kMapSrcY1)},
				{"dst_x1", RoundJsonNumber(kMapDstX1)},
				{"dst_y1", RoundJsonNumber(kMapDstY1)},
				{"src_x2", RoundJsonNumber(kMapSrcX2)},
				{"src_y2", RoundJsonNumber(kMapSrcY2)},
				{"dst_x2", RoundJsonNumber(kMapDstX2)},
				{"dst_y2", RoundJsonNumber(kMapDstY2)},
				{"content_width", RoundJsonNumber(kMapContentWidth)},
				{"content_height", RoundJsonNumber(kMapContentHeight)},
				{"image_width", kMapImageWidth},
				{"image_height", kMapImageHeight}
			}},
			{"markers", std::move(markers)},
			{"teleporters", std::move(teleporters)},
			{"players", std::move(players)},
			{"connections", std::move(connections)}
		};
		return payload.dump();
	}

	std::string BuildRuptureCycleJson(const CargoSnapshot& snapshot)
	{
		const json payload = {
			{"ok", true},
			{"generation", snapshot.Generation},
			{"world", snapshot.WorldName},
			{"timeline", {
				{"cycle_total_seconds", 3240},
				{"phase_seconds", {
					{"burning", 30},
					{"cooling", 60},
					{"stabilizing", 600},
					{"stable", 2550}
				}}
			}},
			{"rupture_cycle", ToJson(snapshot.RuptureCycle)}
		};
		return payload.dump();
	}
}
}
