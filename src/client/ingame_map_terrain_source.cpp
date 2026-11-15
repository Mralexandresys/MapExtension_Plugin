#include "ingame_map_terrain_source.h"

#include "ingame_map_live_state.h"

#include "../map_state_sdk_helpers.h"
#include "../plugin_helpers.h"

#include "ChimeraUI_classes.hpp"
#include "AuItems_classes.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>

#if !defined(MODLOADER_CLIENT_BUILD)
#error "La carte in-game est client-only"
#endif

namespace MapExtensionClient::InGameMap
{
namespace
{
	constexpr int kMaxSegments = 4096;

	// MapTerrainSegmentSize est en metres alors que le pivot et les positions
	// monde sont en cm : mesure en jeu (CL-127004), pivot (-480000, -380000),
	// segment 1000, grille 6x6 -> seule l'echelle x100 englobe l'export web.
	constexpr float kSegmentSizeToWorldUnits = 100.0f;

	SDK::UCrMapMenuTerrainData* ResolveTerrainData(SDK::UCrMapMenuDevSettings* settings)
	{
		SDK::UClass* terrainClass = SDK::UCrMapMenuTerrainData::StaticClass();
		SDK::UObject* terrain = settings->TerrainData.Get();
		if (MapStateSdk::TryIsObjectOfClass(terrain, terrainClass))
		{
			return static_cast<SDK::UCrMapMenuTerrainData*>(terrain);
		}

		// Pas encore charge (carte native jamais ouverte) ou collecte par le GC :
		// chargement bloquant de la soft-ref, patron valide sur feature/new-ui.
		SDK::TSoftObjectPtr<SDK::UObject> untyped{};
		static_cast<SDK::FSoftObjectPtr&>(untyped) = static_cast<const SDK::FSoftObjectPtr&>(settings->TerrainData);
		const auto started = std::chrono::steady_clock::now();
		terrain = SDK::UKismetSystemLibrary::LoadAsset_Blocking(untyped);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		const bool ok = MapStateSdk::TryIsObjectOfClass(terrain, terrainClass);
		if (!ok || ms > 1.0)
		{
			LOG_INFO("[InGameMap] TerrainData blocking load: success=%d load_ms=%.2f", ok ? 1 : 0, ms);
		}
		return ok ? static_cast<SDK::UCrMapMenuTerrainData*>(terrain) : nullptr;
	}

	std::string BrushResourceName(const SDK::FSlateBrush& brush)
	{
		return brush.ResourceObject ? brush.ResourceObject->GetName() : std::string("(none)");
	}

	SDK::UTexture2D* FindSegmentTexture(int gridX, int gridY, bool useR2)
	{
		SDK::UCrMapMenuDevSettings* settings = SDK::UCrMapMenuDevSettings::GetDefaultObj();
		SDK::UCrMapMenuTerrainData* terrain = settings ? ResolveTerrainData(settings) : nullptr;
		if (!terrain)
		{
			return nullptr;
		}
		const int count = (std::min)(terrain->TerrainSegmentsData.Num(), kMaxSegments);
		for (int index = 0; index < count; ++index)
		{
			const SDK::FCrTerrainSegmentData& data = terrain->TerrainSegmentsData[index];
			if (data.TerrainSegmentGridIndex.X != gridX || data.TerrainSegmentGridIndex.Y != gridY)
			{
				continue;
			}
			SDK::UObject* resource = (useR2 ? data.TerrainSegmentTextureRadiation2 : data.TerrainSegmentTexture).ResourceObject;
			return MapStateSdk::TryIsObjectOfClass(resource, SDK::UTexture2D::StaticClass())
				? static_cast<SDK::UTexture2D*>(resource)
				: nullptr;
		}
		return nullptr;
	}


}

