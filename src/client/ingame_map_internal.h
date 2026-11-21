#pragma once

// Interne a la carte en jeu : etat partage entre le service (game thread,
// ingame_map_service.cpp), le rendu (render thread, ingame_map_render.cpp) et
// l'activation (ingame_map_ui.cpp). Ne pas inclure ailleurs.

#include "ingame_map_catalog.h"
#include "ingame_map_live_state.h"
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
	using MapStateRuntime::Detail::CargoKind;
	using MapStateRuntime::Detail::CargoSnapshot;
	using MapStateRuntime::Detail::PoiKind;

	// Orientation prouvee en jeu (journal, 2026-11-14) : index.X de la grille suit
	// Y monde, index.Y suit X monde, textures non transposees.
	constexpr bool kSwapAxes = true;

	enum class CoopRole
	{
		None,           // solo, ou client d'un serveur dedie
		Host,           // hote d'une partie coop
		ClientFromHost, // client coop qui recoit la carte de l'hote
		ClientLocal,    // client coop sans carte de l'hote (partage coupe, autre version)
	};

	// Etat immuable publie par le game thread ; le render callback ne lit que ca.
	struct Published
	{
		CoopRole Coop = CoopRole::None;
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

	inline std::mutex g_publishedMutex;
	inline std::shared_ptr<const Published> g_published;
	inline std::atomic<int64_t> g_lastRenderUnixMs{ 0 };  // "fenetre ouverte" = rendue recemment
	inline PanelHandle g_panel = nullptr;
	inline std::atomic<bool> g_coopShare{ false };  // [Coop] ShareMapWithClients, lu a l'activation
	inline std::string g_toggleKey;  // ecrit a l'activation, lu par le rendu (fermeture par la touche)

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
	inline std::atomic<bool> g_reloadTerrainRequested{ false };  // case "Terrain" recochee
	// Icones d'items des types du catalogue : game thread, quelques-unes par passe.
	constexpr size_t kIconLoadsPerService = 3;
	// Icones retrouvees par nom parmi les items charges par le jeu : items des
	// liaisons cargo (nom affiche du snapshot), Ignitium et Star Tears (classe).
	constexpr char kIgnitiumIconKey[] = "I_FireWaveOre_C";
	constexpr char kStarTearsIconKey[] = "I_StarTears_C";
	constexpr float kNamedIconRetrySeconds = 30.0f;
	inline std::atomic<uint64_t> g_wantedHiRes{ 0 };  // bit k = segment k demande en haute resolution
	inline std::atomic<int> g_hiResBudget{ 23 };
	inline std::atomic<bool> g_closeRequested{ false };    // Echap / touche bindee vues par le rendu
	inline std::atomic<int64_t> g_lastToggleUnixMs{ 0 };   // anti-rebond entre le keybind et le rendu
	constexpr int64_t kToggleDebounceMs = 400;

	constexpr unsigned int kColorSender = 0xFF2090FFu;
	constexpr unsigned int kColorReceiver = 0xFFFFB040u;
	constexpr unsigned int kColorLink = 0xC000D7FFu;
	constexpr unsigned int kColorTeleporter = 0xFFFF60C0u;
	constexpr unsigned int kColorPlayer = 0xFF60E060u;
	constexpr unsigned int kColorSelf = 0xFFFFFFFFu;
	constexpr unsigned int kColorBaseCore = 0xFF40D0FFu;
	constexpr unsigned int kColorPoi = 0xFF40D0D0u;

	inline bool IsOpen()
	{
		return MapStateSdk::GetCurrentUnixTimeMilliseconds() - g_lastRenderUnixMs.load() < kOpenGraceMs;
	}

	inline IPluginImGuiTextures* Textures()
	{
		IPluginHooks* hooks = GetHooks();
		return hooks ? hooks->ImGuiTextures : nullptr;
	}

	inline double MillisecondsSince(std::chrono::steady_clock::time_point started)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	}

	inline void SegmentRangeWorldRect(const TerrainDescriptor& terrain, int minX, int minY, int maxX, int maxY, float& x0, float& y0, float& x1, float& y1)
	{
		float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
		SegmentToWorld(terrain.Grid, static_cast<float>(minX), static_cast<float>(minY), kSwapAxes, ax, ay);
		SegmentToWorld(terrain.Grid, static_cast<float>(maxX + 1), static_cast<float>(maxY + 1), kSwapAxes, bx, by);
		x0 = std::min(ax, bx);
		x1 = std::max(ax, bx);
		y0 = std::min(ay, by);
		y1 = std::max(ay, by);
	}

	// Mesure du rendu (cumul depuis le dernier releve), lue et remise a zero par le service.
	inline std::atomic<int64_t> g_renderFrames{ 0 };
	inline std::atomic<int64_t> g_renderSumUs{ 0 };
	inline std::atomic<int64_t> g_renderMaxUs{ 0 };

	// --- ingame_map_service.cpp (game thread) ---
	void OnTick(float deltaSeconds);
	void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName);
	void OnToggleKey(EModKey key, EModKeyEvent event);
	// Lance le chargement du catalogue ; StopService libere tuiles, icones et catalogue.
	void StartService();
	void StopService();

	// --- ingame_map_render.cpp (render thread) ---
	void Render(IModLoaderImGui* ui);
	void RenderTitleBar(IModLoaderImGui* ui);
}
