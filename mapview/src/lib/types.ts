import type { Language, Messages } from "../lang";
import type { MapPreset } from "./mapPresets";
import type { StaticOrePurity } from "./staticMapCatalog";

export type CargoKind = "sender" | "receiver";
export type EntityToggleKey =
    | "sender"
    | "receiver"
    | "teleporter"
    | "player"
    | "abandonedBase"
    | "plantResource"
    | "ignitium"
    | "starTears";
export type StatusTone = "loading" | "online" | "stale" | "offline";
export type SelectionTone =
    | "sender"
    | "receiver"
    | "teleporter"
    | "player"
    | "neutral";
export type CommandStatTone = "primary" | "neutral" | "warn" | "good";
export type RupturePhaseKey =
    | "burning"
    | "cooling"
    | "stabilizing"
    | "stable"
    | "incoming";

export interface Point2D {
    x: number;
    y: number;
}

export interface Rect2D {
    x: number;
    y: number;
    width: number;
    height: number;
}

export interface Point3D extends Point2D {
    z: number;
}

export interface CargoMarker {
    unique_key: string;
    kind: CargoKind;
    label?: string;
    display_name?: string;
    resource?: string;
    source?: string;
    world: Point3D;
    map: Point2D;
}

export interface CargoConnectionEndpoint {
    map: Point2D;
}

export interface CargoConnection {
    sender_key: string;
    receiver_key: string;
    sender_label?: string;
    receiver_label?: string;
    item?: string;
    requested_amount?: number | null;
    sender: CargoConnectionEndpoint;
    receiver: CargoConnectionEndpoint;
}

export interface NamedMapEntity {
    unique_key: string;
    label?: string;
    source?: string;
    world: Point3D;
    map: Point2D;
}

export type Teleporter = NamedMapEntity;

export interface Player extends NamedMapEntity {
    /** True for the marker representing the local viewer's own player. Older plugins do not send this field. */
    self?: boolean;
}

export type PoiKind = "abandoned_base" | "plant_resource" | "ignitium" | "star_tears";
export type PoiState = "available" | "unavailable" | "unknown" | "depleted";

export interface Poi extends NamedMapEntity {
    kind: PoiKind;
    resource?: string;
    depleted?: boolean;
    state?: PoiState;
}

export interface CargoCounts {
    markers?: number;
    teleporters?: number;
    players?: number;
    pois?: number;
    abandoned_bases?: number;
    plant_resources?: number;
    ignitium?: number;
    star_tears?: number;
}

export interface MapProjection {
    content_width?: number;
    content_height?: number;
    /** World-space (Unreal centimetres) source rectangle used by the projection. */
    src_x1?: number;
    src_y1?: number;
    src_x2?: number;
    src_y2?: number;
    /** Map-image destination rectangle matching the source rectangle. */
    dst_x1?: number;
    dst_y1?: number;
    dst_x2?: number;
    dst_y2?: number;
    image_width?: number;
    image_height?: number;
}

export interface CargoResponse {
    generation: number;
    world?: string;
    counts?: CargoCounts;
    map?: MapProjection;
    markers: CargoMarker[];
    connections: CargoConnection[];
    teleporters: Teleporter[];
    players: Player[];
    /** Optional: older plugins do not send POIs. */
    pois?: Poi[];
}

export interface ViewerUpdateInfo {
    /** Absent on local development builds of the plugin. */
    download_url?: string;
    /** Absent on local development builds of the plugin. */
    release_url?: string;
    mod_page_url?: string;
}

export interface HealthResponse {
    ok?: boolean;
    plugin?: string;
    version?: number;
    world?: string;
    snapshot_generation?: number;
    marker_count: number;
    teleporter_count?: number;
    player_count?: number;
    /** Optional: older plugins do not send these viewer-update fields. */
    plugin_version?: string;
    viewer_contract_version?: number;
    viewer_update?: ViewerUpdateInfo;
}

export type SelectedEntity =
    | { type: "cargo"; raw: CargoMarker }
    | { type: "teleporter"; raw: Teleporter }
    | { type: "player"; raw: Player }
    | { type: "poi"; raw: Poi };

export interface EntityVisibility {
    sender: boolean;
    receiver: boolean;
    teleporter: boolean;
    player: boolean;
    abandonedBase: boolean;
    plantResource: boolean;
    ignitium: boolean;
    starTears: boolean;
}

