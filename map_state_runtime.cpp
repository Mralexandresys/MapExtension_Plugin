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

		if (hooks->RegisterEngineInitCallback)
		{
			hooks->RegisterEngineInitCallback(OnEngineInit);
		}
		if (hooks->RegisterEngineShutdownCallback)
		{
			hooks->RegisterEngineShutdownCallback(OnEngineShutdown);
		}
		if (hooks->RegisterEngineTickCallback)
		{
			hooks->RegisterEngineTickCallback(OnEngineTick);
			LOG_INFO("Requested EngineTick callback for realtime cargo refresh; confirm [EngineTick] logs in modloader.log.");
		}
		else
		{
			LOG_WARN("Engine tick callbacks are unavailable; realtime cargo refresh is disabled.");
		}
		if (hooks->RegisterActorBeginPlayCallback)
		{
			hooks->RegisterActorBeginPlayCallback(OnActorBeginPlay);
		}
		else
		{
			LOG_WARN("ActorBeginPlay callbacks are unavailable; spawn-driven snapshot refresh is disabled.");
		}
		if (hooks->RegisterAnyWorldBeginPlayCallback)
		{
			hooks->RegisterAnyWorldBeginPlayCallback(OnAnyWorldBeginPlay);
		}
		if (hooks->RegisterSaveLoadedCallback)
		{
			hooks->RegisterSaveLoadedCallback(OnSaveLoaded);
		}
		if (hooks->RegisterExperienceLoadCompleteCallback)
		{
			hooks->RegisterExperienceLoadCompleteCallback(OnExperienceLoadComplete);
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
			if (hooks->UnregisterEngineInitCallback)
			{
				hooks->UnregisterEngineInitCallback(OnEngineInit);
			}
			if (hooks->UnregisterEngineShutdownCallback)
			{
				hooks->UnregisterEngineShutdownCallback(OnEngineShutdown);
			}
			if (hooks->UnregisterEngineTickCallback)
			{
				hooks->UnregisterEngineTickCallback(OnEngineTick);
			}
			if (hooks->UnregisterActorBeginPlayCallback)
			{
				hooks->UnregisterActorBeginPlayCallback(OnActorBeginPlay);
			}
			if (hooks->UnregisterAnyWorldBeginPlayCallback)
			{
				hooks->UnregisterAnyWorldBeginPlayCallback(OnAnyWorldBeginPlay);
			}
			if (hooks->UnregisterSaveLoadedCallback)
			{
				hooks->UnregisterSaveLoadedCallback(OnSaveLoaded);
			}
			if (hooks->UnregisterExperienceLoadCompleteCallback)
			{
				hooks->UnregisterExperienceLoadCompleteCallback(OnExperienceLoadComplete);
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
