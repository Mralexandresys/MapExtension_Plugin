#pragma once

#include "plugin_interface.h"

namespace MapExtensionPluginConfig
{
	static constexpr const char* kPluginName = "MapExtension_Plugin";

	static const ConfigEntry CONFIG_ENTRIES[] = {
		{
			"General",
			"Enabled",
			ConfigValueType::Boolean,
			"1",
			"Enable or disable the MapExtension_Plugin mod"
		},
		{
			"Diagnostics",
			"VerboseLifecycleLogs",
			ConfigValueType::Boolean,
			"0",
			"Log world/save/experience callbacks while the cargo snapshot runtime is active"
		},
		{
			"Diagnostics",
			"LogRuntimePlanOnce",
			ConfigValueType::Boolean,
			"0",
			"Log the current runtime acquisition plan once per process"
		},
		{
			"Diagnostics",
			"LogCargoSnapshots",
			ConfigValueType::Boolean,
			"0",
			"Log map snapshots, cargo links, teleporters, and players collected from runtime sources"
		},
		{
			"Diagnostics",
			"LogRuptureCycleEvents",
			ConfigValueType::Boolean,
			"0",
			"Log rupture cycle state changes and rupture-related world/server events on both client and server"
		},
		{
			"Diagnostics",
			"LogResourceObservations",
			ConfigValueType::Boolean,
			"0",
			"Log resource state changes, generation resets and ambiguous depletion matches for in-game validation"
		},
		{
			"Diagnostics",
			"LogActorScanFallback",
			ConfigValueType::Boolean,
			"0",
			"Log actor-scan fallback counts for BP_PackageSender and BP_PackageReceiver"
		},
		{
			"Diagnostics",
			"LogRefreshTimings",
			ConfigValueType::Boolean,
			"0",
			"Log per-phase refresh timings to help diagnose remaining client-side frame cost"
		},
		{
			"Runtime",
			"RefreshIntervalMs",
			ConfigValueType::Integer,
			"2000",
			"Realtime snapshot refresh interval in milliseconds while gameplay is running (lowered from 500 to reduce main-thread stalls during heavy replication)"
		},
		{
			"InGameMap",
			"ToggleKey",
			ConfigValueType::Keybind,
			"F8",
			"Key that opens/closes the in-game logistics map"
		},
		{
			"InGameMap",
			"TileBudgetMiB",
			ConfigValueType::Integer,
			"64",
			"Approximate video memory budget for full-resolution terrain tiles of the in-game map (0 = preview quality only)"
		},
		{
			"InGameMap",
			"ShowPopup",
			ConfigValueType::Boolean,
			"1",
			"Show a window, each time a game is loaded, saying the map opens in game with the toggle key and that players coming from a version older than 0.7 must download the mod again from Nexus Mods"
		}
	};

	static const ConfigSchema SCHEMA = {
		CONFIG_ENTRIES,
		static_cast<int>(sizeof(CONFIG_ENTRIES) / sizeof(ConfigEntry))
	};

	class Config
	{
	public:
		static void Initialize(const IPluginSelf* self)
		{
			s_self = self;
			s_config = self ? self->config : nullptr;
			if (s_config && s_self)
			{
				s_config->InitializeFromSchema(s_self, &SCHEMA);
			}
		}

		static bool IsEnabled()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "General", "Enabled", true) : true;
		}

		static bool VerboseLifecycleLogs()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "VerboseLifecycleLogs", false) : false;
		}

		static bool LogRuntimePlanOnce()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogRuntimePlanOnce", false) : false;
		}

		static bool LogCargoSnapshots()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogCargoSnapshots", false) : false;
		}

		static bool LogRuptureCycleEvents()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogRuptureCycleEvents", false) : false;
		}

		static bool LogActorScanFallback()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogActorScanFallback", false) : false;
		}

		static bool LogResourceObservations()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogResourceObservations", false) : false;
		}

		static bool LogRefreshTimings()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogRefreshTimings", false) : false;
		}

		static int RefreshIntervalMs()
		{
			return (s_config && s_self) ? s_config->ReadInt(s_self, "Runtime", "RefreshIntervalMs", 2000) : 2000;
		}

		static int InGameMapTileBudgetMiB()
		{
			return (s_config && s_self) ? s_config->ReadInt(s_self, "InGameMap", "TileBudgetMiB", 64) : 64;
		}

		static bool InGameMapShowPopup()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "InGameMap", "ShowPopup", true) : true;
		}

		static const char* InGameMapToggleKey()
		{
			static char key[64];
			if (!s_config || !s_self || !s_config->ReadString(s_self, "InGameMap", "ToggleKey", key, sizeof(key), "F8"))
			{
				return "F8";
			}
			return key;
		}

	private:
		static IPluginConfig* s_config;
		static const IPluginSelf* s_self;
	};
}