	bool CaptureTerrain(TerrainDescriptor& out, bool logDetails)
	{
		out = {};
		SDK::UCrMapMenuDevSettings* settings = SDK::UCrMapMenuDevSettings::GetDefaultObj();
		if (!settings)
		{
			return false;
		}
		SDK::UCrMapMenuTerrainData* terrain = ResolveTerrainData(settings);
		if (!terrain)
		{
			return false;
		}

		const int count = terrain->TerrainSegmentsData.Num();
		if (count <= 0 || count > kMaxSegments)
		{
			LOG_WARN("[InGameMap] TerrainSegmentsData count out of range: %d", count);
			return false;
		}

		out.Grid = {
			terrain->MapTerrainTopLeftPivotPoint.X, terrain->MapTerrainTopLeftPivotPoint.Y,
			terrain->MapTerrainSegmentSize.X * kSegmentSizeToWorldUnits,
			terrain->MapTerrainSegmentSize.Y * kSegmentSizeToWorldUnits };
		out.Segments.reserve(static_cast<size_t>(count));
		for (int index = 0; index < count; ++index)
		{
			const SDK::FCrTerrainSegmentData& data = terrain->TerrainSegmentsData[index];
			TerrainSegment segment;
			segment.GridX = data.TerrainSegmentGridIndex.X;
			segment.GridY = data.TerrainSegmentGridIndex.Y;
			segment.R1 = BrushResourceName(data.TerrainSegmentTexture);
			segment.R2 = BrushResourceName(data.TerrainSegmentTextureRadiation2);
			out.MinX = index == 0 ? segment.GridX : (std::min)(out.MinX, segment.GridX);
			out.MaxX = index == 0 ? segment.GridX : (std::max)(out.MaxX, segment.GridX);
			out.MinY = index == 0 ? segment.GridY : (std::min)(out.MinY, segment.GridY);
			out.MaxY = index == 0 ? segment.GridY : (std::max)(out.MaxY, segment.GridY);

			if (logDetails)
			{
				// UVRegion / ImageSize logges pour l'inconnue R2 (2,5) (plan ?10.5).
				const SDK::FSlateBrush& r1 = data.TerrainSegmentTexture;
				const SDK::FSlateBrush& r2 = data.TerrainSegmentTextureRadiation2;
				LOG_INFO(
					"[InGameMap] segment (%d,%d) R1=%s size=%.0fx%.0f uv=[%.3f,%.3f-%.3f,%.3f valid=%d] | R2=%s size=%.0fx%.0f uv=[%.3f,%.3f-%.3f,%.3f valid=%d]",
					segment.GridX, segment.GridY,
					segment.R1.c_str(), r1.ImageSize.X, r1.ImageSize.Y,
					r1.UVRegion.Min.X, r1.UVRegion.Min.Y, r1.UVRegion.Max.X, r1.UVRegion.Max.Y, r1.UVRegion.bIsValid ? 1 : 0,
					segment.R2.c_str(), r2.ImageSize.X, r2.ImageSize.Y,
					r2.UVRegion.Min.X, r2.UVRegion.Min.Y, r2.UVRegion.Max.X, r2.UVRegion.Max.Y, r2.UVRegion.bIsValid ? 1 : 0);
			}
			out.Segments.push_back(std::move(segment));
		}
		out.RadiationLevel = ReadRadiationLevel();
		out.Valid = true;

		if (logDetails)
		{
			LOG_INFO(
				"[InGameMap] terrain: pivot=(%.1f, %.1f, %.1f) segment_size=(%.1f, %.1f, %.1f) segments=%d grid=[%d..%d]x[%d..%d] radiation_level=%d",
				terrain->MapTerrainTopLeftPivotPoint.X, terrain->MapTerrainTopLeftPivotPoint.Y, terrain->MapTerrainTopLeftPivotPoint.Z,
				terrain->MapTerrainSegmentSize.X, terrain->MapTerrainSegmentSize.Y, terrain->MapTerrainSegmentSize.Z,
				count, out.MinX, out.MaxX, out.MinY, out.MaxY, out.RadiationLevel);
			LOG_INFO(
				"[InGameMap] settings: zoom min/default/max=%.3f/%.3f/%.3f map_area_pivot=(%.1f, %.1f, %.1f) mip_change far/near=%d/%d",
				settings->MinZoom, settings->DefaultZoom, settings->MaxZoom,
				settings->MapAreaPivotPoint.X, settings->MapAreaPivotPoint.Y, settings->MapAreaPivotPoint.Z,
				terrain->MapFarToEdgeMipMapChange, terrain->MapNearToEdgeMipMapChange);
			const int mipRules = std::clamp(terrain->TerrainMipMapByZoomValueData.Num(), 0, 64);
			for (int index = 0; index < mipRules; ++index)
			{
				const SDK::FCrTerrainMipMapByZoomValueData& rule = terrain->TerrainMipMapByZoomValueData[index];
				LOG_INFO("[InGameMap] mip rule %d: max_zoom=%.3f mip=%d", index, rule.MaximalZoomValue, rule.MipMap);
			}
		}
		return true;
	}

	void* LoadSegmentTexture(int gridX, int gridY, bool useR2, const char* textureName)
	{
		IPluginHooks* hooks = GetHooks();
		SDK::UTexture2D* texture = FindSegmentTexture(gridX, gridY, useR2);
		if (!texture || !hooks || !hooks->ImGuiTextures || !hooks->ImGuiTextures->LoadFromUTexture2D)
		{
			return nullptr;
		}
		try
		{
			return hooks->ImGuiTextures->LoadFromUTexture2D(texture, textureName);
		}
		catch (const std::out_of_range&)
		{
			LOG_WARN("[InGameMap] no free texture slot for %s", textureName);
			return nullptr;
		}
	}