export interface MapCanvasHandle {
    focusSelection: () => void;
    focusPoint: (mapX: number, mapY: number, desiredScale?: number) => void;
    resetView: () => void;
}

export interface RupturePhaseSeconds {
    burning?: number;
    cooling?: number;
    stabilizing?: number;
    stable?: number;
}

export interface RuptureTimelineInfo {
    cycle_total_seconds?: number;
    phase_seconds?: RupturePhaseSeconds;
}

export interface RuptureCycleState {
    available?: boolean;
    wave?: string;
    stage?: string;
    step?: string;
    elapsed_seconds?: number | null;
    observed_at_unix_ms?: number | null;
}

export interface RuptureCycleResponse {
    ok?: boolean;
    generation?: number;
    world?: string;
    timeline?: RuptureTimelineInfo;
    rupture_cycle?: RuptureCycleState;
}

export interface DetailRow {
    label: string;
    value: string;
}

export interface CommandStat {
    key: string;
    label: string;
    value: string;
    tone: CommandStatTone;
}

export interface RupturePhaseView {
    key: RupturePhaseKey;
    label: string;
    durationSeconds: number;
    durationLabel: string;
    startSeconds: number;
    endSeconds: number;
    visualStartPercent: number;
    visualEndPercent: number;
    widthPercent: number;
    active: boolean;
    statusLabel: string;
    shortStatusLabel: string;
    toneClass: string;
}

export interface ShortcutItem {
    keys: readonly string[];
    label: string;
    description: string;
}

export interface MapControlDockModel {
    settingsOpen: boolean;
    ui: Messages;
    mapMetaLabel: string;
    statusTone: StatusTone;
    statusBadgeLabel: string;
    commandStats: CommandStat[];
    endpointDraft: string;
    defaultEndpoint: string;
    endpointHasPendingChanges: boolean;
    normalizedEndpoint: string;
    languageOptions: Language[];
    lang: Language;
    autoRefresh: boolean;
    refreshIntervalMs: number;
    loading: boolean;
    statusText: string;
    statusError: string;
    currentTimeLabel: string;
    liveAgeLabel: string;
    iconScale: number;
}

export interface MapRupturePanelModel {
    /** Detail dropdown state. The strip itself always shows in the header. */
    detailsOpen: boolean;
    ui: Messages;
    currentPhaseKey: RupturePhaseKey;
    currentPhaseLabel: string;
    currentPhaseRemainingLabel: string;
    phases: RupturePhaseView[];
    markerPercent: number | null;
    markerLabel: string;
    hasLiveData: boolean;
}

export interface MapSelectionPanelModel {
    detailsExpanded: boolean;
    ui: Messages;
    selectedEntityKeyLabel: string;
    selectedEntitySummary: string;
    selectedEntityTone: SelectionTone;
    selectedDisplayName: string;
    selectedPreviewFacts: DetailRow[];
    selectedDetailRows: DetailRow[];
    selectedEntityActive: boolean;
    canEnableFocusMode: boolean;
    focusMode: boolean;
    totalCounts: {
        markers: number;
        teleporters: number;
        players: number;
        pois: number;
        abandonedBases: number;
        plantResources: number;
    };
    visibleCargoConnectionsCount: number;
    statsOverview: DetailRow[];
}

/** What clicking an active-filter chip undoes. */
export type ActiveFilterClear =
    | { kind: "preset" }
    | { kind: "showAllLinks" }
    | { kind: "highlightOrphans" }
    | { kind: "userAnnotationsOnly" }
    | { kind: "focusMode" }
    | { kind: "entity"; key: EntityToggleKey }
    | { kind: "poiGroup"; key: string };

export interface ActiveFilterChip {
    id: string;
    label: string;
    clear: ActiveFilterClear;
}

export interface HarvestOption {
    id: string;
    label: string;
    category: string;
    count: number;
    /** Above the point threshold: too numerous to read as individual markers. */
    common: boolean;
    color: string;
}

/** The filters sidebar shows exactly one of these at a time. */
export type FilterTabKey = "map" | "harvest" | "catalog" | "behavior";

