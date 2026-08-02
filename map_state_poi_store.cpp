#include "map_state_poi_store.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{
	constexpr const char* kCacheFileHeader = "# mapextension-poi-cache v1";

	std::string SanitizeStorageKey(const std::string& storageKey)
	{
		std::string sanitized;
		sanitized.reserve(storageKey.size());
		for (const char character : storageKey)
		{
			const bool keep =
				(character >= 'a' && character <= 'z')
				|| (character >= 'A' && character <= 'Z')
				|| (character >= '0' && character <= '9')
				|| character == '-'
				|| character == '_';
			sanitized.push_back(keep ? character : '_');
		}
		if (sanitized.empty())
		{
			sanitized = "default";
		}
		// Keep file names bounded even if a session name is unusually long.
		if (sanitized.size() > 120)
		{
			sanitized.resize(120);
		}
		return sanitized;
	}

	std::string GetCacheDirectory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&GetCacheDirectory),
			&module))
		{
			return {};
		}

		char modulePath[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameA(module, modulePath, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
		{
			return {};
		}

		std::string directory(modulePath, length);
		const size_t separator = directory.find_last_of("\\/");
		if (separator == std::string::npos)
		{
			return {};
		}
		directory.resize(separator);

		// The DLL lives in <exe_dir>\Plugins; keep the cache under the
		// plugin's own asset folder next to it.
		directory += "\\MapExtension_Plugin";
		CreateDirectoryA(directory.c_str(), nullptr);
		directory += "\\poi_cache";
		CreateDirectoryA(directory.c_str(), nullptr);
		return directory;
	}

	std::string BuildCacheFilePath(const std::string& storageKey)
	{
		const std::string directory = GetCacheDirectory();
		if (directory.empty())
		{
			return {};
		}
		return directory + "\\" + SanitizeStorageKey(storageKey) + ".tsv";
	}
}

namespace MapStatePoiStore
{
	std::vector<PersistedPlant> LoadPlants(const std::string& storageKey)
	{
		std::vector<PersistedPlant> plants;
		const std::string filePath = BuildCacheFilePath(storageKey);
		if (filePath.empty())
		{
			return plants;
		}

		std::ifstream file(filePath);
		if (!file.is_open())
		{
			return plants;
		}

		std::string line;
		while (std::getline(file, line))
		{
			if (line.empty() || line[0] == '#')
			{
				continue;
			}
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
			{
				line.pop_back();
			}

			const size_t firstTab = line.find('\t');
			if (firstTab == std::string::npos || firstTab == 0)
			{
				continue;
			}

			PersistedPlant plant{};
			plant.ResourceName = line.substr(0, firstTab);
			const char* cursor = line.c_str() + firstTab + 1;
			char* end = nullptr;
			plant.X = std::strtod(cursor, &end);
			if (end == cursor || *end != '\t')
			{
				continue;
			}
			cursor = end + 1;
			plant.Y = std::strtod(cursor, &end);
			if (end == cursor || *end != '\t')
			{
				continue;
			}
			cursor = end + 1;
			plant.Z = std::strtod(cursor, &end);
			if (end == cursor)
			{
				continue;
			}
			plants.push_back(std::move(plant));
		}
		return plants;
	}

	bool SavePlants(const std::string& storageKey, const std::vector<PersistedPlant>& plants)
	{
		const std::string filePath = BuildCacheFilePath(storageKey);
		if (filePath.empty())
		{
			return false;
		}

		const std::string temporaryPath = filePath + ".tmp";
		{
			std::ofstream file(temporaryPath, std::ios::trunc);
			if (!file.is_open())
			{
				return false;
			}

			file << kCacheFileHeader << '\n';
			std::ostringstream oss;
			// 9 significant digits round-trip a float exactly, so reloaded
			// coordinates rebuild the same rounded location keys.
			oss.precision(9);
			for (const PersistedPlant& plant : plants)
			{
				oss.str({});
				oss << plant.ResourceName << '\t'
					<< plant.X << '\t'
					<< plant.Y << '\t'
					<< plant.Z << '\n';
				file << oss.str();
			}
			if (!file.good())
			{
				return false;
			}
		}

		return MoveFileExA(
			temporaryPath.c_str(),
			filePath.c_str(),
			MOVEFILE_REPLACE_EXISTING) != 0;
	}
}
