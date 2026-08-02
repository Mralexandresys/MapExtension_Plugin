#include "client/ingame_map_probe.h"

#include "plugin_config.h"
#include "plugin_helpers.h"

#include "ChimeraUI_classes.hpp"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if !defined(MODLOADER_CLIENT_BUILD)
#error "The in-game map probe is client-only"
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace MapExtensionClient::InGameMapProbe
{
	void OnEngineInit();
	void OnEngineShutdown();
	void OnEngineTick(float deltaSeconds);
	void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName);
	void OnBeforeWorldEndPlay(SDK::UWorld* world, const char* worldName);

namespace
{
	constexpr int kMaxTerrainObjects = 16;
	constexpr int kMaxReasonableTerrainSegments = 4096;
	constexpr int kDeferredTextureFreeTicks = 2;
	constexpr float kTextureRetryDelaySeconds = 0.5f;
	constexpr float kMaxPreviewDimension = 512.0f;

	struct InventoryTarget final
	{
		const char* ClassName;
	};

	constexpr std::array<InventoryTarget, 6> kInventoryTargets = {{
		{ "CrMapMenuDevSettings" },
		{ "CrMapMenuTerrainData" },
		{ "CrMapManuSubsystem" },
		{ "WBP_MapMenuTerrain_C" },
		{ "WBP_MapMenu_C" },
		{ "WorldPartitionMiniMap" },
	}};

	struct InventoryCount final
	{
		int Instances = -1;
		int DefaultObjects = -1;
	};

	struct ProbeSnapshot final
	{
		int RunNumber = 0;
		std::string Trigger = "not run";
		std::string LastWorldName = "(none)";
		std::string Status = "Waiting for EngineInit";
		std::array<InventoryCount, kInventoryTargets.size()> Inventory{};
		bool ObjectWalkerReady = false;
		double InventoryMilliseconds = 0.0;

		bool SettingsAvailable = false;
		bool ConfiguredTerrainResolved = false;
		bool ConfiguredTerrainLoadAttempted = false;
		bool ConfiguredTerrainLoadedBlocking = false;
		double ConfiguredTerrainLoadMilliseconds = 0.0;
		float MapAreaPivotX = 0.0f;
		float MapAreaPivotY = 0.0f;
		float MapAreaPivotZ = 0.0f;

		int TerrainDataInstances = 0;
		bool TerrainDataResultsTruncated = false;
		std::string TerrainDataObjectName;
		float TerrainPivotX = 0.0f;
		float TerrainPivotY = 0.0f;
		float TerrainPivotZ = 0.0f;
		float SegmentWorldSizeX = 0.0f;
		float SegmentWorldSizeY = 0.0f;
		float SegmentWorldSizeZ = 0.0f;
		int SegmentCount = 0;
		int MipRuleCount = 0;
		bool GridBoundsAvailable = false;
		int GridMinX = 0;
		int GridMinY = 0;
		int GridMaxX = 0;
		int GridMaxY = 0;
		int NormalResourceCount = 0;
		int RadiationResourceCount = 0;
		int NormalTextureCount = 0;
		int RadiationTextureCount = 0;

		bool SegmentTextureSelected = false;
		int SelectedGridX = 0;
		int SelectedGridY = 0;
		float SelectedBrushWidth = 0.0f;
		float SelectedBrushHeight = 0.0f;
		std::string SelectedBrushVariant;
		std::string SelectedResourceClass;
		std::string SelectedResourceName;

		int TextureSlotsFreeBeforeLoad = -1;
		int TextureSlotCapacity = -1;
		double TextureLoadMilliseconds = 0.0;
		int TextureWidth = 0;
		int TextureHeight = 0;
		double EstimatedRgbaMiB = 0.0;
		bool TextureLoaded = false;

		bool WorldPartitionMiniMapFound = false;
		bool WorldPartitionBoundsValid = false;
		double WorldPartitionMinX = 0.0;
		double WorldPartitionMinY = 0.0;
		double WorldPartitionMaxX = 0.0;
		double WorldPartitionMaxY = 0.0;
		int WorldUnitsPerPixel = 0;
		std::string WorldPartitionTextureName;
	};

	struct SharedState final
	{
		ProbeSnapshot Snapshot;
		PluginTextureHandle Texture = nullptr;
	};

	struct PendingTextureFree final
	{
		PluginTextureHandle Handle = nullptr;
		int TicksRemaining = 0;
	};

	struct BrushObservation final
	{
		SDK::UObject* ResourceObject = nullptr;
		SDK::UTexture2D* Texture = nullptr;
		float Width = 0.0f;
		float Height = 0.0f;
		std::string ResourceClass;
		std::string ResourceName;
	};

	struct TextureCandidate final
	{
		SDK::UTexture2D* Texture = nullptr;
		int GridX = 0;
		int GridY = 0;
		float BrushWidth = 0.0f;
		float BrushHeight = 0.0f;
		const char* Variant = nullptr;
		std::string ResourceClass;
		std::string ResourceName;
	};

	enum class ProbeRunResult
	{
		Complete,
		TextureNotReady
	};

	std::atomic<bool> g_active{ false };
	std::atomic<bool> g_probeRequested{ false };
	std::atomic<long long> g_lastRenderMicroseconds{ 0 };
	std::mutex g_stateMutex;
	SharedState g_sharedState{};
	std::string g_pendingTrigger;
	std::vector<PendingTextureFree> g_pendingTextureFrees;
	PanelHandle g_panelHandle = nullptr;
	int g_textureGeneration = 0;
	bool g_engineInitCallbackRegistered = false;
	bool g_engineShutdownCallbackRegistered = false;
	bool g_engineTickCallbackRegistered = false;
	bool g_worldBeginCallbackRegistered = false;
	bool g_worldEndCallbackRegistered = false;
	bool g_textureRetryScheduled = false;
	float g_textureRetryDelaySecondsRemaining = 0.0f;

