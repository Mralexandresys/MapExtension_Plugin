#pragma once

#include "plugin_interface.h"

// In-game ImGui panel for MapExtension_Plugin. Client build only.
// Provides scan status and a manual rescan trigger for the plant POI catalog.
namespace MapStateUI
{
	// Registers the ImGui panel with the mod loader UI. No-op when hooks->UI is null.
	void Register(IPluginHooks* hooks);

	// Unregisters the panel. Safe to call even if Register was never called.
	void Unregister(IPluginHooks* hooks);
}
