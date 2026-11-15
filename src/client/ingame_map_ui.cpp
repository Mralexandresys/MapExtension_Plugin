#include "ingame_map_ui.h"

#include "ingame_map_internal.h"

namespace MapExtensionClient::InGameMap
{
namespace
{
	PluginPanelDesc g_panelDesc = { "Logistics map", "MapExtension - logistics map", &Render, &RenderTitleBar };

	void Enable()
	{
		IPluginHooks* hooks = GetHooks();
		if (g_panel || !hooks || !hooks->UI || !hooks->UI->RegisterPanel)
		{
			return;
		}
		g_panel = hooks->UI->RegisterPanel(&g_panelDesc);
		if (!g_panel)
		{
			LOG_WARN("[InGameMap] RegisterPanel failed; in-game map disabled");
			return;
		}
		const int budgetMiB = std::clamp(MapExtensionPluginConfig::Config::InGameMapTileBudgetMiB(), 0, 1024);
		g_hiResBudget = std::min(static_cast<int>(static_cast<float>(budgetMiB) / kHiResTileMiB), 64);
		StartService();
		if (hooks->Engine && hooks->Engine->RegisterOnTick)
		{
			hooks->Engine->RegisterOnTick(OnTick);
		}
		if (hooks->World && hooks->World->RegisterOnAnyWorldBeginPlay)
		{
			hooks->World->RegisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
		}
		g_toggleKey = MapExtensionPluginConfig::Config::InGameMapToggleKey();
		if (!g_toggleKey.empty() && hooks->Input && hooks->Input->RegisterKeybindByName)
		{
			hooks->Input->RegisterKeybindByName(g_toggleKey.c_str(), EModKeyEvent::Pressed, OnToggleKey);
		}
		LOG_INFO("[InGameMap] in-game map enabled (toggle key: %s, hi-res budget: %d MiB = %d tiles)",
			g_toggleKey.c_str(), budgetMiB, g_hiResBudget.load());
	}

	void Disable()
	{
		IPluginHooks* hooks = GetHooks();
		if (!g_panel || !hooks)
		{
			return;
		}
		if (!g_toggleKey.empty() && hooks->Input && hooks->Input->UnregisterKeybindByName)
		{
			hooks->Input->UnregisterKeybindByName(g_toggleKey.c_str(), EModKeyEvent::Pressed, OnToggleKey);
		}
		if (hooks->World && hooks->World->UnregisterOnAnyWorldBeginPlay)
		{
			hooks->World->UnregisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
		}
		if (hooks->Engine && hooks->Engine->UnregisterOnTick)
		{
			hooks->Engine->UnregisterOnTick(OnTick);
		}
		if (hooks->UI && hooks->UI->UnregisterPanel)
		{
			hooks->UI->UnregisterPanel(g_panel);
		}
		g_panel = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_publishedMutex);
			g_published.reset();
		}
		StopService();
		LOG_INFO("[InGameMap] in-game map disabled");
	}

	// InGameMap.Enabled s'applique a chaud : pas besoin de recharger le plugin.
	void OnConfigChanged(const char* section, const char* key, const char* newValue)
	{
		if (!section || !key || !newValue || std::strcmp(section, "InGameMap") != 0 || std::strcmp(key, "Enabled") != 0)
		{
			return;
		}
		if (newValue[0] == '1' || newValue[0] == 't' || newValue[0] == 'T')
		{
			Enable();
		}
		else
		{
			Disable();
		}
	}
}

	void Initialize()
	{
		IPluginHooks* hooks = GetHooks();
		if (hooks && hooks->UI && hooks->UI->RegisterOnConfigChanged)
		{
			hooks->UI->RegisterOnConfigChanged(GetPluginSelf(), OnConfigChanged);
		}
		if (MapExtensionPluginConfig::Config::InGameMapEnabled())
		{
			Enable();
		}
	}

	void Shutdown()
	{
		IPluginHooks* hooks = GetHooks();
		if (hooks && hooks->UI && hooks->UI->UnregisterOnConfigChanged)
		{
			hooks->UI->UnregisterOnConfigChanged(GetPluginSelf(), OnConfigChanged);
		}
		Disable();
	}
}
