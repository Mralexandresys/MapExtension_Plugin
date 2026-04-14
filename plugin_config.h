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
			"Legacy rupture logging toggle kept for existing configs"
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

		static bool LogRuptureCycleChat()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogRuptureCycleChat", false) : false;
		}

		static bool LogRuptureCycleEvents()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogRuptureCycleEvents", false) : false;
		}

		static bool LogRuptureDiagnostics()
		{
			return LogRuptureCycleChat() || LogRuptureCycleEvents();
		}

		static bool LogActorScanFallback()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Diagnostics", "LogActorScanFallback", false) : false;
		}

		static int HttpPort()
		{
			return (s_config && s_self) ? s_config->ReadInt(s_self, "Http", "Port", 9000) : 9000;
		}

		static int RefreshIntervalMs()
		{
			return (s_config && s_self) ? s_config->ReadInt(s_self, "Runtime", "RefreshIntervalMs", 500) : 500;
		}

	private:
		static IPluginConfig* s_config;
		static const IPluginSelf* s_self;
	};
}
