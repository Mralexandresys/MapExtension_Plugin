#include "plugin.h"

#include "map_state_runtime.h"
#include "plugin_config.h"
#include "plugin_helpers.h"

static IPluginLogger* g_logger = nullptr;
static IPluginConfig* g_config = nullptr;
static IPluginHooks* g_hooks = nullptr;
static const IPluginSelf* g_pluginSelf = nullptr;

IPluginLogger* GetLogger() { return g_logger; }
IPluginConfig* GetConfig() { return g_config; }
IPluginHooks* GetHooks() { return g_hooks; }
const IPluginSelf* GetPluginSelf() { return g_pluginSelf; }

#ifndef MODLOADER_BUILD_TAG
#define MODLOADER_BUILD_TAG "dev"
#endif

#ifndef MODLOADER_BUILD_AUTHOR
#define MODLOADER_BUILD_AUTHOR "Mralexandresys"
#endif

#if defined(MODLOADER_CLIENT_BUILD) && defined(MODLOADER_SERVER_BUILD)
#error "Only one of MODLOADER_CLIENT_BUILD or MODLOADER_SERVER_BUILD can be defined"
#elif defined(MODLOADER_CLIENT_BUILD)
#define MAPEXTENSION_PLUGIN_TARGET PLUGIN_TARGET_CLIENT
#elif defined(MODLOADER_SERVER_BUILD)
#define MAPEXTENSION_PLUGIN_TARGET PLUGIN_TARGET_SERVER
#else
#error "MapExtension_Plugin must be built as a client or server plugin"
#endif

static PluginInfo s_pluginInfo = {
	"MapExtension_Plugin",
	MODLOADER_BUILD_TAG,
	MODLOADER_BUILD_AUTHOR,
	"Exposes StarRupture map data, cargo links, teleporters, and players over local HTTP",
	PLUGIN_INTERFACE_VERSION,
	MAPEXTENSION_PLUGIN_TARGET
};

extern "C" {

__declspec(dllexport) PluginInfo* GetPluginInfo()
{
	return &s_pluginInfo;
}

__declspec(dllexport) bool PluginInit(IPluginSelf* self)
{
	g_pluginSelf = self;
	g_logger = self ? self->logger : nullptr;
	g_config = self ? self->config : nullptr;
	g_hooks = self ? self->hooks : nullptr;

	LOG_INFO("Plugin initializing...");

	MapExtensionPluginConfig::Config::Initialize(self);
	if (!MapExtensionPluginConfig::Config::IsEnabled())
	{
		LOG_WARN("Plugin is disabled in config");
		return true;
	}

	if (!MapStateRuntime::RegisterCallbacks())
	{
		LOG_ERROR("Failed to register runtime callbacks / HTTP endpoint");
		return false;
	}

	LOG_INFO("Plugin initialized");
	return true;
}

__declspec(dllexport) void PluginShutdown()
{
	MapStateRuntime::UnregisterCallbacks();
	LOG_INFO("Plugin shutting down");

	g_hooks = nullptr;
	g_config = nullptr;
	g_logger = nullptr;
	g_pluginSelf = nullptr;
}

} // extern "C"
