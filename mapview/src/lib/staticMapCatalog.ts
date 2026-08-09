/**
 * Static map catalog: types, loader and decoder for the pre-generated
 * StarRupture world data shipped next to `MapExtensionViewer.html`.
 *
 * The catalog is produced by `tools/build_map_data.py` into `map-data/`.
 * Files are plain compact JSON wrapped in a JSONP call because the viewer is
 * usually opened through `file://`, where `fetch()` is blocked but classic
 * `<script>` tags still work.
 *
 * Positions are stored as integers in decimetres of Unreal world space
 * (centimetres / 10) and converted to map space by the renderer, using the
 * exact same projection as the plugin.
 */

export const STATIC_DATA_BASE_PATH = "map-data";
export const STATIC_DATA_CALLBACK = "SRMAPDATA";

export type StaticLayerKey = "poi" | "resource" | "building" | "zone" | "technical";

export const STATIC_LAYER_KEYS: readonly StaticLayerKey[] = [
    "poi",
    "resource",
    "building",
    "zone",
    "technical",
];

/** Layers loaded and displayed as soon as the catalog is available. */
export const DEFAULT_ENABLED_LAYERS: readonly StaticLayerKey[] = ["poi", "resource"];

export type StaticResourceKind = "pcg" | "actor";

export const STATIC_RESOURCE_KINDS: readonly StaticResourceKind[] = ["pcg", "actor"];

/** Resource representations shown by default. */
export const DEFAULT_ENABLED_KINDS: readonly StaticResourceKind[] = ["pcg", "actor"];

export type StaticElementState =
    | "unknown"
    | "available"
    | "depleted"
    | "permanently_depleted";

export interface StaticManifestPart {
    id: string;
    layer: StaticLayerKey;
    url: string;
    count: number;
    bytes: number;
    category?: string;
    groups?: Record<string, number>;
}

export interface StaticResourceType {
    category: string;
    en: string;
    fr: string;
    counts: Partial<Record<StaticResourceKind, number>>;
    total: number;
}

export interface StaticMapManifest {
    id: "manifest";
    version: number;
    generated_at: string;
    position_scale_cm: number;
    altitude_scale_cm: number;
    resource_kinds: StaticResourceKind[];
    resource_types: Record<string, StaticResourceType>;
    poi_groups: Record<string, number>;
    placement_groups: Record<string, Record<string, number>>;
    parts: StaticManifestPart[];
    rupture?: Record<string, unknown>;
}

interface RawResourceGroup {
    t: string;
    k: number;
    n: number;
    x: number[];
    y: number[];
    z: number[];
}

interface RawResourcePart {
    id: string;
    kind: "resources";
    category: string;
    groups: RawResourceGroup[];
}

type RawPlacementRow = [
    group: number,
    actorType: number,
    x: number,
    y: number,
    z: number,
    label: string,
    boxes?: number[][],
];

interface RawPlacementPart {
    id: string;
    kind: "placements";
    groups: string[];
    actor_types: string[];
    rows: RawPlacementRow[];
}

interface RawPoiRow {
    g: string;
    x: number;
    y: number;
    z: number;
    en: string;
    fr: string;
    den: string;
    dfr: string;
    guid: string;
}

interface RawPoiPart {
    id: string;
    kind: "pois";
    rows: RawPoiRow[];
}

type RawPart = RawResourcePart | RawPlacementPart | RawPoiPart;

// ── Decoded runtime model ─────────────────────────────────────────────────────

/** A batch of same-type points, stored column-wise for canvas rendering. */
export interface StaticPointSeries {
    key: string;
    layer: StaticLayerKey;
    /** Filter group: resource type id for resources, category group otherwise. */
    group: string;
    kind: StaticResourceKind;
    category: string;
    color: string;
    count: number;
    /** Unreal world decimetres. */
    x: Int32Array;
    y: Int32Array;
    /** Altitude in metres. */
    z: Int16Array;
    /** Per-point dynamic state, filled from live plugin observations. */
    state: Uint8Array;
}

export interface StaticBox {
    x: number;
    y: number;
    extentX: number;
    extentY: number;
    yaw: number;
}