	void RenderPanel(IModLoaderImGui* imgui);

	PluginPanelDesc g_panelDesc = {
		"Map probe",
		"MapExtension native map probe",
		&RenderPanel
	};

	bool IsObjectOfClass(SDK::UObject* object, SDK::UClass* expectedClass)
	{
		if (!object || !expectedClass)
		{
			return false;
		}

		__try
		{
			return object->IsA(expectedClass);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	std::string GetObjectName(SDK::UObject* object)
	{
		if (!object)
		{
			return "(null)";
		}

		try
		{
			return object->GetName();
		}
		catch (...)
		{
			return "(unreadable)";
		}
	}

	std::string GetObjectClassName(SDK::UObject* object)
	{
		if (!object || !object->Class)
		{
			return "(null)";
		}

		try
		{
			return object->Class->GetName();
		}
		catch (...)
		{
			return "(unreadable)";
		}
	}

	void QueueTextureFree(PluginTextureHandle handle)
	{
		if (handle)
		{
			g_pendingTextureFrees.push_back({ handle, kDeferredTextureFreeTicks });
		}
	}

	void RetireCurrentTexture(const char* status)
	{
		PluginTextureHandle retired = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			retired = g_sharedState.Texture;
			g_sharedState.Texture = nullptr;
			g_sharedState.Snapshot.TextureLoaded = false;
			if (status)
			{
				g_sharedState.Snapshot.Status = status;
			}
		}
		QueueTextureFree(retired);
	}

	void ProcessPendingTextureFrees()
	{
		IPluginHooks* hooks = GetHooks();
		IPluginImGuiTextures* textures = hooks ? hooks->ImGuiTextures : nullptr;
		if (!textures || !textures->FreeTexture)
		{
			return;
		}

		for (auto it = g_pendingTextureFrees.begin(); it != g_pendingTextureFrees.end();)
		{
			--it->TicksRemaining;
			if (it->TicksRemaining <= 0)
			{
				textures->FreeTexture(it->Handle);
				it = g_pendingTextureFrees.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void RequestProbe(const char* trigger)
	{
		if (!g_active.load())
		{
			return;
		}

		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			g_pendingTrigger = trigger ? trigger : "unknown";
			g_sharedState.Snapshot.Status = "Probe queued on the game thread";
		}
		g_probeRequested.store(true);
	}

	int CountObjects(
		IPluginObjectWalker* walker,
		const char* className,
		PluginObjectLookupMode mode)
	{
		if (!walker || !walker->FindObjectsByClassNameInto)
		{
			return -1;
		}

		return walker->FindObjectsByClassNameInto(className, mode, nullptr, 0);
	}

	BrushObservation ObserveBrush(const SDK::FSlateBrush& brush, SDK::UClass* textureClass)
	{
		BrushObservation observation{};
		observation.ResourceObject = brush.ResourceObject;
		observation.Width = brush.ImageSize.X;
		observation.Height = brush.ImageSize.Y;
		observation.ResourceClass = GetObjectClassName(observation.ResourceObject);
		observation.ResourceName = GetObjectName(observation.ResourceObject);
		if (IsObjectOfClass(observation.ResourceObject, textureClass))
		{
			observation.Texture = static_cast<SDK::UTexture2D*>(observation.ResourceObject);
		}
		return observation;
	}

	void InspectWorldPartitionMiniMap(IPluginObjectWalker* walker, ProbeSnapshot& snapshot)
	{
		PluginObjectInfo objectInfo{};
		const int count = walker->FindObjectsByClassNameInto(
			"WorldPartitionMiniMap",
			PluginObjectLookup_InstanceOnly,
			&objectInfo,
			1);
		if (count <= 0 || !objectInfo.object)
		{
			return;
		}

		auto* object = static_cast<SDK::UObject*>(objectInfo.object);
		if (!IsObjectOfClass(object, SDK::AWorldPartitionMiniMap::StaticClass()))
		{
			return;
		}

		auto* miniMap = static_cast<SDK::AWorldPartitionMiniMap*>(object);
		snapshot.WorldPartitionMiniMapFound = true;
		snapshot.WorldPartitionBoundsValid = miniMap->MiniMapWorldBounds.IsValid;
		snapshot.WorldPartitionMinX = miniMap->MiniMapWorldBounds.Min.X;
		snapshot.WorldPartitionMinY = miniMap->MiniMapWorldBounds.Min.Y;
		snapshot.WorldPartitionMaxX = miniMap->MiniMapWorldBounds.Max.X;
		snapshot.WorldPartitionMaxY = miniMap->MiniMapWorldBounds.Max.Y;
		snapshot.WorldUnitsPerPixel = miniMap->WorldUnitsPerPixel;
		snapshot.WorldPartitionTextureName = GetObjectName(miniMap->MiniMapTexture);

		LOG_INFO(
			"In-game map probe WorldPartitionMiniMap: object=%s bounds_valid=%d bounds_xy=(%.3f,%.3f)-(%.3f,%.3f) world_units_per_pixel=%d texture=%s",
			objectInfo.objectName,
			snapshot.WorldPartitionBoundsValid ? 1 : 0,
			snapshot.WorldPartitionMinX,
			snapshot.WorldPartitionMinY,
			snapshot.WorldPartitionMaxX,
			snapshot.WorldPartitionMaxY,
			snapshot.WorldUnitsPerPixel,
			snapshot.WorldPartitionTextureName.c_str());
	}

	SDK::UCrMapMenuTerrainData* FindTerrainData(
		IPluginObjectWalker* walker,
		ProbeSnapshot& snapshot,
		std::array<PluginObjectInfo, kMaxTerrainObjects>& terrainObjects,
		int& capturedTerrainObjects,
		bool allowBlockingLoad)
	{
		SDK::UCrMapMenuDevSettings* settings = SDK::UCrMapMenuDevSettings::GetDefaultObj();
		SDK::UCrMapMenuTerrainData* configuredTerrain = nullptr;
		if (settings)
		{
			snapshot.SettingsAvailable = true;
			snapshot.MapAreaPivotX = settings->MapAreaPivotPoint.X;
			snapshot.MapAreaPivotY = settings->MapAreaPivotPoint.Y;
			snapshot.MapAreaPivotZ = settings->MapAreaPivotPoint.Z;
			configuredTerrain = settings->TerrainData.Get();
			snapshot.ConfiguredTerrainResolved = IsObjectOfClass(
				configuredTerrain,
				SDK::UCrMapMenuTerrainData::StaticClass());
			if (!snapshot.ConfiguredTerrainResolved)
			{
				configuredTerrain = nullptr;
			}
		}

		const int total = walker->FindObjectsByClassNameInto(
			"CrMapMenuTerrainData",
			PluginObjectLookup_InstanceOnly,
			terrainObjects.data(),
			static_cast<int>(terrainObjects.size()));
		snapshot.TerrainDataInstances = std::max(total, 0);
		snapshot.TerrainDataResultsTruncated = total > static_cast<int>(terrainObjects.size());
		capturedTerrainObjects = std::clamp(total, 0, static_cast<int>(terrainObjects.size()));

		if (configuredTerrain)
		{
			return configuredTerrain;
		}

		SDK::UClass* terrainClass = SDK::UCrMapMenuTerrainData::StaticClass();
		for (int index = 0; index < capturedTerrainObjects; ++index)
		{
			auto* object = static_cast<SDK::UObject*>(terrainObjects[static_cast<size_t>(index)].object);
			if (IsObjectOfClass(object, terrainClass))
			{
				return static_cast<SDK::UCrMapMenuTerrainData*>(object);
			}
		}

		if (!allowBlockingLoad || !settings)
		{
			return nullptr;
		}

		snapshot.ConfiguredTerrainLoadAttempted = true;
		SDK::TSoftObjectPtr<SDK::UObject> untypedTerrainReference{};
		static_cast<SDK::FSoftObjectPtr&>(untypedTerrainReference) =
			static_cast<const SDK::FSoftObjectPtr&>(settings->TerrainData);

		const auto loadStarted = std::chrono::steady_clock::now();
		SDK::UObject* loadedObject = SDK::UKismetSystemLibrary::LoadAsset_Blocking(untypedTerrainReference);
		const auto loadFinished = std::chrono::steady_clock::now();
		snapshot.ConfiguredTerrainLoadMilliseconds = std::chrono::duration<double, std::milli>(
			loadFinished - loadStarted).count();
		snapshot.ConfiguredTerrainLoadedBlocking = IsObjectOfClass(loadedObject, terrainClass);

		LOG_INFO(
			"In-game map probe configured TerrainData blocking load: success=%d object=%s class=%s load_ms=%.3f",
			snapshot.ConfiguredTerrainLoadedBlocking ? 1 : 0,
			GetObjectName(loadedObject).c_str(),
			GetObjectClassName(loadedObject).c_str(),
			snapshot.ConfiguredTerrainLoadMilliseconds);

		return snapshot.ConfiguredTerrainLoadedBlocking
			? static_cast<SDK::UCrMapMenuTerrainData*>(loadedObject)
			: nullptr;
	}

	bool InspectTerrainData(
		SDK::UCrMapMenuTerrainData* terrainData,
		ProbeSnapshot& snapshot,
		TextureCandidate& selectedTexture)
	{
		if (!terrainData)
		{
			return false;
		}

		snapshot.TerrainDataObjectName = GetObjectName(terrainData);
		snapshot.TerrainPivotX = terrainData->MapTerrainTopLeftPivotPoint.X;
		snapshot.TerrainPivotY = terrainData->MapTerrainTopLeftPivotPoint.Y;
		snapshot.TerrainPivotZ = terrainData->MapTerrainTopLeftPivotPoint.Z;
		snapshot.SegmentWorldSizeX = terrainData->MapTerrainSegmentSize.X;
		snapshot.SegmentWorldSizeY = terrainData->MapTerrainSegmentSize.Y;
		snapshot.SegmentWorldSizeZ = terrainData->MapTerrainSegmentSize.Z;
		snapshot.MipRuleCount = terrainData->TerrainMipMapByZoomValueData.Num();
		snapshot.SegmentCount = terrainData->TerrainSegmentsData.Num();

		LOG_INFO(
			"In-game map probe terrain: object=%s segments=%d pivot=(%.3f,%.3f,%.3f) segment_world_size=(%.3f,%.3f,%.3f) mip_rules=%d far_mip_change=%d near_mip_change=%d",
			snapshot.TerrainDataObjectName.c_str(),
			snapshot.SegmentCount,
			snapshot.TerrainPivotX,
			snapshot.TerrainPivotY,
			snapshot.TerrainPivotZ,
			snapshot.SegmentWorldSizeX,
			snapshot.SegmentWorldSizeY,
			snapshot.SegmentWorldSizeZ,
			snapshot.MipRuleCount,
			terrainData->MapFarToEdgeMipMapChange,
			terrainData->MapNearToEdgeMipMapChange);

		const auto* mipRules = terrainData->TerrainMipMapByZoomValueData.GetDataPtr();
		if (snapshot.MipRuleCount > 0 && mipRules)
		{
			for (int index = 0; index < snapshot.MipRuleCount; ++index)
			{
				LOG_INFO(
					"In-game map probe mip rule: index=%d maximal_zoom=%.4f mip=%d",
					index,
					mipRules[index].MaximalZoomValue,
					mipRules[index].MipMap);
			}
		}

		if (snapshot.SegmentCount <= 0
			|| snapshot.SegmentCount > kMaxReasonableTerrainSegments)
		{
			LOG_WARN(
				"In-game map probe rejected terrain segment count=%d (expected 1..%d)",
				snapshot.SegmentCount,
				kMaxReasonableTerrainSegments);
			return false;
		}

		const auto* segments = terrainData->TerrainSegmentsData.GetDataPtr();
		if (!segments)
		{
			return false;
		}

		SDK::UClass* textureClass = SDK::UTexture2D::StaticClass();
		TextureCandidate normalCandidate{};
		TextureCandidate radiationCandidate{};
		int minX = std::numeric_limits<int>::max();
		int minY = std::numeric_limits<int>::max();
		int maxX = std::numeric_limits<int>::min();
		int maxY = std::numeric_limits<int>::min();

		for (int index = 0; index < snapshot.SegmentCount; ++index)
		{
			const auto& segment = segments[index];
			minX = std::min(minX, segment.TerrainSegmentGridIndex.X);
			minY = std::min(minY, segment.TerrainSegmentGridIndex.Y);
			maxX = std::max(maxX, segment.TerrainSegmentGridIndex.X);
			maxY = std::max(maxY, segment.TerrainSegmentGridIndex.Y);

			const BrushObservation normal = ObserveBrush(segment.TerrainSegmentTexture, textureClass);
			const BrushObservation radiation = ObserveBrush(segment.TerrainSegmentTextureRadiation2, textureClass);
			if (normal.ResourceObject)
			{
				++snapshot.NormalResourceCount;
			}
			if (radiation.ResourceObject)
			{
				++snapshot.RadiationResourceCount;
			}
			if (normal.Texture)
			{
				++snapshot.NormalTextureCount;
				if (!normalCandidate.Texture)
				{
					normalCandidate = {
						normal.Texture,
						segment.TerrainSegmentGridIndex.X,
						segment.TerrainSegmentGridIndex.Y,
						normal.Width,
						normal.Height,
						"normal",
						normal.ResourceClass,
						normal.ResourceName
					};
				}
			}
			if (radiation.Texture)
			{
				++snapshot.RadiationTextureCount;
				if (!radiationCandidate.Texture)
				{
					radiationCandidate = {
						radiation.Texture,
						segment.TerrainSegmentGridIndex.X,
						segment.TerrainSegmentGridIndex.Y,
						radiation.Width,
						radiation.Height,
						"radiation2",
						radiation.ResourceClass,
						radiation.ResourceName
					};
				}
			}

			LOG_INFO(
				"In-game map probe segment: index=%d grid=(%d,%d) normal_brush=(%.1f,%.1f) normal_resource=%p normal_class=%s normal_name=%s normal_texture2d=%d radiation_brush=(%.1f,%.1f) radiation_resource=%p radiation_class=%s radiation_name=%s radiation_texture2d=%d",
				index,
				segment.TerrainSegmentGridIndex.X,
				segment.TerrainSegmentGridIndex.Y,
				normal.Width,
				normal.Height,
				static_cast<void*>(normal.ResourceObject),
				normal.ResourceClass.c_str(),
				normal.ResourceName.c_str(),
				normal.Texture ? 1 : 0,
				radiation.Width,
				radiation.Height,
				static_cast<void*>(radiation.ResourceObject),
				radiation.ResourceClass.c_str(),
				radiation.ResourceName.c_str(),
				radiation.Texture ? 1 : 0);
		}

		snapshot.GridBoundsAvailable = true;
		snapshot.GridMinX = minX;
		snapshot.GridMinY = minY;
		snapshot.GridMaxX = maxX;
		snapshot.GridMaxY = maxY;

		selectedTexture = normalCandidate.Texture ? normalCandidate : radiationCandidate;
		if (selectedTexture.Texture)
		{
			snapshot.SegmentTextureSelected = true;
			snapshot.SelectedGridX = selectedTexture.GridX;
			snapshot.SelectedGridY = selectedTexture.GridY;
			snapshot.SelectedBrushWidth = selectedTexture.BrushWidth;
			snapshot.SelectedBrushHeight = selectedTexture.BrushHeight;
			snapshot.SelectedBrushVariant = selectedTexture.Variant;
			snapshot.SelectedResourceClass = selectedTexture.ResourceClass;
			snapshot.SelectedResourceName = selectedTexture.ResourceName;
		}

		LOG_INFO(
			"In-game map probe terrain summary: grid=(%d,%d)-(%d,%d) normal_resources=%d normal_texture2d=%d radiation_resources=%d radiation_texture2d=%d",
			snapshot.GridMinX,
			snapshot.GridMinY,
			snapshot.GridMaxX,
			snapshot.GridMaxY,
			snapshot.NormalResourceCount,
			snapshot.NormalTextureCount,
			snapshot.RadiationResourceCount,
			snapshot.RadiationTextureCount);
		return true;
	}

	void PublishSnapshot(ProbeSnapshot snapshot, PluginTextureHandle texture)
	{
		PluginTextureHandle replaced = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			replaced = g_sharedState.Texture;
			g_sharedState.Texture = texture;
			g_sharedState.Snapshot = std::move(snapshot);
		}
		QueueTextureFree(replaced);
	}

	ProbeRunResult RunProbeOnGameThread(const std::string& trigger, bool allowAutomaticTextureRetry)
	{
		RetireCurrentTexture(nullptr);

		ProbeSnapshot snapshot{};
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			snapshot.RunNumber = g_sharedState.Snapshot.RunNumber + 1;
			snapshot.LastWorldName = g_sharedState.Snapshot.LastWorldName;
		}
		snapshot.Trigger = trigger.empty() ? "unknown" : trigger;
		snapshot.Status = "Running UObject inventory";

		IPluginHooks* hooks = GetHooks();
		IPluginObjectWalker* walker = hooks ? hooks->ObjectWalker : nullptr;
		if (!walker || !walker->IsReady || !walker->IsReady())
		{
			snapshot.Status = "ObjectWalker is not ready; retrying on the next tick";
			PublishSnapshot(std::move(snapshot), nullptr);
			g_probeRequested.store(true);
			return ProbeRunResult::Complete;
		}
		snapshot.ObjectWalkerReady = true;

		const auto inventoryStarted = std::chrono::steady_clock::now();
		for (size_t index = 0; index < kInventoryTargets.size(); ++index)
		{
			snapshot.Inventory[index].Instances = CountObjects(
				walker,
				kInventoryTargets[index].ClassName,
				PluginObjectLookup_InstanceOnly);
			snapshot.Inventory[index].DefaultObjects = CountObjects(
				walker,
				kInventoryTargets[index].ClassName,
				PluginObjectLookup_CDOOnly);
		}

		LOG_INFO(
			"In-game map probe #%d started: trigger=%s world=%s",
			snapshot.RunNumber,
			snapshot.Trigger.c_str(),
			snapshot.LastWorldName.c_str());
		for (size_t index = 0; index < kInventoryTargets.size(); ++index)
		{
			LOG_INFO(
				"In-game map probe inventory: class=%s instances=%d cdo=%d",
				kInventoryTargets[index].ClassName,
				snapshot.Inventory[index].Instances,
				snapshot.Inventory[index].DefaultObjects);
		}

		InspectWorldPartitionMiniMap(walker, snapshot);

		std::array<PluginObjectInfo, kMaxTerrainObjects> terrainObjects{};
		int capturedTerrainObjects = 0;
		const bool isChimeraMain = std::strstr(snapshot.LastWorldName.c_str(), "ChimeraMain") != nullptr;
		SDK::UCrMapMenuTerrainData* terrainData = FindTerrainData(
			walker,
			snapshot,
			terrainObjects,
			capturedTerrainObjects,
			isChimeraMain);
		(void)capturedTerrainObjects;

		TextureCandidate selectedTexture{};
		const bool terrainInspected = InspectTerrainData(terrainData, snapshot, selectedTexture);
		const auto inventoryFinished = std::chrono::steady_clock::now();
		snapshot.InventoryMilliseconds = std::chrono::duration<double, std::milli>(
			inventoryFinished - inventoryStarted).count();

		if (!terrainData)
		{
			snapshot.Status = snapshot.ConfiguredTerrainLoadAttempted
				? "The configured TerrainData soft reference could not be loaded"
				: "No loaded CrMapMenuTerrainData; enter ChimeraMain or open the native map and retry";
			PublishSnapshot(std::move(snapshot), nullptr);
			return ProbeRunResult::Complete;
		}
		if (!terrainInspected)
		{
			snapshot.Status = "Terrain data was found but its segment array was unavailable or invalid";
			PublishSnapshot(std::move(snapshot), nullptr);
			return ProbeRunResult::Complete;
		}
		if (!selectedTexture.Texture)
		{
			snapshot.Status = "No terrain brush ResourceObject is a UTexture2D; open the native map and retry";
			PublishSnapshot(std::move(snapshot), nullptr);
			return ProbeRunResult::Complete;
		}

		IPluginImGuiTextures* textures = hooks->ImGuiTextures;
		if (!textures || !textures->LoadFromUTexture2D)
		{
			snapshot.Status = "The ModLoader UTexture2D-to-ImGui bridge is unavailable";
			PublishSnapshot(std::move(snapshot), nullptr);
			return ProbeRunResult::Complete;
		}

		if (textures->GetCapacity)
		{
			snapshot.TextureSlotCapacity = textures->GetCapacity();
		}
		if (textures->GetFreeSlotCount)
		{
			snapshot.TextureSlotsFreeBeforeLoad = textures->GetFreeSlotCount();
			if (snapshot.TextureSlotsFreeBeforeLoad <= 0)
			{
				snapshot.Status = "No free ModLoader texture slot";
				PublishSnapshot(std::move(snapshot), nullptr);
				return ProbeRunResult::Complete;
			}
		}

		char textureName[96]{};
		std::snprintf(
			textureName,
			sizeof(textureName),
			"MapExtension.NativeMapProbe.%d",
			++g_textureGeneration);

		PluginTextureHandle loadedTexture = nullptr;
		const auto loadStarted = std::chrono::steady_clock::now();
		try
		{
			loadedTexture = textures->LoadFromUTexture2D(selectedTexture.Texture, textureName);
		}
		catch (const std::out_of_range&)
		{
			snapshot.Status = "ModLoader texture capacity was exhausted during the copy";
		}
		catch (const std::exception& exception)
		{
			snapshot.Status = std::string("Texture copy failed: ") + exception.what();
		}
		catch (...)
		{
			snapshot.Status = "Texture copy failed with an unknown error";
		}
		const auto loadFinished = std::chrono::steady_clock::now();
		snapshot.TextureLoadMilliseconds = std::chrono::duration<double, std::milli>(
			loadFinished - loadStarted).count();

		if (!loadedTexture)
		{
			const bool textureResourceNotReady = snapshot.Status == "Running UObject inventory";
			if (textureResourceNotReady)
			{
				snapshot.Status = allowAutomaticTextureRetry
					? "UTexture2D found but its GPU resource is still streaming; retrying automatically"
					: "UTexture2D found but its GPU resource is still not ready; run the probe again";
			}
			LOG_WARN(
				"In-game map probe texture copy failed: source=%s class=%s load_ms=%.3f",
				snapshot.SelectedResourceName.c_str(),
				snapshot.SelectedResourceClass.c_str(),
				snapshot.TextureLoadMilliseconds);
			PublishSnapshot(std::move(snapshot), nullptr);
			return textureResourceNotReady
				? ProbeRunResult::TextureNotReady
				: ProbeRunResult::Complete;
		}

		if (textures->GetSize)
		{
			textures->GetSize(loadedTexture, &snapshot.TextureWidth, &snapshot.TextureHeight);
		}
		if (snapshot.TextureWidth > 0 && snapshot.TextureHeight > 0)
		{
			const double bytes = static_cast<double>(snapshot.TextureWidth)
				* static_cast<double>(snapshot.TextureHeight)
				* 4.0;
			snapshot.EstimatedRgbaMiB = bytes / (1024.0 * 1024.0);
		}
		snapshot.TextureLoaded = true;
		snapshot.Status = "Success: one native terrain segment was copied and is rendered below";

		LOG_INFO(
			"In-game map probe texture copy succeeded: source=%s class=%s variant=%s grid=(%d,%d) brush=(%.1f,%.1f) texture=%dx%d load_ms=%.3f estimated_rgba_mib=%.3f slots_before=%d capacity=%d",
			snapshot.SelectedResourceName.c_str(),
			snapshot.SelectedResourceClass.c_str(),
			snapshot.SelectedBrushVariant.c_str(),
			snapshot.SelectedGridX,
			snapshot.SelectedGridY,
			snapshot.SelectedBrushWidth,
			snapshot.SelectedBrushHeight,
			snapshot.TextureWidth,
			snapshot.TextureHeight,
			snapshot.TextureLoadMilliseconds,
			snapshot.EstimatedRgbaMiB,
			snapshot.TextureSlotsFreeBeforeLoad,
			snapshot.TextureSlotCapacity);

		PublishSnapshot(std::move(snapshot), loadedTexture);
		return ProbeRunResult::Complete;
	}

	void RenderInventory(IModLoaderImGui* imgui, const ProbeSnapshot& snapshot)
	{
		char line[256]{};
		imgui->SeparatorText("Runtime UObject inventory");
		for (size_t index = 0; index < kInventoryTargets.size(); ++index)
		{
			std::snprintf(
				line,
				sizeof(line),
				"%s: instances=%d, CDO=%d",
				kInventoryTargets[index].ClassName,
				snapshot.Inventory[index].Instances,
				snapshot.Inventory[index].DefaultObjects);
			imgui->Text(line);
		}
	}

	void RenderTerrainDiagnostics(IModLoaderImGui* imgui, const ProbeSnapshot& snapshot)
	{
		char line[512]{};
		imgui->SeparatorText("Terrain diagnostics");
		std::snprintf(
			line,
			sizeof(line),
			"Settings CDO: %s | TerrainData already resolved: %s | loaded instances before fallback: %d%s",
			snapshot.SettingsAvailable ? "yes" : "no",
			snapshot.ConfiguredTerrainResolved ? "yes" : "no",
			snapshot.TerrainDataInstances,
			snapshot.TerrainDataResultsTruncated ? " (results truncated)" : "");
		imgui->Text(line);
		std::snprintf(
			line,
			sizeof(line),
			"Configured soft-reference blocking load: attempted=%s, succeeded=%s, %.3f ms",
			snapshot.ConfiguredTerrainLoadAttempted ? "yes" : "no",
			snapshot.ConfiguredTerrainLoadedBlocking ? "yes" : "no",
			snapshot.ConfiguredTerrainLoadMilliseconds);
		imgui->Text(line);

		if (snapshot.SettingsAvailable)
		{
			std::snprintf(
				line,
				sizeof(line),
				"MapAreaPivotPoint: (%.3f, %.3f, %.3f)",
				snapshot.MapAreaPivotX,
				snapshot.MapAreaPivotY,
				snapshot.MapAreaPivotZ);
			imgui->Text(line);
		}

		if (snapshot.TerrainDataObjectName.empty())
		{
			imgui->TextDisabled("No terrain data asset is currently loaded.");
			return;
		}

		std::snprintf(line, sizeof(line), "Terrain object: %s", snapshot.TerrainDataObjectName.c_str());
		imgui->Text(line);
		std::snprintf(
			line,
			sizeof(line),
			"Top-left pivot: (%.3f, %.3f, %.3f) | segment world size: (%.3f, %.3f, %.3f)",
			snapshot.TerrainPivotX,
			snapshot.TerrainPivotY,
			snapshot.TerrainPivotZ,
			snapshot.SegmentWorldSizeX,
			snapshot.SegmentWorldSizeY,
			snapshot.SegmentWorldSizeZ);
		imgui->Text(line);
		std::snprintf(
			line,
			sizeof(line),
			"Segments: %d | mip rules: %d | normal resources/Texture2D: %d/%d | radiation resources/Texture2D: %d/%d",
			snapshot.SegmentCount,
			snapshot.MipRuleCount,
			snapshot.NormalResourceCount,
			snapshot.NormalTextureCount,
			snapshot.RadiationResourceCount,
			snapshot.RadiationTextureCount);
		imgui->Text(line);
		if (snapshot.GridBoundsAvailable)
		{
			std::snprintf(
				line,
				sizeof(line),
				"Observed grid bounds: (%d, %d) to (%d, %d)",
				snapshot.GridMinX,
				snapshot.GridMinY,
				snapshot.GridMaxX,
				snapshot.GridMaxY);
			imgui->Text(line);
		}
		if (snapshot.SegmentTextureSelected)
		{
			std::snprintf(
				line,
				sizeof(line),
				"Selected %s segment (%d, %d): %s [%s], brush %.1f x %.1f",
				snapshot.SelectedBrushVariant.c_str(),
				snapshot.SelectedGridX,
				snapshot.SelectedGridY,
				snapshot.SelectedResourceName.c_str(),
				snapshot.SelectedResourceClass.c_str(),
				snapshot.SelectedBrushWidth,
				snapshot.SelectedBrushHeight);
			imgui->Text(line);
		}
	}

	void RenderWorldPartitionDiagnostics(IModLoaderImGui* imgui, const ProbeSnapshot& snapshot)
	{
		char line[384]{};
		imgui->SeparatorText("World Partition MiniMap (alternate source)");
		if (!snapshot.WorldPartitionMiniMapFound)
		{
			imgui->TextDisabled("No live WorldPartitionMiniMap instance found.");
			return;
		}

		std::snprintf(
			line,
			sizeof(line),
			"Bounds valid: %s | XY: (%.3f, %.3f) to (%.3f, %.3f) | units/pixel: %d | texture: %s",
			snapshot.WorldPartitionBoundsValid ? "yes" : "no",
			snapshot.WorldPartitionMinX,
			snapshot.WorldPartitionMinY,
			snapshot.WorldPartitionMaxX,
			snapshot.WorldPartitionMaxY,
			snapshot.WorldUnitsPerPixel,
			snapshot.WorldPartitionTextureName.c_str());
		imgui->TextWrapped(line);
	}

	void RenderPanel(IModLoaderImGui* imgui)
	{
		if (!imgui)
		{
			return;
		}

		const auto renderStarted = std::chrono::steady_clock::now();
		ProbeSnapshot snapshot{};
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			snapshot = g_sharedState.Snapshot;
		}

		imgui->TextWrapped(
			"Experimental client-only probe. It observes native map assets and copies exactly one terrain segment; it does not replace the browser viewer.");
		if (imgui->Button("Run inventory + one-segment probe"))
		{
			RequestProbe("manual panel request");
		}
		imgui->SameLine(0.0f, 8.0f);
		imgui->TextDisabled("Run once before and once after opening the native map.");

		char line[512]{};
		std::snprintf(
			line,
			sizeof(line),
			"Run #%d | trigger: %s | world: %s",
			snapshot.RunNumber,
			snapshot.Trigger.c_str(),
			snapshot.LastWorldName.c_str());
		imgui->Text(line);
		imgui->TextWrapped(snapshot.Status.c_str());
		std::snprintf(
			line,
			sizeof(line),
			"Inventory/inspection: %.3f ms | texture copy: %.3f ms | previous render callback: %.3f ms",
			snapshot.InventoryMilliseconds,
			snapshot.TextureLoadMilliseconds,
			static_cast<double>(g_lastRenderMicroseconds.load()) / 1000.0);
		imgui->Text(line);

		RenderInventory(imgui, snapshot);
		RenderTerrainDiagnostics(imgui, snapshot);
		RenderWorldPartitionDiagnostics(imgui, snapshot);

		imgui->SeparatorText("Native segment preview");
		std::snprintf(
			line,
			sizeof(line),
			"GPU copy: %dx%d | estimated RGBA base level: %.3f MiB | free slots before load: %d/%d",
			snapshot.TextureWidth,
			snapshot.TextureHeight,
			snapshot.EstimatedRgbaMiB,
			snapshot.TextureSlotsFreeBeforeLoad,
			snapshot.TextureSlotCapacity);
		imgui->Text(line);

		IPluginHooks* hooks = GetHooks();
		IPluginImGuiTextures* textures = hooks ? hooks->ImGuiTextures : nullptr;
		if (textures && textures->Image)
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			if (g_sharedState.Texture)
			{
				float availableWidth = kMaxPreviewDimension;
				float ignoredHeight = 0.0f;
				if (imgui->GetContentRegionAvail)
				{
					imgui->GetContentRegionAvail(&availableWidth, &ignoredHeight);
				}
				availableWidth = std::clamp(availableWidth, 64.0f, kMaxPreviewDimension);
				const int textureWidth = std::max(g_sharedState.Snapshot.TextureWidth, 1);
				const int textureHeight = std::max(g_sharedState.Snapshot.TextureHeight, 1);
				const float aspect = static_cast<float>(textureHeight) / static_cast<float>(textureWidth);
				textures->Image(g_sharedState.Texture, availableWidth, availableWidth * aspect);
			}
			else
			{
				imgui->TextDisabled("No copied segment is available yet.");
			}
		}
		else
		{
			imgui->TextDisabled("The ModLoader image renderer is unavailable.");
		}

