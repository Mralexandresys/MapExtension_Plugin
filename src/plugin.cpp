#include "plugin.h"

#include "game_fields.h"
#include "map_state_runtime.h"
#include "plugin_config.h"
#include "plugin_helpers.h"

#if defined(MODLOADER_CLIENT_BUILD)
#include "client/ingame_map_live_state.h"
#endif

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

// Every game member, size and function the client reads comes from the loader by name
// (game_fields.h), so it may run on a game build it was not compiled for. The server build
// has no OnPluginLoadHooks to resolve them and stays bound to its own build.
#if defined(MODLOADER_CLIENT_BUILD)
// A migrated plugin that names no game build is refused on every build: stop the build instead.
static_assert(PLUGIN_GAME_BUILT_CL != 0,
	"PLUGIN_GAME_BUILT_CL is 0: the Game-SDK has no Client/build_cl.txt; pass /p:PluginGameBuiltCL=<CL>");
#define MAPEXTENSION_MAP_FLAGS PLUGIN_MAP_MIGRATED
#else
#define MAPEXTENSION_MAP_FLAGS 0u
#endif

static PluginInfo s_pluginInfo = {
	"MapExtension_Plugin",
	MODLOADER_BUILD_TAG,
	MODLOADER_BUILD_AUTHOR,
	"In-game logistics map: cargo links, teleporters, players, base cores, monoliths and the rupture cycle",
	PLUGIN_INTERFACE_VERSION,
	MAPEXTENSION_PLUGIN_TARGET,
	PLUGIN_GAME_BUILT_CL,
	MAPEXTENSION_MAP_FLAGS
};

extern "C" {

__declspec(dllexport) PluginInfo* GetPluginInfo()
{
	return &s_pluginInfo;
}

#if defined(MODLOADER_CLIENT_BUILD)
// Export optionnel : seul moment ou le loader accepte une resolution d'adresse.
__declspec(dllexport) void OnPluginLoadHooks(IPluginSelf* self, IPluginHookScanner* scanner)
{
	GameFields::ResolveAll(self, scanner);
	MapExtensionClient::InGameMap::ResolveNativeAccess(self, scanner);
}
#endif

__declspec(dllexport) bool PluginInit(IPluginSelf* self)
{
	// First, before any early return: the SDK's GObjects and FName point at the running build.
	const bool sdkGlobals = PluginSdkRuntime::Init(self ? self->hooks : nullptr);
	g_pluginSelf = self;
	g_logger = self ? self->logger : nullptr;
	g_config = self ? self->config : nullptr;
	g_hooks = self ? self->hooks : nullptr;

	LOG_INFO("Plugin initializing...");

	MapExtensionPluginConfig::Config::Initialize(self);
	if (!sdkGlobals)
	{
		LOG_WARN("The mod loader gave no Game-SDK globals: the SDK keeps its compiled addresses");
	}
	GameFields::LogResolution();
	if (!MapExtensionPluginConfig::Config::IsEnabled())
	{
		LOG_WARN("Plugin is disabled in config");
		return true;
	}

	if (!MapStateRuntime::RegisterCallbacks())
	{
		LOG_ERROR("Failed to register runtime callbacks");
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
