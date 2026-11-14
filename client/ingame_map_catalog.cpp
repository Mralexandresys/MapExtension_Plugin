#include "ingame_map_catalog.h"

#include "../third_party/nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if !defined(MODLOADER_CLIENT_BUILD)
#error "La carte in-game est client-only"
#endif

namespace MapExtensionClient::InGameMap
{
namespace
{
	using nlohmann::json;

	// Les fichiers sont du JSON enveloppe en JSONP : `SRMAPDATA(<payload>);`.
	json ReadJsonp(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			throw std::runtime_error("cannot open " + path.filename().string());
		}
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const size_t begin = text.find('(');
		const size_t end = text.rfind(')');
		if (begin == std::string::npos || end == std::string::npos || end <= begin)
		{
			throw std::runtime_error("not a SRMAPDATA file: " + path.filename().string());
		}
		return json::parse(text.begin() + static_cast<std::ptrdiff_t>(begin) + 1, text.begin() + static_cast<std::ptrdiff_t>(end));
	}

	CatalogCategory ParseCategory(const std::string& category)
	{
		if (category == "mineral") return CatalogCategory::Mineral;
		if (category == "plant") return CatalogCategory::Plant;
		return CatalogCategory::Creature;
	}

	// "abandoned_base" -> "Abandoned base"
	std::string Humanize(std::string identifier)
	{
		std::replace(identifier.begin(), identifier.end(), '_', ' ');
		if (!identifier.empty() && identifier[0] >= 'a' && identifier[0] <= 'z')
		{
			identifier[0] = static_cast<char>(identifier[0] - 'a' + 'A');
		}
		return identifier;
	}

	void LoadResources(const std::filesystem::path& directory, const json& manifest, float scale, Catalog& catalog)
	{
		const json& types = manifest.at("resource_types");
		for (const char* part : { "resources-mineral.js", "resources-plant.js", "resources-creature.js" })
		{
			const json payload = ReadJsonp(directory / part);
			for (const json& source : payload.at("groups"))
			{
				const std::string typeId = source.at("t").get<std::string>();
				const json& type = types.at(typeId);
				CatalogGroup group;
				group.Category = ParseCategory(type.at("category").get<std::string>());
				group.Label = type.value("en", typeId);
				group.ItemClassPath = type.value("item", std::string());

				// Coordonnees en entiers (decimetres) encodes en delta.
				const json& xs = source.at("x");
				const json& ys = source.at("y");
				if (xs.size() != ys.size())
				{
					throw std::runtime_error("x/y length mismatch for " + typeId);
				}
				group.Points.reserve(xs.size());
				long long x = 0, y = 0;
				for (size_t index = 0; index < xs.size(); ++index)
				{
					x += xs[index].get<long long>();
					y += ys[index].get<long long>();
					group.Points.push_back({ static_cast<float>(x) * scale, static_cast<float>(y) * scale });
				}
				if (source.contains("p") && source["p"].size() == xs.size())
				{
					group.Purity = source["p"].get<std::vector<uint8_t>>();
				}
				catalog.PointCount += group.Points.size();
				catalog.Groups.push_back(std::move(group));
			}
		}
	}

	void LoadPois(const std::filesystem::path& directory, float scale, Catalog& catalog)
	{
		const json payload = ReadJsonp(directory / "pois.js");
		std::vector<CatalogGroup> groups;
		for (const json& row : payload.at("rows"))
		{
			const std::string label = Humanize(row.at("g").get<std::string>());
			auto group = std::find_if(groups.begin(), groups.end(), [&](const CatalogGroup& candidate) { return candidate.Label == label; });
			if (group == groups.end())
			{
				groups.emplace_back();
				group = std::prev(groups.end());
				group->Category = CatalogCategory::Poi;
				group->Label = label;
			}
			group->Points.push_back({ row.at("x").get<float>() * scale, row.at("y").get<float>() * scale });
			group->Names.push_back(row.value("en", label));
			++catalog.PointCount;
		}
		catalog.Groups.insert(catalog.Groups.begin(), std::make_move_iterator(groups.begin()), std::make_move_iterator(groups.end()));
	}
}

	const char* PurityName(uint8_t purity)
	{
		static const char* const names[] = { "", "impure", "normal", "pure" };
		return purity < 4 ? names[purity] : "";
	}

	std::wstring DefaultCatalogDirectory()
	{
		HMODULE module = nullptr;
		wchar_t path[MAX_PATH * 2] = L"";
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&DefaultCatalogDirectory), &module)
			|| GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) == 0)
		{
			return {};
		}
		return (std::filesystem::path(path).parent_path() / L"MapExtension" / L"map-data").wstring();
	}

	std::shared_ptr<const Catalog> LoadCatalog(const std::wstring& directory)
	{
		auto catalog = std::make_shared<Catalog>();
		try
		{
			const std::filesystem::path root(directory);
			const json manifest = ReadJsonp(root / "manifest.js");
			const float scale = manifest.value("position_scale_cm", 10.0f);
			LoadResources(root, manifest, scale, *catalog);
			LoadPois(root, scale, *catalog);
			std::stable_sort(catalog->Groups.begin(), catalog->Groups.end(),
				[](const CatalogGroup& a, const CatalogGroup& b) { return a.Category < b.Category; });
		}
		catch (const std::exception& error)
		{
			*catalog = {};
			catalog->Error = error.what();
		}
		return catalog;
	}
}
