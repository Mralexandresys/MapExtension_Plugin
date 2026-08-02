#pragma once

#include "map_state_types.h"

namespace SDK
{
	class AActor;
	class UWorld;
}

namespace MapStateRuntime
{
	namespace Detail
	{
	struct PoiScanInfo
	{
		std::string WorldName;
		int PlantCount = 0;
		int LastGatherableActorCount = 0;
		int LastLiveTrackedPlantActorCount = 0;
		int ObservedPlantCount = 0;
		uint64_t ObservationRevision = 0;
		uint64_t ObservationEventCount = 0;
		int64_t LastPlantObservedAtUnixMs = 0;
		int64_t LastScanAtUnixMs = 0;
		bool ScanInProgress = false;
	};

	CargoSnapshot CopySnapshot();
	void LogRuntimePlanIfNeeded();
	bool IsRelevantRealtimeActor(SDK::AActor* actor);
	bool RefreshCargoSnapshot(SDK::UWorld* world, const char* reason);
	void RequestCargoSnapshotRefresh(const char* reason);
	void TryRefreshCurrentWorld(const char* reason);
	void ShutdownRuptureCycleDelegateHooks();
	// Resets the POI scan throttle and schedules an immediate refresh so
	// all currently loaded gatherable actors are rescanned and the result
	// is saved to the per-session POI cache on disk.
	void ForcePoiRescan();
	// Returns world name, plant count, last scan timestamp and whether a
	// scan is currently in progress. Intended for the in-game UI panel.
	PoiScanInfo GetPoiScanInfo();
	// Returns the PublicKey a PlayerMarker captured for this player
	// controller would carry, or an empty string when it cannot be derived.
	// Used by the server sync module to flag the requesting player's own
	// marker before streaming a snapshot.
	std::string BuildPlayerPublicKeyForController(void* playerController);
}
}
