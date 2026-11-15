#pragma once

// Guarded SDK accessors and rupture-cycle enum names shared by the client-side
// capture path (map_state_capture.cpp) and the server sync module
// (server/map_sync_server.cpp). Both translation units are compiled into the
// same DLL, so keeping two copies of these helpers only duplicated the code
// without buying any isolation.

#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"

#include <chrono>
#include <cstdint>
#include <excpt.h> // EXCEPTION_EXECUTE_HANDLER for the __except filters below
#include <cstring>
#include <string>

namespace MapStateSdk
{
	inline int64_t GetCurrentUnixTimeMilliseconds()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	inline bool IsChimeraWorldName(const char* worldName)
	{
		return worldName != nullptr && std::strstr(worldName, "ChimeraMain") != nullptr;
	}

	inline bool IsChimeraWorldName(const std::string& worldName)
	{
		return worldName.find("ChimeraMain") != std::string::npos;
	}

	// The world pointer can be stale after a level transition; touching the name
	// index behind SEH tells us whether it is still readable at all.
	inline bool TryProbeWorldNameRaw(SDK::UWorld* world)
	{
		if (!world)
		{
			return false;
		}

		__try
		{
			volatile auto nameIndex = world->Name.ComparisonIndex;
			(void)nameIndex;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	inline bool TryIsObjectOfClass(SDK::UObject* obj, SDK::UClass* expectedClass)
	{
		if (!obj || !expectedClass)
		{
			return false;
		}

		__try
		{
			return obj->IsA(expectedClass);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	template <typename TObjectClass>
	SDK::UClass* TryGetStaticClass()
	{
		__try
		{
			return TObjectClass::StaticClass();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return nullptr;
		}
	}

	template <typename TSubsystem>
	TSubsystem* TryGetWorldSubsystem(SDK::UWorld* world, SDK::UClass* subsystemClass)
	{
		if (!world || !subsystemClass)
		{
			return nullptr;
		}

		auto* subsystem = SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, subsystemClass);
		if (!subsystem || !subsystem->IsA(subsystemClass))
		{
			return nullptr;
		}

		return static_cast<TSubsystem*>(subsystem);
	}

	inline const char* EnviroWaveToString(SDK::EEnviroWave wave)
	{
		switch (wave)
		{
		case SDK::EEnviroWave::None:
			return "None";
		case SDK::EEnviroWave::Heat:
			return "Heat";
		case SDK::EEnviroWave::Cold:
			return "Cold";
		default:
			return "Unknown";
		}
	}

	inline const char* EnviroWaveStageToString(SDK::EEnviroWaveStage stage)
	{
		switch (stage)
		{
		case SDK::EEnviroWaveStage::None:
			return "None";
		case SDK::EEnviroWaveStage::PreWave:
			return "PreWave";
		case SDK::EEnviroWaveStage::Moving:
			return "Moving";
		case SDK::EEnviroWaveStage::Fadeout:
			return "Fadeout";
		case SDK::EEnviroWaveStage::Growback:
			return "Growback";
		default:
			return "Unknown";
		}
	}

	inline const char* PreWaveSubstageToString(SDK::EEnviroWavePreWaveSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWavePreWaveSubstage::None:
			return "None";
		case SDK::EEnviroWavePreWaveSubstage::BeforeExplosion:
			return "BeforeExplosion";
		case SDK::EEnviroWavePreWaveSubstage::AfterExplosion:
			return "AfterExplosion";
		default:
			return "Unknown";
		}
	}

	inline const char* FadeoutSubstageToString(SDK::EEnviroWaveFadeoutSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWaveFadeoutSubstage::None:
			return "None";
		case SDK::EEnviroWaveFadeoutSubstage::FireWave:
			return "FireWave";
		case SDK::EEnviroWaveFadeoutSubstage::Burning:
			return "Burning";
		case SDK::EEnviroWaveFadeoutSubstage::Fading:
			return "Fading";
		default:
			return "Unknown";
		}
	}

	inline const char* GrowbackSubstageToString(SDK::EEnviroWaveGrowbackSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWaveGrowbackSubstage::None:
			return "None";
		case SDK::EEnviroWaveGrowbackSubstage::MoonPhase:
			return "MoonPhase";
		case SDK::EEnviroWaveGrowbackSubstage::RegrowthStart:
			return "RegrowthStart";
		case SDK::EEnviroWaveGrowbackSubstage::Regrowth:
			return "Regrowth";
		default:
			return "Unknown";
		}
	}

	inline const char* EnviroWaveStepToString(
		SDK::EEnviroWaveStage stage,
		SDK::EEnviroWavePreWaveSubstage preWaveSubstage,
		SDK::EEnviroWaveFadeoutSubstage fadeoutSubstage,
		SDK::EEnviroWaveGrowbackSubstage growbackSubstage)
	{
		if (stage == SDK::EEnviroWaveStage::PreWave && preWaveSubstage != SDK::EEnviroWavePreWaveSubstage::None)
		{
			return PreWaveSubstageToString(preWaveSubstage);
		}
		if (stage == SDK::EEnviroWaveStage::Fadeout && fadeoutSubstage != SDK::EEnviroWaveFadeoutSubstage::None)
		{
			return FadeoutSubstageToString(fadeoutSubstage);
		}
		if (stage == SDK::EEnviroWaveStage::Growback && growbackSubstage != SDK::EEnviroWaveGrowbackSubstage::None)
		{
			return GrowbackSubstageToString(growbackSubstage);
		}
		return "None";
	}
}
