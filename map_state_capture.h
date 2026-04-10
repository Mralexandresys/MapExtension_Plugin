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
}
}
