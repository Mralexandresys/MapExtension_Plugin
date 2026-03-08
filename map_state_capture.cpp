#include "map_state_capture.h"

#include "plugin_config.h"
#include "plugin_helpers.h"

#include "AuItems_classes.hpp"
#include "BP_PackageReceiver_classes.hpp"
#include "BP_PackageSender_classes.hpp"
#include "BP_Teleporter_classes.hpp"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "Engine_classes.hpp"

#include <algorithm>
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
using PlayerMarker = MapStateRuntime::Detail::PlayerMarker;
using ReceiverLinkInfo = MapStateRuntime::Detail::ReceiverLinkInfo;
using TeleporterMarker = MapStateRuntime::Detail::TeleporterMarker;

namespace
{
	constexpr int kMaxLoggedMarkersPerSnapshot = 8;
	constexpr int kMaxLoggedActorsPerKind = 4;
	bool g_runtimePlanLogged = false;
	bool g_engineTickSeen = false;
	int g_anyWorldBeginPlayCount = 0;
	int g_saveLoadedCount = 0;
	int g_experienceLoadedCount = 0;
	SDK::UWorld* g_lastChimeraWorld = nullptr;
	std::string g_lastWorldName;
	float g_engineTickAccumulatorSeconds = 0.0f;

	std::mutex g_snapshotMutex;
	CargoSnapshot g_snapshot{};
	std::unordered_set<uint32_t> g_requestedCustomNameEntityIds;

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

