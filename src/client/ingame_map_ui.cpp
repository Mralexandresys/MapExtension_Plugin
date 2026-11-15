#include "ingame_map_ui.h"

#include "ingame_map_internal.h"

namespace MapExtensionClient::InGameMap
{
namespace
{
	PluginPanelDesc g_panelDesc = { "Logistics map", "MapExtension - logistics map", &Render, &RenderTitleBar };

	// Rappel affiche a chaque partie chargee : les joueurs ne lisent pas le changelog
	// et cherchent encore la page web. Fenetre modale centree, fermee par OK ;
	// desactivable par InGameMap.ShowPopup. Le widget n'est qu'un hote invisible :
	// ImGui centre la modale tout seul, et la capture d'entree rend OK cliquable.
	PluginWindowHints g_popupHints = { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0,
		PluginWindowFlags_NoTitleBar | PluginWindowFlags_NoResize | PluginWindowFlags_NoMove |
		PluginWindowFlags_NoBackground | PluginWindowFlags_NoSavedSettings | PluginWindowFlags_NoMouseInputs };
	WidgetHandle g_popup = nullptr;
	void* g_popupCapture = nullptr;                 // game thread uniquement
	std::atomic<bool> g_popupOpenPending{ false };  // game thread -> rendu : OpenPopup a faire

	void SetPopupVisible(bool visible)
	{
		IPluginHooks* hooks = GetHooks();
		if (!g_popup || !hooks || !hooks->UI)
		{
			return;
		}
		g_popupOpenPending = visible;
		if (hooks->UI->SetWidgetVisible)
		{
			hooks->UI->SetWidgetVisible(g_popup, visible);
		}
		if (visible && !g_popupCapture && hooks->UI->AcquireInputCapture)
		{
			g_popupCapture = hooks->UI->AcquireInputCapture();
		}
		else if (!visible && g_popupCapture && hooks->UI->ReleaseInputCapture)
		{
			hooks->UI->ReleaseInputCapture(g_popupCapture);
			g_popupCapture = nullptr;
		}
	}

	void HidePopupOnGameThread(void*)
	{
		SetPopupVisible(false);
	}

	void RenderPopup(IModLoaderImGui* ui)
	{
		constexpr const char* kId = "MapExtension 0.9###mapext_popup";
		if (g_popupOpenPending.exchange(false))
		{
			ui->OpenPopup(kId, 0);
		}
		ui->SetNextWindowSize(520.0f, 0.0f, PluginCond_Always);
		if (!ui->BeginPopupModal(kId, nullptr, PluginWindowFlags_NoResize | PluginWindowFlags_NoMove | PluginWindowFlags_NoSavedSettings))
		{
			return;
		}
		const std::string key = g_toggleKey.empty() ? std::string("the map key") : g_toggleKey;
		ui->TextColored(1.0f, 0.8f, 0.25f, 1.0f, "MapExtension now runs inside the game");
		ui->TextWrapped(("Press " + key + " to open the logistics map. The browser page (MapExtensionViewer.html) is gone.").c_str());
		ui->Spacing();
		ui->Separator();
		ui->Spacing();
		ui->TextColored(1.0f, 0.55f, 0.35f, 1.0f, "Coming from a version older than 0.7?");
		ui->TextWrapped("Download the mod again from Nexus Mods and install the whole archive: nexusmods.com/starrupture/mods/91");
		ui->Spacing();
		ui->TextColored(0.7f, 0.7f, 0.7f, 1.0f, "Hide this message: mod options > InGameMap > ShowPopup");
		ui->Spacing();

		constexpr float kButtonWidth = 120.0f;
		float availX = 0.0f, availY = 0.0f;
		ui->GetContentRegionAvail(&availX, &availY);
		ui->SetCursorPosX(ui->GetCursorPosX() + std::max(0.0f, (availX - kButtonWidth) * 0.5f));
		if (ui->ButtonSized("OK", kButtonWidth, 0.0f))
		{
			ui->CloseCurrentPopup();
			IPluginHooks* hooks = GetHooks();
			if (hooks && hooks->Engine && hooks->Engine->PostToGameThread)
			{
				hooks->Engine->PostToGameThread(HidePopupOnGameThread, nullptr);
			}
		}
		ui->EndPopup();
	}

	PluginWidgetDesc g_popupDesc = { "MapExtension##popup", &RenderPopup, &g_popupHints };

	void OnUIStateChanged(uint64_t oldState, uint64_t newState, void*)
	{
		const bool wasInGame = (oldState & PLUGIN_UI_IN_GAMEPLAY) != 0;
		const bool inGame = (newState & PLUGIN_UI_IN_GAMEPLAY) != 0;
		if (inGame && !wasInGame)
		{
			SetPopupVisible(MapExtensionPluginConfig::Config::InGameMapShowPopup());
		}
		else if (!inGame && wasInGame)
		{
			SetPopupVisible(false);
		}
	}

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
		StartService();
		if (hooks->Engine && hooks->Engine->RegisterOnTick)
		{
			hooks->Engine->RegisterOnTick(OnTick);
		}
		if (hooks->UI->RegisterWidget && hooks->GameMenu && hooks->GameMenu->RegisterOnUIStateChanged)
		{
			g_popup = hooks->UI->RegisterWidget(&g_popupDesc);
			if (g_popup)
			{
				SetPopupVisible(false);
				hooks->GameMenu->RegisterOnUIStateChanged(PLUGIN_UI_IN_GAMEPLAY, OnUIStateChanged, nullptr);
			}
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
		LOG_INFO("[InGameMap] in-game map enabled (toggle key: %s, hi-res budget: %d MiB = %d tiles)",
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
		if (g_popup)
		{
			if (hooks->GameMenu && hooks->GameMenu->UnregisterOnUIStateChanged)
			{
				hooks->GameMenu->UnregisterOnUIStateChanged(OnUIStateChanged, nullptr);
			}
			SetPopupVisible(false);  // rend le jeton de capture
			if (hooks->UI && hooks->UI->UnregisterWidget)
			{
				hooks->UI->UnregisterWidget(g_popup);
			}
			g_popup = nullptr;
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
		StopService();
		LOG_INFO("[InGameMap] in-game map disabled");
	}
}

	void Initialize()
	{
		Enable();
	}

	void Shutdown()
	{
		Disable();
	}
}
