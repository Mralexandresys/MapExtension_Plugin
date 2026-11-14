#pragma once

namespace MapExtensionClient::InGameMap
{
	// POC carte logistique in-game (INGAME_MAP_POC_PLAN.md). Sans effet tant que
	// InGameMap.Enabled=0.
	void Initialize();
	void Shutdown();
}
