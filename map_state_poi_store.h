#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Disk persistence for discovered plant POIs. The game only materializes
// gatherable actors around loaded World Partition areas, so the in-memory
// catalog built during a session is the only full-map knowledge available.
// Persisting it per save session lets coverage survive game restarts.
namespace MapStatePoiStore
{
	struct PersistedPlant
	{
		std::string ResourceName;
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		bool Depleted = false;
	};

	enum class LoadStatus
	{
		Missing,
		Loaded,
		Error
	};

	struct LoadResult
	{
		LoadStatus Status = LoadStatus::Error;
		std::vector<PersistedPlant> Plants;
	};

	// Distinguishes a missing cache (safe to create) from an I/O failure (must
	// not be overwritten with a partial in-memory catalog).
	LoadResult LoadPlants(const std::string& storageKey);

	// Atomically replaces the persisted plant list for a storage key.
	// Returns false when the cache directory or file cannot be written.
	bool SavePlants(const std::string& storageKey, const std::vector<PersistedPlant>& plants);
}
