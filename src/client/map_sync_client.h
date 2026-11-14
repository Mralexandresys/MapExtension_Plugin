#pragma once

#include "../map_state_types.h"

namespace SDK
{
	class UWorld;
}

namespace MapExtensionClient
{
	namespace Sync
	{
		bool Initialize();
		void Shutdown();
		void ResetRuntimeState();
		bool RequestSnapshotIfNeeded(SDK::UWorld* world, const char* reason);
		bool TryCopyRemoteRuptureCycleSnapshot(MapStateRuntime::Detail::RuptureCycleSnapshot& outSnapshot);
		bool TryCopyRemoteCargoSnapshot(MapStateRuntime::Detail::CargoSnapshot& outSnapshot);
	}
}