export interface StaticPlacement {
    key: string;
    layer: StaticLayerKey;
    group: string;
    actorType: string;
    label: string;
    x: number;
    y: number;
    z: number;
    boxes?: StaticBox[];
}

export interface StaticPoi {
    key: string;
    group: string;
    x: number;
    y: number;
    z: number;
    nameEn: string;
    nameFr: string;
    descriptionEn: string;
    descriptionFr: string;
    guid: string;
}

export interface StaticPartData {
    id: string;
    layer: StaticLayerKey;
    series: StaticPointSeries[];
    placements: StaticPlacement[];
    pois: StaticPoi[];
}

// ── Palette ───────────────────────────────────────────────────────────────────

const RESOURCE_COLORS: Record<string, string> = {
    titanium: "#d4d4d8",
    goethite: "#b45309",
    wolfram: "#60a5fa",
    sulphur: "#facc15",
    calcium: "#fde68a",
    quartz: "#f0abfc",
    helium_3: "#22d3ee",
    unknown_ore: "#94a3b8",
    hydrobulb: "#38bdf8",
    polifruit: "#f472b6",
    serpent_root: "#84cc16",
    nootka_lupine: "#a78bfa",
    thornfruit: "#ef4444",
    purplant: "#c084fc",
    oxallop: "#fb923c",
    sikkim_rhubarb: "#fda4af",
    aggressive_plant: "#dc2626",
    gold_fruit: "#fbbf24",
    soulheart: "#f87171",
    coralion_egg: "#2dd4bf",
    fox_egg: "#fb7185",
    skylisk: "#fde047",
};

const GROUP_COLORS: Record<string, string> = {
    cave: "#94a3b8",
    abandoned_base: "#f59e0b",
    forgotten_engine: "#a855f7",
    antenna: "#38bdf8",
    obelisk: "#c084fc",
    monument: "#e879f9",
    orbital_lander: "#22d3ee",
    ore_actor: "#f97316",
    spawn_zone: "#22c55e",
    exclusion_zone: "#ef4444",
    spawn_region: "#3b82f6",
    resource_marker: "#eab308",
    deposit_socket: "#14b8a6",
    poi_proxy: "#64748b",
};

export const STATE_COLORS: Record<StaticElementState, string | null> = {
    unknown: null,
    available: "#4ade80",
    depleted: "#6b7280",
    permanently_depleted: "#450a0a",
};

export const STATE_CODES: readonly StaticElementState[] = [
    "unknown",
    "available",
    "depleted",
    "permanently_depleted",
];

function hashColor(identifier: string): string {
    let hash = 0;
    for (let index = 0; index < identifier.length; index += 1) {
        hash = (hash * 31 + identifier.charCodeAt(index)) >>> 0;
    }
    return `hsl(${hash % 360}, 68%, 62%)`;
}

export function resourceColor(typeId: string): string {
    return RESOURCE_COLORS[typeId] ?? hashColor(typeId);
}

export function groupColor(group: string): string {
    return GROUP_COLORS[group] ?? hashColor(group);
}

// ── JSONP loading ─────────────────────────────────────────────────────────────

type JsonpPayload = { id: string } & Record<string, unknown>;

interface PendingPart {
    resolve: (payload: JsonpPayload) => void;
    reject: (error: Error) => void;
}

const pending = new Map<string, PendingPart>();

function ensureCallback(): void {
    const host = window as unknown as Record<string, unknown>;
    if (typeof host[STATIC_DATA_CALLBACK] === "function") return;
    host[STATIC_DATA_CALLBACK] = (payload: JsonpPayload) => {
        const entry = payload && pending.get(payload.id);
        if (!entry) return;
        pending.delete(payload.id);
        entry.resolve(payload);
    };
}

function loadScript(id: string, url: string): Promise<JsonpPayload> {
    ensureCallback();
    return new Promise<JsonpPayload>((resolve, reject) => {
        const previous = pending.get(id);
        if (previous) {
            previous.reject(new Error(`Duplicate load for ${id}`));
        }
        pending.set(id, { resolve, reject });

        const script = document.createElement("script");
        script.src = url;
        script.async = true;
        script.addEventListener("error", () => {
            pending.delete(id);
            script.remove();
            reject(new Error(`Unable to load ${url}`));
        });
        script.addEventListener("load", () => {
            script.remove();
            // The callback fires while the script executes; anything still
            // pending here means the file did not register a payload.
            if (pending.has(id)) {
                pending.delete(id);
                reject(new Error(`No catalog payload in ${url}`));
            }
        });
        document.head.appendChild(script);
    });
}

