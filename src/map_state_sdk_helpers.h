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
#include "game_fields.h"

#include <chrono>
#include <cstdint>
#include <excpt.h> // EXCEPTION_EXECUTE_HANDLER for the __except filters below
#include <cstring>
#include <string>

namespace MapStateSdk
{
	// Game enums read on the running build, back to the compiled SDK values (by entry name).
	inline SDK::EEnviroWave LiveWave(SDK::EEnviroWave live)
	{
		using E = SDK::EEnviroWave;
		static GameFields::LiveEnum<E> map("EEnviroWave", E::None, { { "None", E::None }, { "Heat", E::Heat }, { "Cold", E::Cold } });
		return map(live);
	}
	inline SDK::EEnviroWaveStage LiveStage(SDK::EEnviroWaveStage live)
	{
		using E = SDK::EEnviroWaveStage;
		static GameFields::LiveEnum<E> map("EEnviroWaveStage", E::None, { { "None", E::None }, { "PreWave", E::PreWave },
			{ "Moving", E::Moving }, { "Fadeout", E::Fadeout }, { "Growback", E::Growback } });
		return map(live);
	}
	inline SDK::EEnviroWavePreWaveSubstage LivePreWave(SDK::EEnviroWavePreWaveSubstage live)
	{
		using E = SDK::EEnviroWavePreWaveSubstage;
		static GameFields::LiveEnum<E> map("EEnviroWavePreWaveSubstage", E::None, { { "None", E::None },
			{ "BeforeExplosion", E::BeforeExplosion }, { "AfterExplosion", E::AfterExplosion } });
		return map(live);
	}
	inline SDK::EEnviroWaveFadeoutSubstage LiveFadeout(SDK::EEnviroWaveFadeoutSubstage live)
	{
		using E = SDK::EEnviroWaveFadeoutSubstage;
		static GameFields::LiveEnum<E> map("EEnviroWaveFadeoutSubstage", E::None, { { "None", E::None },
			{ "FireWave", E::FireWave }, { "Burning", E::Burning }, { "Fading", E::Fading } });
		return map(live);
	}
	inline SDK::EEnviroWaveGrowbackSubstage LiveGrowback(SDK::EEnviroWaveGrowbackSubstage live)
	{
		using E = SDK::EEnviroWaveGrowbackSubstage;
		static GameFields::LiveEnum<E> map("EEnviroWaveGrowbackSubstage", E::None, { { "None", E::None },
			{ "MoonPhase", E::MoonPhase }, { "RegrowthStart", E::RegrowthStart }, { "Regrowth", E::Regrowth } });
		return map(live);
	}
	inline SDK::ECrAlienObeliskState LiveObeliskState(SDK::ECrAlienObeliskState live)
	{
		using E = SDK::ECrAlienObeliskState;
		static GameFields::LiveEnum<E> map("ECrAlienObeliskState", E::Disabled, { { "Disabled", E::Disabled },
			{ "Cooldown", E::Cooldown }, { "Enabled", E::Enabled }, { "EnabledAndVisiblyCharged", E::EnabledAndVisiblyCharged },
			{ "AttackInProgress", E::AttackInProgress } });
		return map(live);
	}
	inline SDK::ECrAntennaState LiveAntennaState(SDK::ECrAntennaState live)
	{
		using E = SDK::ECrAntennaState;
		static GameFields::LiveEnum<E> map("ECrAntennaState", E::Unknown, { { "Unknown", E::Unknown }, { "Infected", E::Infected },
			{ "ReadyToCharge", E::ReadyToCharge }, { "Charging", E::Charging }, { "ReadyToActivate", E::ReadyToActivate },
			{ "Activated", E::Activated }, { "Locked", E::Locked } });
		return map(live);
	}
	inline SDK::EPointOfInterestState LivePoiState(SDK::EPointOfInterestState live)
	{
		using E = SDK::EPointOfInterestState;
		static GameFields::LiveEnum<E> map("EPointOfInterestState", E::Unknown, { { "Hidden", E::Hidden },
			{ "VisibleUnknown", E::VisibleUnknown }, { "VisibleDiscovered", E::VisibleDiscovered }, { "Disabled", E::Disabled },
			{ "Unknown", E::Unknown } });
		return map(live);
	}

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
