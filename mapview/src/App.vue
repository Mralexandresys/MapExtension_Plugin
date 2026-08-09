<script setup lang="ts">
import { computed, readonly, ref, watch } from "vue";

import MapAnnotationFab from "./components/mapview/MapAnnotationFab.vue";
import MapCanvas from "./components/MapCanvas.vue";
import MapCanvasToolbar from "./components/mapview/MapCanvasToolbar.vue";
import MapControlDock from "./components/mapview/MapControlDock.vue";
import MapFiltersPanel from "./components/mapview/MapFiltersPanel.vue";
import MapNotesPanel from "./components/mapview/MapNotesPanel.vue";
import MapRupturePanel from "./components/mapview/MapRupturePanel.vue";
import MapSelectionPanel from "./components/mapview/MapSelectionPanel.vue";
import MapShortcutDialog from "./components/mapview/MapShortcutDialog.vue";
import MapViewerUpdateDialog from "./components/mapview/MapViewerUpdateDialog.vue";
import { useMapViewState } from "./composables/useMapViewState";
import {
    useStaticMapData,
    type StaticMapPoiView,
    type StaticSelection,
} from "./composables/useStaticMapData";
import {
    applyStaticFilterToggle,
    useStaticFiltersModel,
    useStaticMapFilters,
} from "./composables/useStaticMapFilters";
import { useUserAnnotations } from "./composables/useUserAnnotations";
import { resolveMapProjection, worldToMap } from "./lib/mapProjection";
import type {
    StaticPlacement,
    StaticPointSeries,
} from "./lib/staticMapCatalog";
import type {
    DetailRow,
    MapCanvasHandle,
    MapCanvasToolbarModel,
    MapControlDockModel,
    MapFiltersPanelModel,
    MapNotesPanelModel,
    MapRupturePanelModel,
    MapSelectionPanelModel,
    MapViewerUpdateDialogModel,
    Rect2D,
    StaticFilterToggle,
    UserAnnotationDraft,
    UserAnnotationSelection,
} from "./lib/types";

const mapCanvasRef = ref<MapCanvasHandle | null>(null);

const {
    DEFAULT_ENDPOINT,
    cargo,
    endpointDraft,
    lang,
    showAllLinks,
    highlightOrphans,
    userAnnotationsOnly,
    focusMode,
    viewMode,
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
    entityToggleOptions,
    shortcutItems,
    endpointHasPendingChanges,
    normalizedEndpoint,
    displayedCargoMarkers,
    visibleCargoConnections,
    displayedTeleporters,
    displayedPlayers,
    displayedPois,
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
    clearFilters,
    toggleFiltersPanel,
    toggleEntity,
    resetMapView,
    openShortcuts,
    closeShortcuts,
    dismissViewerUpdate,
} = useMapViewState(mapCanvasRef);

// ── Annotations ───────────────────────────────────────────────────────────────

const {
    markers: userMarkers,
    zones: userZones,
    annotationMode,
    annotationEditMode,
    selectedAnnotation,
    selectedZoneLocked,
    importError,
    draft: annotationDraft,
    setAnnotationMode,
    setAnnotationEditMode,
    selectMarker,
    selectZone,
    clearAnnotationSelection,
    addMarker,
    addZone,
    moveSelectedMarker,
    updateSelectedZoneRect,
    toggleSelectedZoneLock,
    updateSelectedDraft,
    deleteSelectedAnnotation,
    exportAnnotations,
    importAnnotations,
} = useUserAnnotations(ui);

function handleAnnotationSelect(sel: UserAnnotationSelection): void {
    if (!sel) {
        clearAnnotationSelection();
        return;
    }
    if (sel.type === "marker") selectMarker(sel.id);
    else selectZone(sel.id);
    // clear entity selection when an annotation is selected
    clearSelection();
}

function handleEntitySelect(key: string): void {
    clearAnnotationSelection();
    selectEntity(key);
}

function clearAllSelection(): void {
    clearSelection();
    clearAnnotationSelection();
    clearStaticSelection();
}

