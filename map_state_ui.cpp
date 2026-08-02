#include "map_state_ui.h"

#include "map_state_capture.h"
#include "map_state_full_scan.h"
#include "plugin_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

namespace
{
	PanelHandle g_panelHandle = nullptr;

	void FormatUnixTimestampLocal(int64_t unixMs, char* buf, size_t bufSize)
	{
		if (unixMs == 0)
		{
			std::snprintf(buf, bufSize, "jamais");
			return;
		}
		const time_t seconds = static_cast<time_t>(unixMs / 1000);
		struct tm localTime{};
#if defined(_WIN32)
		localtime_s(&localTime, &seconds);
#else
		localtime_r(&seconds, &localTime);
#endif
		std::snprintf(
			buf,
			bufSize,
			"%02d:%02d:%02d",
			localTime.tm_hour,
			localTime.tm_min,
			localTime.tm_sec);
	}

	// Throttle the scan button: prevent multiple rapid clicks.
	std::atomic<int64_t> g_lastScanRequestAtMs = 0;
	constexpr int64_t kScanButtonCooldownMs = 2000;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(
			steady_clock::now().time_since_epoch()).count();
	}

	bool IsFullScanActive(MapStateFullScan::Phase phase)
	{
		switch (phase)
		{
		case MapStateFullScan::Phase::CreatingSource:
		case MapStateFullScan::Phase::Moving:
		case MapStateFullScan::Phase::WaitingForStreaming:
		case MapStateFullScan::Phase::SettlingPcg:
		case MapStateFullScan::Phase::Capturing:
		case MapStateFullScan::Phase::Restoring:
			return true;
		default:
			return false;
		}
	}

	const char* FullScanPhaseLabel(MapStateFullScan::Phase phase)
	{
		switch (phase)
		{
		case MapStateFullScan::Phase::Idle: return "Inactif";
		case MapStateFullScan::Phase::CreatingSource: return "Creation de la source";
		case MapStateFullScan::Phase::Moving: return "Deplacement";
		case MapStateFullScan::Phase::WaitingForStreaming: return "Chargement World Partition";
		case MapStateFullScan::Phase::SettlingPcg: return "Generation PCG";
		case MapStateFullScan::Phase::Capturing: return "Capture et sauvegarde";
		case MapStateFullScan::Phase::Restoring: return "Restauration de la zone joueur";
		case MapStateFullScan::Phase::Completed: return "Termine";
		case MapStateFullScan::Phase::CompletedPartial: return "Termine partiellement";
		case MapStateFullScan::Phase::Cancelled: return "Annule";
		case MapStateFullScan::Phase::Failed: return "Echec";
		default: return "Inconnu";
		}
	}

	void RenderPanel(IModLoaderImGui* imgui)
	{
		if (!imgui)
		{
			return;
		}

		const MapStateRuntime::Detail::PoiScanInfo info =
			MapStateRuntime::Detail::GetPoiScanInfo();
		const MapStateFullScan::Status fullScan = MapStateFullScan::CopyStatus();
		const bool fullScanActive = IsFullScanActive(fullScan.CurrentPhase);

		// Header
		imgui->SeparatorText("Plantes (points d'interet)");
		imgui->Spacing();

		// World
		if (info.WorldName.empty())
		{
			imgui->TextDisabled("Monde : (aucun monde Chimera charge)");
		}
		else
		{
			char worldBuf[128];
			std::snprintf(worldBuf, sizeof(worldBuf), "Monde : %s", info.WorldName.c_str());
			imgui->Text(worldBuf);
		}

		// Plant count
		{
			char countBuf[64];
			std::snprintf(countBuf, sizeof(countBuf), "Plantes connues : %d", info.PlantCount);
			imgui->Text(countBuf);
		}

		// Last scan time
		{
			char timeBuf[32];
			FormatUnixTimestampLocal(info.LastScanAtUnixMs, timeBuf, sizeof(timeBuf));
			char lastBuf[80];
			std::snprintf(lastBuf, sizeof(lastBuf), "Dernier scan : %s", timeBuf);
			imgui->Text(lastBuf);
		}

		imgui->Spacing();

		// Local scan state
		if (info.ScanInProgress || fullScanActive)
		{
			imgui->TextColored(1.0f, 0.8f, 0.2f, 1.0f, "Scan en cours...");
		}
		else if (info.WorldName.empty())
		{
			imgui->TextDisabled("En attente du chargement du monde");
		}
		else
		{
			imgui->TextColored(0.4f, 1.0f, 0.4f, 1.0f, "Pret");
		}

		imgui->Spacing();

		// Local rescan button
		const int64_t nowMs = NowMs();
		const int64_t lastRequest = g_lastScanRequestAtMs.load();
		const bool cooldownActive =
			lastRequest > 0 && nowMs - lastRequest < kScanButtonCooldownMs;
		const bool canScan =
			!info.WorldName.empty()
			&& !info.ScanInProgress
			&& !fullScanActive
			&& !cooldownActive;

		if (!canScan)
		{
			imgui->TextDisabled("Rescanner la zone chargee");
		}
		else if (imgui->Button("Rescanner la zone chargee"))
		{
			g_lastScanRequestAtMs.store(nowMs);
			MapStateRuntime::Detail::ForcePoiRescan();
		}

		imgui->Spacing();
		imgui->Separator();
		imgui->Spacing();

		imgui->SeparatorText("Scan complet de la carte");
		imgui->TextWrapped(
			"Attention : cette operation charge et genere progressivement les "
			"cellules World Partition. Elle peut durer plusieurs minutes et "
			"provoquer de forts ralentissements. Disponible uniquement en solo/local.");
		imgui->Spacing();

		{
			char phaseBuf[128];
			std::snprintf(
				phaseBuf,
				sizeof(phaseBuf),
				"Etat : %s | Mode : %s",
				FullScanPhaseLabel(fullScan.CurrentPhase),
				fullScan.CurrentMode == MapStateFullScan::Mode::MassSource
					? "Source Mass"
					: "Fallback joueur");
			imgui->Text(phaseBuf);
		}

		if (fullScan.TotalZones > 0)
		{
			const double percent = 100.0
				* static_cast<double>(fullScan.CurrentZone)
				/ static_cast<double>(fullScan.TotalZones);
			char progressBuf[160];
			std::snprintf(
				progressBuf,
				sizeof(progressBuf),
				"Progression : %zu / %zu zones (%.1f %%)",
				fullScan.CurrentZone,
				fullScan.TotalZones,
				percent);
			imgui->Text(progressBuf);

			char resultsBuf[160];
			std::snprintf(
				resultsBuf,
				sizeof(resultsBuf),
				"Plantes : %d (%+d) | Zones expirees : %zu",
				fullScan.CurrentPlantCount,
				fullScan.CurrentPlantCount - fullScan.StartingPlantCount,
				fullScan.TimedOutZones);
			imgui->Text(resultsBuf);

			char liveBuf[96];
			std::snprintf(
				liveBuf,
				sizeof(liveBuf),
				"Acteurs plantes vivants observes : %d",
				fullScan.LiveTrackedPlantObservations);
			imgui->Text(liveBuf);
		}
		if (!fullScan.Message.empty())
		{
			imgui->TextWrapped(fullScan.Message.c_str());
		}

		imgui->Spacing();
		if (fullScanActive)
		{
			if (imgui->Button("Annuler le scan complet"))
			{
				MapStateFullScan::RequestCancel();
			}
		}
		else if (info.WorldName.empty())
		{
			imgui->TextDisabled("Scanner toute la carte");
		}
		else if (imgui->Button("Scanner toute la carte"))
		{
			MapStateFullScan::RequestStart();
		}

		imgui->Spacing();
		imgui->Separator();
		imgui->Spacing();

		imgui->TextWrapped(
			"Chaque zone terminee enrichit immediatement le cache sur disque. "
			"Une annulation conserve donc toutes les plantes deja trouvees. "
			"Si Mass ignore la source temporaire, le fallback deplace le joueur "
			"pendant le scan puis le replace exactement a sa position initiale.");
	}
}

namespace MapStateUI
{
	void Register(IPluginHooks* hooks)
	{
		if (!hooks || !hooks->UI || !hooks->UI->RegisterPanel)
		{
			return;
		}
		if (g_panelHandle)
		{
			return;
		}

		// RegisterPanel retains the descriptor pointer. It must therefore live
		// for as long as the panel instead of being allocated on this stack.
		static const PluginPanelDesc desc{
			"MapExtension",
			"MapExtension - Plantes",
			RenderPanel
		};
		g_panelHandle = hooks->UI->RegisterPanel(&desc);
	}

	void Unregister(IPluginHooks* hooks)
	{
		if (!g_panelHandle)
		{
			return;
		}
		if (hooks && hooks->UI && hooks->UI->UnregisterPanel)
		{
			hooks->UI->UnregisterPanel(g_panelHandle);
		}
		g_panelHandle = nullptr;
	}
}
