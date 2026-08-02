#include "map_state_poi_store.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{
	constexpr const char* kCacheFileHeader = "# mapextension-poi-cache v2";

	std::string SanitizeStorageKey(const std::string& storageKey, size_t maxLength)
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
		if (sanitized.size() > maxLength)
		{
			sanitized.resize(maxLength);
		}
		return sanitized;
	}

	uint64_t HashStorageKey(const std::string& storageKey)
	{
		uint64_t hash = 14695981039346656037ull;
		for (const unsigned char character : storageKey)
		{
			hash ^= character;
			hash *= 1099511628211ull;
		}
		return hash;
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
		char hashText[17] = {};
		std::snprintf(
			hashText,
			sizeof(hashText),
			"%016llx",
			static_cast<unsigned long long>(HashStorageKey(storageKey)));
		return directory + "\\" + SanitizeStorageKey(storageKey, 96)
			+ "-" + hashText + ".tsv";
	}

	std::string BuildLegacyCacheFilePath(const std::string& storageKey)
	{
		const std::string directory = GetCacheDirectory();
		if (directory.empty())
		{
			return {};
		}
		return directory + "\\" + SanitizeStorageKey(storageKey, 120) + ".tsv";
	}
}

namespace MapStatePoiStore
{
	LoadResult LoadPlants(const std::string& storageKey)
	{
		LoadResult result{};
		std::string filePath = BuildCacheFilePath(storageKey);
		if (filePath.empty())
		{
			return result;
		}

		DWORD attributes = GetFileAttributesA(filePath.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			const DWORD error = GetLastError();
			if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
			{
				return result;
			}

			// Version 1 used only the sanitized key. Read it once for backward
			// compatibility; the next save writes the collision-resistant path.
			filePath = BuildLegacyCacheFilePath(storageKey);
			if (filePath.empty())
			{
				return result;
			}
			attributes = GetFileAttributesA(filePath.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES)
			{
				const DWORD legacyError = GetLastError();
				if (legacyError == ERROR_FILE_NOT_FOUND || legacyError == ERROR_PATH_NOT_FOUND)
				{
					result.Status = LoadStatus::Missing;
				}
				return result;
			}
		}

		std::ifstream file(filePath);
		if (!file.is_open())
		{
			return result;
		}

		std::vector<PersistedPlant>& plants = result.Plants;
		bool parseError = false;

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
				parseError = true;
				continue;
			}

			PersistedPlant plant{};
			plant.ResourceName = line.substr(0, firstTab);
			const char* cursor = line.c_str() + firstTab + 1;
			char* end = nullptr;
			plant.X = std::strtod(cursor, &end);
			if (end == cursor || *end != '\t')
			{
				parseError = true;
				continue;
			}
			cursor = end + 1;
			plant.Y = std::strtod(cursor, &end);
			if (end == cursor || *end != '\t')
			{
				parseError = true;
				continue;
			}
			cursor = end + 1;
			plant.Z = std::strtod(cursor, &end);
			if (end == cursor)
			{
				parseError = true;
				continue;
			}
			// Version 1 rows end after Z. Version 2 appends a depleted flag.
			if (*end == '\t')
			{
				cursor = end + 1;
				const long depleted = std::strtol(cursor, &end, 10);
				if (end == cursor || *end != '\0' || (depleted != 0 && depleted != 1))
				{
					parseError = true;
					continue;
				}
				plant.Depleted = depleted != 0;
			}
			else if (*end != '\0')
			{
				parseError = true;
				continue;
			}
			plants.push_back(std::move(plant));
		}
		if (file.bad() || parseError)
		{
			result.Plants.clear();
			return result;
		}
		result.Status = LoadStatus::Loaded;
		return result;
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
					<< plant.Z << '\t'
					<< (plant.Depleted ? 1 : 0) << '\n';
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
