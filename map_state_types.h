#pragma once

#include "Basic.hpp"
#include "CoreUObject_structs.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace MapStateRuntime
{
namespace Detail
{
	constexpr float kMapSrcX1 = -358583.0f;
	constexpr float kMapSrcY1 = -263782.0f;
	constexpr float kMapDstX1 = 380.0f;
	constexpr float kMapDstY1 = 567.0f;
	constexpr float kMapSrcX2 = -98583.0f;
	constexpr float kMapSrcY2 = -9439.0f;
	constexpr float kMapDstX2 = 3927.0f;
	constexpr float kMapDstY2 = 4038.0f;
	constexpr float kMapScaleX = (kMapDstX2 - kMapDstX1) / (kMapSrcX2 - kMapSrcX1);
	constexpr float kMapScaleY = (kMapDstY2 - kMapDstY1) / (kMapSrcY2 - kMapSrcY1);
	constexpr float kMapContentWidth = kMapDstX2 - kMapDstX1;
	constexpr float kMapContentHeight = kMapDstY2 - kMapDstY1;
	constexpr int kMapImageWidth = 4352;
	constexpr int kMapImageHeight = 5120;

	inline SDK::FVector2f WorldToMap(const SDK::FVector& worldLocation)
	{
		SDK::FVector2f out{};
		out.X = static_cast<float>((worldLocation.X - kMapSrcX1) * kMapScaleX);
		out.Y = static_cast<float>((worldLocation.Y - kMapSrcY1) * kMapScaleY);
		return out;
	}

	enum class CargoKind : uint8_t
	{
		Sender,
		Receiver
	};

	struct CargoMarker
	{
		CargoKind Kind = CargoKind::Receiver;
		SDK::FVector WorldLocation{};
		SDK::FVector2f MapLocation{};
		std::string DisplayName;
		std::string ResourceSummary;
		std::string Source;
		std::string InternalKey;
		std::string PublicKey;
	};

	struct CargoConnection
	{
		std::string SenderKey;
		std::string ReceiverKey;
		std::string SenderLabel;
		std::string ReceiverLabel;
		std::string ItemDisplayName;
		int RequestedAmount = 0;
		SDK::FVector SenderWorldLocation{};
		SDK::FVector ReceiverWorldLocation{};
		SDK::FVector2f SenderMapLocation{};
		SDK::FVector2f ReceiverMapLocation{};
		int64_t LastObservedAtUnixMs = 0;
	};

	struct TeleporterMarker
	{
		SDK::FVector WorldLocation{};
		SDK::FVector2f MapLocation{};
		std::string DisplayName;
		std::string Source;
		std::string InternalKey;
		std::string PublicKey;
	};

	struct PlayerMarker
	{
		SDK::FVector WorldLocation{};
		SDK::FVector2f MapLocation{};
		std::string DisplayName;
		std::string Source;
		std::string InternalKey;
		std::string PublicKey;
	};

	struct RuptureCycleSnapshot
	{
		bool Available = false;
		std::string Wave;
		std::string Stage;
		std::string Step;
		double ElapsedSeconds = 0.0;
		bool HasElapsed = false;
		int64_t ObservedAtUnixMs = 0;
		bool HasObservedAtUnixMs = false;
	};

	struct CargoSnapshot
	{
		uint64_t Generation = 0;
		std::string Reason;
		std::string WorldName;
		bool UsedReplicator = false;
		bool UsedTeleporterReplicator = false;
		bool UsedActorFallback = false;
		bool HasPackageTransportReplicator = false;
		bool HasTeleportReplicator = false;
		int ReceiverCount = 0;
		int SenderCount = 0;
		int ConnectionCount = 0;
		int ActorReceiverCount = 0;
		int ActorSenderCount = 0;
		int TeleporterCount = 0;
		int PlayerCount = 0;
		std::vector<CargoMarker> Markers;
		std::vector<CargoConnection> Connections;
		std::vector<TeleporterMarker> Teleporters;
		std::vector<PlayerMarker> Players;
		RuptureCycleSnapshot RuptureCycle;
	};

	struct ReceiverLinkInfo
	{
		size_t MarkerIndex = 0;
		SDK::FVector WorldLocation{};
		SDK::FVector2f MapLocation{};
		std::string PublicKey;
	};
}
}
