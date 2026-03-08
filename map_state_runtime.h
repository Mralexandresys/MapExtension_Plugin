#pragma once

namespace SDK { class UWorld; }

namespace MapStateRuntime
{
	bool RegisterCallbacks();
	void UnregisterCallbacks();
	void HandleProcessDetach(bool processTerminating);

	void OnEngineInit();
	void OnEngineShutdown();
	void OnEngineTick(float deltaSeconds);
	void OnActorBeginPlay(void* actor);
	void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName);
	void OnSaveLoaded();
	void OnExperienceLoadComplete();
}
