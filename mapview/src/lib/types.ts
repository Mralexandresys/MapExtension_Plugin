import type { Language, Messages } from "../lang";

export type CargoKind = "sender" | "receiver";
export type EntityToggleKey = "sender" | "receiver" | "teleporter" | "player";
export type ViewMode = "network" | "resources" | "teleporters" | "players";
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

export interface Teleporter extends NamedMapEntity {}

export interface Player extends NamedMapEntity {}

export interface CargoCounts {
    markers?: number;
    teleporters?: number;
    players?: number;
}

export interface MapProjection {
    content_width?: number;
    content_height?: number;
    dst_x1?: number;
    dst_y1?: number;
    image_width?: number;
    image_height?: number;
}

export interface CargoResponse {
    generation: number;
    world?: string;
    reason?: string;
    counts?: CargoCounts;
    map?: MapProjection;
    markers: CargoMarker[];
    connections: CargoConnection[];
    teleporters: Teleporter[];
    players: Player[];
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
}

export type SelectedEntity =
    | { type: "cargo"; raw: CargoMarker }
    | { type: "teleporter"; raw: Teleporter }
    | { type: "player"; raw: Player };

export interface EntityVisibility {
    sender: boolean;
    receiver: boolean;
    teleporter: boolean;
    player: boolean;
}

export interface MapCanvasHandle {
    focusSelection: () => void;
    focusPoint: (mapX: number, mapY: number, desiredScale?: number) => void;
    resetView: () => void;
}

export interface EntityEntry {
    type: "cargo" | "teleporter" | "player";
    sortType: number;
    unique_key: string;
    label: string;
    badgeClass: EntityToggleKey;
    badgeLabel: string;
    meta1: string;
    meta2: string;
    orphan: boolean;
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

export interface RuptureTimelineTick {
    key: string;
    label: string;
    leftPercent: number;
    align: "left" | "center" | "right";
    stackLevel: number;
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
    loading: boolean;
    statusText: string;
    statusError: string;
    currentTimeLabel: string;
    liveAgeLabel: string;
    iconScale: number;
}

export interface MapRupturePanelModel {
    collapsed: boolean;
    ui: Messages;
    currentPhaseKey: RupturePhaseKey;
    currentPhaseLabel: string;
    currentPhaseRemainingLabel: string;
    phases: RupturePhaseView[];
    markerPercent: number | null;
    markerLabel: string;
    hasLiveData: boolean;
    timelineTicks: RuptureTimelineTick[];
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
    totalCounts: { markers: number; teleporters: number; players: number };
    visibleCargoConnectionsCount: number;
    statsOverview: DetailRow[];
}

export interface MapFiltersPanelModel {
    collapsed: boolean;
    ui: Messages;
    activeFilterChips: string[];
    viewMode: ViewMode;
    entityToggleOptions: Array<{
        key: EntityToggleKey;
        label: string;
        count: number;
    }>;
    entityVisibility: EntityVisibility;
    showAllLinks: boolean;
    highlightOrphans: boolean;
    canEnableFocusMode: boolean;
    focusMode: boolean;
}

export interface MapCanvasToolbarModel {
    ui: Messages;
    selectedEntityActive: boolean;
    canEnableFocusMode: boolean;
    focusMode: boolean;
    filtersOpen: boolean;
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
