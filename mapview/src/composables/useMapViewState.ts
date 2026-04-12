import {
    computed,
    onBeforeUnmount,
    onMounted,
    ref,
    watch,
    type Ref,
} from "vue";

import { formatRelativeAge } from "../lib/formatters";
import type {
    EntityToggleKey,
    MapCanvasHandle,
    ShortcutItem,
    StatusTone,
} from "../lib/types";
import { useMapViewDataSource } from "./useMapViewDataSource";
import { useMapViewEntities } from "./useMapViewEntities";
import { useRuptureTimeline } from "./useRuptureTimeline";

function isTypingTarget(target: EventTarget | null): boolean {
    const element = target as HTMLElement | null;
    if (!element) return false;
    const tagName = element.tagName;
    return (
        tagName === "INPUT" ||
        tagName === "TEXTAREA" ||
        tagName === "SELECT" ||
        element.isContentEditable
    );
}

export function useMapViewState(mapCanvasRef: Ref<MapCanvasHandle | null>) {
    const {
        DEFAULT_ENDPOINT,
        cargo,
        health,
        ruptureCycle,
        endpointDraft,
        lang,
        showAllLinks,
        highlightOrphans,
        autoRefresh,
        lastUpdatedAt,
        now,
        viewMode,
        entityVisibility,
        status,
        ui,
        languageOptions,
        normalizedEndpoint,
        endpointHasPendingChanges,
        handleEndpointKeydown,
        refreshData,
        applyEndpoint,
    } = useMapViewDataSource();

    const focusMode = ref(false);
    const selectedKey = ref<string | null>(null);
    const hoveredKey = ref<string | null>(null);
    const controlSettingsOpen = ref(false);
    const rupturePanelCollapsed = ref(false);
    const detailsPanelExpanded = ref(false);
    const filtersPanelCollapsed = ref(true);
    const shortcutsOpen = ref(false);

    const entityToggleOptions = computed<
        Array<{ key: EntityToggleKey; label: string }>
    >(() => [
        { key: "sender", label: ui.value.entityLabels.sender },
        { key: "receiver", label: ui.value.entityLabels.receiver },
        { key: "teleporter", label: ui.value.entityLabels.teleporter },
        { key: "player", label: ui.value.entityLabels.player },
    ]);

    const shortcutItems = computed<ShortcutItem[]>(() => [
        {
            keys: ["?", "/"],
            label: ui.value.shortcuts.items.help.label,
            description: ui.value.shortcuts.items.help.description,
        },
        {
            keys: ["R"],
            label: ui.value.shortcuts.items.refresh.label,
            description: ui.value.shortcuts.items.refresh.description,
        },
        {
            keys: ["L"],
            label: ui.value.shortcuts.items.live.label,
            description: ui.value.shortcuts.items.live.description,
        },
        {
            keys: ["G"],
            label: ui.value.shortcuts.items.focus.label,
            description: ui.value.shortcuts.items.focus.description,
        },
        {
            keys: ["E"],
            label: ui.value.shortcuts.items.details.label,
            description: ui.value.shortcuts.items.details.description,
        },
        {
            keys: ["F"],
            label: ui.value.shortcuts.items.filters.label,
            description: ui.value.shortcuts.items.filters.description,
        },
        {
            keys: ["S"],
            label: ui.value.selection.observabilityTitle,
            description: ui.value.selection.observabilityHelp,
        },
        {
            keys: ["C"],
            label: ui.value.shortcuts.items.center.label,
            description: ui.value.shortcuts.items.center.description,
        },
        {
            keys: ["0"],
            label: ui.value.shortcuts.items.reset.label,
            description: ui.value.shortcuts.items.reset.description,
        },
        {
            keys: ["Esc"],
            label: ui.value.shortcuts.items.close.label,
            description: ui.value.shortcuts.items.close.description,
        },
    ]);

    const liveAgeLabel = computed(() =>
        formatRelativeAge(
            lastUpdatedAt.value,
            now.value,
            ui.value.locale,
            ui.value.status.lastUpdatedPrefix,
            ui.value.status.lastUpdatedMissing,
        ),
    );

    const statusTone = computed<StatusTone>(() => {
        if (status.loading) return "loading";
        if (status.online) return "online";
        return cargo.value ? "stale" : "offline";
    });

    const statusBadgeLabel = computed(() => {
        if (status.loading) return ui.value.status.sync;
        if (status.online) return ui.value.format.liveAge(liveAgeLabel.value);
        if (cargo.value) return ui.value.format.cacheAge(liveAgeLabel.value);
        return ui.value.status.offline;
    });

    const entityState = useMapViewEntities({
        cargo,
        health,
        ui,
        entityVisibility,
        viewMode,
        showAllLinks,
        highlightOrphans,
        focusMode,
        selectedKey,
        hoveredKey,
        entityToggleOptions,
        normalizedEndpoint,
        autoRefresh,
        liveAgeLabel,
        statusTone,
        now,
        lastUpdatedAt,
    });

    const {
        displayedCargoMarkers,
        visibleCargoConnections,
        displayedTeleporters,
        displayedPlayers,
        entityFilterCounts,
        visibleEntityKeys,
        selectedEntity,
        orphanKeySet,
        focusKeys,
        focusCargoKey,
        commandStats,
        mapMetaLabel,
        selectedEntitySummary,
        selectedEntityKeyLabel,
        selectedEntityTone,
        selectedDisplayName,
        selectedPreviewFacts,
        canEnableFocusMode,
        selectedDetailRows,
        totalCounts,
        statsOverview,
        activeFilterChips,
        currentTimeLabel,
    } = entityState;

    const {
        ruptureCurrentPhaseKey,
        ruptureCurrentPhaseLabel,
        ruptureCurrentPhaseRemainingLabel,
        rupturePhases,
        ruptureMarkerPercent,
        ruptureHasLiveData,
        ruptureTimelineTicks,
        ruptureMarkerLabel,
    } = useRuptureTimeline(ruptureCycle, now, ui);

    function clearFilters(): void {
        showAllLinks.value = true;
        highlightOrphans.value = false;
        focusMode.value = false;
        viewMode.value = "network";
        entityVisibility.sender = true;
        entityVisibility.receiver = true;
        entityVisibility.teleporter = true;
        entityVisibility.player = true;
    }

    function clearSelection(): void {
        selectedKey.value = null;
        hoveredKey.value = null;
        focusMode.value = false;
        detailsPanelExpanded.value = false;
    }

    function toggleEntity(key: EntityToggleKey): void {
        entityVisibility[key] = !entityVisibility[key];
    }

    function selectEntity(key: string): void {
        selectedKey.value = key;
        detailsPanelExpanded.value = false;
    }

    function resetMapView(): void {
        mapCanvasRef.value?.resetView();
    }

    function centerSelection(): void {
        mapCanvasRef.value?.focusSelection();
    }

    function toggleFocusMode(): void {
        if (!canEnableFocusMode.value) return;
        focusMode.value = !focusMode.value;
    }

    function openPanel(): void {
        if (!selectedEntity.value) return;
        detailsPanelExpanded.value = true;
    }

    function toggleControlSettings(): void {
        controlSettingsOpen.value = !controlSettingsOpen.value;
    }

    function toggleRupturePanel(): void {
        rupturePanelCollapsed.value = !rupturePanelCollapsed.value;
    }

    function toggleDetailsPanel(): void {
        if (!selectedEntity.value) return;
        detailsPanelExpanded.value = !detailsPanelExpanded.value;
    }

    function toggleFiltersPanel(): void {
        filtersPanelCollapsed.value = !filtersPanelCollapsed.value;
    }

    function handleKeydown(event: KeyboardEvent): void {
        if (event.metaKey || event.ctrlKey || event.altKey) return;
        if (isTypingTarget(event.target)) return;

        if (event.key === "Escape") {
            if (shortcutsOpen.value) {
                shortcutsOpen.value = false;
                return;
            }
            if (controlSettingsOpen.value) {
                controlSettingsOpen.value = false;
                return;
            }
            if (selectedKey.value) {
                clearSelection();
                return;
            }
            if (detailsPanelExpanded.value) {
                detailsPanelExpanded.value = false;
            }
            return;
        }

        if (event.key === "?" || event.key === "/") {
            event.preventDefault();
            shortcutsOpen.value = !shortcutsOpen.value;
            return;
        }

        switch (event.key) {
            case "0":
                event.preventDefault();
                resetMapView();
                return;
            default:
                break;
        }

        switch (event.key.toLowerCase()) {
            case "r":
                event.preventDefault();
                void refreshData();
                return;
            case "l":
                event.preventDefault();
                autoRefresh.value = !autoRefresh.value;
                return;
            case "g":
                event.preventDefault();
                toggleFocusMode();
                return;
            case "e":
                event.preventDefault();
                openPanel();
                return;
            case "f":
                event.preventDefault();
                filtersPanelCollapsed.value = false;
                return;
            case "s":
                event.preventDefault();
                openPanel();
                return;
            case "c":
                event.preventDefault();
                centerSelection();
                return;
            default:
                return;
        }
    }

    watch(
        [selectedKey, visibleEntityKeys],
        ([currentKey, keys]) => {
            if (currentKey && !keys.has(currentKey)) {
                clearSelection();
            }
        },
        { immediate: true },
    );

    watch(
        () => selectedEntity.value,
        (value) => {
            if (!value) {
                focusMode.value = false;
            }
        },
    );

    onMounted(() => {
        window.addEventListener("keydown", handleKeydown);
    });

    onBeforeUnmount(() => {
        window.removeEventListener("keydown", handleKeydown);
    });

    return {
        DEFAULT_ENDPOINT,
        cargo,
        endpointDraft,
        lang,
        showAllLinks,
        highlightOrphans,
        focusMode,
        viewMode,
        autoRefresh,
        selectedKey,
        hoveredKey,
        controlSettingsOpen,
        rupturePanelCollapsed,
        detailsPanelExpanded,
        filtersPanelCollapsed,
        shortcutsOpen,
        entityVisibility,
        status,
        ui,
        languageOptions,
        entityToggleOptions: computed(() =>
            entityToggleOptions.value.map((option) => ({
                ...option,
                count: entityFilterCounts.value[option.key],
            })),
        ),
        shortcutItems,
        endpointHasPendingChanges,
        normalizedEndpoint,
        displayedCargoMarkers,
        visibleCargoConnections,
        displayedTeleporters,
        displayedPlayers,
        selectedEntity,
        orphanKeySet,
        focusKeys,
        focusCargoKey,
        commandStats,
        mapMetaLabel,
        statusTone,
        statusBadgeLabel,
        selectedEntitySummary,
        selectedEntityKeyLabel,
        selectedEntityTone,
        selectedDisplayName,
        selectedPreviewFacts,
        canEnableFocusMode,
        selectedDetailRows,
        totalCounts,
        statsOverview,
        activeFilterChips,
        currentTimeLabel,
        liveAgeLabel,
        ruptureCurrentPhaseKey,
        ruptureCurrentPhaseLabel,
        ruptureCurrentPhaseRemainingLabel,
        rupturePhases,
        ruptureMarkerPercent,
        ruptureHasLiveData,
        ruptureTimelineTicks,
        ruptureMarkerLabel,
        handleEndpointKeydown,
        refreshData,
        applyEndpoint,
        clearSelection,
        selectEntity,
        toggleControlSettings,
        toggleRupturePanel,
        toggleDetailsPanel,
        centerSelection,
        toggleFocusMode,
        clearFilters,
        toggleFiltersPanel,
        toggleEntity,
        resetMapView,
        openShortcuts: () => {
            shortcutsOpen.value = true;
        },
        closeShortcuts: () => {
            shortcutsOpen.value = false;
        },
    };
}
