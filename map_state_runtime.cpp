#include "map_state_runtime.h"

#include "map_state_capture.h"
#include "map_state_http.h"
#include "plugin_helpers.h"

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
		if (!Detail::StartHttpServer())
		{
			return false;
		}

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
			hooks->World->RegisterOnSaveLoaded(OnSaveLoaded);
		}
		if (hooks->World && hooks->World->RegisterOnExperienceLoadComplete)
		{
			hooks->World->RegisterOnExperienceLoadComplete(OnExperienceLoadComplete);
		}

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
			if (hooks->World && hooks->World->UnregisterOnSaveLoaded)
			{
				hooks->World->UnregisterOnSaveLoaded(OnSaveLoaded);
			}
			if (hooks->World && hooks->World->UnregisterOnExperienceLoadComplete)
			{
				hooks->World->UnregisterOnExperienceLoadComplete(OnExperienceLoadComplete);
			}
		}

		g_registered = false;
		Detail::StopHttpServer();
		LOG_INFO("Runtime callbacks unregistered");
	}

	void HandleProcessDetach(bool processTerminating)
	{
		Detail::HandleHttpProcessDetach(processTerminating);
	}
}