// The notes panel and the selection panel share the top-right overlay slot.
// Entering an annotation mode while an entity was selected mounted both, the
// notes panel silently covering the selection.
function handleAnnotationModeToggle(mode: "marker" | "zone"): void {
    clearSelection();
    clearStaticSelection();
    setAnnotationMode(mode);
}

function handleCreateMarker(point: { x: number; y: number }): void {
    clearSelection();
    addMarker(point);
}

function handleDraftUpdate(d: UserAnnotationDraft): void {
    updateSelectedDraft(d);
}

function startSelectedAnnotationEdit(): void {
    const sel = selectedAnnotation.value;
    if (!sel) return;
    setAnnotationEditMode(sel.type === "marker" ? "move-marker" : "edit-zone");
}

function handleMoveMarker(point: { x: number; y: number }): void {
    moveSelectedMarker(point);
}

function handleUpdateZone(rect: Rect2D): void {
    updateSelectedZoneRect(rect);
}

async function handleImport(file: File): Promise<void> {
    await importAnnotations(file);
}

function handleCreateZone(rect: Rect2D): void {
    clearSelection();
    addZone(rect);
}

// ── Static world catalog ───────────────────────────────────────────────

const projection = computed(() => resolveMapProjection(cargo.value?.map));
const staticData = useStaticMapData(projection);
const staticFilters = useStaticMapFilters(staticData.manifest);
const staticSelection = ref<StaticSelection | null>(null);
const matchedLivePoiKeys = ref<Set<string>>(new Set());

watch(
    [staticFilters.enabledLayers, staticData.manifest],
    ([layers]) => {
        void staticData.ensureLayers(layers);
    },
    { immediate: true },
);

// Live observations are folded into the catalog instead of adding a second
// marker at the same spot.
watch(
    [() => cargo.value?.pois, staticData.series],
    () => {
        matchedLivePoiKeys.value = staticData.applyDynamicPois(
            cargo.value?.pois ?? [],
        );
    },
    { immediate: true },
);

watch(selectedKey, (key) => {
    if (key) staticSelection.value = null;
});

const visiblePois = computed(() =>
    displayedPois.value.filter(
        (poi) => !matchedLivePoiKeys.value.has(poi.unique_key),
    ),
);

const staticLoadedCount = computed(
    () =>
        staticData.totalPointCount.value +
        staticData.placements.value.length +
        staticData.pois.value.length,
);

const staticFiltersModel = useStaticFiltersModel({
    manifest: staticData.manifest,
    filters: staticFilters,
    ui,
    lang,
    loading: staticData.loading,
    error: staticData.error,
    available: staticData.available,
    loadedCount: staticLoadedCount,
});

function isStaticSeriesVisible(entry: StaticPointSeries): boolean {
    return staticFilters.isSeriesVisible(entry);
}

function isStaticPlacementVisible(entry: StaticPlacement): boolean {
    return staticFilters.isPlacementVisible(entry);
}

function isStaticPoiVisible(entry: StaticMapPoiView): boolean {
    return staticFilters.isPoiGroupEnabled(entry.group);
}

function pickStatic(
    x: number,
    y: number,
    radius: number,
): StaticSelection | null {
    return staticData.findNearest(
        x,
        y,
        radius,
        isStaticSeriesVisible,
        isStaticPlacementVisible,
    );
}

function staticGroupLabel(key: string): string {
    const groups = ui.value.staticFilters.groups as Record<string, string>;
    return groups[key] ?? key;
}

function staticResourceLabel(typeId: string): string {
    const entry = staticData.manifest.value?.resource_types?.[typeId];
    if (!entry) return typeId;
    return lang.value === "fr" ? entry.fr : entry.en;
}

function staticTitle(selection: StaticSelection): string {
    if (selection.kind === "resource") return staticResourceLabel(selection.group);
    if (selection.kind === "poi") {
        return selection.label || staticGroupLabel(selection.group);
    }
    return selection.label || selection.actorType || staticGroupLabel(selection.group);
}