	std::string CargoKindToString(CargoKind kind)
	{
		return kind == CargoKind::Sender ? "sender" : "receiver";
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

	SDK::FVector MakeVector(float x, float y, float z)
	{
		SDK::FVector value{};
		value.X = x;
		value.Y = y;
		value.Z = z;
		return value;
	}

	SDK::ACrGameStateBase* TryGetGameState(SDK::UWorld* world)
	{
		if (!world)
		{
			return nullptr;
		}

		SDK::AGameStateBase* gameStateBase = SDK::UGameplayStatics::GetGameState(world);
		return reinterpret_cast<SDK::ACrGameStateBase*>(gameStateBase);
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
		return reason != nullptr && std::strcmp(reason, "EngineTick") == 0;
	}

	bool IsRelevantRealtimeActorImpl(SDK::AActor* actor)
	{
		return actor
			&& (
				actor->IsA(SDK::ABP_PackageReceiver_C::StaticClass())
				|| actor->IsA(SDK::ABP_PackageSender_C::StaticClass())
				|| actor->IsA(SDK::ACrItemReceiverBuilding::StaticClass())
				|| actor->IsA(SDK::ACrItemSenderBuilding::StaticClass())
				|| actor->IsA(SDK::ACrTeleporter::StaticClass())
				|| actor->IsA(SDK::ABP_Teleporter_C::StaticClass())
				|| actor->IsA(SDK::ACrMegamachineTeleporterDevice::StaticClass())
				|| actor->IsA(SDK::ACrCharacterPlayerBase::StaticClass()));
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
		if (!world)
		{
			return nullptr;
		}

		return static_cast<SDK::UCrBuildingCustomNameSubsystem*>(
			SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(
				world,
				SDK::UCrBuildingCustomNameSubsystem::StaticClass()));
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

		return static_cast<SDK::ACrPlayerControllerBase*>(
			SDK::UGameplayStatics::GetPlayerController(world, 0));
	}

	std::string FindCachedBuildingCustomName(
		SDK::UCrBuildingCustomNameSubsystem* customNameSubsystem,
		const SDK::FCrMassEntityReplicationHelper& helper)
	{
		if (!customNameSubsystem)
		{
			return {};
		}

		const uint32_t targetId = GetReplicationHelperEntityIdValue(helper);
		if (targetId == 0)
		{
			return {};
		}

		for (const auto& entry : customNameSubsystem->CustomNames)
		{
			if (GetPersistentEntityIdValue(entry.Key()) == targetId)
			{
				return entry.Value().ToString();
			}
		}

		return {};
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

		if (!g_requestedCustomNameEntityIds.insert(entityId).second)
		{
			return;
		}

		SDK::ACrPlayerControllerBase* playerController = TryGetLocalCrPlayerController(world);
		if (!playerController)
		{
			g_requestedCustomNameEntityIds.erase(entityId);
			return;
		}

		playerController->ServerGetBuildingCustomName(helper);
	}

	std::string GetBuildingCustomName(
		SDK::UWorld* world,
		const SDK::FCrMassEntityReplicationHelper& helper)
	{
		if (!world)
		{
			return {};
		}

		SDK::UCrBuildingCustomNameSubsystem* customNameSubsystem =
			TryGetBuildingCustomNameSubsystem(world);
		const std::string cachedName = FindCachedBuildingCustomName(customNameSubsystem, helper);
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
		std::unordered_set<std::string> connectionKeys;

		const SDK::FCrReceiversContainer& receivers = replicator->ReceiversContainer;
		for (int index = 0; index < receivers.ReceiversData.Num(); ++index)
		{
			const SDK::FCrReceiverData& receiver = receivers.ReceiversData[index];
			const std::string receiverKey = BuildReplicationHelperKey(receiver.Receiver);
			const SDK::FVector worldLocation = MakeVector(receiver.Location.X, receiver.Location.Y, receiver.Location.Z);
			CargoMarker* marker = AddOrUpdateMarker(
				snapshot,
				markerIndexes,
				CargoKind::Receiver,
				worldLocation,
				receiverKey,
				"package_transport_replicator.receiver",
				GetBuildingCustomName(world, receiver.Receiver),
				{});
			if (marker)
			{
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
		}

		const SDK::FCrPackageTransportConnectionsContainer& connections = replicator->ConnectionsContainer;
		snapshot.ConnectionCount = connections.ConnectionsData.Num();
		for (int index = 0; index < connections.ConnectionsData.Num(); ++index)
		{
			const SDK::FCrPackageTransportConnectionData& connection = connections.ConnectionsData[index];
			const std::string senderKey = BuildReplicationHelperKey(connection.Sender);
			const std::string receiverKey = BuildReplicationHelperKey(connection.Receiver);
			const std::string resourceName = GetItemDisplayName(connection.Item);
			const SDK::FVector senderLocation = MakeVector(connection.SenderLocation.X, connection.SenderLocation.Y, connection.SenderLocation.Z);
			CargoMarker* senderMarker = AddOrUpdateMarker(
				snapshot,
				markerIndexes,
				CargoKind::Sender,
				senderLocation,
				senderKey,
				"package_transport_replicator.connection",
				GetBuildingCustomName(world, connection.Sender),
				resourceName);

			auto receiverIt = receiverLookup.find(receiverKey);
			if (!senderMarker || receiverIt == receiverLookup.end())
			{
				continue;
			}

			CargoMarker& receiverMarker = snapshot.Markers[receiverIt->second.MarkerIndex];
			AppendResourceName(receiverMarker, resourceName);

			std::string dedupeKey = senderMarker->InternalKey;
			dedupeKey.push_back('\x1F');
			dedupeKey += receiverKey;
			dedupeKey.push_back('\x1F');
			dedupeKey += resourceName;
			if (!connectionKeys.insert(dedupeKey).second)
			{
				continue;
			}

			CargoConnection route{};
			route.SenderKey = senderMarker->PublicKey;
			route.ReceiverKey = receiverIt->second.PublicKey;
			route.SenderLabel = ComposeMarkerDisplayName(*senderMarker);
			route.ReceiverLabel = receiverMarker.DisplayName;
			route.ItemDisplayName = resourceName;
			route.RequestedAmount = connection.RequestedAmount;
			route.SenderWorldLocation = senderMarker->WorldLocation;
			route.ReceiverWorldLocation = receiverMarker.WorldLocation;
			route.SenderMapLocation = senderMarker->MapLocation;
			route.ReceiverMapLocation = receiverMarker.MapLocation;
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
		SDK::ACrGameStateBase* gameState)
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
						GetBuildingCustomName(world, teleporter.Teleporter));
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
		CargoSnapshot nextSnapshot{};
		CargoSnapshot previousSnapshot{};
		{
			std::lock_guard<std::mutex> lock(g_snapshotMutex);
			nextSnapshot.Generation = g_snapshot.Generation + 1;
			previousSnapshot = g_snapshot;
		}
		nextSnapshot.Reason = reason ? reason : "unknown";
		nextSnapshot.WorldName = g_lastWorldName;

		std::unordered_map<std::string, size_t> markerIndexes;
		SDK::ACrGameStateBase* gameState = TryGetGameState(world);
		CaptureFromReplicator(world, nextSnapshot, gameState, markerIndexes);
		if (!isRealtimeRefresh || !nextSnapshot.HasPackageTransportReplicator)
		{
			CaptureActorFallback(world, nextSnapshot, markerIndexes);
		}
		CaptureTeleporters(world, nextSnapshot, gameState);
		CapturePlayers(world, nextSnapshot);

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

		{
			std::lock_guard<std::mutex> lock(g_snapshotMutex);
			g_snapshot = nextSnapshot;
		}

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
		SDK::UWorld* world = g_lastChimeraWorld ? g_lastChimeraWorld : SDK::UWorld::GetWorld();
		RefreshCargoSnapshotImpl(world, reason);
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
		g_requestedCustomNameEntityIds.clear();
	}

	void OnEngineTick(float deltaSeconds)
	{
		if (!g_lastChimeraWorld)
		{
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

		const float refreshIntervalSeconds = GetRefreshIntervalSeconds();
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
		Detail::RefreshCargoSnapshot(g_lastChimeraWorld, "EngineTick");
	}

	void OnActorBeginPlay(void* actor)
	{
		if (!g_lastChimeraWorld || !actor)
		{
			return;
		}

		SDK::AActor* beginPlayActor = static_cast<SDK::AActor*>(actor);
		if (!Detail::IsRelevantRealtimeActor(beginPlayActor))
		{
			return;
		}

		if (ShouldLogLifecycle())
		{
			LOG_INFO("ActorBeginPlay refresh trigger: class=%s actor=%s",
				beginPlayActor->Class ? beginPlayActor->Class->GetName().c_str() : "(null)",
				beginPlayActor->GetName().c_str());
		}

		g_engineTickAccumulatorSeconds = 0.0f;
		Detail::RefreshCargoSnapshot(g_lastChimeraWorld, "ActorBeginPlay");
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
			g_lastChimeraWorld = world;
			g_lastWorldName = safeName;
			g_engineTickAccumulatorSeconds = 0.0f;
			g_requestedCustomNameEntityIds.clear();
			if (ShouldLogLifecycle())
			{
				LOG_INFO("ChimeraMain detected: capturing cargo snapshot for HTTP endpoint");
			}
			Detail::RefreshCargoSnapshot(world, "AnyWorldBeginPlay(ChimeraMain)");
		}
	}

	void OnSaveLoaded()
	{
		++g_saveLoadedCount;
		if (ShouldLogLifecycle())
		{
			LOG_INFO("SaveLoaded #%d: refreshing cargo snapshot from save state", g_saveLoadedCount);
		}
		Detail::TryRefreshCurrentWorld("SaveLoaded");
	}

	void OnExperienceLoadComplete()
	{
		++g_experienceLoadedCount;
		if (ShouldLogLifecycle())
		{
			LOG_INFO("ExperienceLoadComplete #%d: refreshing cargo snapshot from runtime replication", g_experienceLoadedCount);
		}
		Detail::TryRefreshCurrentWorld("ExperienceLoadComplete");
	}
}
