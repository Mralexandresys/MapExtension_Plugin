#include "ingame_map_ui.h"

#include "ingame_map_catalog.h"
#include "ingame_map_terrain_source.h"

#include "../map_state_capture.h"
#include "../map_state_sdk_helpers.h"
#include "../plugin_config.h"
#include "../plugin_helpers.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if !defined(MODLOADER_CLIENT_BUILD)
#error "La carte in-game est client-only"
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace MapExtensionClient::InGameMap
{
namespace
{
	using MapStateRuntime::Detail::CargoKind;
	using MapStateRuntime::Detail::CargoSnapshot;
	using MapStateRuntime::Detail::PoiKind;

	// Orientation prouvee en jeu (journal, 2026-11-14) : index.X de la grille suit
	// Y monde, index.Y suit X monde, textures non transposees.
	constexpr bool kSwapAxes = true;

	// Etat immuable publie par le game thread ; le render callback ne lit que ca.
	struct Published
	{
		TerrainDescriptor Terrain;
		std::vector<PluginTextureHandle> Tiles;  // aligne sur Terrain.Segments, nullptr = pas de texture
		int HiResCount = 0;
		CargoSnapshot Snapshot;
		std::shared_ptr<const Catalog> StaticCatalog;  // nullptr tant que le chargement n'est pas fini
		std::vector<PluginTextureHandle> CatalogIcons;  // aligne sur StaticCatalog->Groups, nullptr = pas d'icone
	};

	constexpr float kServiceIntervalSeconds = 0.5f;
	constexpr int64_t kOpenGraceMs = 1000;

	std::mutex g_publishedMutex;
	std::shared_ptr<const Published> g_published;
	std::atomic<int64_t> g_lastRenderUnixMs{ 0 };  // "fenetre ouverte" = rendue recemment
	PanelHandle g_panel = nullptr;
	std::string g_toggleKey;
	float g_serviceAccumulator = kServiceIntervalSeconds;
	bool g_terrainLogged = false;
	int g_lastRadiationLevel = -2;

	// --- Tuiles : game thread uniquement (sauf Disable, cf. ponytail plus bas) ---
	//
	// Apercu permanent : mip resident de chaque segment (64 px mesure en jeu).
	// Haute resolution (piste A du plan, retenue) : SetForceMipLevelsToBeResident
	// sur les tuiles demandees par la vue, puis recopie quand le streaming a suivi
	// (2048 px, copie mesuree a 2.6 ms).
	// La carte native affiche les brushes "Radiation2" (constat en jeu, niveau de
	// radiation 1) : c'est la seule variante chargee.
	constexpr bool kUseR2 = true;
	// L'apercu est recopie plusieurs fois apres l'arrivee en monde : la premiere
	// copie, prise juste apres le chargement des textures, sortait incorrecte en
	// jeu (il fallait recharger a la main). Secondes depuis le debut de la sequence.
	constexpr float kPreviewLoadTimes[] = { 1.0f, 5.0f, 15.0f };
	constexpr float kForceMipSeconds = 20.0f;
	constexpr float kHiResFirstCopyDelaySeconds = 1.5f;
	constexpr float kHiResTimeoutSeconds = 15.0f;
	constexpr float kHiResEvictSeconds = 10.0f;
	constexpr int kHiResMinWidth = 1024;
	constexpr int kHiResMaxCopiesPerService = 3;  // borne le cout par tick (~3 ms par copie)
	// Budget : tuile 2048x2048 en BC1 avec mips ~ 2.7 MiB (format non mesurable d'ici).
	constexpr float kHiResTileMiB = 2.7f;
	constexpr float kHiResMinTilePixels = 200.0f;  // en dessous l'apercu suffit

	struct TileState
	{
		PluginTextureHandle Preview = nullptr;
		PluginTextureHandle HiRes = nullptr;
		int PreviewWidth = 0;
		float PendingSeconds = -1.0f;  // < 0 : pas de demande en cours
		float UnwantedSeconds = 0.0f;
		bool Failed = false;
	};

	std::vector<TileState> g_tiles;  // aligne sur Terrain.Segments
	float g_previewClock = 0.0f;
	size_t g_previewLoadsDone = 0;
	int g_tileSerial = 0;
	std::atomic<bool> g_reloadTerrainRequested{ false };  // case "Terrain" recochee

	// Catalogue statique : charge en tache de fond, publie par le game thread.
	std::future<std::shared_ptr<const Catalog>> g_catalogFuture;
	std::shared_ptr<const Catalog> g_catalog;
	// Icones d'items des types du catalogue : game thread, quelques-unes par passe.
	constexpr size_t kIconLoadsPerService = 3;
	std::vector<PluginTextureHandle> g_catalogIcons;
	size_t g_catalogIconsNext = 0;
	std::atomic<uint64_t> g_wantedHiRes{ 0 };  // bit k = segment k demande en haute resolution
	std::atomic<int> g_hiResBudget{ 23 };

	// --- Etat de vue : render callback uniquement ---
	bool g_fitRequested = true;
	bool g_centerOnSelfRequested = false;
	bool g_followSelf = false;
	double g_viewCenterX = 0.0;
	double g_viewCenterY = 0.0;
	float g_pixelsPerUnit = 0.001f;
	bool g_showTerrain = true;
	bool g_showCargo = true;
	bool g_showConnections = true;
	bool g_showTeleporters = true;
	bool g_showPlayers = true;
	bool g_showPois = false;
	bool g_showCatalog = true;
	bool g_showFilters = true;
	std::vector<char> g_catalogEnabled;  // aligne sur Catalog::Groups
	bool g_purityEnabled[4] = { true, true, true, true };  // inconnu, impur, normal, pur

	constexpr unsigned int kColorSender = 0xFF2090FFu;
	constexpr unsigned int kColorReceiver = 0xFFFFB040u;
	constexpr unsigned int kColorLink = 0xC000D7FFu;
	constexpr unsigned int kColorTeleporter = 0xFFFF60C0u;
	constexpr unsigned int kColorPlayer = 0xFF60E060u;
	constexpr unsigned int kColorSelf = 0xFFFFFFFFu;
	constexpr unsigned int kColorPoi = 0xFF40D0D0u;

	bool IsOpen()
	{
		return MapStateSdk::GetCurrentUnixTimeMilliseconds() - g_lastRenderUnixMs.load() < kOpenGraceMs;
	}

	IPluginImGuiTextures* Textures()
	{
		IPluginHooks* hooks = GetHooks();
		return hooks ? hooks->ImGuiTextures : nullptr;
	}

	double MillisecondsSince(std::chrono::steady_clock::time_point started)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	}

	void SegmentRangeWorldRect(const TerrainDescriptor& terrain, int minX, int minY, int maxX, int maxY, float& x0, float& y0, float& x1, float& y1)
	{
		float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
		SegmentToWorld(terrain.Grid, static_cast<float>(minX), static_cast<float>(minY), kSwapAxes, ax, ay);
		SegmentToWorld(terrain.Grid, static_cast<float>(maxX + 1), static_cast<float>(maxY + 1), kSwapAxes, bx, by);
		x0 = std::min(ax, bx);
		x1 = std::max(ax, bx);
		y0 = std::min(ay, by);
		y1 = std::max(ay, by);
	}

	void FreeHiResTiles()
	{
		IPluginImGuiTextures* textures = Textures();
		for (TileState& tile : g_tiles)
		{
			if (tile.HiRes && textures)
			{
				textures->FreeTexture(tile.HiRes);
			}
			tile.HiRes = nullptr;
			tile.PendingSeconds = -1.0f;
			tile.Failed = false;
		}
	}

	void FreeTiles()
	{
		FreeHiResTiles();
		IPluginImGuiTextures* textures = Textures();
		for (const TileState& tile : g_tiles)
		{
			if (tile.Preview && textures)
			{
				textures->FreeTexture(tile.Preview);
			}
		}
		g_tiles.clear();
	}

	void FreeCatalogIcons()
	{
		IPluginImGuiTextures* textures = Textures();
		for (PluginTextureHandle icon : g_catalogIcons)
		{
			if (icon && textures)
			{
				textures->FreeTexture(icon);
			}
		}
		g_catalogIcons.clear();
		g_catalogIconsNext = 0;
	}

	// Charge les icones d'items du catalogue, quelques-unes par passe (chaque
	// chargement de classe peut bloquer quelques ms). Un seul essai par type.
	void ServiceCatalogIcons()
	{
		if (!g_catalog || !g_catalog->Error.empty() || g_catalogIconsNext >= g_catalog->Groups.size())
		{
			return;
		}
		g_catalogIcons.resize(g_catalog->Groups.size());
		for (size_t loads = 0; loads < kIconLoadsPerService && g_catalogIconsNext < g_catalog->Groups.size(); ++g_catalogIconsNext)
		{
			const CatalogGroup& group = g_catalog->Groups[g_catalogIconsNext];
			if (group.ItemClassPath.empty())
			{
				continue;
			}
			++loads;
			const std::string name = "MapExtension_item_" + group.ItemClassPath;
			g_catalogIcons[g_catalogIconsNext] = LoadItemIcon(group.ItemClassPath.c_str(), name.c_str());
			if (!g_catalogIcons[g_catalogIconsNext])
			{
				LOG_WARN("[InGameMap] no icon for %s (%s)", group.Label.c_str(), group.ItemClassPath.c_str());
			}
		}
		if (g_catalogIconsNext >= g_catalog->Groups.size())
		{
			LOG_INFO("[InGameMap] item icons loaded: %d",
				static_cast<int>(std::count_if(g_catalogIcons.begin(), g_catalogIcons.end(), [](PluginTextureHandle icon) { return icon != nullptr; })));
		}
	}

	// Nom unique a chaque copie : le loader partage les textures par nom, et une
	// texture en cours de liberation ne doit jamais etre reutilisee.
	std::string NextTileName()
	{
		return "MapExtension_tile_" + std::to_string(++g_tileSerial);
	}

	// Apercu : mip resident de chaque segment, charge d'un bloc (~20-40 ms mesures
	// pour 36). Les tuiles haute resolution deja copiees sont conservees.
	void LoadPreviewTiles(const TerrainDescriptor& terrain)
	{
		IPluginImGuiTextures* textures = Textures();
		if (!textures)
		{
			return;
		}
		if (g_tiles.size() != terrain.Segments.size())
		{
			FreeTiles();
			g_tiles.resize(terrain.Segments.size());
		}
		const auto started = std::chrono::steady_clock::now();
		int loaded = 0;
		for (size_t index = 0; index < terrain.Segments.size(); ++index)
		{
			const TerrainSegment& segment = terrain.Segments[index];
			TileState& tile = g_tiles[index];
			PluginTextureHandle handle = LoadSegmentTexture(segment.GridX, segment.GridY, kUseR2, NextTileName().c_str());
			if (!handle)
			{
				continue;
			}
			textures->FreeTexture(tile.Preview);
			tile.Preview = handle;
			textures->GetSize(handle, &tile.PreviewWidth, nullptr);
			++loaded;
		}
		LOG_INFO("[InGameMap] preview tiles: loaded=%d/%d total_ms=%.1f free_slots=%d/%d",
			loaded, static_cast<int>(terrain.Segments.size()),
			MillisecondsSince(started), textures->GetFreeSlotCount(), textures->GetCapacity());
	}

	// Haute resolution a la demande : force les mips des tuiles voulues par la vue,
	// recopie quand ils sont residents, libere celles qui ne sont plus regardees.
	void ServiceHiResTiles(const TerrainDescriptor& terrain)
	{
		IPluginImGuiTextures* textures = Textures();
		if (!textures)
		{
			return;
		}
		const uint64_t wanted = g_wantedHiRes.load();
		const size_t count = std::min<size_t>({ g_tiles.size(), terrain.Segments.size(), 64 });
		int copies = 0;
		for (size_t index = 0; index < count; ++index)
		{
			TileState& tile = g_tiles[index];
			const TerrainSegment& segment = terrain.Segments[index];
			if (((wanted >> index) & 1u) == 0)
			{
				tile.Failed = false;
				tile.PendingSeconds = -1.0f;
				if (tile.HiRes && (tile.UnwantedSeconds += kServiceIntervalSeconds) >= kHiResEvictSeconds)
				{
					textures->FreeTexture(tile.HiRes);
					tile.HiRes = nullptr;
				}
				continue;
			}
			tile.UnwantedSeconds = 0.0f;
			if (tile.HiRes || tile.Failed || tile.PreviewWidth >= kHiResMinWidth)
			{
				continue;
			}
			if (tile.PendingSeconds < 0.0f)
			{
				tile.Failed = !RequestSegmentMips(segment.GridX, segment.GridY, kUseR2, kForceMipSeconds);
				tile.PendingSeconds = tile.Failed ? -1.0f : 0.0f;
				continue;
			}
			tile.PendingSeconds += kServiceIntervalSeconds;
			if (tile.PendingSeconds < kHiResFirstCopyDelaySeconds || copies >= kHiResMaxCopiesPerService)
			{
				continue;
			}
			++copies;
			PluginTextureHandle handle = LoadSegmentTexture(segment.GridX, segment.GridY, kUseR2, NextTileName().c_str());
			int width = 0;
			if (handle)
			{
				textures->GetSize(handle, &width, nullptr);
			}
			if (width >= kHiResMinWidth)
			{
				tile.HiRes = handle;
				tile.PendingSeconds = -1.0f;
				LOG_DEBUG("[InGameMap] hi-res tile (%d,%d) ready: %d px", segment.GridX, segment.GridY, width);
				continue;
			}
			textures->FreeTexture(handle);
			if (tile.PendingSeconds >= kHiResTimeoutSeconds)
			{
				tile.Failed = true;
				tile.PendingSeconds = -1.0f;
				LOG_WARN("[InGameMap] hi-res tile (%d,%d) did not stream in within %.0f s (last width %d px)",
					segment.GridX, segment.GridY, kHiResTimeoutSeconds, width);
			}
		}
	}

	// Game thread : capture terrain + snapshot, publication pour le render callback.
	void Service()
	{
		auto next = std::make_shared<Published>();
		next->Snapshot = MapStateRuntime::Detail::CopySnapshot();
		if (MapStateSdk::IsChimeraWorldName(next->Snapshot.WorldName))
		{
			CaptureTerrain(next->Terrain, !g_terrainLogged);
			if (next->Terrain.Valid)
			{
				g_terrainLogged = true;
				if (next->Terrain.RadiationLevel != g_lastRadiationLevel)
				{
					LOG_INFO("[InGameMap] radiation level %d -> %d", g_lastRadiationLevel, next->Terrain.RadiationLevel);
					g_lastRadiationLevel = next->Terrain.RadiationLevel;
				}

				if (g_reloadTerrainRequested.exchange(false))
				{
					g_previewClock = kPreviewLoadTimes[0];
					g_previewLoadsDone = 0;
				}
				g_previewClock += kServiceIntervalSeconds;
				if (g_previewLoadsDone < std::size(kPreviewLoadTimes) && g_previewClock >= kPreviewLoadTimes[g_previewLoadsDone])
				{
					++g_previewLoadsDone;
					LoadPreviewTiles(next->Terrain);
				}
				ServiceHiResTiles(next->Terrain);
				ServiceCatalogIcons();

				next->Tiles.reserve(g_tiles.size());
				for (const TileState& tile : g_tiles)
				{
					next->Tiles.push_back(tile.HiRes ? tile.HiRes : tile.Preview);
					next->HiResCount += (tile.HiRes || tile.PreviewWidth >= kHiResMinWidth) ? 1 : 0;
				}
			}
		}
		if (g_catalogFuture.valid() && g_catalogFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			g_catalog = g_catalogFuture.get();
			if (g_catalog->Error.empty())
			{
				LOG_INFO("[InGameMap] static catalog loaded: %d points in %d groups",
					static_cast<int>(g_catalog->PointCount), static_cast<int>(g_catalog->Groups.size()));
			}
			else
			{
				LOG_WARN("[InGameMap] static catalog not loaded (%s); expected folder: Plugins\\MapExtension\\map-data", g_catalog->Error.c_str());
			}
		}
		next->StaticCatalog = g_catalog;
		next->CatalogIcons = g_catalogIcons;
		std::lock_guard<std::mutex> lock(g_publishedMutex);
		g_published = std::move(next);
	}

	void OnTick(float deltaSeconds)
	{
		g_serviceAccumulator += deltaSeconds;
		if (g_serviceAccumulator < kServiceIntervalSeconds)
		{
			return;
		}
		g_serviceAccumulator = 0.0f;
		if (!IsOpen())
		{
			// Fenetre fermee : on ne garde que l'apercu.
			g_wantedHiRes = 0;
			FreeHiResTiles();
			return;
		}
		Service();
	}

	// Filet si EngineTick ne tourne pas (cf. AGENTS.md) : au moins une publication par monde.
	void OnAnyWorldBeginPlay(SDK::UWorld*, const char* worldName)
	{
		if (MapStateSdk::IsChimeraWorldName(worldName))
		{
			// Nouveau monde : textures rechargees par le jeu, on rejoue la sequence d'apercu.
			g_previewClock = 0.0f;
			g_previewLoadsDone = 0;
			Service();
		}
	}

	void OnToggleKey(EModKey, EModKeyEvent)
	{
		IPluginHooks* hooks = GetHooks();
		if (!hooks || !hooks->UI || !g_panel)
		{
			return;
		}
		if (IsOpen())
		{
			hooks->UI->SetPanelClose(g_panel);
			g_lastRenderUnixMs = 0;
		}
		else
		{
			hooks->UI->SetPanelOpen(g_panel);
		}
	}

	// ------------------------------------------------------------------ rendu

	enum class HoverKind { None, Cargo, Teleporter, Player, Poi, Link, Catalog };

	struct Canvas
	{
		IModLoaderImGui* Ui = nullptr;
		PluginDrawList DrawList = nullptr;
		float X = 0.0f, Y = 0.0f, W = 0.0f, H = 0.0f;
		float MouseX = 0.0f, MouseY = 0.0f;
		bool Hovered = false;
		HoverKind Hover = HoverKind::None;
		size_t HoverIndex = 0;
		size_t HoverGroup = 0;           // HoverKind::Catalog : groupe du catalogue
		float HoverDistanceSq = 100.0f;  // rayon de survol : 10 px

		float ScreenX(double worldX) const { return X + W * 0.5f + static_cast<float>((worldX - g_viewCenterX) * g_pixelsPerUnit); }
		float ScreenY(double worldY) const { return Y + H * 0.5f + static_cast<float>((worldY - g_viewCenterY) * g_pixelsPerUnit); }
		double WorldX(float screenX) const { return g_viewCenterX + (screenX - (X + W * 0.5f)) / g_pixelsPerUnit; }
		double WorldY(float screenY) const { return g_viewCenterY + (screenY - (Y + H * 0.5f)) / g_pixelsPerUnit; }

		void TryHover(HoverKind kind, size_t index, float distanceSq)
		{
			if (Hovered && distanceSq < HoverDistanceSq)
			{
				HoverDistanceSq = distanceSq;
				Hover = kind;
				HoverIndex = index;
			}
		}

		// Texte avec ombre, lisible sur le terrain.
		void Label(float x, float y, unsigned int color, const char* text)
		{
			Ui->DL_AddText(DrawList, x + 1.0f, y + 1.0f, 0xE0000000u, text);
			Ui->DL_AddText(DrawList, x, y, color, text);
		}

		void Marker(const SDK::FVector& world, unsigned int color, float radius, int sides, HoverKind kind, size_t index, const char* label = nullptr)
		{
			const float sx = ScreenX(world.X);
			const float sy = ScreenY(world.Y);
			if (sx < X - radius || sx > X + W + radius || sy < Y - radius || sy > Y + H + radius)
			{
				return;
			}
			if (sides > 0)
			{
				Ui->DL_AddNgonFilled(DrawList, sx, sy, radius, color, sides);
				Ui->DL_AddNgon(DrawList, sx, sy, radius, 0xFF000000u, sides, 1.0f);
			}
			else
			{
				Ui->DL_AddCircleFilled(DrawList, sx, sy, radius, color, 12);
				Ui->DL_AddCircle(DrawList, sx, sy, radius, 0xFF000000u, 12, 1.0f);
			}
			if (label && *label)
			{
				Label(sx + radius + 3.0f, sy - 7.0f, 0xFFFFFFFFu, label);
			}
			const float dx = sx - MouseX;
			const float dy = sy - MouseY;
			TryHover(kind, index, dx * dx + dy * dy);
		}
	};

	void FitView(const TerrainDescriptor& terrain, float width, float height)
	{
		float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
		SegmentRangeWorldRect(terrain, terrain.MinX, terrain.MinY, terrain.MaxX, terrain.MaxY, x0, y0, x1, y1);
		g_viewCenterX = (x0 + x1) * 0.5;
		g_viewCenterY = (y0 + y1) * 0.5;
		g_pixelsPerUnit = std::min(width / std::max(x1 - x0, 1.0f), height / std::max(y1 - y0, 1.0f));
	}

	// Dessine le terrain et publie les tuiles a passer en haute resolution :
	// les plus proches du centre de la vue, dans la limite du budget.
	void DrawTerrain(Canvas& canvas, const Published& published)
	{
		const TerrainDescriptor& terrain = published.Terrain;
		const float tilePixels = std::fabs(terrain.Grid.SegmentX) * g_pixelsPerUnit;
		std::vector<std::pair<float, size_t>> visible;
		for (size_t index = 0; index < terrain.Segments.size() && index < published.Tiles.size(); ++index)
		{
			const TerrainSegment& segment = terrain.Segments[index];
			float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
			SegmentRangeWorldRect(terrain, segment.GridX, segment.GridY, segment.GridX, segment.GridY, x0, y0, x1, y1);
			const float sx0 = canvas.ScreenX(x0), sy0 = canvas.ScreenY(y0);
			const float sx1 = canvas.ScreenX(x1), sy1 = canvas.ScreenY(y1);
			if (sx1 < canvas.X || sx0 > canvas.X + canvas.W || sy1 < canvas.Y || sy0 > canvas.Y + canvas.H)
			{
				continue;
			}
			if (published.Tiles[index])
			{
				canvas.Ui->DL_AddImage(canvas.DrawList, published.Tiles[index], sx0, sy0, sx1, sy1, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
			}
			const float dx = (sx0 + sx1) * 0.5f - (canvas.X + canvas.W * 0.5f);
			const float dy = (sy0 + sy1) * 0.5f - (canvas.Y + canvas.H * 0.5f);
			visible.emplace_back(dx * dx + dy * dy, index);
		}

		uint64_t wanted = 0;
		if (tilePixels >= kHiResMinTilePixels)
		{
			std::sort(visible.begin(), visible.end());
			const size_t budget = static_cast<size_t>(std::max(g_hiResBudget.load(), 0));
			for (size_t rank = 0; rank < visible.size() && rank < budget; ++rank)
			{
				if (visible[rank].second < 64)
				{
					wanted |= uint64_t{ 1 } << visible[rank].second;
				}
			}
		}
		g_wantedHiRes = wanted;
	}

	constexpr unsigned int kCatalogPalette[] = {
		0xFF4B4BE6u, 0xFF4BB43Cu, 0xFF19E1FFu, 0xFFD86343u, 0xFF3182F5u, 0xFFB41E91u,
		0xFFF0F046u, 0xFFE632F0u, 0xFF0CF6BCu, 0xFFBEBEFAu, 0xFF808000u, 0xFFFFBEE6u,
	};

	unsigned int CatalogColor(size_t groupIndex)
	{
		return kCatalogPalette[groupIndex % std::size(kCatalogPalette)];
	}

	const char* CategoryName(CatalogCategory category)
	{
		switch (category)
		{
		case CatalogCategory::Poi: return "Points of interest";
		case CatalogCategory::Mineral: return "Minerals";
		case CatalogCategory::Plant: return "Plants";
		case CatalogCategory::Creature: return "Creatures";
		}
		return "";
	}

	// ponytail: plafond de points dessines par image ; au-dela on en saute un sur N
	// (sous-echantillonnage regulier). Passer a une grille spatiale si le parcours
	// lineaire des ~57 000 points devient mesurable.
	constexpr size_t kMaxCatalogPointsPerFrame = 12000;

	// Icones a partir de ~2.5 m/px pour les plantes (trop denses avant) ; toujours
	// pour les minerais et les creatures.
	constexpr float kPlantIconMinPixelsPerUnit = 0.004f;
	constexpr float kCatalogIconHalfSize = 9.0f;

	void DrawCatalog(Canvas& canvas, const Catalog& catalog, const std::vector<PluginTextureHandle>& icons)
	{
		if (g_catalogEnabled.size() != catalog.Groups.size())
		{
			// Par defaut : points d'interet et minerais ; plantes et creatures a la demande.
			g_catalogEnabled.resize(catalog.Groups.size());
			for (size_t index = 0; index < catalog.Groups.size(); ++index)
			{
				g_catalogEnabled[index] = catalog.Groups[index].Category <= CatalogCategory::Mineral;
			}
		}
		const float minX = static_cast<float>(canvas.WorldX(canvas.X)), maxX = static_cast<float>(canvas.WorldX(canvas.X + canvas.W));
		const float minY = static_cast<float>(canvas.WorldY(canvas.Y)), maxY = static_cast<float>(canvas.WorldY(canvas.Y + canvas.H));
		// Visible = dans la vue et, pour un gisement, de purete cochee.
		const auto shown = [&](const CatalogGroup& group, size_t index)
		{
			const CatalogPoint& point = group.Points[index];
			return point.X >= minX && point.X <= maxX && point.Y >= minY && point.Y <= maxY
				&& (index >= group.Purity.size() || g_purityEnabled[group.Purity[index] & 3]);
		};

		size_t visible = 0;
		for (size_t groupIndex = 0; groupIndex < catalog.Groups.size(); ++groupIndex)
		{
			const CatalogGroup& group = catalog.Groups[groupIndex];
			for (size_t index = 0; g_catalogEnabled[groupIndex] && index < group.Points.size(); ++index)
			{
				visible += shown(group, index) ? 1 : 0;
			}
		}
		const size_t stride = visible / kMaxCatalogPointsPerFrame + 1;

		size_t counter = 0;
		for (size_t groupIndex = 0; groupIndex < catalog.Groups.size(); ++groupIndex)
		{
			if (!g_catalogEnabled[groupIndex])
			{
				continue;
			}
			const CatalogGroup& group = catalog.Groups[groupIndex];
			const unsigned int color = CatalogColor(groupIndex);
			const bool detailed = group.Category <= CatalogCategory::Mineral;  // peu de points : contour + forme
			const float half = group.Category == CatalogCategory::Plant ? 1.5f : 2.5f;
			PluginTextureHandle icon = groupIndex < icons.size() ? icons[groupIndex] : nullptr;
			if (group.Category == CatalogCategory::Plant && g_pixelsPerUnit < kPlantIconMinPixelsPerUnit)
			{
				icon = nullptr;
			}
			for (size_t index = 0; index < group.Points.size(); ++index)
			{
				const CatalogPoint& point = group.Points[index];
				if (!shown(group, index) || (counter++ % stride) != 0)
				{
					continue;
				}
				const float sx = canvas.ScreenX(point.X), sy = canvas.ScreenY(point.Y);
				if (icon)
				{
					const float h = kCatalogIconHalfSize;
					canvas.Ui->DL_AddImage(canvas.DrawList, icon, sx - h, sy - h, sx + h, sy + h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
					if (index < group.Purity.size() && group.Purity[index] == 3)
					{
						// Gisement pur : cadre blanc autour de l'icone.
						canvas.Ui->DL_AddRect(canvas.DrawList, sx - h, sy - h, sx + h, sy + h, 0xFFFFFFFFu, 3.0f, 0, 1.5f);
					}
				}
				else if (group.Category == CatalogCategory::Poi)
				{
					canvas.Ui->DL_AddNgonFilled(canvas.DrawList, sx, sy, 6.0f, color, 5);
					canvas.Ui->DL_AddNgon(canvas.DrawList, sx, sy, 6.0f, 0xFF000000u, 5, 1.0f);
				}
				else if (detailed)
				{
					// Minerai : contour blanc = pur, noir sinon.
					const bool pure = index < group.Purity.size() && group.Purity[index] == 3;
					canvas.Ui->DL_AddRectFilled(canvas.DrawList, sx - 3.5f, sy - 3.5f, sx + 3.5f, sy + 3.5f, color, 0.0f, 0);
					canvas.Ui->DL_AddRect(canvas.DrawList, sx - 3.5f, sy - 3.5f, sx + 3.5f, sy + 3.5f, pure ? 0xFFFFFFFFu : 0xFF000000u, 0.0f, 0, 1.0f);
				}
				else
				{
					canvas.Ui->DL_AddRectFilled(canvas.DrawList, sx - half, sy - half, sx + half, sy + half, color, 0.0f, 0);
				}
				const float dx = sx - canvas.MouseX, dy = sy - canvas.MouseY;
				if (canvas.Hovered && dx * dx + dy * dy < canvas.HoverDistanceSq)
				{
					canvas.TryHover(HoverKind::Catalog, index, dx * dx + dy * dy);
					canvas.HoverGroup = groupIndex;
				}
			}
		}
	}

	float DistanceToSegmentSq(float px, float py, float ax, float ay, float bx, float by)
	{
		const float abx = bx - ax, aby = by - ay;
		const float lengthSq = abx * abx + aby * aby;
		const float t = lengthSq > 0.0f ? std::clamp(((px - ax) * abx + (py - ay) * aby) / lengthSq, 0.0f, 1.0f) : 0.0f;
		const float dx = px - (ax + t * abx), dy = py - (ay + t * aby);
		return dx * dx + dy * dy;
	}

	unsigned int PoiColor(const MapStateRuntime::Detail::PoiMarker& poi)
	{
		unsigned int color = kColorPoi;
		switch (poi.Kind)
		{
		case PoiKind::AbandonedBase: color = 0xFFB0B0B0u; break;
		case PoiKind::PlantResource: color = 0xFF50C878u; break;
		case PoiKind::Ignitium: color = 0xFF3060FFu; break;
		case PoiKind::StarTears: color = 0xFFFFE080u; break;
		}
		return poi.Depleted ? (color & 0x00FFFFFFu) | 0x60000000u : color;
	}

	void DrawOverlays(Canvas& canvas, const CargoSnapshot& snapshot)
	{
		// Noms des teleporteurs affiches en permanence a partir de ~1 px pour 4 m.
		const bool showLabels = g_pixelsPerUnit >= 0.0025f;

		if (g_showPois)
		{
			for (size_t index = 0; index < snapshot.Pois.size(); ++index)
			{
				const auto& poi = snapshot.Pois[index];
				const bool base = poi.Kind == PoiKind::AbandonedBase;
				canvas.Marker(poi.WorldLocation, PoiColor(poi), base ? 5.0f : 3.0f, base ? 4 : 0, HoverKind::Poi, index);
			}
		}
		if (g_showConnections)
		{
			for (size_t index = 0; index < snapshot.Connections.size(); ++index)
			{
				const auto& connection = snapshot.Connections[index];
				const float ax = canvas.ScreenX(connection.SenderWorldLocation.X), ay = canvas.ScreenY(connection.SenderWorldLocation.Y);
				const float bx = canvas.ScreenX(connection.ReceiverWorldLocation.X), by = canvas.ScreenY(connection.ReceiverWorldLocation.Y);
				canvas.Ui->DL_AddLine(canvas.DrawList, ax, ay, bx, by, kColorLink, 1.5f);
				// Lien survole a moins de 4 px ; +36 pour qu'un marqueur proche garde la priorite.
				const float distanceSq = DistanceToSegmentSq(canvas.MouseX, canvas.MouseY, ax, ay, bx, by);
				if (distanceSq < 16.0f)
				{
					canvas.TryHover(HoverKind::Link, index, distanceSq + 36.0f);
				}
			}
		}
		if (g_showCargo)
		{
			for (size_t index = 0; index < snapshot.Markers.size(); ++index)
			{
				const auto& marker = snapshot.Markers[index];
				canvas.Marker(marker.WorldLocation, marker.Kind == CargoKind::Sender ? kColorSender : kColorReceiver, 4.5f, 0, HoverKind::Cargo, index);
			}
		}
		if (g_showTeleporters)
		{
			for (size_t index = 0; index < snapshot.Teleporters.size(); ++index)
			{
				const auto& teleporter = snapshot.Teleporters[index];
				canvas.Marker(teleporter.WorldLocation, kColorTeleporter, 6.5f, 4, HoverKind::Teleporter, index,
					showLabels ? teleporter.DisplayName.c_str() : nullptr);
			}
		}
		if (g_showPlayers)
		{
			for (size_t index = 0; index < snapshot.Players.size(); ++index)
			{
				const auto& player = snapshot.Players[index];
				canvas.Marker(player.WorldLocation, player.IsSelf ? kColorSelf : kColorPlayer, player.IsSelf ? 7.0f : 5.5f, 3,
					HoverKind::Player, index, player.DisplayName.c_str());
			}
		}
	}

	std::string BuildTooltip(const Canvas& canvas, const CargoSnapshot& snapshot, const Catalog* catalog)
	{
		char text[640] = "";
		const auto place = [&](const char* kind, const std::string& name, const SDK::FVector& world)
		{
			std::snprintf(text, sizeof(text), "%s: %s\nX %.0f  Y %.0f  Z %.0f", kind, name.c_str(), world.X, world.Y, world.Z);
		};
		switch (canvas.Hover)
		{
		case HoverKind::Cargo:
			if (canvas.HoverIndex < snapshot.Markers.size())
			{
				const auto& marker = snapshot.Markers[canvas.HoverIndex];
				place(marker.Kind == CargoKind::Sender ? "Dispatcher" : "Receiver",
					MapStateRuntime::Detail::ComposeMarkerDisplayName(marker), marker.WorldLocation);
			}
			break;
		case HoverKind::Teleporter:
			if (canvas.HoverIndex < snapshot.Teleporters.size())
			{
				place("Teleporter", snapshot.Teleporters[canvas.HoverIndex].DisplayName, snapshot.Teleporters[canvas.HoverIndex].WorldLocation);
			}
			break;
		case HoverKind::Player:
			if (canvas.HoverIndex < snapshot.Players.size())
			{
				place(snapshot.Players[canvas.HoverIndex].IsSelf ? "Me" : "Player",
					snapshot.Players[canvas.HoverIndex].DisplayName, snapshot.Players[canvas.HoverIndex].WorldLocation);
			}
			break;
		case HoverKind::Poi:
			if (canvas.HoverIndex < snapshot.Pois.size())
			{
				const auto& poi = snapshot.Pois[canvas.HoverIndex];
				place(poi.Depleted ? "Depleted" : "Resource", poi.DisplayName.empty() ? poi.ResourceName : poi.DisplayName, poi.WorldLocation);
			}
			break;
		case HoverKind::Link:
			if (canvas.HoverIndex < snapshot.Connections.size())
			{
				const auto& connection = snapshot.Connections[canvas.HoverIndex];
				std::snprintf(text, sizeof(text), "%s x%d\n%s  ->  %s", connection.ItemDisplayName.c_str(), connection.RequestedAmount,
					connection.SenderLabel.c_str(), connection.ReceiverLabel.c_str());
			}
			break;
		case HoverKind::Catalog:
			if (catalog && canvas.HoverGroup < catalog->Groups.size() && canvas.HoverIndex < catalog->Groups[canvas.HoverGroup].Points.size())
			{
				const CatalogGroup& group = catalog->Groups[canvas.HoverGroup];
				const CatalogPoint& point = group.Points[canvas.HoverIndex];
				const char* purity = canvas.HoverIndex < group.Purity.size() ? PurityName(group.Purity[canvas.HoverIndex]) : "";
				if (canvas.HoverIndex < group.Names.size())
				{
					std::snprintf(text, sizeof(text), "%s: %s\nX %.0f  Y %.0f", group.Label.c_str(), group.Names[canvas.HoverIndex].c_str(), point.X, point.Y);
				}
				else if (*purity)
				{
					std::snprintf(text, sizeof(text), "%s (%s)\nX %.0f  Y %.0f", group.Label.c_str(), purity, point.X, point.Y);
				}
				else
				{
					std::snprintf(text, sizeof(text), "%s\nX %.0f  Y %.0f", group.Label.c_str(), point.X, point.Y);
				}
			}
			break;
		case HoverKind::None:
			break;
		}
		return text;
	}

	// Encart rupture en haut a droite de la carte. Meme modele que la timeline du
	// viewer web (useRuptureTimeline.ts) : la phase se deduit du temps ecoule dans
	// le cycle, car wave/stage/step valent "None" en dehors d'une vague. Durees
	// calibrees par observation, identiques a BuildRuptureCycleJson.
	void DrawRuptureBox(Canvas& canvas, const CargoSnapshot& snapshot)
	{
		struct Phase { const char* Name; double Seconds; unsigned int Color; };
		static const Phase phases[] = {
			{ "Arcadia burning", 30.0, 0xFF3030FFu },
			{ "Arcadia cooling", 60.0, 0xFF30A0FFu },
			{ "Arcadia stabilizing", 600.0, 0xFF30E0E0u },
			{ "Arcadia stable", 2430.0, 0xFF60E060u },
			{ "Rupture incoming", 120.0, 0xFF3060FFu },
		};
		constexpr double kCycleSeconds = 3240.0;

		const auto& rupture = snapshot.RuptureCycle;
		if (!rupture.Available)
		{
			return;
		}
		char text[256];
		unsigned int color = phases[3].Color;
		if (rupture.HasElapsed)
		{
			double elapsed = rupture.ElapsedSeconds;
			if (rupture.HasObservedAtUnixMs)
			{
				elapsed += std::max(0.0, static_cast<double>(MapStateSdk::GetCurrentUnixTimeMilliseconds() - rupture.ObservedAtUnixMs) / 1000.0);
			}
			double position = std::fmod(elapsed, kCycleSeconds);
			position = position < 0.0 ? position + kCycleSeconds : position;
			size_t current = 0;
			double phaseEnd = phases[0].Seconds;
			while (current + 1 < std::size(phases) && position >= phaseEnd)
			{
				phaseEnd += phases[++current].Seconds;
			}
			color = phases[current].Color;
			const int phaseLeft = static_cast<int>(phaseEnd - position);
			const int cycleLeft = static_cast<int>(kCycleSeconds - position);
			std::snprintf(text, sizeof(text), "%s\nPhase ends in %02d:%02d\nNext rupture in %02d:%02d",
				phases[current].Name, phaseLeft / 60, phaseLeft % 60, cycleLeft / 60, cycleLeft % 60);
		}
		else
		{
			// Pas de temps ecoule : repli sur l'etape brute du jeu.
			const char* name = phases[3].Name;
			if (rupture.Stage == "Moving" || (rupture.Stage == "Fadeout" && rupture.Step == "FireWave")) name = phases[0].Name;
			else if (rupture.Stage == "Fadeout") name = phases[1].Name;
			else if (rupture.Stage == "Growback") name = phases[2].Name;
			std::snprintf(text, sizeof(text), "%s", name);
		}
		float textW = 0.0f, textH = 0.0f;
		canvas.Ui->CalcTextSize(text, &textW, &textH, false, -1.0f);
		const float x1 = canvas.X + canvas.W - 8.0f, y0 = canvas.Y + 8.0f;
		const float x0 = x1 - textW - 16.0f, y1 = y0 + textH + 12.0f;
		canvas.Ui->DL_AddRectFilled(canvas.DrawList, x0, y0, x1, y1, 0xD0101010u, 4.0f, 0);
		canvas.Ui->DL_AddRect(canvas.DrawList, x0, y0, x1, y1, color, 4.0f, 0, 2.0f);
		canvas.Ui->DL_AddText(canvas.DrawList, x0 + 8.0f, y0 + 6.0f, 0xFFFFFFFFu, text);
	}

	// Case a cocher precedee d'une pastille de la couleur du calque : sert de legende.
	void LayerCheckbox(IModLoaderImGui* ui, const char* label, bool* value, unsigned int color, int sides)
	{
		float x = 0.0f, y = 0.0f;
		ui->GetCursorScreenPos(&x, &y);
		const float size = ui->GetFrameHeight();
		ui->Dummy(size * 0.7f, size);
		PluginDrawList drawList = ui->GetWindowDrawList();
		if (sides > 0)
		{
			ui->DL_AddNgonFilled(drawList, x + size * 0.35f, y + size * 0.5f, size * 0.3f, color, sides);
		}
		else
		{
			ui->DL_AddCircleFilled(drawList, x + size * 0.35f, y + size * 0.5f, size * 0.28f, color, 12);
		}
		ui->SameLine(0.0f, 2.0f);
		ui->Checkbox(label, value);
		ui->SameLine(0.0f, -1.0f);
	}

	void RenderToolbar(IModLoaderImGui* ui)
	{
		LayerCheckbox(ui, "Cargo", &g_showCargo, kColorSender, 0);
		LayerCheckbox(ui, "Links", &g_showConnections, kColorLink, 0);
		LayerCheckbox(ui, "Teleporters", &g_showTeleporters, kColorTeleporter, 4);
		LayerCheckbox(ui, "Players", &g_showPlayers, kColorPlayer, 3);
		ui->Checkbox("Resources", &g_showCatalog);
		ui->SameLine(0.0f, -1.0f);
		ui->Checkbox("Filters", &g_showFilters);
		ui->SameLine(0.0f, -1.0f);
		if (ui->Checkbox("Terrain", &g_showTerrain) && g_showTerrain)
		{
			g_reloadTerrainRequested = true;  // recocher recharge l'apercu
		}
		ui->SameLine(0.0f, -1.0f);
		ui->Checkbox("Follow me", &g_followSelf);
	}

	// Volet de gauche : un filtre par type de ressource du catalogue, comme le viewer web.
	// Comme LayerCheckbox, avec l'icone de l'item a la place de la pastille.
	void IconCheckbox(IModLoaderImGui* ui, const char* label, bool* value, PluginTextureHandle icon)
	{
		float x = 0.0f, y = 0.0f;
		ui->GetCursorScreenPos(&x, &y);
		const float size = ui->GetFrameHeight();
		ui->Dummy(size, size);
		ui->DL_AddImage(ui->GetWindowDrawList(), icon, x, y, x + size, y + size, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
		ui->SameLine(0.0f, 4.0f);
		ui->Checkbox(label, value);
	}

	void RenderFilters(IModLoaderImGui* ui, const Catalog* catalog, const std::vector<PluginTextureHandle>& icons)
	{
		ui->BeginChild("##ingame_map_filters", 250.0f, 0.0f, true);
		LayerCheckbox(ui, "Observed in game", &g_showPois, kColorPoi, 0);
		ui->Dummy(0.0f, 0.0f);
		if (!catalog)
		{
			ui->TextDisabled("Loading catalog...");
		}
		else if (!catalog->Error.empty())
		{
			ui->TextDisabled("Catalog files not found.");
			ui->TextDisabled("Expected folder:");
			ui->TextDisabled("Plugins\\MapExtension\\map-data");
		}
		else if (g_catalogEnabled.size() == catalog->Groups.size())
		{
			char label[160];
			for (size_t index = 0; index < catalog->Groups.size(); ++index)
			{
				const CatalogGroup& group = catalog->Groups[index];
				if (index == 0 || catalog->Groups[index - 1].Category != group.Category)
				{
					ui->SeparatorText(CategoryName(group.Category));
					ui->PushIDInt(static_cast<int>(group.Category));
					const bool all = ui->SmallButton("All");
					ui->SameLine(0.0f, -1.0f);
					const bool none = ui->SmallButton("None");
					ui->PopID();
					for (size_t other = index; (all || none) && other < catalog->Groups.size() && catalog->Groups[other].Category == group.Category; ++other)
					{
						g_catalogEnabled[other] = all;
					}
					if (group.Category == CatalogCategory::Mineral)
					{
						// Purete des gisements (les types sans purete ne sont pas filtres).
						ui->Checkbox("Pure", &g_purityEnabled[3]);
						ui->SameLine(0.0f, -1.0f);
						ui->Checkbox("Normal", &g_purityEnabled[2]);
						ui->SameLine(0.0f, -1.0f);
						ui->Checkbox("Impure", &g_purityEnabled[1]);
						ui->Separator();
					}
				}
				std::snprintf(label, sizeof(label), "%s (%d)", group.Label.c_str(), static_cast<int>(group.Points.size()));
				bool enabled = g_catalogEnabled[index] != 0;
				if (index < icons.size() && icons[index])
				{
					IconCheckbox(ui, label, &enabled, icons[index]);
				}
				else
				{
					LayerCheckbox(ui, label, &enabled, CatalogColor(index), group.Category == CatalogCategory::Poi ? 5 : 4);
					ui->Dummy(0.0f, 0.0f);
				}
				g_catalogEnabled[index] = enabled;
			}
		}
		ui->EndChild();
		ui->SameLine(0.0f, -1.0f);
	}

	// Boutons dans la barre de titre du panneau (v75).
	void RenderTitleBar(IModLoaderImGui* ui)
	{
		ui->SameLine(0.0f, -1.0f);
		if (ui->SmallButton("Fit"))
		{
			g_fitRequested = true;
			g_followSelf = false;
		}
		ui->SameLine(0.0f, -1.0f);
		if (ui->SmallButton("Me"))
		{
			g_centerOnSelfRequested = true;
		}
		ui->SameLine(0.0f, -1.0f);
		if (ui->SmallButton("Filters"))
		{
			g_showFilters = !g_showFilters;
		}
	}

	void Render(IModLoaderImGui* ui)
	{
		g_lastRenderUnixMs = MapStateSdk::GetCurrentUnixTimeMilliseconds();
		std::shared_ptr<const Published> published;
		{
			std::lock_guard<std::mutex> lock(g_publishedMutex);
			published = g_published;
		}

		RenderToolbar(ui);
		if (g_showFilters)
		{
			static const std::vector<PluginTextureHandle> noIcons;
			RenderFilters(ui, published ? published->StaticCatalog.get() : nullptr, published ? published->CatalogIcons : noIcons);
		}

		// Le canvas occupe exactement la place restante : rien ne deborde, donc
		// la molette ne fait pas defiler le panneau (plan 7).
		Canvas canvas;
		canvas.Ui = ui;
		ui->GetContentRegionAvail(&canvas.W, &canvas.H);
		canvas.W = canvas.W < 120.0f ? 900.0f : canvas.W;
		canvas.H = canvas.H < 120.0f ? 640.0f : canvas.H;
		ui->GetCursorScreenPos(&canvas.X, &canvas.Y);
		ui->InvisibleButton("##ingame_map_canvas", canvas.W, canvas.H);
		canvas.Hovered = ui->IsItemHovered();
		ui->GetMousePos(&canvas.MouseX, &canvas.MouseY);

		const bool terrainReady = published && published->Terrain.Valid;
		if (g_fitRequested && terrainReady)
		{
			g_fitRequested = false;
			FitView(published->Terrain, canvas.W, canvas.H);
		}
		if (ui->IsItemActive() && ui->IsMouseDragging(0, 0.0f))
		{
			float dx = 0.0f, dy = 0.0f;
			ui->GetMouseDragDelta(0, 0.0f, &dx, &dy);
			ui->ResetMouseDragDelta(0);
			g_viewCenterX -= dx / g_pixelsPerUnit;
			g_viewCenterY -= dy / g_pixelsPerUnit;
			g_followSelf = false;
		}
		const float wheel = canvas.Hovered ? ui->GetMouseWheel() : 0.0f;
		if (wheel != 0.0f)
		{
			// Zoom ancre sur le point monde sous le curseur. Bornes : 50 m/px a 2 cm/px.
			const double worldX = canvas.WorldX(canvas.MouseX);
			const double worldY = canvas.WorldY(canvas.MouseY);
			g_pixelsPerUnit = std::clamp(g_pixelsPerUnit * std::pow(1.2f, wheel), 0.0002f, 0.5f);
			g_viewCenterX += worldX - canvas.WorldX(canvas.MouseX);
			g_viewCenterY += worldY - canvas.WorldY(canvas.MouseY);
		}
		if ((g_followSelf || g_centerOnSelfRequested) && published)
		{
			g_centerOnSelfRequested = false;
			for (const auto& player : published->Snapshot.Players)
			{
				if (player.IsSelf)
				{
					g_viewCenterX = player.WorldLocation.X;
					g_viewCenterY = player.WorldLocation.Y;
				}
			}
		}

		canvas.DrawList = ui->GetWindowDrawList();
		ui->DL_PushClipRect(canvas.DrawList, canvas.X, canvas.Y, canvas.X + canvas.W, canvas.Y + canvas.H, true);
		ui->DL_AddRectFilled(canvas.DrawList, canvas.X, canvas.Y, canvas.X + canvas.W, canvas.Y + canvas.H, 0xFF18130Fu, 0.0f, 0);

		char status[200];
		if (terrainReady && g_showTerrain)
		{
			DrawTerrain(canvas, *published);
		}
		else
		{
			g_wantedHiRes = 0;
		}
		if (!published)
		{
			std::snprintf(status, sizeof(status), "Waiting for the game thread...");
		}
		else
		{
			if (g_showCatalog && published->StaticCatalog && published->StaticCatalog->Error.empty())
			{
				DrawCatalog(canvas, *published->StaticCatalog, published->CatalogIcons);
			}
			DrawOverlays(canvas, published->Snapshot);
			DrawRuptureBox(canvas, published->Snapshot);
			const int segments = static_cast<int>(published->Terrain.Segments.size());
			if (!terrainReady)
			{
				std::snprintf(status, sizeof(status), "Terrain not available yet (load a save)");
			}
			else if (canvas.Hovered)
			{
				std::snprintf(status, sizeof(status), "X %.0f  Y %.0f   |   HD tiles %d/%d",
					canvas.WorldX(canvas.MouseX), canvas.WorldY(canvas.MouseY), published->HiResCount, segments);
			}
			else
			{
				std::snprintf(status, sizeof(status), "HD tiles %d/%d", published->HiResCount, segments);
			}
		}
		canvas.Label(canvas.X + 8.0f, canvas.Y + canvas.H - 22.0f, 0xFFE0E0E0u, status);
		ui->DL_PopClipRect(canvas.DrawList);

		if (published && canvas.Hover != HoverKind::None)
		{
			const std::string tooltip = BuildTooltip(canvas, published->Snapshot, published->StaticCatalog.get());
			if (!tooltip.empty())
			{
				ui->SetTooltip(tooltip.c_str());
			}
		}
	}

	PluginPanelDesc g_panelDesc = { "Logistics map", "MapExtension - logistics map", &Render, &RenderTitleBar };

	void Enable()
	{
		IPluginHooks* hooks = GetHooks();
		if (g_panel || !hooks || !hooks->UI || !hooks->UI->RegisterPanel)
		{
			return;
		}
		g_panel = hooks->UI->RegisterPanel(&g_panelDesc);
		if (!g_panel)
		{
			LOG_WARN("[InGameMap] RegisterPanel failed; in-game map disabled");
			return;
		}
		const int budgetMiB = std::clamp(MapExtensionPluginConfig::Config::InGameMapTileBudgetMiB(), 0, 1024);
		g_hiResBudget = std::min(static_cast<int>(static_cast<float>(budgetMiB) / kHiResTileMiB), 64);
		g_catalogFuture = std::async(std::launch::async, LoadCatalog, DefaultCatalogDirectory());
		if (hooks->Engine && hooks->Engine->RegisterOnTick)
		{
			hooks->Engine->RegisterOnTick(OnTick);
		}
		if (hooks->World && hooks->World->RegisterOnAnyWorldBeginPlay)
		{
			hooks->World->RegisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
		}
		g_toggleKey = MapExtensionPluginConfig::Config::InGameMapToggleKey();
		if (!g_toggleKey.empty() && hooks->Input && hooks->Input->RegisterKeybindByName)
		{
			hooks->Input->RegisterKeybindByName(g_toggleKey.c_str(), EModKeyEvent::Pressed, OnToggleKey);
		}
		LOG_INFO("[InGameMap] experimental in-game map enabled (toggle key: %s, hi-res budget: %d MiB = %d tiles)",
			g_toggleKey.c_str(), budgetMiB, g_hiResBudget.load());
	}

	void Disable()
	{
		IPluginHooks* hooks = GetHooks();
		if (!g_panel || !hooks)
		{
			return;
		}
		if (!g_toggleKey.empty() && hooks->Input && hooks->Input->UnregisterKeybindByName)
		{
			hooks->Input->UnregisterKeybindByName(g_toggleKey.c_str(), EModKeyEvent::Pressed, OnToggleKey);
		}
		if (hooks->World && hooks->World->UnregisterOnAnyWorldBeginPlay)
		{
			hooks->World->UnregisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
		}
		if (hooks->Engine && hooks->Engine->UnregisterOnTick)
		{
			hooks->Engine->UnregisterOnTick(OnTick);
		}
		if (hooks->UI && hooks->UI->UnregisterPanel)
		{
			hooks->UI->UnregisterPanel(g_panel);
		}
		g_panel = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_publishedMutex);
			g_published.reset();
		}
		// ponytail: g_tiles appartient au game thread et Disable peut venir du thread
		// de config ; le tick vient d'etre desenregistre, pas de verrou. A proteger
		// par mutex si un crash apparait au changement d'option (J5).
		FreeTiles();
		g_previewClock = 0.0f;
		g_previewLoadsDone = 0;
		g_wantedHiRes = 0;
		if (g_catalogFuture.valid())
		{
			g_catalogFuture.wait();  // le thread de chargement ne doit pas survivre au plugin
			g_catalogFuture = {};
		}
		g_catalog.reset();
		FreeCatalogIcons();
		LOG_INFO("[InGameMap] in-game map disabled");
	}

	// InGameMap.Enabled s'applique a chaud : pas besoin de recharger le plugin.
	void OnConfigChanged(const char* section, const char* key, const char* newValue)
	{
		if (!section || !key || !newValue || std::strcmp(section, "InGameMap") != 0 || std::strcmp(key, "Enabled") != 0)
		{
			return;
		}
		if (newValue[0] == '1' || newValue[0] == 't' || newValue[0] == 'T')
		{
			Enable();
		}
		else
		{
			Disable();
		}
	}
}

	void Initialize()
	{
		IPluginHooks* hooks = GetHooks();
		if (hooks && hooks->UI && hooks->UI->RegisterOnConfigChanged)
		{
			hooks->UI->RegisterOnConfigChanged(GetPluginSelf(), OnConfigChanged);
		}
		if (MapExtensionPluginConfig::Config::InGameMapEnabled())
		{
			Enable();
		}
	}

	void Shutdown()
	{
		IPluginHooks* hooks = GetHooks();
		if (hooks && hooks->UI && hooks->UI->UnregisterOnConfigChanged)
		{
			hooks->UI->UnregisterOnConfigChanged(GetPluginSelf(), OnConfigChanged);
		}
		Disable();
	}
}
