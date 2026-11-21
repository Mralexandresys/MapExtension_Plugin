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
		// True on a client connected to a co-op host (net mode Client, not a
		// dedicated server).
		bool IsCoopClient();
		// True on a co-op client that received a host snapshot recently.
		bool HasFreshCoopHostSnapshot();
	}
}