function staticDetailRows(selection: StaticSelection): DetailRow[] {
    const messages = ui.value.staticFilters;
    const rows: DetailRow[] = [
        {
            label: messages.details.catalog,
            value: messages.layers[selection.layer],
        },
    ];

    if (selection.kind === "resource") {
        const categories = messages.categories as Record<string, string>;
        rows.push({
            label: messages.details.group,
            value: categories[selection.category ?? ""] ?? selection.category ?? "",
        });
        if (selection.representation) {
            rows.push({
                label: messages.details.representation,
                value: messages.representations[selection.representation],
            });
        }
        rows.push({
            label: ui.value.selection.state,
            value: messages.states[selection.state],
        });
    } else {
        rows.push({
            label: messages.details.group,
            value: staticGroupLabel(selection.group),
        });
    }

    if (selection.actorType) {
        rows.push({
            label: messages.details.actorType,
            value: selection.actorType,
        });
    }

    const poi = selection.poi;
    if (poi) {
        const description =
            lang.value === "fr"
                ? poi.descriptionFr || poi.descriptionEn
                : poi.descriptionEn || poi.descriptionFr;
        if (description) {
            rows.push({
                label: messages.details.description,
                value: description,
            });
        }
        if (poi.guid) {
            rows.push({ label: messages.details.guid, value: poi.guid });
        }
    }

    rows.push({
        label: messages.details.position,
        value: `X ${Math.round(selection.x / 10)} m | Y ${Math.round(selection.y / 10)} m`,
    });
    rows.push({
        label: messages.details.altitude,
        value: `${selection.z} m`,
    });

    return rows;
}

function describeStatic(selection: StaticSelection): {
    title: string;
    lines: string[];
} {
    return {
        title: staticTitle(selection),
        lines: staticDetailRows(selection).map(
            (row) => `${row.label}: ${row.value}`,
        ),
    };
}

function handleStaticSelect(selection: StaticSelection | null): void {
    clearSelection();
    clearAnnotationSelection();
    staticSelection.value = selection;
}

function clearStaticSelection(): void {
    staticSelection.value = null;
}

function handleStaticFilterToggle(toggle: StaticFilterToggle): void {
    applyStaticFilterToggle(staticFilters, toggle);
}

function centerCurrentSelection(): void {
    const selection = staticSelection.value;
    if (selection && !selectedEntity.value) {
        const point = worldToMap(
            { x: selection.x * 10, y: selection.y * 10 },
            projection.value,
        );
        mapCanvasRef.value?.focusPoint(point.x, point.y);
        return;
    }
    centerSelection();
}

// ── Panel models ──────────────────────────────────────────────────────────────

const controlDockPanel = computed<MapControlDockModel>(() => ({
    settingsOpen: controlSettingsOpen.value,
    ui: ui.value,
    mapMetaLabel: mapMetaLabel.value,
    statusTone: statusTone.value,
    statusBadgeLabel: statusBadgeLabel.value,
    commandStats: commandStats.value,
    endpointDraft: endpointDraft.value,
    defaultEndpoint: DEFAULT_ENDPOINT,
    endpointHasPendingChanges: endpointHasPendingChanges.value,
    normalizedEndpoint: normalizedEndpoint.value,
    languageOptions,
    lang: lang.value,
    autoRefresh: autoRefresh.value,
    refreshIntervalMs: refreshIntervalMs.value,
    iconScale: iconScale.value,
    loading: status.loading,
    statusText: status.text,
    statusError: status.error,
    currentTimeLabel: currentTimeLabel.value,
    liveAgeLabel: liveAgeLabel.value,
}));

const rupturePanel = computed<MapRupturePanelModel>(() => ({
    detailsOpen: ruptureDetailsOpen.value,
    ui: ui.value,
    currentPhaseKey: ruptureCurrentPhaseKey.value,
    currentPhaseLabel: ruptureCurrentPhaseLabel.value,
    currentPhaseRemainingLabel: ruptureCurrentPhaseRemainingLabel.value,
    phases: rupturePhases.value,
    markerPercent: ruptureMarkerPercent.value,
    markerLabel: ruptureMarkerLabel.value,
    hasLiveData: ruptureHasLiveData.value,
    timelineTicks: ruptureTimelineTicks.value,
}));

