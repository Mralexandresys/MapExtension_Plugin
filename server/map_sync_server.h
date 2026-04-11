#pragma once

namespace SDK
{
	class UWorld;
}

namespace MapExtensionServer
{
		namespace Sync
		{
			bool Initialize();
			void Shutdown();
			void OnEngineInit();
			void OnEngineShutdown();
			void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName);
			void OnBeforeWorldEndPlay(SDK::UWorld* world, const char* worldName);
			void OnAfterWorldEndPlay(SDK::UWorld* world, const char* worldName);
			void OnExperienceLoadComplete();
			void OnPlayerJoined(void* playerController);
		}
	}
