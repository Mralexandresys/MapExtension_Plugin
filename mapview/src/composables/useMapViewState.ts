import {
    computed,
    onBeforeUnmount,
    onMounted,
    ref,
    watch,
    type Ref,
} from "vue";

import { formatRelativeAge } from "../lib/formatters";
import {
    DEFAULT_MAP_PRESET,
    PRESET_DEFINITIONS,
    type MapPreset,
} from "../lib/mapPresets";
import type {
    ActiveFilterClear,
    EntityToggleKey,
    FilterSectionKey,
    MapCanvasHandle,
    ShortcutItem,
    StatusTone,
} from "../lib/types";
import { useMapViewDataSource } from "./useMapViewDataSource";
import { useMapViewEntities } from "./useMapViewEntities";
import { useRuptureTimeline } from "./useRuptureTimeline";
import { useViewerUpdateNotice } from "./useViewerUpdateNotice";

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
        refreshIntervalMs,
        iconScale,
        lastUpdatedAt,
        now,
        preset,
        harvestResource,
        entityVisibility,
        filtersPanelCollapsed,
        filterSectionsOpen,
        status,
        ui,
        languageOptions,
        normalizedEndpoint,
        endpointHasPendingChanges,
        pluginVersion,
        viewerOutdated,
        viewerUpdateDownloadUrl,
        viewerUpdateReleaseUrl,
        viewerUpdateModPageUrl,
        handleEndpointKeydown,
        updateRefreshInterval,
        refreshData,
        applyEndpoint,
    } = useMapViewDataSource();

    const { viewerUpdateOpen, dismissViewerUpdate } = useViewerUpdateNotice(
        viewerOutdated,
        pluginVersion,
    );

    const focusMode = ref(false);
    const userAnnotationsOnly = ref(false);
    const selectedKey = ref<string | null>(null);
    const hoveredKey = ref<string | null>(null);
    const controlSettingsOpen = ref(false);
    // Transient: the timeline strip lives in the header, this only controls its
    // detail dropdown.
    const ruptureDetailsOpen = ref(false);
    const detailsPanelExpanded = ref(false);
    const shortcutsOpen = ref(false);

    const entityToggleOptions = computed<
        Array<{ key: EntityToggleKey; label: string }>
    >(() => [
        { key: "sender", label: ui.value.entityLabels.sender },
        { key: "receiver", label: ui.value.entityLabels.receiver },
        { key: "teleporter", label: ui.value.entityLabels.teleporter },
        { key: "player", label: ui.value.entityLabels.player },
        { key: "abandonedBase", label: ui.value.entityLabels.abandonedBase },
        { key: "plantResource", label: ui.value.entityLabels.plantResource },
        { key: "ignitium", label: ui.value.entityLabels.ignitium },
        { key: "starTears", label: ui.value.entityLabels.starTears },
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
            keys: ["C"],
            label: ui.value.shortcuts.items.center.label,
            description: ui.value.shortcuts.items.center.description,
        },
        {
            keys: ["P"],
            label: ui.value.shortcuts.items.centerPlayer.label,
            description: ui.value.shortcuts.items.centerPlayer.description,
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

    // Short form on purpose: the freshness value is already shown by the header
    // meta line and by the "last update" stat card. Repeating it here made the
    // pill read "OK - Derniere mise a jour : 1,8s" next to the title.
    const statusBadgeLabel = computed(() => {
        if (status.loading) return ui.value.status.sync;
        if (status.online) return ui.value.status.ok;
        if (cargo.value) return ui.value.status.cache;
        return ui.value.status.offline;
    });

    const {
        ruptureCurrentPhaseKey,
        ruptureCurrentPhaseLabel,
        ruptureCurrentPhaseRemainingLabel,
        rupturePhases,
        ruptureMarkerPercent,
        ruptureHasLiveData,
        ruptureMarkerLabel,
    } = useRuptureTimeline(ruptureCycle, now, ui);

    // Filled in by the app once the catalog filters exist; until then every
    // observed plant is shown.
    const plantResourceFilter = ref<((resource: string) => boolean) | null>(null);

    const entityState = useMapViewEntities({
        cargo,
        health,
        ui,
        entityVisibility,
        preset,
        showAllLinks,
        highlightOrphans,
        userAnnotationsOnly,
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
        ruptureCurrentPhaseKey,
        ruptureHasLiveData,
        plantResourceFilter,
    });

    const {
        displayedCargoMarkers,
        visibleCargoConnections,
        displayedTeleporters,
        displayedPlayers,
        displayedPois,
        livePlantResourceCounts,
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

    /** Applies the live-entity half of a preset. The catalog half lives in
     *  `useStaticMapFilters.applyPreset`, called from App.vue. */
    function applyPresetEntities(next: MapPreset): void {
        const definition = PRESET_DEFINITIONS[next];
        for (const key of Object.keys(entityVisibility) as EntityToggleKey[]) {
            entityVisibility[key] = definition.entities[key];
        }
    }

    function setPreset(next: MapPreset): void {
        preset.value = next;
        applyPresetEntities(next);
        showAllLinks.value = true;
        highlightOrphans.value = false;
        focusMode.value = false;
    }

    /** Back to the current preset's own defaults, not to "everything on". */
    function clearFilters(): void {
        showAllLinks.value = true;
        highlightOrphans.value = false;
        userAnnotationsOnly.value = false;
        focusMode.value = false;
        applyPresetEntities(preset.value);
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

    /** Undoes a single active filter from its chip, without touching the rest. */
    function clearFilterChip(clear: ActiveFilterClear): void {
        switch (clear.kind) {
            case "preset":
                setPreset(DEFAULT_MAP_PRESET);
                return;
            case "showAllLinks":
                showAllLinks.value = true;
                return;
            case "highlightOrphans":
                highlightOrphans.value = false;
                return;
            case "userAnnotationsOnly":
                userAnnotationsOnly.value = false;
                return;
            case "focusMode":
                focusMode.value = false;
                return;
            case "entity":
                entityVisibility[clear.key] =
                    PRESET_DEFINITIONS[preset.value].entities[clear.key];
                return;
        }
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

    const canCenterOnPlayer = computed(
        () => (cargo.value?.players?.length ?? 0) > 0,
    );

    function centerOnPlayer(): void {
        const players = cargo.value?.players ?? [];
        const player = players.find((entry) => entry.self) ?? players[0];
        if (!player) return;
        mapCanvasRef.value?.focusPoint(player.map.x, player.map.y);
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

    function toggleRuptureDetails(): void {
        ruptureDetailsOpen.value = !ruptureDetailsOpen.value;
    }

    function closeRuptureDetails(): void {
        ruptureDetailsOpen.value = false;
    }

    function toggleFilterSection(key: FilterSectionKey): void {
        filterSectionsOpen[key] = !filterSectionsOpen[key];
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

        // Letter shortcuts used to keep firing behind an open modal, refreshing
        // or opening panels the user could not see.
        if (shortcutsOpen.value || viewerUpdateOpen.value) return;

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
                // Toggle, to match the FILTERS button in the map toolbar.
                toggleFiltersPanel();
                return;
            case "c":
                event.preventDefault();
                centerSelection();
                return;
            case "p":
                event.preventDefault();
                centerOnPlayer();
                return;
            default:
                return;
        }
    }

    watch(userAnnotationsOnly, (enabled) => {
        if (enabled) {
            clearSelection();
        }
    });

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
        userAnnotationsOnly,
        focusMode,
        preset,
        harvestResource,
        autoRefresh,
        refreshIntervalMs,
        iconScale,
        selectedKey,
        hoveredKey,
        controlSettingsOpen,
        ruptureDetailsOpen,
        filterSectionsOpen,
        detailsPanelExpanded,
        filtersPanelCollapsed,
        shortcutsOpen,
        viewerUpdateOpen,
        pluginVersion,
        viewerUpdateDownloadUrl,
        viewerUpdateReleaseUrl,
        viewerUpdateModPageUrl,
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
        displayedPois,
        livePlantResourceCounts,
        plantResourceFilter,
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
        ruptureMarkerLabel,
        handleEndpointKeydown,
        updateRefreshInterval,
        refreshData,
        applyEndpoint,
        clearSelection,
        selectEntity,
        toggleControlSettings,
        toggleRuptureDetails,
        closeRuptureDetails,
        toggleFilterSection,
        toggleDetailsPanel,
        centerSelection,
        canCenterOnPlayer,
        centerOnPlayer,
        toggleFocusMode,
        setPreset,
        applyPresetEntities,
        clearFilters,
        clearFilterChip,
        toggleFiltersPanel,
        toggleEntity,
        resetMapView,
        openShortcuts: () => {
            shortcutsOpen.value = true;
        },
        closeShortcuts: () => {
            shortcutsOpen.value = false;
        },
        dismissViewerUpdate,
    };
}
