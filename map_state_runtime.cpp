#include "map_state_runtime.h"

#include "map_state_capture.h"
#include "plugin_helpers.h"

#if defined(MODLOADER_CLIENT_BUILD)
#include "map_state_http.h"
#endif

#if defined(MODLOADER_CLIENT_BUILD)
#include "client/map_sync_client.h"
#endif

#if defined(MODLOADER_SERVER_BUILD)
#include "server/map_sync_server.h"
#endif

namespace
{
	bool g_registered = false;
}

namespace MapStateRuntime
{
	bool RegisterCallbacks()
	{
		IPluginHooks* hooks = GetHooks();
		if (!hooks)
		{
			LOG_ERROR("Cannot register callbacks: hooks interface is null");
			return false;
		}
		if (g_registered)
		{
			return true;
		}

#if defined(MODLOADER_CLIENT_BUILD)
		if (!Detail::StartHttpServer())
		{
			return false;
		}
#endif

		if (hooks->Engine && hooks->Engine->RegisterOnInit)
		{
			hooks->Engine->RegisterOnInit(OnEngineInit);
		}
		if (hooks->Engine && hooks->Engine->RegisterOnShutdown)
		{
			hooks->Engine->RegisterOnShutdown(OnEngineShutdown);
		}
		if (hooks->Engine && hooks->Engine->RegisterOnTick)
		{
			hooks->Engine->RegisterOnTick(OnEngineTick);
			LOG_INFO("Requested EngineTick callback for realtime cargo refresh; confirm [EngineTick] logs in modloader.log.");
		}
		else
		{
			LOG_WARN("Engine tick callbacks are unavailable; realtime cargo refresh is disabled.");
		}
		if (hooks->Actors && hooks->Actors->RegisterOnActorBeginPlay)
		{
			hooks->Actors->RegisterOnActorBeginPlay(OnActorBeginPlay);
		}
		else
		{
			LOG_WARN("ActorBeginPlay callbacks are unavailable; spawn-driven snapshot refresh is disabled.");
		}
		if (hooks->World && hooks->World->RegisterOnAnyWorldBeginPlay)
		{
			hooks->World->RegisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
		}
		if (hooks->World && hooks->World->RegisterOnSaveLoaded)
		{
			// Disabled on the current build line: the process dies during OnSaveLoaded
			// before the plugin callback is entered, so keep runtime refresh on the
			// safer world/experience/tick paths instead.
			LOG_WARN("SaveLoaded callback registration is disabled on this build line.");
		}
		if (hooks->World && hooks->World->RegisterOnExperienceLoadComplete)
		{
			// Disabled on the current client build line: world begin play is observed,
			// then EngineTick marks ChimeraMain ready and drives the first refresh.
			// This avoids an early transition-time refresh path that can still be fragile.
			LOG_WARN("ExperienceLoadComplete callback registration is disabled on this client build line.");
		}

		// WorldEndPlay hooks (v20)
		if (hooks->World && hooks->World->RegisterOnBeforeWorldEndPlay)
		{
			hooks->World->RegisterOnBeforeWorldEndPlay(OnBeforeWorldEndPlay);
		}
		if (hooks->World && hooks->World->RegisterOnAfterWorldEndPlay)
		{
			hooks->World->RegisterOnAfterWorldEndPlay(OnAfterWorldEndPlay);
		}

		// PlayerJoined hook (v14)
		if (hooks->Players && hooks->Players->RegisterOnPlayerJoined)
		{
			hooks->Players->RegisterOnPlayerJoined(OnPlayerJoined);
		}

		// Initialize network sync modules
#if defined(MODLOADER_CLIENT_BUILD)
		if (!MapExtensionClient::Sync::Initialize())
		{
			LOG_WARN("Client network sync initialization failed; dedicated snapshot sync disabled.");
		}
#endif

#if defined(MODLOADER_SERVER_BUILD)
		if (!MapExtensionServer::Sync::Initialize())
		{
			LOG_WARN("Server network sync initialization failed; snapshot requests will not be served.");
		}
#endif

		g_registered = true;
		LOG_INFO("Runtime callbacks registered");
		Detail::LogRuntimePlanIfNeeded();
		Detail::TryRefreshCurrentWorld("RegisterCallbacks");
		return true;
	}

	void UnregisterCallbacks()
	{
		IPluginHooks* hooks = GetHooks();
		if (hooks && g_registered)
		{
			if (hooks->Engine && hooks->Engine->UnregisterOnInit)
			{
				hooks->Engine->UnregisterOnInit(OnEngineInit);
			}
			if (hooks->Engine && hooks->Engine->UnregisterOnShutdown)
			{
				hooks->Engine->UnregisterOnShutdown(OnEngineShutdown);
			}
			if (hooks->Engine && hooks->Engine->UnregisterOnTick)
			{
				hooks->Engine->UnregisterOnTick(OnEngineTick);
			}
			if (hooks->Actors && hooks->Actors->UnregisterOnActorBeginPlay)
			{
				hooks->Actors->UnregisterOnActorBeginPlay(OnActorBeginPlay);
			}
			if (hooks->World && hooks->World->UnregisterOnAnyWorldBeginPlay)
			{
				hooks->World->UnregisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
			}
			if (hooks->World && hooks->World->UnregisterOnExperienceLoadComplete)
			{
				hooks->World->UnregisterOnExperienceLoadComplete(OnExperienceLoadComplete);
			}
			if (hooks->World && hooks->World->UnregisterOnBeforeWorldEndPlay)
			{
				hooks->World->UnregisterOnBeforeWorldEndPlay(OnBeforeWorldEndPlay);
			}
			if (hooks->World && hooks->World->UnregisterOnAfterWorldEndPlay)
			{
				hooks->World->UnregisterOnAfterWorldEndPlay(OnAfterWorldEndPlay);
			}
			if (hooks->Players && hooks->Players->UnregisterOnPlayerJoined)
			{
				hooks->Players->UnregisterOnPlayerJoined(OnPlayerJoined);
			}
		}

		// Shutdown network sync modules
#if defined(MODLOADER_CLIENT_BUILD)
		MapExtensionClient::Sync::Shutdown();
#endif

#if defined(MODLOADER_SERVER_BUILD)
		MapExtensionServer::Sync::Shutdown();
#endif

		g_registered = false;
#if defined(MODLOADER_CLIENT_BUILD)
		Detail::StopHttpServer();
#endif
		LOG_INFO("Runtime callbacks unregistered");
	}

	void HandleProcessDetach(bool processTerminating)
	{
#if defined(MODLOADER_CLIENT_BUILD)
		Detail::HandleHttpProcessDetach(processTerminating);
#else
		(void)processTerminating;
#endif
	}
}
