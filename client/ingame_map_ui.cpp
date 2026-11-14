#include "ingame_map_ui.h"

#include "ingame_map_catalog.h"
#include "ingame_map_terrain_source.h"

#include "../map_state_capture.h"
#include "../map_state_sdk_helpers.h"
#include "../plugin_config.h"
#include "../plugin_helpers.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <map>
#include <set>
#include <string_view>
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
		std::map<std::string, PluginTextureHandle> ItemIcons;  // cle : nom affiche (cargo) ou nom de classe
		std::shared_ptr<const PoiStateMap> PoiStates;  // etat des points d'interet du catalogue, par GUID
		std::shared_ptr<const LiveStructures> Live;    // antennes et obelisques lus en jeu
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
	// Icones retrouvees par nom parmi les items charges par le jeu : items des
	// liaisons cargo (nom affiche du snapshot), Ignitium et Star Tears (classe).
	constexpr char kIgnitiumIconKey[] = "I_FireWaveOre_C";
	constexpr char kStarTearsIconKey[] = "I_StarTears_C";
	constexpr float kNamedIconRetrySeconds = 30.0f;
	std::map<std::string, PluginTextureHandle> g_namedIcons;  // nullptr = introuvable pour l'instant
	float g_namedIconRetryClock = 0.0f;
	// Etat des points d'interet : relu toutes les 2 s (4 passes de service).
	std::shared_ptr<const PoiStateMap> g_poiStates;
	int g_poiStatePass = 0;
	int g_poiStatesLogged = -1;
	std::shared_ptr<const LiveStructures> g_live;  // relu toutes les 4 s (parcours d'acteurs)
	int g_liveLogged = -1;
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
	bool g_showUnconnectedCargo = true;
	// Render thread uniquement. Items coches dans la liste cargo ; vide = pas de filtre.
	std::set<std::string> g_cargoItemFilter;
	char g_cargoItemSearch[64] = "";
	bool g_showTeleporters = true;
	bool g_showPlayers = true;
	bool g_showBaseCores = true;
	bool g_showIgnitium = true;
	std::string g_selectedCargoKey;  // PublicKey du cargo clique ; vide = aucun
	bool g_canvasDragged = false;    // distingue un clic d'un deplacement de la carte
	bool g_toggleKeyWasHeld = false;
	std::atomic<bool> g_closeRequested{ false };    // Echap / touche bindee vues par le rendu
	std::atomic<int64_t> g_lastToggleUnixMs{ 0 };   // anti-rebond entre le keybind et le rendu
	constexpr int64_t kToggleDebounceMs = 400;
	bool g_showStarTears = true;
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
	constexpr unsigned int kColorBaseCore = 0xFF40D0FFu;
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

	void FreeNamedIcons()
	{
		IPluginImGuiTextures* textures = Textures();
		for (const auto& entry : g_namedIcons)
		{
			if (entry.second && textures)
			{
				textures->FreeTexture(entry.second);
			}
		}
		g_namedIcons.clear();
	}

	// Un parcours de GObjects seulement quand un nom inconnu apparait ; les noms
	// introuvables (classe pas encore chargee) sont retentes toutes les 30 s.
	// Icones de marqueur de la carte native du jeu, rangees dans g_namedIcons sous "marker:<nom>".
	struct MarkerIcon { const char* Key; const char* Folder; const char* Asset; };
	constexpr char kMarkerFolder[] = "/Game/Chimera/UI/Map/Markers/";
	constexpr MarkerIcon kMarkerIcons[] = {
		{ "abandonedBase", kMarkerFolder, "T_UI_abandonedBase_mapIcon" }, { "antenna", kMarkerFolder, "T_UI_antenna_mapIcon" },
		{ "baseCore", kMarkerFolder, "T_UI_baseCore_mapIcon" }, { "cave", kMarkerFolder, "T_UI_cave_mapIcon" },
		{ "forgottenEngine", kMarkerFolder, "T_UI_forgottenEngine_mapIcon" }, { "obelisk", kMarkerFolder, "T_UI_obelisk_mapIcon" },
		{ "orbitalLander", kMarkerFolder, "T_UI_orbitalLander_mapIcon" }, { "player", kMarkerFolder, "T_UI_player_mapIcon" },
		{ "teleporter", "/Game/Chimera/UI/Buildings/Teleporter/Textures/", "T_UI_teleporterIcon_128x128" } };

	void ServiceMarkerIcons()
	{
		size_t loads = 0;
		for (const MarkerIcon& marker : kMarkerIcons)
		{
			const std::string key = std::string("marker:") + marker.Key;
			if (g_namedIcons.contains(key) || loads++ >= kIconLoadsPerService)
			{
				continue;  // un echec (nullptr) est retente avec les autres icones nommees
			}
			const std::string asset = marker.Asset;
			const std::string path = marker.Folder + asset + "." + asset;
			const char* reason = "";
			PluginTextureHandle icon = LoadTextureAsset(path.c_str(), ("MapExtension_marker_" + asset).c_str(), &reason);
			// Texture tout juste chargee, pas encore sur le GPU : on repasse a la passe suivante
			// (0,5 s) tant que le jeu ne l'a pas liberee, 20 fois au plus.
			static std::map<std::string, int> waits;
			if (!icon && std::strcmp(reason, "not resident") == 0 && ++waits[key] < 20)
			{
				continue;
			}
			g_namedIcons[key] = icon;
			static std::map<std::string, std::string> logged;  // un log par changement d'etat, pas par essai
			if (const std::string state = icon ? "loaded" : reason; logged[key] != state)
			{
				logged[key] = state;
				LOG_INFO("[InGameMap] marker icon %s: %s (waits=%d)", asset.c_str(), state.c_str(), waits[key]);
			}
			waits[key] = 0;
		}
	}

	void ServiceNamedIcons(const CargoSnapshot& snapshot)
	{
		if ((g_namedIconRetryClock += kServiceIntervalSeconds) >= kNamedIconRetrySeconds)
		{
			g_namedIconRetryClock = 0.0f;
			std::erase_if(g_namedIcons, [](const auto& entry) { return entry.second == nullptr; });
		}
		std::vector<std::string> wanted;
		const auto want = [&](const std::string& key)
		{
			if (!key.empty() && !g_namedIcons.contains(key) && std::find(wanted.begin(), wanted.end(), key) == wanted.end())
			{
				wanted.push_back(key);
			}
		};
		want(kIgnitiumIconKey);
		want(kStarTearsIconKey);
		for (const auto& connection : snapshot.Connections)
		{
			want(connection.ItemDisplayName);
		}
		if (wanted.empty())
		{
			return;
		}
		const auto started = std::chrono::steady_clock::now();
		const std::vector<void*> icons = LoadItemIconsByName(wanted);
		int found = 0;
		for (size_t index = 0; index < wanted.size(); ++index)
		{
			g_namedIcons[wanted[index]] = icons[index];
			found += icons[index] ? 1 : 0;
		}
		const double ms = MillisecondsSince(started);
		if (found > 0 || ms > 5.0)
		{
			LOG_INFO("[InGameMap] item icons by name: %d/%d found, scan_ms=%.1f (first: %s)",
				found, static_cast<int>(wanted.size()), ms, wanted.front().c_str());
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
				ServiceNamedIcons(next->Snapshot);
				ServiceMarkerIcons();
				if (g_poiStatePass++ % 4 == 0)
				{
					g_poiStates = std::make_shared<const PoiStateMap>(CapturePoiStates());
				}
				if (g_poiStatePass % 8 == 1)
				{
					const auto started = std::chrono::steady_clock::now();
					auto live = std::make_shared<LiveStructures>(CaptureLiveStructures());
					const int signature = static_cast<int>(live->Antennas.size()) * 1000000 + live->ObeliskMarkers * 10000
						+ live->ObeliskReplicated * 100 + live->ObeliskActors + (live->ObeliskNative + 1) * 100000000
						+ live->BuildingMarkers * 7 + static_cast<int>(live->BaseCores.size()) * 13 + live->BaseCoresJoined * 17;
					if (signature != g_liveLogged)
					{
						g_liveLogged = signature;
						const LiveAntenna first = live->Antennas.empty() ? LiveAntenna{} : live->Antennas.front();
						const LiveObelisk obelisk = live->Obelisks.empty() ? LiveObelisk{} : live->Obelisks.front();
						LOG_INFO("[InGameMap] live structures: antennas=%d (first x=%.0f y=%.0f state=%d) obelisks native=%d (entities=%d) map_markers=%d replicated=%d actors=%d (first x=%.0f y=%.0f state=%d charge=%.3f max=%.3f) scan_ms=%.1f",
							static_cast<int>(live->Antennas.size()), first.X, first.Y, first.State,
							live->ObeliskNative, live->EntitiesScanned, live->ObeliskMarkers, live->ObeliskReplicated, live->ObeliskActors,
							obelisk.X, obelisk.Y, obelisk.State, obelisk.Charge, obelisk.ChargeMax, MillisecondsSince(started));
						LOG_INFO("[InGameMap] building markers: %d, base cores=%d, core data=%d joined=%d (first level=%d), unique names: %s",
							live->BuildingMarkers, static_cast<int>(live->BaseCores.size()), live->BaseCoreData, live->BaseCoresJoined,
							live->BaseCores.empty() ? -1 : live->BaseCores.front().Level, live->BuildingNames.c_str());
					}
					// Le premier passage paie la resolution des classes (62 ms mesures) ; on
					// verifie sur quelques passages suivants que le cout retombe.
					static int slowLogs = 0;
					if (const double ms = MillisecondsSince(started); ms > 10.0 && slowLogs < 5)
					{
						++slowLogs;
						LOG_WARN("[InGameMap] live structures scan is slow: %.1f ms", ms);
					}
					g_live = std::move(live);
				}

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
		next->ItemIcons = g_namedIcons;
		next->PoiStates = g_poiStates;
		next->Live = g_live;
		if (g_poiStates && g_catalog && g_catalog->Error.empty())
		{
			// Preuve de la jointure par GUID : combien d'etats du jeu retrouvent un point du catalogue.
			int matched = 0, bases = 0;
			for (const CatalogGroup& group : g_catalog->Groups)
			{
				for (const std::string& guid : group.Guids)
				{
					const auto found = g_poiStates->find(guid);
					matched += found != g_poiStates->end() ? 1 : 0;
					bases += found != g_poiStates->end() && found->second.IsBase ? 1 : 0;
				}
			}
			const int signature = static_cast<int>(g_poiStates->size()) * 100000 + matched * 100 + bases;
			if (signature != g_poiStatesLogged)
			{
				g_poiStatesLogged = signature;
				LOG_INFO("[InGameMap] poi states: game=%d matched_catalog=%d abandoned_bases=%d",
					static_cast<int>(g_poiStates->size()), matched, bases);
			}
		}
		std::lock_guard<std::mutex> lock(g_publishedMutex);
		g_published = std::move(next);
	}

	void ClosePanel()
	{
		IPluginHooks* hooks = GetHooks();
		if (hooks && hooks->UI && g_panel)
		{
			hooks->UI->SetPanelClose(g_panel);
			g_lastRenderUnixMs = 0;
		}
	}

	void OnTick(float deltaSeconds)
	{
		if (g_closeRequested.exchange(false))
		{
			ClosePanel();
		}
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
		const int64_t now = MapStateSdk::GetCurrentUnixTimeMilliseconds();
		if (!hooks || !hooks->UI || !g_panel || now - g_lastToggleUnixMs.load() < kToggleDebounceMs)
		{
			return;  // le rendu vient de traiter cette meme pression
		}
		g_lastToggleUnixMs = now;
		if (IsOpen())
		{
			ClosePanel();
		}
		else
		{
			hooks->UI->SetPanelOpen(g_panel);
		}
	}

	// ------------------------------------------------------------------ rendu

	enum class HoverKind { None, Cargo, Teleporter, Player, Poi, Link, Catalog, Antenna, Obelisk, BaseCore };

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

		// Icone d'item sur pastille sombre, lisible sur le terrain.
		void Icon(PluginTextureHandle icon, float sx, float sy, float half, unsigned int tint = 0xFFFFFFFFu)
		{
			Ui->DL_AddCircleFilled(DrawList, sx, sy, half + 2.0f, 0xC0101010u, 16);
			Ui->DL_AddImage(DrawList, icon, sx - half, sy - half, sx + half, sy + half, 0.0f, 0.0f, 1.0f, 1.0f, tint);
		}

		bool Visible(float sx, float sy, float margin) const
		{
			return sx >= X - margin && sx <= X + W + margin && sy >= Y - margin && sy <= Y + H + margin;
		}

		void Marker(const SDK::FVector& world, unsigned int color, float radius, int sides, HoverKind kind, size_t index, const char* label = nullptr, PluginTextureHandle icon = nullptr)
		{
			const float sx = ScreenX(world.X);
			const float sy = ScreenY(world.Y);
			if (sx < X - radius || sx > X + W + radius || sy < Y - radius || sy > Y + H + radius)
			{
				return;
			}
			if (icon)
			{
				radius += 2.0f;
				Icon(icon, sx, sy, radius, color);  // icone du jeu teintee par l'etat
			}
			else if (sides > 0)
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

	const PoiLiveState* FindPoiState(const PoiStateMap* states, const CatalogGroup& group, size_t index)
	{
		if (!states || index >= group.Guids.size())
		{
			return nullptr;
		}
		const auto found = states->find(group.Guids[index]);
		return found == states->end() ? nullptr : &found->second;
	}

	// Base abandonnee : -1 pas suivie, sinon nombre d'objectifs faits / total.
	bool BaseProgress(const PoiLiveState* state, int& done, int& total)
	{
		done = total = 0;
		for (int kind = 0; state && state->IsBase && kind < 4; ++kind)
		{
			done += state->Done[kind];
			total += state->Total[kind];
		}
		return total > 0;
	}

	// Les calques "Antenna" et "Obelisk" du catalogue pilotent aussi les structures lues en jeu.
	using ItemIconMap = std::map<std::string, PluginTextureHandle>;

	PluginTextureHandle FindIcon(const ItemIconMap& icons, const std::string& key)
	{
		const auto found = icons.find(key);
		return found == icons.end() ? nullptr : found->second;
	}

	constexpr char kAntennaGroup[] = "Antenna";
	constexpr char kObeliskGroup[] = "Obelisk";

	bool CatalogGroupEnabled(const Catalog* catalog, const char* label)
	{
		for (size_t index = 0; catalog && index < catalog->Groups.size() && index < g_catalogEnabled.size(); ++index)
		{
			if (catalog->Groups[index].Label == label)
			{
				return g_showCatalog && g_catalogEnabled[index] != 0;
			}
		}
		return true;
	}

	struct StateStyle { const char* Name; unsigned int Color; };
	StateStyle AntennaStyle(int state)
	{
		static const StateStyle styles[] = {
			{ "Unknown", 0xFF909090u }, { "Infected", 0xFFD050C0u }, { "Ready to charge", 0xFF30D0F0u }, { "Charging", 0xFF30A0FFu },
			{ "Ready to activate", 0xFFF0D040u }, { "Activated", 0xFF50E060u }, { "Locked", 0xFF707070u } };
		return styles[state >= 0 && state < 7 ? state : 0];
	}
	StateStyle ObeliskStyle(int state)
	{
		static const StateStyle styles[] = {
			{ "Disabled", 0xFF808080u }, { "Cooldown", 0xFFF0A040u }, { "Enabled", 0xFF50E060u },
			{ "Charged", 0xFF30D0F0u }, { "Attack in progress", 0xFF3030FFu } };
		return state >= 0 && state < 5 ? styles[state] : StateStyle{ "State unknown", 0xFFC0C0C0u };
	}

	// Charge d'un obelisque en pourcentage du seuil d'attaque ; -1 si la charge ou le seuil manque.
	int ObeliskChargePercent(const LiveObelisk& obelisk)
	{
		return obelisk.Charge < 0.0f || obelisk.ChargeMax <= 0.0f
			? -1 : static_cast<int>(std::lround(std::clamp(obelisk.Charge / obelisk.ChargeMax, 0.0f, 1.0f) * 100.0f));
	}

	void DrawLiveStructures(Canvas& canvas, const LiveStructures& live, const Catalog* catalog, const ItemIconMap& icons)
	{
		const PluginTextureHandle antennaIcon = FindIcon(icons, "marker:antenna");
		const PluginTextureHandle obeliskIcon = FindIcon(icons, "marker:obelisk");
		const bool showLabels = g_pixelsPerUnit >= 0.0025f;
		if (g_showBaseCores)
		{
			const PluginTextureHandle coreIcon = FindIcon(icons, "marker:baseCore");
			for (size_t index = 0; index < live.BaseCores.size(); ++index)
			{
				const LiveBaseCore& core = live.BaseCores[index];
				char label[96];
				if (core.Level >= 0)
				{
					std::snprintf(label, sizeof(label), "%s (lvl %d)", core.Name.c_str(), core.Level);
				}
				else
				{
					std::snprintf(label, sizeof(label), "%s", core.Name.c_str());
				}
				canvas.Marker(SDK::FVector{ core.X, core.Y, 0.0 }, core.Infected ? 0xFF3030FFu : kColorBaseCore, 7.0f, 4,
					HoverKind::BaseCore, index, showLabels ? label : nullptr, coreIcon);
			}
		}
		if (CatalogGroupEnabled(catalog, kAntennaGroup))
		{
			for (size_t index = 0; index < live.Antennas.size(); ++index)
			{
				const LiveAntenna& antenna = live.Antennas[index];
				canvas.Marker(SDK::FVector{ antenna.X, antenna.Y, 0.0 }, AntennaStyle(antenna.State).Color, 6.5f, 3, HoverKind::Antenna, index, nullptr, antennaIcon);
			}
		}
		if (CatalogGroupEnabled(catalog, kObeliskGroup))
		{
			for (size_t index = 0; index < live.Obelisks.size(); ++index)
			{
				const LiveObelisk& obelisk = live.Obelisks[index];
				if (obelisk.HasAttack)
				{
					// Cible de l'attaque : trait rouge de l'obelisque a la base visee.
					canvas.Ui->DL_AddLine(canvas.DrawList, canvas.ScreenX(obelisk.X), canvas.ScreenY(obelisk.Y),
						canvas.ScreenX(obelisk.AttackX), canvas.ScreenY(obelisk.AttackY), 0xC03030FFu, 2.0f);
				}
				char label[16] = "";
				const int percent = ObeliskChargePercent(obelisk);
				if (showLabels && percent >= 0)
				{
					std::snprintf(label, sizeof(label), "%d%%", percent);
				}
				canvas.Marker(SDK::FVector{ obelisk.X, obelisk.Y, 0.0 }, ObeliskStyle(obelisk.State).Color, 7.0f, 6, HoverKind::Obelisk, index, label, obeliskIcon);
			}
		}
	}

	void DrawCatalog(Canvas& canvas, const Catalog& catalog, const std::vector<PluginTextureHandle>& icons, const PoiStateMap* states, bool liveAntennas, const ItemIconMap& markerIcons)
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
			if (liveAntennas && group.Label == kAntennaGroup)
			{
				continue;  // remplacees par les antennes lues en jeu, avec leur etat
			}
			const bool detailed = group.Category <= CatalogCategory::Mineral;  // peu de points : contour + forme
			const float half = group.Category == CatalogCategory::Plant ? 1.5f : 2.5f;
			PluginTextureHandle icon = groupIndex < icons.size() ? icons[groupIndex] : nullptr;
			PluginTextureHandle poiIcon = nullptr;
			if (group.Category == CatalogCategory::Poi)
			{
				// "Abandoned base" -> "marker:abandonedBase" : meme nom que la texture du jeu.
				std::string key = "marker:";
				bool upper = false;
				for (const char c : group.Label)
				{
					if (c == ' ') { upper = true; continue; }
					key += static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
					upper = false;
				}
				poiIcon = FindIcon(markerIcons, key);
			}
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
					// Decouvert en jeu : plein. Pas encore decouvert (ou etat inconnu) : attenue.
					const PoiLiveState* state = FindPoiState(states, group, index);
					const bool discovered = state && state->State == 2;
					if (poiIcon)
					{
						canvas.Icon(poiIcon, sx, sy, 8.0f, discovered ? 0xFFFFFFFFu : 0x80FFFFFFu);
					}
					else
					{
						canvas.Ui->DL_AddNgonFilled(canvas.DrawList, sx, sy, 6.0f, discovered ? color : (color & 0x00FFFFFFu) | 0x70000000u, 5);
						canvas.Ui->DL_AddNgon(canvas.DrawList, sx, sy, 6.0f, 0xFF000000u, 5, 1.0f);
					}
					int done = 0, total = 0;
					if (BaseProgress(state, done, total))
					{
						// Base abandonnee : anneau vert = tout fait, orange = en cours, gris = rien.
						canvas.Ui->DL_AddCircle(canvas.DrawList, sx, sy, poiIcon ? 11.5f : 9.5f,
							done >= total ? 0xFF50E060u : done > 0 ? 0xFF30A0FFu : 0xFF909090u, 20, 2.0f);
					}
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


	bool IsRuptureResource(PoiKind kind)
	{
		return kind == PoiKind::Ignitium || kind == PoiKind::StarTears;
	}

	// Star Tears projete par le plugin depuis un site Ignitium (game-context.md) :
	// une estimation, pas l'observation d'un acteur.
	bool IsEstimated(const MapStateRuntime::Detail::PoiMarker& poi)
	{
		return poi.Source.rfind("rupture_phase.", 0) == 0;
	}

	// Phase de transition (Arcadia stabilizing) : le jeu fait deja exister l'acteur
	// Star Tears sur le site d'un Ignitium encore intact. L'item du moment est
	// l'Ignitium : le Star Tears du meme site (300 cm, comme la capture) est masque.
	bool HiddenBehindIgnitium(const CargoSnapshot& snapshot, const MapStateRuntime::Detail::PoiMarker& poi)
	{
		if (poi.Kind != PoiKind::StarTears)
		{
			return false;
		}
		for (const auto& other : snapshot.Pois)
		{
			const double dx = other.WorldLocation.X - poi.WorldLocation.X, dy = other.WorldLocation.Y - poi.WorldLocation.Y;
			if (other.Kind == PoiKind::Ignitium && !other.Depleted && dx * dx + dy * dy <= 300.0 * 300.0)
			{
				return true;
			}
		}
		return false;
	}

	void DrawOverlays(Canvas& canvas, const CargoSnapshot& snapshot, const ItemIconMap& icons)
	{
		// Noms des teleporteurs affiches en permanence a partir de ~1 px pour 4 m.
		const bool showLabels = g_pixelsPerUnit >= 0.0025f;

		for (size_t index = 0; index < snapshot.Pois.size(); ++index)
		{
			const auto& poi = snapshot.Pois[index];
			// Bases et plantes observees : deja couvertes par le catalogue.
			if (!IsRuptureResource(poi.Kind) || HiddenBehindIgnitium(snapshot, poi))
			{
				continue;
			}
			const bool ignitium = poi.Kind == PoiKind::Ignitium;
			if (!(ignitium ? g_showIgnitium : g_showStarTears))
			{
				continue;
			}
			PluginTextureHandle icon = FindIcon(icons, ignitium ? kIgnitiumIconKey : kStarTearsIconKey);
			if (!icon)
			{
				canvas.Marker(poi.WorldLocation, PoiColor(poi), 5.0f, 6, HoverKind::Poi, index);
				continue;
			}
			const float sx = canvas.ScreenX(poi.WorldLocation.X), sy = canvas.ScreenY(poi.WorldLocation.Y);
			if (!canvas.Visible(sx, sy, 12.0f))
			{
				continue;
			}
			// Epuise : presque efface. Present mais pas recoltable : attenue.
			const unsigned int tint = poi.Depleted ? 0x50FFFFFFu
				: poi.Harvestability == MapResources::Harvestability::Available ? 0xFFFFFFFFu : 0xA0FFFFFFu;
			canvas.Icon(icon, sx, sy, 10.0f, tint);
			if (IsEstimated(poi))
			{
				canvas.Ui->DL_AddCircle(canvas.DrawList, sx, sy, 13.0f, 0xFF40D0FFu, 16, 1.5f);
			}
			const float dx = sx - canvas.MouseX, dy = sy - canvas.MouseY;
			canvas.TryHover(HoverKind::Poi, index, dx * dx + dy * dy);
		}
		const bool hasSelection = !g_selectedCargoKey.empty();
		// Filtre par item : seuls les liens des items coches restent, avec leurs deux cargos.
		const bool itemFilter = !g_cargoItemFilter.empty();
		const auto linkShown = [&](const auto& connection)
		{
			return !itemFilter || g_cargoItemFilter.contains(connection.ItemDisplayName);
		};
		std::set<std::string_view> linkedCargo;  // cargos ayant au moins un lien affiche
		for (const auto& connection : snapshot.Connections)
		{
			if (linkShown(connection))
			{
				linkedCargo.insert(connection.SenderKey);
				linkedCargo.insert(connection.ReceiverKey);
			}
		}
		if (g_showConnections)
		{
			for (size_t index = 0; index < snapshot.Connections.size(); ++index)
			{
				const auto& connection = snapshot.Connections[index];
				if (!linkShown(connection))
				{
					continue;
				}
				const float ax = canvas.ScreenX(connection.SenderWorldLocation.X), ay = canvas.ScreenY(connection.SenderWorldLocation.Y);
				const float bx = canvas.ScreenX(connection.ReceiverWorldLocation.X), by = canvas.ScreenY(connection.ReceiverWorldLocation.Y);
				// Cargo selectionne : ses liens ressortent, les autres s'effacent.
				const bool selected = hasSelection
					&& (connection.SenderKey == g_selectedCargoKey || connection.ReceiverKey == g_selectedCargoKey);
				canvas.Ui->DL_AddLine(canvas.DrawList, ax, ay, bx, by,
					!hasSelection ? kColorLink : selected ? 0xFF00D7FFu : 0x2800D7FFu, selected ? 3.0f : 1.5f);
				if (hasSelection && !selected)
				{
					continue;  // un lien efface ne capte pas le survol
				}
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
				// Sans lien affiche : masque par le filtre d'item, ou par la case "Unconnected".
				if (!linkedCargo.contains(marker.PublicKey) && (itemFilter || !g_showUnconnectedCargo))
				{
					continue;
				}
				if (hasSelection && marker.PublicKey == g_selectedCargoKey)
				{
					canvas.Ui->DL_AddCircle(canvas.DrawList, canvas.ScreenX(marker.WorldLocation.X), canvas.ScreenY(marker.WorldLocation.Y),
						9.0f, 0xFFFFFFFFu, 20, 2.0f);
				}
				canvas.Marker(marker.WorldLocation, marker.Kind == CargoKind::Sender ? kColorSender : kColorReceiver, 4.5f, 0, HoverKind::Cargo, index);
			}
		}
		if (g_showTeleporters)
		{
			for (size_t index = 0; index < snapshot.Teleporters.size(); ++index)
			{
				const auto& teleporter = snapshot.Teleporters[index];
				canvas.Marker(teleporter.WorldLocation, kColorTeleporter, 6.5f, 4, HoverKind::Teleporter, index,
					showLabels ? teleporter.DisplayName.c_str() : nullptr, FindIcon(icons, "marker:teleporter"));
			}
		}
		if (g_showPlayers)
		{
			for (size_t index = 0; index < snapshot.Players.size(); ++index)
			{
				const auto& player = snapshot.Players[index];
				canvas.Marker(player.WorldLocation, player.IsSelf ? kColorSelf : kColorPlayer, player.IsSelf ? 7.0f : 5.5f, 3,
					HoverKind::Player, index, player.DisplayName.c_str(), FindIcon(icons, "marker:player"));
			}
		}
	}

	std::string BuildTooltip(const Canvas& canvas, const CargoSnapshot& snapshot, const Catalog* catalog, const PoiStateMap* states, const LiveStructures* live)
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
				place(marker.Kind == CargoKind::Sender ? "Dispatcher" : "Receiver", marker.DisplayName, marker.WorldLocation);
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
				if (IsRuptureResource(poi.Kind))
				{
					std::snprintf(text, sizeof(text), "%s: %s%s\nX %.0f  Y %.0f  Z %.0f",
						poi.Kind == PoiKind::Ignitium ? "Ignitium" : "Star Tears",
						MapResources::StateName(poi.Depleted, poi.Harvestability),
						IsEstimated(poi) ? " (estimated from an Ignitium site)" : "",
						poi.WorldLocation.X, poi.WorldLocation.Y, poi.WorldLocation.Z);
					break;
				}
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
					std::string lines = text;
					if (const PoiLiveState* state = FindPoiState(states, group, canvas.HoverIndex))
					{
						static const char* const stateNames[] = { "Hidden", "Not discovered", "Discovered", "Disabled" };
						if (state->State >= 0 && state->State < 4)
						{
							lines += std::string("\n") + stateNames[state->State];
						}
						static const char* const kinds[] = { "Items found", "Blueprint chests", "Story items", "Infection cysts" };
						for (int kind = 0; state->IsBase && kind < 4; ++kind)
						{
							if (state->Total[kind] > 0)
							{
								lines += std::string("\n") + kinds[kind] + ": " + std::to_string(state->Done[kind]) + "/" + std::to_string(state->Total[kind]);
							}
						}
					}
					else if (states && !states->empty())
					{
						lines += "\nNo state from the game";
					}
					return lines;
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
		case HoverKind::BaseCore:
			if (live && canvas.HoverIndex < live->BaseCores.size())
			{
				const LiveBaseCore& core = live->BaseCores[canvas.HoverIndex];
				if (core.Level >= 0)
				{
					std::snprintf(text, sizeof(text), "Base core: %s\nLevel %d%s\nCooling %.0f / %.0f used\nX %.0f  Y %.0f", core.Name.c_str(),
						core.Level, core.Infected ? " - INFECTED" : "", core.CoolingUsed, core.Cooling, core.X, core.Y);
				}
				else
				{
					std::snprintf(text, sizeof(text), "Base core: %s\nLevel not exposed by the game\nX %.0f  Y %.0f", core.Name.c_str(), core.X, core.Y);
				}
			}
			break;
		case HoverKind::Antenna:
			if (live && canvas.HoverIndex < live->Antennas.size())
			{
				const LiveAntenna& antenna = live->Antennas[canvas.HoverIndex];
				std::snprintf(text, sizeof(text), "Antenna (geo scanner): %s\nX %.0f  Y %.0f", AntennaStyle(antenna.State).Name, antenna.X, antenna.Y);
			}
			break;
		case HoverKind::Obelisk:
			if (live && canvas.HoverIndex < live->Obelisks.size())
			{
				const LiveObelisk& obelisk = live->Obelisks[canvas.HoverIndex];
				const int percent = ObeliskChargePercent(obelisk);
				char charge[64] = "Charge: not exposed by the game here";
				if (percent >= 0)
				{
					std::snprintf(charge, sizeof(charge), "Charge %d%% (%.1f / %.1f)", percent, obelisk.Charge, obelisk.ChargeMax);
				}
				else if (obelisk.Charge >= 0.0f)
				{
					std::snprintf(charge, sizeof(charge), "Charge %.1f (attack threshold unknown)", obelisk.Charge);
				}
				std::snprintf(text, sizeof(text), "Monolith: %s\n%s\nX %.0f  Y %.0f", ObeliskStyle(obelisk.State).Name, charge, obelisk.X, obelisk.Y);
			}
			break;
		case HoverKind::None:
			break;
		}
		return text;
	}

	// Infobulle d'un cargo ou d'un lien : une ligne par item en transit, avec son
	// icone. Seul endroit ou les icones d'items des cargos sont affichees.
	bool RenderCargoTooltip(IModLoaderImGui* ui, const Canvas& canvas, const Published& published)
	{
		const CargoSnapshot& snapshot = published.Snapshot;
		const bool link = canvas.Hover == HoverKind::Link && canvas.HoverIndex < snapshot.Connections.size();
		const bool cargo = canvas.Hover == HoverKind::Cargo && canvas.HoverIndex < snapshot.Markers.size();
		if ((!link && !cargo) || !ui->BeginTooltip())
		{
			return link || cargo;
		}
		IPluginImGuiTextures* textures = Textures();
		const float iconSize = ui->GetFrameHeight();
		char text[320];
		const auto row = [&](const MapStateRuntime::Detail::CargoConnection& connection, const char* arrow, const std::string& other)
		{
			if (PluginTextureHandle icon = FindIcon(published.ItemIcons, connection.ItemDisplayName); icon && textures && textures->Image)
			{
				textures->Image(icon, iconSize, iconSize);
				ui->SameLine(0.0f, 6.0f);
			}
			ui->AlignTextToFramePadding();
			std::snprintf(text, sizeof(text), "%s x%d%s%s", connection.ItemDisplayName.c_str(), connection.RequestedAmount, arrow, other.c_str());
			ui->Text(text);
		};
		if (link)
		{
			const auto& connection = snapshot.Connections[canvas.HoverIndex];
			row(connection, "", std::string());
			std::snprintf(text, sizeof(text), "%s  ->  %s", connection.SenderLabel.c_str(), connection.ReceiverLabel.c_str());
			ui->Text(text);
		}
		else
		{
			const auto& marker = snapshot.Markers[canvas.HoverIndex];
			const bool sender = marker.Kind == CargoKind::Sender;
			std::snprintf(text, sizeof(text), "%s: %s", sender ? "Dispatcher" : "Receiver", marker.DisplayName.c_str());
			ui->Text(text);
			int rows = 0;
			for (const auto& connection : snapshot.Connections)
			{
				if ((sender ? connection.SenderKey : connection.ReceiverKey) == marker.PublicKey && rows++ < 12)
				{
					row(connection, sender ? "  ->  " : "  <-  ", sender ? connection.ReceiverLabel : connection.SenderLabel);
				}
			}
			if (rows == 0)
			{
				ui->TextDisabled(marker.ResourceSummary.empty() ? "No item in transit" : marker.ResourceSummary.c_str());
			}
			std::snprintf(text, sizeof(text), "X %.0f  Y %.0f  Z %.0f", marker.WorldLocation.X, marker.WorldLocation.Y, marker.WorldLocation.Z);
			ui->TextDisabled(text);
			ui->TextDisabled(marker.PublicKey == g_selectedCargoKey ? "Click to clear the selection" : "Click to isolate its links");
		}
		ui->EndTooltip();
		return true;
	}

	// Bandeau rupture en haut de la carte, repris de la "rupture-strip" du viewer
	// web (MapRupturePanel.vue, useRuptureTimeline.ts) : phase courante, piste des
	// cinq phases avec curseur, temps restant. La phase se deduit du temps ecoule
	// dans le cycle, car wave/stage/step valent "None" en dehors d'une vague.
	// Durees calibrees par observation, identiques a BuildRuptureCycleJson.
	void DrawRuptureStrip(Canvas& canvas, const CargoSnapshot& snapshot, const ItemIconMap& icons)
	{
		// Resource : ce que la capture publie pendant la phase (ApplyRuptureResourcePhase,
		// game-context.md) : 0 = Ignitium, 1 = transition (les deux), 2 = Star Tears.
		struct Phase { const char* Name; double Seconds; unsigned int Color; int Resource; };
		static const Phase phases[] = {
			{ "Arcadia burning", 30.0, 0x007171F8u, 0 },
			{ "Arcadia cooling", 60.0, 0x000B9EF5u, 0 },
			{ "Arcadia stabilizing", 600.0, 0x00EBE7E5u, 1 },
			{ "Arcadia stable", 2430.0, 0x008DC431u, 2 },
			{ "Rupture incoming", 120.0, 0x00F755A8u, 2 },
		};
		constexpr double kCycleSeconds = 3240.0;
		constexpr float kIncomingMinWidth = 0.02f;  // comme le viewer : la derniere phase reste visible

		const auto& rupture = snapshot.RuptureCycle;
		if (!rupture.Available)
		{
			return;
		}
		size_t current = 3;
		double position = -1.0;
		double phaseStart = 0.0;
		if (rupture.HasElapsed)
		{
			double elapsed = rupture.ElapsedSeconds;
			if (rupture.HasObservedAtUnixMs)
			{
				elapsed += std::max(0.0, static_cast<double>(MapStateSdk::GetCurrentUnixTimeMilliseconds() - rupture.ObservedAtUnixMs) / 1000.0);
			}
			position = std::fmod(elapsed, kCycleSeconds);
			position = position < 0.0 ? position + kCycleSeconds : position;
			current = 0;
			while (current + 1 < std::size(phases) && position >= phaseStart + phases[current].Seconds)
			{
				phaseStart += phases[current++].Seconds;
			}
		}
		else if (rupture.Stage == "Moving" || (rupture.Stage == "Fadeout" && rupture.Step == "FireWave")) current = 0;
		else if (rupture.Stage == "Fadeout") current = 1;
		else if (rupture.Stage == "Growback") current = 2;

		const float w = std::min(560.0f, canvas.W - 16.0f), h = 62.0f;
		if (w < 260.0f)
		{
			return;
		}
		const float x0 = canvas.X + (canvas.W - w) * 0.5f, y0 = canvas.Y + 8.0f;
		const unsigned int color = phases[current].Color | 0xFF000000u;
		canvas.Ui->DL_AddRectFilled(canvas.DrawList, x0, y0, x0 + w, y0 + h, 0xE0101010u, 5.0f, 0);
		canvas.Ui->DL_AddRect(canvas.DrawList, x0, y0, x0 + w, y0 + h, 0x60FFFFFFu, 5.0f, 0, 1.0f);

		char text[96];
		canvas.Ui->DL_AddText(canvas.DrawList, x0 + 10.0f, y0 + 6.0f, color, phases[current].Name);
		if (position >= 0.0)
		{
			const int phaseLeft = static_cast<int>(phaseStart + phases[current].Seconds - position);
			const int cycleLeft = static_cast<int>(kCycleSeconds - position);
			std::snprintf(text, sizeof(text), "%02d:%02d left   |   next rupture %02d:%02d", phaseLeft / 60, phaseLeft % 60, cycleLeft / 60, cycleLeft % 60);
			float textW = 0.0f, textH = 0.0f;
			canvas.Ui->CalcTextSize(text, &textW, &textH, false, -1.0f);
			canvas.Ui->DL_AddText(canvas.DrawList, x0 + w - 10.0f - textW, y0 + 6.0f, 0xFFFFFFFFu, text);
		}

		// Piste : largeur proportionnelle a la duree, minimum pour "Rupture incoming"
		// repris sur "Arcadia stable".
		const float trackX = x0 + 10.0f, trackW = w - 20.0f, trackY = y0 + 26.0f, trackH = 10.0f;
		const float incoming = std::max(static_cast<float>(phases[4].Seconds / kCycleSeconds), kIncomingMinWidth);
		float cursor = 0.0f, markerAt = -1.0f;
		for (size_t index = 0; index < std::size(phases); ++index)
		{
			const float real = static_cast<float>(phases[index].Seconds / kCycleSeconds);
			const float width = index == 4 ? incoming : index == 3 ? real - (incoming - static_cast<float>(phases[4].Seconds / kCycleSeconds)) : real;
			canvas.Ui->DL_AddRectFilled(canvas.DrawList, trackX + trackW * cursor, trackY, trackX + trackW * (cursor + width) - 1.0f, trackY + trackH,
				phases[index].Color | (index == current ? 0xFF000000u : 0x70000000u), 0.0f, 0);
			if (index == current && position >= 0.0)
			{
				markerAt = cursor + width * static_cast<float>((position - phaseStart) / phases[index].Seconds);
			}
			cursor += width;
		}
		if (markerAt >= 0.0f)
		{
			const float mx = trackX + trackW * markerAt;
			canvas.Ui->DL_AddLine(canvas.DrawList, mx, trackY - 3.0f, mx, trackY + trackH + 3.0f, 0xFFFFFFFFu, 2.0f);
		}

		// Ressource de rupture presente sur la carte pendant cette phase.
		float lineX = x0 + 10.0f;
		const float lineY = y0 + 42.0f;
		const auto resource = [&](const char* name, const char* iconKey)
		{
			if (PluginTextureHandle icon = FindIcon(icons, iconKey))
			{
				canvas.Ui->DL_AddImage(canvas.DrawList, icon, lineX, lineY - 1.0f, lineX + 16.0f, lineY + 15.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
				lineX += 20.0f;
			}
			float textW = 0.0f, textH = 0.0f;
			canvas.Ui->CalcTextSize(name, &textW, &textH, false, -1.0f);
			canvas.Ui->DL_AddText(canvas.DrawList, lineX, lineY, 0xFFE0E0E0u, name);
			lineX += textW + 10.0f;
		};
		if (phases[current].Resource <= 1) resource(phases[current].Resource == 1 ? "Ignitium  ->" : "Ignitium", kIgnitiumIconKey);
		if (phases[current].Resource >= 1) resource("Star Tears", kStarTearsIconKey);
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
		LayerCheckbox(ui, "Teleporters", &g_showTeleporters, kColorTeleporter, 4);
		LayerCheckbox(ui, "Players", &g_showPlayers, kColorPlayer, 3);
		LayerCheckbox(ui, "Base cores", &g_showBaseCores, kColorBaseCore, 4);
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

	// Section cargo du volet : calques, cargos sans lien, et filtre par item transporte.
	void RenderCargoFilters(IModLoaderImGui* ui, const Published* published)
	{
		ui->SeparatorText("Cargo");
		LayerCheckbox(ui, "Cargo", &g_showCargo, kColorSender, 0);
		LayerCheckbox(ui, "Links", &g_showConnections, kColorLink, 0);
		ui->Dummy(0.0f, 0.0f);  // LayerCheckbox reste sur la ligne : on la termine
		ui->Checkbox("Unconnected cargo", &g_showUnconnectedCargo);

		std::map<std::string, int> items;  // item -> nombre de liens
		for (size_t index = 0; published && index < published->Snapshot.Connections.size(); ++index)
		{
			++items[published->Snapshot.Connections[index].ItemDisplayName];
		}
		// Un item coche qui ne transite plus reste liste, pour pouvoir le decocher.
		for (const std::string& item : g_cargoItemFilter)
		{
			items.try_emplace(item, 0);
		}
		const auto lower = [](std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		};
		ui->InputTextWithHint("##cargo_item_search", "Search item...", g_cargoItemSearch, sizeof(g_cargoItemSearch));
		if (!g_cargoItemFilter.empty())
		{
			ui->SameLine(0.0f, 4.0f);
			if (ui->SmallButton("All"))
			{
				g_cargoItemFilter.clear();
			}
		}
		const std::string needle = lower(g_cargoItemSearch);
		ui->BeginChild("##cargo_items", 0.0f, 150.0f, true);
		if (items.empty())
		{
			ui->TextDisabled("No item in transit");
		}
		for (const auto& [item, count] : items)
		{
			if (!needle.empty() && lower(item).find(needle) == std::string::npos)
			{
				continue;
			}
			char label[160];
			std::snprintf(label, sizeof(label), "%s (%d)##cargo_item_%s", item.empty() ? "(unknown item)" : item.c_str(), count, item.c_str());
			bool checked = g_cargoItemFilter.contains(item);
			const bool before = checked;
			if (PluginTextureHandle icon = published ? FindIcon(published->ItemIcons, item) : nullptr)
			{
				IconCheckbox(ui, label, &checked, icon);
			}
			else
			{
				ui->Checkbox(label, &checked);
			}
			if (checked != before)
			{
				checked ? static_cast<void>(g_cargoItemFilter.insert(item)) : static_cast<void>(g_cargoItemFilter.erase(item));
			}
		}
		ui->EndChild();
	}

	void RenderFilters(IModLoaderImGui* ui, const Published* published)
	{
		static const std::vector<PluginTextureHandle> noIcons;
		const Catalog* catalog = published ? published->StaticCatalog.get() : nullptr;
		const std::vector<PluginTextureHandle>& icons = published ? published->CatalogIcons : noIcons;

		ui->BeginChild("##ingame_map_filters", 250.0f, 0.0f, true);
		RenderCargoFilters(ui, published);
		// Ressources du cycle de rupture : observees en jeu, jamais dans le catalogue.
		ui->SeparatorText("Rupture resources");
		const auto ruptureFilter = [&](const char* name, const char* iconKey, int count, bool* value, unsigned int color)
		{
			char label[64];
			std::snprintf(label, sizeof(label), "%s (%d)", name, count);
			if (PluginTextureHandle icon = published ? FindIcon(published->ItemIcons, iconKey) : nullptr)
			{
				IconCheckbox(ui, label, value, icon);
			}
			else
			{
				LayerCheckbox(ui, label, value, color, 6);
				ui->Dummy(0.0f, 0.0f);
			}
		};
		// Compteurs = ce qui est dessine (un Star Tears masque par son Ignitium ne compte pas).
		int ignitium = 0, starTears = 0;
		for (size_t index = 0; published && index < published->Snapshot.Pois.size(); ++index)
		{
			const auto& poi = published->Snapshot.Pois[index];
			ignitium += poi.Kind == PoiKind::Ignitium ? 1 : 0;
			starTears += poi.Kind == PoiKind::StarTears && !HiddenBehindIgnitium(published->Snapshot, poi) ? 1 : 0;
		}
		ruptureFilter("Ignitium", kIgnitiumIconKey, ignitium, &g_showIgnitium, 0xFF3060FFu);
		ruptureFilter("Star Tears", kStarTearsIconKey, starTears, &g_showStarTears, 0xFFFFE080u);
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
			RenderFilters(ui, published.get());
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
			g_canvasDragged = g_canvasDragged || dx != 0.0f || dy != 0.0f;
		}
		const bool clicked = canvas.Hovered && ui->IsMouseReleased(0) && !g_canvasDragged;
		if (ui->IsMouseClicked(0, false))
		{
			g_canvasDragged = false;
		}

		// Fermeture au clavier : Echap, ou la touche bindee quand le keybind du
		// loader ne la voit pas (panneau au premier plan). Fermeture sur le game thread.
		IPluginHooks* hooks = GetHooks();
		const bool toggleHeld = hooks && hooks->Input && hooks->Input->IsKeyHeld && !g_toggleKey.empty()
			&& hooks->Input->IsKeyHeld(g_toggleKey.c_str());
		const int64_t now = g_lastRenderUnixMs.load();
		const bool togglePressed = toggleHeld && !g_toggleKeyWasHeld && now - g_lastToggleUnixMs.load() >= kToggleDebounceMs;
		g_toggleKeyWasHeld = toggleHeld;
		constexpr int kVkEscape = 0x1B, kFocusedRootAndChildWindows = 3;
		if (togglePressed || ((canvas.Hovered || ui->IsWindowFocused(kFocusedRootAndChildWindows)) && ui->IsKeyPressed(kVkEscape, false)))
		{
			g_lastToggleUnixMs = now;
			g_closeRequested = true;
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
				DrawCatalog(canvas, *published->StaticCatalog, published->CatalogIcons, published->PoiStates.get(),
					published->Live && !published->Live->Antennas.empty(), published->ItemIcons);
			}
			if (published->Live)
			{
				DrawLiveStructures(canvas, *published->Live, published->StaticCatalog.get(), published->ItemIcons);
			}
			DrawOverlays(canvas, published->Snapshot, published->ItemIcons);
			DrawRuptureStrip(canvas, published->Snapshot, published->ItemIcons);
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

		if (published && clicked)
		{
			// Clic sur un cargo : isole ses liens. Re-clic ou clic dans le vide : retour a la normale.
			const std::string key = canvas.Hover == HoverKind::Cargo && canvas.HoverIndex < published->Snapshot.Markers.size()
				? published->Snapshot.Markers[canvas.HoverIndex].PublicKey : std::string();
			g_selectedCargoKey = key == g_selectedCargoKey ? std::string() : key;
		}
		if (published && canvas.Hover != HoverKind::None && !RenderCargoTooltip(ui, canvas, *published))
		{
			const std::string tooltip = BuildTooltip(canvas, published->Snapshot, published->StaticCatalog.get(), published->PoiStates.get(), published->Live.get());
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
		FreeNamedIcons();
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
