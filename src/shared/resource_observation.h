#pragma once

#include <cstdint>
#include <optional>

namespace MapResources
{
	// Harvestability is independent of depletion: an intact ore may temporarily
	// stop accepting mining while its site still qualifies for the next phase.
	enum class Harvestability : uint8_t
	{
		Available,
		Unavailable,
		Unknown
	};

	inline const char* StateName(bool depleted, Harvestability harvestability)
	{
		if (depleted) return "depleted";
		if (harvestability == Harvestability::Unavailable) return "unavailable";
		if (harvestability == Harvestability::Unknown) return "unknown";
		return "available";
	}

	// Spatial depletion records carry no resource kind. Never resolve an
	// ambiguous record by iteration order or by ignoring depleted neighbours.
	struct UniqueMatch
	{
		uint8_t Count = 0;
		void Add() { if (Count < 2) ++Count; }
		bool IsUnique() const { return Count == 1; }
	};

	struct GenerationTracker
	{
		std::optional<int32_t> Seed;
		std::optional<bool> WasHeatMoving;
		uint64_t Generation = 0;

		bool Observe(std::optional<int32_t> seed, std::optional<bool> heatMoving)
		{
			// The replicated seed is authoritative when present. In standalone
			// worlds there is no replication actor; follow the normal Heat cycle.
			const bool changed = seed
				? Seed.has_value() && *seed != *Seed
				: heatMoving && WasHeatMoving && *heatMoving && !*WasHeatMoving;
			Seed = seed;
			if (heatMoving) WasHeatMoving = heatMoving;
			if (changed) ++Generation;
			return changed;
		}
	};
}