export async function loadStaticManifest(): Promise<StaticMapManifest> {
    const payload = await loadScript(
        "manifest",
        `${STATIC_DATA_BASE_PATH}/manifest.js`,
    );
    return payload as unknown as StaticMapManifest;
}

export async function loadStaticPart(
    part: StaticManifestPart,
): Promise<StaticPartData> {
    const payload = (await loadScript(part.id, part.url)) as unknown as RawPart;
    return decodePart(part, payload);
}

// ── Decoding ──────────────────────────────────────────────────────────────────

function decodeResourcePart(
    part: StaticManifestPart,
    payload: RawResourcePart,
): StaticPointSeries[] {
    return payload.groups.map((group) => {
        const count = group.n;
        const x = new Int32Array(count);
        const y = new Int32Array(count);
        const z = new Int16Array(count);
        let currentX = 0;
        let currentY = 0;
        let currentZ = 0;
        for (let index = 0; index < count; index += 1) {
            currentX += group.x[index];
            currentY += group.y[index];
            currentZ += group.z[index];
            x[index] = currentX;
            y[index] = currentY;
            z[index] = currentZ;
        }
        const kind = STATIC_RESOURCE_KINDS[group.k] ?? "pcg";
        return {
            key: `${part.id}:${group.t}:${kind}`,
            layer: part.layer,
            group: group.t,
            kind,
            category: payload.category,
            color: resourceColor(group.t),
            count,
            x,
            y,
            z,
            state: new Uint8Array(count),
        };
    });
}

function decodePlacementPart(
    part: StaticManifestPart,
    payload: RawPlacementPart,
): StaticPlacement[] {
    return payload.rows.map((row, index) => {
        const boxes = row[6];
        return {
            key: `${part.id}:${index}`,
            layer: part.layer,
            group: payload.groups[row[0]] ?? "unknown",
            actorType: payload.actor_types[row[1]] ?? "",
            label: row[5] ?? "",
            x: row[2],
            y: row[3],
            z: row[4],
            boxes: boxes?.map((box) => ({
                x: box[0],
                y: box[1],
                extentX: box[2],
                extentY: box[3],
                yaw: box[4],
            })),
        };
    });
}

function decodePoiPart(payload: RawPoiPart): StaticPoi[] {
    return payload.rows.map((row, index) => ({
        key: `poi:${row.guid || index}`,
        group: row.g,
        x: row.x,
        y: row.y,
        z: row.z,
        nameEn: row.en,
        nameFr: row.fr,
        descriptionEn: row.den,
        descriptionFr: row.dfr,
        guid: row.guid,
    }));
}

function decodePart(part: StaticManifestPart, payload: RawPart): StaticPartData {
    const data: StaticPartData = {
        id: part.id,
        layer: part.layer,
        series: [],
        placements: [],
        pois: [],
    };

    if (payload.kind === "resources") {
        data.series = decodeResourcePart(part, payload);
    } else if (payload.kind === "placements") {
        data.placements = decodePlacementPart(part, payload);
    } else if (payload.kind === "pois") {
        data.pois = decodePoiPart(payload);
    }

    return data;
}

// ── Label helpers ─────────────────────────────────────────────────────────────

export function resourceTypeLabel(
    manifest: StaticMapManifest | null,
    typeId: string,
    lang: string,
): string {
    const entry = manifest?.resource_types?.[typeId];
    if (!entry) return typeId;
    return lang === "fr" ? entry.fr : entry.en;
}

/** Maps a plugin-provided resource label back to a catalog type id. */
export function buildResourceLabelIndex(
    manifest: StaticMapManifest | null,
): Map<string, string> {
    const index = new Map<string, string>();
    if (!manifest) return index;
    for (const [typeId, entry] of Object.entries(manifest.resource_types)) {
        index.set(typeId.toLowerCase(), typeId);
        index.set(entry.en.toLowerCase(), typeId);
        index.set(entry.fr.toLowerCase(), typeId);
    }
    return index;
}
