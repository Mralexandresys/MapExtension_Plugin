#include "map_state_json.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <string>
#include <utility>

#ifndef MODLOADER_BUILD_TAG
#define MODLOADER_BUILD_TAG "dev"
#endif

namespace MapStateRuntime
{
namespace Detail
{
	namespace
	{
		using nlohmann::json;

		// Compatibility version of the HTTP payloads consumed by the viewer.
		//
		// The modloader auto-updater replaces MapExtension_Plugin.dll only, never
		// MapExtensionViewer.html / map-tiles/, so a recent plugin can end up talking
		// to an older viewer. Bump this ONLY when a payload change makes older
		// viewers incorrect, and bump VIEWER_CONTRACT_VERSION in
		// mapview/src/lib/viewerContract.ts to the same value in the same change.
		// The viewer shows an update prompt when this value is greater than its own.
		// Purely additive payload fields do not need a bump.
		constexpr int kViewerContractVersion = 2;

		constexpr const char* kProjectReleasesBaseUrl =
			"https://github.com/Mralexandresys/MapExtension_Plugin/releases";
		constexpr const char* kModPageUrl = "https://www.nexusmods.com/starrupture/mods/91";

		// Release builds are tagged by CI through /p:ModLoaderBuildTag. Local builds
		// fall back to PropertySheet.props, which also defines MAPEXTENSION_LOCAL_BUILD:
		// their tag looks like a release tag but has no matching published assets, so
		// they must not advertise a download URL.
#if defined(MAPEXTENSION_LOCAL_BUILD)
		constexpr bool kIsPublishedBuild = false;
#else
		constexpr bool kIsPublishedBuild = true;
#endif

		bool IsReleaseBuildTag(const std::string& buildTag)
		{
			return buildTag.rfind("ML-", 0) == 0 && buildTag.find("-v") != std::string::npos;
		}

		json BuildViewerUpdateJson(const std::string& buildTag)
		{
			json viewerUpdate = json::object();
			viewerUpdate["mod_page_url"] = kModPageUrl;

			if (kIsPublishedBuild && IsReleaseBuildTag(buildTag))
			{
				const std::string releasesBaseUrl = kProjectReleasesBaseUrl;
				viewerUpdate["release_url"] = releasesBaseUrl + "/tag/" + buildTag;
				viewerUpdate["download_url"] = releasesBaseUrl + "/download/" + buildTag
					+ "/MapExtension_Plugin-" + buildTag + "-viewer.zip";
			}

			return viewerUpdate;
		}

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
				{"self", player.IsSelf},
				{"world", ToJson(player.WorldLocation)},
				{"map", ToJson(player.MapLocation)}
			};
		}

		std::string PoiKindToString(PoiKind kind)
		{
			switch (kind)
			{
			case PoiKind::AbandonedBase:
				return "abandoned_base";
			case PoiKind::PlantResource:
				return "plant_resource";
			case PoiKind::Ignitium:
				return "ignitium";
			case PoiKind::StarTears:
				return "star_tears";
			}
			return "unknown";
		}

		json ToJson(const PoiMarker& poi)
		{
			return json{
				{"kind", PoiKindToString(poi.Kind)},
				{"label", poi.DisplayName},
				{"resource", poi.ResourceName},
				{"depleted", poi.Depleted},
				{"source", poi.Source},
				{"unique_key", poi.PublicKey},
				{"world", ToJson(poi.WorldLocation)},
				{"map", ToJson(poi.MapLocation)}
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
				{"wave", snapshot.Wave},
				{"stage", snapshot.Stage},
				{"step", snapshot.Step},
				{"elapsed_seconds", snapshot.HasElapsed ? json(RoundJsonNumber(snapshot.ElapsedSeconds, 3)) : json(nullptr)},
				{"observed_at_unix_ms", snapshot.HasObservedAtUnixMs ? json(snapshot.ObservedAtUnixMs) : json(nullptr)}
			};
		}
	}

	std::string BuildHealthJson(const CargoSnapshot& snapshot, int httpPort)
	{
		const std::string buildTag = MODLOADER_BUILD_TAG;
		const json payload = {
			{"ok", true},
			{"plugin", "MapExtension_Plugin"},
			{"plugin_version", buildTag},
			{"viewer_contract_version", kViewerContractVersion},
			{"viewer_update", BuildViewerUpdateJson(buildTag)},
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

		json pois = json::array();
		for (const PoiMarker& poi : snapshot.Pois)
		{
			pois.push_back(ToJson(poi));
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
				{"players", snapshot.Players.size()},
				{"pois", snapshot.Pois.size()},
				{"abandoned_bases", snapshot.AbandonedBaseCount},
				{"plant_resources", snapshot.PlantResourceCount},
				{"ignitium", snapshot.IgnitiumCount},
				{"star_tears", snapshot.StarTearsCount}
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
			{"connections", std::move(connections)},
			{"pois", std::move(pois)}
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
