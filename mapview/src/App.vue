<script setup lang="ts">
import { computed, readonly, ref } from "vue";

import MapAnnotationFab from "./components/mapview/MapAnnotationFab.vue";
import MapCanvas from "./components/MapCanvas.vue";
import MapCanvasToolbar from "./components/mapview/MapCanvasToolbar.vue";
import MapControlDock from "./components/mapview/MapControlDock.vue";
import MapFiltersPanel from "./components/mapview/MapFiltersPanel.vue";
import MapNotesPanel from "./components/mapview/MapNotesPanel.vue";
import MapRupturePanel from "./components/mapview/MapRupturePanel.vue";
import MapSelectionPanel from "./components/mapview/MapSelectionPanel.vue";
import MapShortcutDialog from "./components/mapview/MapShortcutDialog.vue";
import { useMapViewState } from "./composables/useMapViewState";
import { useUserAnnotations } from "./composables/useUserAnnotations";
import type {
    MapCanvasHandle,
    MapCanvasToolbarModel,
    MapControlDockModel,
    MapFiltersPanelModel,
    MapNotesPanelModel,
    MapRupturePanelModel,
    MapSelectionPanelModel,
    Rect2D,
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
    focusMode,
    viewMode,
    autoRefresh,
    refreshIntervalMs,
    iconScale,
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
    entityToggleOptions,
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
    updateRefreshInterval,
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
    openShortcuts,
    closeShortcuts,
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
} = useUserAnnotations();

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
}

function centerSelectedAnnotation(): void {
    const sel = selectedAnnotation.value;
    if (!sel) return;
    if (sel.type === "marker") {
        const m = userMarkers.value.find((x) => x.id === sel.id);
        if (m) mapCanvasRef.value?.focusPoint(m.map.x, m.map.y);
    } else {
        const z = userZones.value.find((x) => x.id === sel.id);
        if (z) mapCanvasRef.value?.focusPoint(
            z.rect.x + z.rect.width / 2,
            z.rect.y + z.rect.height / 2,
        );
    }
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
    collapsed: rupturePanelCollapsed.value,
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

const selectionPanel = computed<MapSelectionPanelModel>(() => ({
    detailsExpanded: detailsPanelExpanded.value,
    ui: ui.value,
    selectedEntityKeyLabel: selectedEntityKeyLabel.value,
    selectedEntitySummary: selectedEntitySummary.value,
    selectedEntityTone: selectedEntityTone.value,
    selectedDisplayName: selectedDisplayName.value,
    selectedPreviewFacts: selectedPreviewFacts.value,
    selectedDetailRows: selectedDetailRows.value,
    selectedEntityActive: !!selectedEntity.value,
    canEnableFocusMode: canEnableFocusMode.value,
    focusMode: focusMode.value,
    totalCounts: totalCounts.value,
    visibleCargoConnectionsCount: visibleCargoConnections.value.length,
    statsOverview: statsOverview.value,
}));

const filtersPanel = computed<MapFiltersPanelModel>(() => ({
    collapsed: filtersPanelCollapsed.value,
    ui: ui.value,
    activeFilterChips: activeFilterChips.value,
    viewMode: viewMode.value,
    entityToggleOptions: entityToggleOptions.value,
    entityVisibility: readonly(entityVisibility),
    showAllLinks: showAllLinks.value,
    highlightOrphans: highlightOrphans.value,
    canEnableFocusMode: canEnableFocusMode.value,
    focusMode: focusMode.value,
}));

const canvasToolbarPanel = computed<MapCanvasToolbarModel>(() => ({
    ui: ui.value,
    selectedEntityActive: !!selectedEntity.value,
    canEnableFocusMode: canEnableFocusMode.value,
    focusMode: focusMode.value,
    filtersOpen: !filtersPanelCollapsed.value,
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
            />
        </header>

        <section class="card map-stage">
            <MapCanvas
                ref="mapCanvasRef"
                :loading="status.loading"
                :cargo="cargo"
                :cargo-markers="displayedCargoMarkers"
                :cargo-connections="visibleCargoConnections"
                :teleporters="displayedTeleporters"
                :players="displayedPlayers"
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
                @select="handleEntitySelect"
                @clear-selection="clearAllSelection"
                @hover="hoveredKey = $event"
                @select-annotation="handleAnnotationSelect"
                @create-marker="handleCreateMarker"
                @create-zone="handleCreateZone"
                @move-marker="handleMoveMarker"
                @update-zone="handleUpdateZone"
            />

            <MapCanvasToolbar
                :panel="canvasToolbarPanel"
                @reset="
                    () => {
                        clearFilters();
                        resetMapView();
                    }
                "
                @center="centerSelection"
                @toggle-focus="toggleFocusMode"
                @toggle-filters="toggleFiltersPanel"
            />

            <MapRupturePanel
                :panel="rupturePanel"
                @toggle-collapse="toggleRupturePanel"
            />

            <MapNotesPanel
                v-if="notesPanel.selectedAnnotation || annotationMode !== 'idle' || notesPanel.importError"
                :panel="notesPanel"
                @toggle-mode="setAnnotationMode"
                @select-annotation="handleAnnotationSelect"
                @update:draft="handleDraftUpdate"
                @clear-selection="clearAnnotationSelection"
                @delete-selected="deleteSelectedAnnotation"
                @center-selected="startSelectedAnnotationEdit"
                @toggle-zone-lock="toggleSelectedZoneLock"
            />

            <MapAnnotationFab
                :annotation-mode="annotationMode"
                @toggle-mode="setAnnotationMode"
            />

            <MapSelectionPanel
                v-if="selectionPanel.selectedEntityActive"
                :panel="selectionPanel"
                @toggle-details="toggleDetailsPanel"
                @center="centerSelection"
                @toggle-focus="toggleFocusMode"
                @clear-selection="clearSelection"
                @open-shortcuts="openShortcuts"
            />

            <MapFiltersPanel
                :panel="filtersPanel"
                @toggle-collapse="toggleFiltersPanel"
                @clear="clearFilters"
                @toggle-entity="toggleEntity"
                @update:view-mode="viewMode = $event"
                @update:show-all-links="showAllLinks = $event"
                @update:highlight-orphans="highlightOrphans = $event"
                @toggle-focus="toggleFocusMode"
            />
        </section>

        <MapShortcutDialog
            :open="shortcutsOpen"
            :ui="ui"
            :items="shortcutItems"
            @close="closeShortcuts"
        />
    </div>
</template>