const selectionPanel = computed<MapSelectionPanelModel>(() => {
    const staticEntry = staticSelection.value;
    const showStatic = !!staticEntry && !selectedEntity.value;
    const staticRows = showStatic && staticEntry ? staticDetailRows(staticEntry) : [];

    return {
        detailsExpanded: detailsPanelExpanded.value,
        ui: ui.value,
        selectedEntityKeyLabel: showStatic && staticEntry
            ? staticEntry.key
            : selectedEntityKeyLabel.value,
        selectedEntitySummary: showStatic && staticEntry
            ? ui.value.staticFilters.layers[staticEntry.layer]
            : selectedEntitySummary.value,
        selectedEntityTone: showStatic ? "neutral" : selectedEntityTone.value,
        selectedDisplayName: showStatic && staticEntry
            ? staticTitle(staticEntry)
            : selectedDisplayName.value,
        selectedPreviewFacts: showStatic
            ? staticRows.slice(0, 3)
            : selectedPreviewFacts.value,
        selectedDetailRows: showStatic ? staticRows : selectedDetailRows.value,
        selectedEntityActive: showStatic || !!selectedEntity.value,
        canEnableFocusMode: showStatic ? false : canEnableFocusMode.value,
        focusMode: showStatic ? false : focusMode.value,
        totalCounts: totalCounts.value,
        visibleCargoConnectionsCount: visibleCargoConnections.value.length,
        statsOverview: statsOverview.value,
    };
});

const filtersPanel = computed<MapFiltersPanelModel>(() => ({
    collapsed: filtersPanelCollapsed.value,
    sectionsOpen: { ...filterSectionsOpen },
    ui: ui.value,
    activeFilterChips: activeFilterChips.value,
    viewMode: viewMode.value,
    entityToggleOptions: entityToggleOptions.value,
    entityVisibility: readonly(entityVisibility),
    showAllLinks: showAllLinks.value,
    highlightOrphans: highlightOrphans.value,
    userAnnotationsOnly: userAnnotationsOnly.value,
    canEnableFocusMode: canEnableFocusMode.value,
    focusMode: focusMode.value,
    staticFilters: staticFiltersModel.value,
}));

const canvasToolbarPanel = computed<MapCanvasToolbarModel>(() => ({
    ui: ui.value,
    selectedEntityActive: !!selectedEntity.value || !!staticSelection.value,
    canEnableFocusMode: canEnableFocusMode.value,
    focusMode: focusMode.value,
    filtersOpen: !filtersPanelCollapsed.value,
    canCenterOnPlayer: canCenterOnPlayer.value,
}));

const notesPanel = computed<MapNotesPanelModel>(() => ({
    ui: ui.value,
    annotationMode: annotationMode.value,
    annotationEditMode: annotationEditMode.value,
    selectedAnnotation: selectedAnnotation.value,
    draft: annotationDraft.value,
    selectedZoneLocked: selectedZoneLocked.value,
    importError: importError.value,
}));

const viewerUpdatePanel = computed<MapViewerUpdateDialogModel>(() => ({
    open: viewerUpdateOpen.value,
    ui: ui.value,
    pluginVersion: pluginVersion.value,
    downloadUrl: viewerUpdateDownloadUrl.value,
    releaseUrl: viewerUpdateReleaseUrl.value,
    modPageUrl: viewerUpdateModPageUrl.value,
}));
</script>