	void* LoadItemIcon(const char* itemClassPath, const char* textureName)
	{
		IPluginHooks* hooks = GetHooks();
		if (!itemClassPath || !*itemClassPath || !hooks || !hooks->ImGuiTextures || !hooks->ImGuiTextures->LoadFromUTexture2D)
		{
			return nullptr;
		}
		// Chemin ASCII issu du catalogue ; resolution par les fonctions reflechies du moteur.
		const std::wstring widePath(itemClassPath, itemClassPath + std::strlen(itemClassPath));
		const SDK::FString pathString(widePath.c_str());
		SDK::UClass* itemClass = SDK::UKismetSystemLibrary::LoadClassAsset_Blocking(
			SDK::UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(SDK::UKismetSystemLibrary::MakeSoftClassPath(pathString)));
		if (!MapStateSdk::TryIsObjectOfClass(itemClass, SDK::UClass::StaticClass()))
		{
			return nullptr;
		}
		SDK::UObject* defaults = itemClass->ClassDefaultObject;
		if (!MapStateSdk::TryIsObjectOfClass(defaults, SDK::UAuItemDataBase::StaticClass()))
		{
			return nullptr;
		}
		SDK::UObject* resource = static_cast<SDK::UAuItemDataBase*>(defaults)->ItemIcon.ResourceObject;
		if (!MapStateSdk::TryIsObjectOfClass(resource, SDK::UTexture2D::StaticClass()))
		{
			return nullptr;
		}
		try
		{
			return hooks->ImGuiTextures->LoadFromUTexture2D(static_cast<SDK::UTexture2D*>(resource), textureName);
		}
		catch (const std::out_of_range&)
		{
			return nullptr;
		}
	}

	void* LoadTextureAsset(const char* assetPath, const char* textureName, const char** reason)
	{
		const char* ignored = nullptr;
		const char*& why = reason ? *reason : ignored;
		why = "texture API unavailable";
		IPluginHooks* hooks = GetHooks();
		if (!assetPath || !*assetPath || !hooks || !hooks->ImGuiTextures || !hooks->ImGuiTextures->LoadFromUTexture2D)
		{
			return nullptr;
		}
		const std::wstring widePath(assetPath, assetPath + std::strlen(assetPath));
		SDK::UObject* texture = SDK::UKismetSystemLibrary::LoadAsset_Blocking(
			SDK::UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(SDK::UKismetSystemLibrary::MakeSoftObjectPath(SDK::FString(widePath.c_str()))));
		why = texture ? "not a Texture2D" : "asset not found";
		if (!MapStateSdk::TryIsObjectOfClass(texture, SDK::UTexture2D::StaticClass()))
		{
			return nullptr;
		}
		try
		{
			void* handle = hooks->ImGuiTextures->LoadFromUTexture2D(static_cast<SDK::UTexture2D*>(texture), textureName);
			if (!handle)
			{
				// Le loader refuse une texture dont le streaming est en cours : on la retient.
				why = "not resident";
				static_cast<SDK::UTexture2D*>(texture)->SetForceMipLevelsToBeResident(30.0f, 0);
			}
			return handle;
		}
		catch (const std::out_of_range&)
		{
			why = "no free texture slot";
			return nullptr;
		}
	}

	std::vector<void*> LoadItemIconsByName(const std::vector<std::string>& keys)
	{
		std::vector<void*> icons(keys.size(), nullptr);
		IPluginHooks* hooks = GetHooks();
		if (keys.empty() || !hooks || !hooks->ImGuiTextures || !hooks->ImGuiTextures->LoadFromUTexture2D || !SDK::UObject::GObjects)
		{
			return icons;
		}
		SDK::UClass* itemBase = SDK::UAuItemDataBase::StaticClass();
		SDK::UClass* textureClass = SDK::UTexture2D::StaticClass();
		size_t missing = keys.size();
		const int count = SDK::UObject::GObjects->Num();
		for (int index = 0; index < count && missing > 0; ++index)
		{
			SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(index);
			if (!object || !object->Class || !object->IsDefaultObject() || !object->IsA(itemBase))
			{
				continue;
			}
			auto* item = static_cast<SDK::UAuItemDataBase*>(object);
			// Memes noms que GetItemDisplayName (map_state_capture.cpp), plus la classe.
			const std::string className = object->Class->GetName();
			const std::string displayName = item->ItemName.ToString();
			const std::string uniqueName = item->UniqueItemName.IsNone() ? std::string() : item->UniqueItemName.ToString();
			for (size_t key = 0; key < keys.size(); ++key)
			{
				if (icons[key] || keys[key].empty() || (keys[key] != className && keys[key] != displayName && keys[key] != uniqueName))
				{
					continue;
				}
				SDK::UObject* resource = item->ItemIcon.ResourceObject;
				if (!MapStateSdk::TryIsObjectOfClass(resource, textureClass))
				{
					continue;
				}
				try
				{
					icons[key] = hooks->ImGuiTextures->LoadFromUTexture2D(
						static_cast<SDK::UTexture2D*>(resource), ("MapExtension_icon_" + className).c_str());
				}
				catch (const std::out_of_range&)
				{
					return icons;
				}
				missing -= icons[key] ? 1 : 0;
			}
		}
		return icons;
	}

	bool RequestSegmentMips(int gridX, int gridY, bool useR2, float seconds)
	{
		SDK::UTexture2D* texture = FindSegmentTexture(gridX, gridY, useR2);
		if (!texture)
		{
			return false;
		}
		texture->SetForceMipLevelsToBeResident(seconds, 0);
		return true;
	}
}
