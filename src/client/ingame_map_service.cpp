#include "ingame_map_internal.h"

// Game thread : chargement des textures, capture de l'etat du jeu et publication
// de l'instantane immuable lu par le rendu.

namespace MapExtensionClient::InGameMap
{
namespace
{
	float g_serviceAccumulator = kServiceIntervalSeconds;
	bool g_terrainLogged = false;
	int g_lastRadiationLevel = -2;

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

	// Catalogue statique : charge en tache de fond, publie par le game thread.
	std::future<std::shared_ptr<const Catalog>> g_catalogFuture;
	std::shared_ptr<const Catalog> g_catalog;
	std::vector<PluginTextureHandle> g_catalogIcons;
	size_t g_catalogIconsNext = 0;
	std::map<std::string, PluginTextureHandle> g_namedIcons;  // nullptr = introuvable pour l'instant
	float g_namedIconRetryClock = 0.0f;
	// Etat des points d'interet : relu toutes les 2 s (4 passes de service).
	std::shared_ptr<const PoiStateMap> g_poiStates;
	int g_poiStatePass = 0;
	int g_poiStatesLogged = -1;
	std::shared_ptr<const LiveStructures> g_live;  // relu toutes les 4 s (parcours d'acteurs)
	int g_liveLogged = -1;

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
			else if (IPluginImGuiTextures* textures = Textures())
			{
				// Taille copiee : une icone prise avant la fin du streaming sort en petit mip.
				int width = 0, height = 0;
				textures->GetSize(g_catalogIcons[g_catalogIconsNext], &width, &height);
				LOG_INFO("[InGameMap] item icon %s: %dx%d", group.Label.c_str(), width, height);
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
				int width = 0, height = 0;
				if (IPluginImGuiTextures* textures = Textures(); icon && textures)
				{
					textures->GetSize(icon, &width, &height);
				}
				LOG_INFO("[InGameMap] marker icon %s: %s (waits=%d, %dx%d)", asset.c_str(), state.c_str(), waits[key], width, height);
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
		// Les 36 tuiles d'un coup (~70 a 90 ms mesures en Release, 3 fois par arrivee
		// en monde). Etaler la copie a ete essaye puis retire : l'apercu mettait
		// 5,5 s a apparaitre, juge pire que l'a-coup par l'utilisateur.
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
						+ live->BuildingMarkers * 7 + static_cast<int>(live->BaseCores.size()) * 13 + live->BaseCoresJoined * 17 + live->BaseCoresNamed * 19;
					if (signature != g_liveLogged)
					{
						g_liveLogged = signature;
						const LiveAntenna first = live->Antennas.empty() ? LiveAntenna{} : live->Antennas.front();
						const LiveObelisk obelisk = live->Obelisks.empty() ? LiveObelisk{} : live->Obelisks.front();
						LOG_INFO("[InGameMap] live structures: antennas=%d (first x=%.0f y=%.0f state=%d) obelisks native=%d (entities=%d) map_markers=%d replicated=%d actors=%d (first x=%.0f y=%.0f state=%d charge=%.3f max=%.3f) scan_ms=%.1f",
							static_cast<int>(live->Antennas.size()), first.X, first.Y, first.State,
							live->ObeliskNative, live->EntitiesScanned, live->ObeliskMarkers, live->ObeliskReplicated, live->ObeliskActors,
							obelisk.X, obelisk.Y, obelisk.State, obelisk.Charge, obelisk.ChargeMax, MillisecondsSince(started));
						LOG_INFO("[InGameMap] building markers: %d, base cores=%d (native=%d), core data=%d joined=%d named=%d/%d (first level=%d), unique names: %s",
							live->BuildingMarkers, static_cast<int>(live->BaseCores.size()), live->BaseCoresNative, live->BaseCoreData, live->BaseCoresJoined,
							live->BaseCoresNamed, live->CustomNames,
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
		const auto started = std::chrono::steady_clock::now();
		Service();

		// Releve de performance toutes les 30 s d'ouverture, si Diagnostics.LogRefreshTimings=1.
		static int passes = 0;
		static double sumMs = 0.0, maxMs = 0.0;
		const double ms = MillisecondsSince(started);
		sumMs += ms;
		maxMs = (std::max)(maxMs, ms);
		if (++passes >= 60)
		{
			const int64_t frames = g_renderFrames.exchange(0);
			const int64_t renderSumUs = g_renderSumUs.exchange(0);
			const int64_t renderMaxUs = g_renderMaxUs.exchange(0);
			if (MapExtensionPluginConfig::Config::LogRefreshTimings())
			{
				LOG_INFO("[InGameMap] timings over %d passes: service avg=%.2f ms max=%.2f ms | render frames=%lld avg=%.3f ms max=%.3f ms",
					passes, sumMs / passes, maxMs, static_cast<long long>(frames),
					frames > 0 ? static_cast<double>(renderSumUs) / 1000.0 / static_cast<double>(frames) : 0.0,
					static_cast<double>(renderMaxUs) / 1000.0);
			}
			passes = 0;
			sumMs = maxMs = 0.0;
		}
	}

	// Filet si EngineTick ne tourne pas (cf. AGENTS.md) : au moins une publication par monde.
	void OnAnyWorldBeginPlay(SDK::UWorld*, const char* worldName)
	{
		if (MapStateSdk::IsChimeraWorldName(worldName))
		{
			// Nouveau monde : textures rechargees par le jeu, on rejoue la sequence d'apercu.
			g_previewClock = 0.0f;
			g_previewLoadsDone = 0;
			// Pendant le chargement du monde : l'a-coup des premieres resolutions n'est pas visible.
			static bool warmedUp = false;
			if (!warmedUp)
			{
				warmedUp = true;
				WarmUpLiveState();
			}
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

	void StartService()
	{
		g_catalogFuture = std::async(std::launch::async, LoadCatalog, DefaultCatalogDirectory());
	}

	void StopService()
	{
		// ponytail: g_tiles appartient au game thread et l'arret peut venir du thread
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
	}
}