<template>
    <div class="app-shell atlas-shell">
        <header class="app-header">
            <MapControlDock
                :panel="controlDockPanel"
                @toggle-settings="toggleControlSettings"
                @update:endpoint-draft="endpointDraft = $event"
                @endpoint-keydown="handleEndpointKeydown"
                @apply-endpoint="applyEndpoint(true)"
                @refresh="refreshData"
                @toggle-auto-refresh="autoRefresh = !autoRefresh"
                @update:refresh-interval-ms="updateRefreshInterval($event)"
                @update:icon-scale="iconScale = $event"
                @open-shortcuts="openShortcuts"
                @update:lang="lang = $event"
                @export-json="exportAnnotations"
                @import-json="handleImport"
            >
                <template #timeline>
                    <MapRupturePanel
                        :panel="rupturePanel"
                        @toggle-details="toggleRuptureDetails"
                        @close-details="closeRuptureDetails"
                    />
                </template>
            </MapControlDock>
        </header>

        <section
            class="card map-stage"
            :class="{ 'sidebar-open': !filtersPanelCollapsed }"
        >
            <MapCanvas
                ref="mapCanvasRef"
                :loading="status.loading"
                :cargo="cargo"
                :cargo-markers="displayedCargoMarkers"
                :cargo-connections="visibleCargoConnections"
                :teleporters="displayedTeleporters"
                :players="displayedPlayers"
                :pois="visiblePois"
                :selected-key="selectedKey"
                :selected-entity="selectedEntity"
                :orphan-keys="Array.from(orphanKeySet)"
                :focus-keys="Array.from(focusKeys)"
                :focus-cargo-key="focusCargoKey"
                :lang="lang"
                :icon-scale="iconScale"
                :user-markers="userMarkers"
                :user-zones="userZones"
                :annotation-mode="annotationMode"
                :annotation-edit-mode="annotationEditMode"
                :selected-annotation="selectedAnnotation"
                :static-series="staticData.series.value"
                :static-placements="staticData.placements.value"
                :static-pois="staticData.poiViews.value"
                :static-state-version="staticData.stateVersion.value"
                :static-selection="staticSelection"
                :static-series-visible="isStaticSeriesVisible"
                :static-placement-visible="isStaticPlacementVisible"
                :static-poi-visible="isStaticPoiVisible"
                :static-pick="pickStatic"
                :static-describe="describeStatic"
                @select="handleEntitySelect"
                @clear-selection="clearAllSelection"
                @hover="hoveredKey = $event"
                @select-annotation="handleAnnotationSelect"
                @select-static="handleStaticSelect"
                @create-marker="handleCreateMarker"
                @create-zone="handleCreateZone"
                @move-marker="handleMoveMarker"
                @update-zone="handleUpdateZone"
            />

            <MapCanvasToolbar
                :panel="canvasToolbarPanel"
                @reset="resetMapView"
                @center="centerCurrentSelection"
                @center-player="centerOnPlayer"
                @toggle-focus="toggleFocusMode"
                @toggle-filters="toggleFiltersPanel"
            />

            <MapNotesPanel
                v-if="notesPanel.selectedAnnotation || annotationMode !== 'idle' || notesPanel.importError"
                :panel="notesPanel"
                @toggle-mode="handleAnnotationModeToggle"
                @select-annotation="handleAnnotationSelect"
                @update:draft="handleDraftUpdate"
                @clear-selection="clearAnnotationSelection"
                @delete-selected="deleteSelectedAnnotation"
                @edit-selected="startSelectedAnnotationEdit"
                @toggle-zone-lock="toggleSelectedZoneLock"
            />

            <MapAnnotationFab
                :annotation-mode="annotationMode"
                :ui="ui"
                @toggle-mode="handleAnnotationModeToggle"
            />

            <MapSelectionPanel
                v-if="selectionPanel.selectedEntityActive"
                :panel="selectionPanel"
                @toggle-details="toggleDetailsPanel"
                @center="centerCurrentSelection"
                @toggle-focus="toggleFocusMode"
                @clear-selection="clearAllSelection"
                @open-shortcuts="openShortcuts"
            />

            <MapFiltersPanel
                :panel="filtersPanel"
                @toggle-collapse="toggleFiltersPanel"
                @toggle-section="toggleFilterSection"
                @clear="clearFilters"
                @toggle-entity="toggleEntity"
                @update:view-mode="viewMode = $event"
                @update:show-all-links="showAllLinks = $event"
                @update:highlight-orphans="highlightOrphans = $event"
                @update:user-annotations-only="userAnnotationsOnly = $event"
                @toggle-focus="toggleFocusMode"
                @static-toggle="handleStaticFilterToggle"
                @update:static-search="staticFilters.search.value = $event"
                @static-show-all="staticFilters.setAll(true)"
                @static-hide-all="staticFilters.setAll(false)"
            />
        </section>

        <MapShortcutDialog
            :open="shortcutsOpen"
            :ui="ui"
            :items="shortcutItems"
            @close="closeShortcuts"
        />

        <MapViewerUpdateDialog
            :panel="viewerUpdatePanel"
            @close="dismissViewerUpdate"
        />
    </div>
</template>