		const auto renderFinished = std::chrono::steady_clock::now();
		g_lastRenderMicroseconds.store(
			std::chrono::duration_cast<std::chrono::microseconds>(renderFinished - renderStarted).count());
	}
}

	bool Initialize()
	{
		if (!MapExtensionPluginConfig::Config::InGameMapProbeEnabled())
		{
			return true;
		}
		if (g_active.load())
		{
			return true;
		}

		IPluginHooks* hooks = GetHooks();
		if (!hooks)
		{
			LOG_WARN("Cannot initialize the in-game map probe: hooks interface is null");
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			g_sharedState = {};
			g_sharedState.Snapshot.Status = "Waiting for EngineInit";
			g_pendingTrigger.clear();
		}
		g_pendingTextureFrees.clear();
		g_textureGeneration = 0;
		g_textureRetryScheduled = false;
		g_textureRetryDelaySecondsRemaining = 0.0f;
		g_lastRenderMicroseconds.store(0);
		g_active.store(true);

		if (hooks->Engine && hooks->Engine->RegisterOnInit)
		{
			hooks->Engine->RegisterOnInit(OnEngineInit);
			g_engineInitCallbackRegistered = true;
		}
		if (hooks->Engine && hooks->Engine->RegisterOnShutdown)
		{
			hooks->Engine->RegisterOnShutdown(OnEngineShutdown);
			g_engineShutdownCallbackRegistered = true;
		}
		if (hooks->Engine && hooks->Engine->RegisterOnTick)
		{
			hooks->Engine->RegisterOnTick(OnEngineTick);
			g_engineTickCallbackRegistered = true;
		}
		if (hooks->World && hooks->World->RegisterOnAnyWorldBeginPlay)
		{
			hooks->World->RegisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
			g_worldBeginCallbackRegistered = true;
		}
		if (hooks->World && hooks->World->RegisterOnBeforeWorldEndPlay)
		{
			hooks->World->RegisterOnBeforeWorldEndPlay(OnBeforeWorldEndPlay);
			g_worldEndCallbackRegistered = true;
		}

		if (hooks->UI && hooks->UI->RegisterPanel)
		{
			g_panelHandle = hooks->UI->RegisterPanel(&g_panelDesc);
		}
		if (!g_panelHandle)
		{
			LOG_WARN("In-game map probe UI panel registration failed; log-only probing remains active");
		}
		if (!g_engineTickCallbackRegistered)
		{
			LOG_WARN("In-game map probe EngineTick callback is unavailable; queued probes cannot run");
		}
		if (!hooks->ObjectWalker)
		{
			LOG_WARN("In-game map probe ObjectWalker interface is unavailable");
		}
		if (!hooks->ImGuiTextures)
		{
			LOG_WARN("In-game map probe texture interface is unavailable");
		}

		LOG_INFO("Experimental in-game map probe initialized (client-only, one terrain segment)");
		RequestProbe("plugin initialization");
		return true;
	}

	void Shutdown()
	{
		const bool wasActive = g_active.exchange(false);
		g_probeRequested.store(false);

		IPluginHooks* hooks = GetHooks();
		if (hooks && hooks->Engine)
		{
			if (g_engineInitCallbackRegistered && hooks->Engine->UnregisterOnInit)
			{
				hooks->Engine->UnregisterOnInit(OnEngineInit);
			}
			if (g_engineShutdownCallbackRegistered && hooks->Engine->UnregisterOnShutdown)
			{
				hooks->Engine->UnregisterOnShutdown(OnEngineShutdown);
			}
			if (g_engineTickCallbackRegistered && hooks->Engine->UnregisterOnTick)
			{
				hooks->Engine->UnregisterOnTick(OnEngineTick);
			}
		}
		if (hooks && hooks->World)
		{
			if (g_worldBeginCallbackRegistered && hooks->World->UnregisterOnAnyWorldBeginPlay)
			{
				hooks->World->UnregisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
			}
			if (g_worldEndCallbackRegistered && hooks->World->UnregisterOnBeforeWorldEndPlay)
			{
				hooks->World->UnregisterOnBeforeWorldEndPlay(OnBeforeWorldEndPlay);
			}
		}
		g_engineInitCallbackRegistered = false;
		g_engineShutdownCallbackRegistered = false;
		g_engineTickCallbackRegistered = false;
		g_worldBeginCallbackRegistered = false;
		g_worldEndCallbackRegistered = false;

		if (g_panelHandle && hooks && hooks->UI && hooks->UI->UnregisterPanel)
		{
			hooks->UI->UnregisterPanel(g_panelHandle);
		}
		g_panelHandle = nullptr;

		std::vector<PluginTextureHandle> texturesToFree;
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			if (g_sharedState.Texture)
			{
				texturesToFree.push_back(g_sharedState.Texture);
				g_sharedState.Texture = nullptr;
			}
			g_sharedState.Snapshot.TextureLoaded = false;
			g_sharedState.Snapshot.Status = "Probe shut down";
			g_pendingTrigger.clear();
		}
		for (const PendingTextureFree& pending : g_pendingTextureFrees)
		{
			if (pending.Handle)
			{
				texturesToFree.push_back(pending.Handle);
			}
		}
		g_pendingTextureFrees.clear();
		g_textureRetryScheduled = false;
		g_textureRetryDelaySecondsRemaining = 0.0f;

		IPluginImGuiTextures* textures = hooks ? hooks->ImGuiTextures : nullptr;
		if (textures && textures->FreeTexture)
		{
			for (PluginTextureHandle handle : texturesToFree)
			{
				textures->FreeTexture(handle);
			}
		}
		if (wasActive)
		{
			LOG_INFO("Experimental in-game map probe shut down");
		}
	}

	void OnEngineInit()
	{
		RequestProbe("EngineInit");
	}

	void OnEngineShutdown()
	{
		if (!g_active.load())
		{
			return;
		}
		g_textureRetryScheduled = false;
		g_textureRetryDelaySecondsRemaining = 0.0f;
		RetireCurrentTexture("Engine shutdown observed; waiting for a new world");
	}

	void OnEngineTick(float deltaSeconds)
	{
		if (!g_active.load())
		{
			return;
		}

		ProcessPendingTextureFrees();
		if (g_probeRequested.exchange(false))
		{
			g_textureRetryScheduled = false;
			g_textureRetryDelaySecondsRemaining = 0.0f;

			std::string trigger;
			{
				std::lock_guard<std::mutex> lock(g_stateMutex);
				trigger = g_pendingTrigger;
			}
			if (RunProbeOnGameThread(trigger, true) == ProbeRunResult::TextureNotReady)
			{
				g_textureRetryScheduled = true;
				g_textureRetryDelaySecondsRemaining = kTextureRetryDelaySeconds;
			}
			return;
		}

		if (!g_textureRetryScheduled)
		{
			return;
		}

		g_textureRetryDelaySecondsRemaining -= std::max(deltaSeconds, 0.0f);
		if (g_textureRetryDelaySecondsRemaining > 0.0f)
		{
			return;
		}

		g_textureRetryScheduled = false;
		g_textureRetryDelaySecondsRemaining = 0.0f;
		RunProbeOnGameThread("automatic GPU readiness retry", false);
	}

	void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName)
	{
		(void)world;
		if (!g_active.load())
		{
			return;
		}

		const char* safeName = worldName ? worldName : "(null)";
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			g_sharedState.Snapshot.LastWorldName = safeName;
		}
		if (std::strstr(safeName, "ChimeraMain") != nullptr)
		{
			std::string trigger = "WorldBeginPlay: ";
			trigger += safeName;
			RequestProbe(trigger.c_str());
		}
	}

	void OnBeforeWorldEndPlay(SDK::UWorld* world, const char* worldName)
	{
		(void)world;
		if (!g_active.load())
		{
			return;
		}

		const char* safeName = worldName ? worldName : "(null)";
		if (std::strstr(safeName, "ChimeraMain") != nullptr)
		{
			g_probeRequested.store(false);
			g_textureRetryScheduled = false;
			g_textureRetryDelaySecondsRemaining = 0.0f;
			RetireCurrentTexture("ChimeraMain is ending; native texture preview retired");
		}
	}
}
