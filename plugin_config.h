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
			"LogRuptureCycleChat",
			ConfigValueType::Boolean,
			"0",
			"Log rupture cycle state parsed from server chat or recovered locally in solo sessions"
		},
		{
			"Diagnostics",
			"LogActorScanFallback",
			ConfigValueType::Boolean,
			"0",
			"Log actor-scan fallback counts for BP_PackageSender and BP_PackageReceiver"
		},
		{
			"Http",
			"Port",
			ConfigValueType::Integer,
			"9000",
			"Local HTTP port used to publish map snapshot JSON"
		},
		{
			"Runtime",
			"RefreshIntervalMs",
			ConfigValueType::Integer,
			"500",
			"Realtime snapshot refresh interval in milliseconds while gameplay is running"
		},
		{
			"Chat",
			"RuptureCyclePrefix",
			ConfigValueType::String,
			"[RUPTURE_CYCLE]",
			"Prefix used by the server chat plugin when broadcasting rupture cycle state"
		}
	};

	static const ConfigSchema SCHEMA = {
		CONFIG_ENTRIES,
		static_cast<int>(sizeof(CONFIG_ENTRIES) / sizeof(ConfigEntry))
	};

	class Config
	{
	public:
		static void Initialize(IPluginConfig* config)
		{
			s_config = config;
			if (s_config)
			{
				s_config->InitializeFromSchema(kPluginName, &SCHEMA);
			}
		}

		static bool IsEnabled()
		{
			return s_config ? s_config->ReadBool(kPluginName, "General", "Enabled", true) : true;
		}

		static bool VerboseLifecycleLogs()
		{
			return s_config ? s_config->ReadBool(kPluginName, "Diagnostics", "VerboseLifecycleLogs", false) : false;
		}

		static bool LogRuntimePlanOnce()
		{
			return s_config ? s_config->ReadBool(kPluginName, "Diagnostics", "LogRuntimePlanOnce", false) : false;
		}

		static bool LogCargoSnapshots()
		{
			return s_config ? s_config->ReadBool(kPluginName, "Diagnostics", "LogCargoSnapshots", false) : false;
		}

		static bool LogRuptureCycleChat()
		{
			return s_config ? s_config->ReadBool(kPluginName, "Diagnostics", "LogRuptureCycleChat", false) : false;
		}

		static bool LogActorScanFallback()
		{
			return s_config ? s_config->ReadBool(kPluginName, "Diagnostics", "LogActorScanFallback", false) : false;
		}

		static int HttpPort()
		{
			return s_config ? s_config->ReadInt(kPluginName, "Http", "Port", 9000) : 9000;
		}

		static int RefreshIntervalMs()
		{
			return s_config ? s_config->ReadInt(kPluginName, "Runtime", "RefreshIntervalMs", 500) : 500;
		}

		static const char* RuptureCyclePrefix()
		{
			static char buffer[64] = {};
			if (s_config && s_config->ReadString(kPluginName, "Chat", "RuptureCyclePrefix", buffer, sizeof(buffer), "[RUPTURE_CYCLE]"))
			{
				return buffer;
			}
			return "[RUPTURE_CYCLE]";
		}

	private:
		static IPluginConfig* s_config;
	};
}
