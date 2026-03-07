#include "plugin.h"

#include "cargo_runtime.h"
#include "plugin_config.h"
#include "plugin_helpers.h"

static IPluginLogger* g_logger = nullptr;
static IPluginConfig* g_config = nullptr;
static IPluginScanner* g_scanner = nullptr;
static IPluginHooks* g_hooks = nullptr;

IPluginLogger* GetLogger() { return g_logger; }
IPluginConfig* GetConfig() { return g_config; }
IPluginScanner* GetScanner() { return g_scanner; }
IPluginHooks* GetHooks() { return g_hooks; }

#ifndef MODLOADER_BUILD_TAG
#define MODLOADER_BUILD_TAG "dev"
#endif

static PluginInfo s_pluginInfo = {
	"MapExtension_Plugin",
	MODLOADER_BUILD_TAG,
	"OpenAI",
	"Publishes cargo sender and receiver positions over local HTTP",
	PLUGIN_INTERFACE_VERSION
};

extern "C" {

__declspec(dllexport) PluginInfo* GetPluginInfo()
{
	return &s_pluginInfo;
}

__declspec(dllexport) bool PluginInit(IPluginLogger* logger, IPluginConfig* config, IPluginScanner* scanner, IPluginHooks* hooks)
{
	g_logger = logger;
	g_config = config;
	g_scanner = scanner;
	g_hooks = hooks;

	LOG_INFO("Plugin initializing...");

	MapExtensionPluginConfig::Config::Initialize(config);
	if (!MapExtensionPluginConfig::Config::IsEnabled())
	{
		LOG_WARN("Plugin is disabled in config");
		return true;
	}

	if (!CargoRuntime::RegisterCallbacks())
	{
		LOG_ERROR("Failed to register runtime callbacks / HTTP endpoint");
		return false;
	}

	LOG_INFO("Plugin initialized");
	return true;
}

__declspec(dllexport) void PluginShutdown()
{
	CargoRuntime::UnregisterCallbacks();
	LOG_INFO("Plugin shutting down");

	g_hooks = nullptr;
	g_scanner = nullptr;
	g_config = nullptr;
	g_logger = nullptr;
}

} // extern "C"
