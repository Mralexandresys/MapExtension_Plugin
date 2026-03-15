export type CargoKind = "sender" | "receiver";
export type EntityToggleKey = "sender" | "receiver" | "teleporter" | "player";
export type ViewMode = "network" | "resources" | "teleporters" | "players";
export type SidebarTab = "entities" | "stats";

export interface Point2D {
    x: number;
    y: number;
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
