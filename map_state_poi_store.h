#pragma once

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
	};

	// Loads the persisted plant list for a storage key. Returns an empty list
	// when no cache file exists or the file cannot be parsed.
	std::vector<PersistedPlant> LoadPlants(const std::string& storageKey);

	// Atomically replaces the persisted plant list for a storage key.
	// Returns false when the cache directory or file cannot be written.
	bool SavePlants(const std::string& storageKey, const std::vector<PersistedPlant>& plants);
}
