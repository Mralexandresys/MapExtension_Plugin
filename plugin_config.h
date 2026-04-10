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
			"EnableRuptureCycleInfoRequest",
			ConfigValueType::Boolean,
			"1",
			"Enable or disable the client chat request used to ask the server for rupture cycle info"
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

		static bool EnableRuptureCycleInfoRequest()
		{
			return (s_config && s_self) ? s_config->ReadBool(s_self, "Chat", "EnableRuptureCycleInfoRequest", true) : true;
		}

		static const char* RuptureCyclePrefix()
		{
			static char buffer[64] = {};
			if (s_config && s_self && s_config->ReadString(s_self, "Chat", "RuptureCyclePrefix", buffer, sizeof(buffer), "[RUPTURE_CYCLE]"))
			{
				return buffer;
			}
			return "[RUPTURE_CYCLE]";
		}

	private:
		static IPluginConfig* s_config;
		static const IPluginSelf* s_self;
	};
}
