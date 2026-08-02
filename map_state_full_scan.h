#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace SDK
{
	class UWorld;
}

namespace MapStateFullScan
{
	enum class Mode : uint8_t
	{
		MassSource,
		PlayerFallback
	};

	enum class Phase : uint8_t
	{
		Idle,
		CreatingSource,
		Moving,
		WaitingForStreaming,
		SettlingPcg,
		Capturing,
		Restoring,
		Completed,
		CompletedPartial,
		Cancelled,
		Failed
	};

	struct Status
	{
		Phase CurrentPhase = Phase::Idle;
		Mode CurrentMode = Mode::MassSource;
		size_t CurrentZone = 0;
		size_t TotalZones = 0;
		size_t TimedOutZones = 0;
		int StartingPlantCount = 0;
		int CurrentPlantCount = 0;
		int LiveTrackedPlantObservations = 0;
		std::string Message;
	};

	// UI-thread-safe requests. Unreal work is deferred to Tick on the game thread.
	void RequestStart();
	void RequestCancel();
	Status CopyStatus();
	bool IsActive();

	// Must run on the game thread.
	void Tick(SDK::UWorld* world, float deltaSeconds);
	void Shutdown(SDK::UWorld* world);
}
