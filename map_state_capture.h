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
		CargoSnapshot CopySnapshot();
		void LogRuntimePlanIfNeeded();
		bool IsRelevantRealtimeActor(SDK::AActor* actor);
		bool RefreshCargoSnapshot(SDK::UWorld* world, const char* reason);
		void RequestCargoSnapshotRefresh(const char* reason);
		void TryRefreshCurrentWorld(const char* reason);
		void ShutdownRuptureCycleDelegateHooks();
		// Returns the PublicKey a PlayerMarker captured for this player
		// controller would carry, or an empty string when it cannot be derived.
		// Used by the server sync module to flag the requesting player's own
		// marker before streaming a snapshot.
		std::string BuildPlayerPublicKeyForController(void* playerController);
}
}