export interface MapFiltersPanelModel {
    collapsed: boolean;
    activeTab: FilterTabKey;
    ui: Messages;
    /** How many filters deviate from the preset; drives the header line only. */
    activeFilterCount: number;
    preset: MapPreset;
    harvestResource: string | null;
    harvestOptions: HarvestOption[];
    /** Catalog elements actually drawn, as opposed to merely loaded. */
    staticVisibleCount: number;
    entityToggleOptions: Array<{
        key: EntityToggleKey;
        label: string;
        count: number;
    }>;
    entityVisibility: EntityVisibility;
    showAllLinks: boolean;
    highlightOrphans: boolean;
    userAnnotationsOnly: boolean;
    canEnableFocusMode: boolean;
    focusMode: boolean;
    /** Absent when the static world catalog is not bundled with the viewer. */
    staticFilters?: MapStaticFiltersModel;
}

// ── Static world catalog filters ─────────────────────────────────────────

export type StaticFilterScope =
    | "layer"
    | "poiGroup"
    | "resourceType"
    | "representation"
    | "orePurity"
    | "placementGroup";

export interface StaticFilterToggle {
    /** Explicit target for group actions, independent of sibling changes. */
    enabled?: boolean;
    scope: StaticFilterScope;
    key: string;
    /** Only set for placement groups, which are namespaced per layer. */
    layer?: string;
}

export interface StaticFilterOption {
    key: string;
    label: string;
    count: number;
    enabled: boolean;
    color?: string;
}

export interface StaticFilterCategory extends StaticFilterOption {
    types: StaticFilterOption[];
}

/** One ore in the deposits block: its veins, split by quality. */
export interface StaticDepositTypeOption extends StaticFilterOption {
    purityCounts: Array<{ key: StaticOrePurity; count: number; color: string }>;
}

/**
 * Extractor deposits, the only catalog elements carrying a quality. Grouped as
 * one block because "where do I put a drill, and on what quality?" is a single
 * question: the quality chips filter this block and nothing else.
 */
export interface MapStaticDepositsModel {
    count: number;
    purities: StaticFilterOption[];
    types: StaticDepositTypeOption[];
}

export interface StaticFilterPlacementSection {
    layer: string;
    title: string;
    options: StaticFilterOption[];
}

export interface MapStaticFiltersModel {
    ui: Messages;
    available: boolean;
    loading: boolean;
    error: string;
    search: string;
    loadedCount: number;
    layers: StaticFilterOption[];
    poiGroups: StaticFilterOption[];
    /** Everything gathered by hand, excluding the extractor deposits. */
    resourceCategories: StaticFilterCategory[];
    deposits: MapStaticDepositsModel;
    representations: StaticFilterOption[];
    placementSections: StaticFilterPlacementSection[];
}

export interface MapCanvasToolbarModel {
    ui: Messages;
    selectedEntityActive: boolean;
    canEnableFocusMode: boolean;
    focusMode: boolean;
    filtersOpen: boolean;
    canCenterOnPlayer: boolean;
}

export interface MapViewerUpdateDialogModel {
    open: boolean;
    ui: Messages;
    pluginVersion: string;
    downloadUrl: string;
    releaseUrl: string;
    modPageUrl: string;
}

// ── User Annotations ──────────────────────────────────────────────────────────

export type UserAnnotationMode = "idle" | "marker" | "zone";
export type UserAnnotationEditMode = "idle" | "move-marker" | "edit-zone";

export interface UserMarker {
    id: string;
    label: string;
    description: string;
    color: string;
    map: Point2D;
    createdAt: string;
}

export interface UserZone {
    id: string;
    label: string;
    description: string;
    color: string;
    locked: boolean;
    rect: Rect2D;
    createdAt: string;
}

export type UserAnnotationSelection =
    | { type: "marker"; id: string }
    | { type: "zone"; id: string }
    | null;

export interface UserAnnotationExport {
    version: 1;
    exportedAt: string;
    markers: UserMarker[];
    zones: UserZone[];
}

export interface UserAnnotationDraft {
    label: string;
    description: string;
    color: string;
}

export interface UserAnnotationSummary {
    id: string;
    type: "marker" | "zone";
    label: string;
    description: string;
    meta: string;
    createdAt: string;
}

export interface MapNotesPanelModel {
    ui: Messages;
    annotationMode: UserAnnotationMode;
    annotationEditMode: UserAnnotationEditMode;
    selectedAnnotation: UserAnnotationSelection;
    draft: UserAnnotationDraft;
    selectedZoneLocked: boolean;
    importError: string;
}
