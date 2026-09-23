import { computed, onMounted, ref, shallowRef, type Ref } from "vue";

import { worldToMap, type MapProjectionConstants } from "../lib/mapProjection";
import {
    buildResourceLabelIndex,
    loadStaticManifest,
    loadStaticPart,
    STATE_CODES,
    type StaticElementState,
    type StaticLayerKey,
    STATIC_ORE_CONFIDENCE_LEVELS,
    STATIC_ORE_PURITY_LEVELS,
    type StaticManifestPart,
    type StaticMapManifest,
    type StaticPlacement,
    type StaticPoi,
    type StaticPointSeries,
    type StaticResourceKind,
    type StaticOreConfidence,
    type StaticOrePurity,
} from "../lib/staticMapCatalog";
import type { Poi, Point2D } from "../lib/types";

/** World decimetres tolerated when matching a live observation to a catalog point. */
const DYNAMIC_MATCH_RADIUS_DM = 15; // 150 cm
const MATCH_CELL_DM = 15;
const PICK_CELL_DM = 200; // 20 m cells keep viewport hit-tests inexpensive.

export interface StaticSelection {
    kind: "resource" | "placement" | "poi";
    key: string;
    layer: StaticLayerKey;
    group: string;
    /** World decimetres. */
    x: number;
    y: number;
    /** Altitude in metres. */
    z: number;
    state: StaticElementState;
    seriesKey?: string;
    index?: number;
    representation?: StaticResourceKind;
    purity?: StaticOrePurity;
    /** How that purity was established; absent outside extractor deposits. */
    purityConfidence?: StaticOreConfidence;
    category?: string;
    actorType?: string;
    label?: string;
    guid?: string;
    poi?: StaticPoi;
}

export interface StaticMapPoiView extends StaticPoi {
    map: Point2D;
}

function cellKey(x: number, y: number, cellSize: number): string {
    return `${Math.floor(x / cellSize)},${Math.floor(y / cellSize)}`;
}

export function useStaticMapData(projection: Ref<MapProjectionConstants>) {
    const manifest = shallowRef<StaticMapManifest | null>(null);
    const series = shallowRef<StaticPointSeries[]>([]);
    const placements = shallowRef<StaticPlacement[]>([]);
    const pois = shallowRef<StaticPoi[]>([]);
    const loading = ref(false);
    const error = ref("");
    const available = ref(false);
    const stateVersion = ref(0);

    const loadedParts = new Set<string>();
    const inFlight = new Map<string, Promise<void>>();
    const matchIndexes = new Map<string, Map<string, number[]>>();
    const pickIndexes = new Map<string, Map<string, number[]>>();
    const dirtyStateSeries = new Set<string>();

    const resourceLabelIndex = computed(() => buildResourceLabelIndex(manifest.value));

    const poiViews = computed<StaticMapPoiView[]>(() => {
        const current = projection.value;
        return pois.value.map((poi) => ({
            ...poi,
            map: worldToMap({ x: poi.x * 10, y: poi.y * 10 }, current),
        }));
    });

    const totalPointCount = computed(() =>
        series.value.reduce((total, entry) => total + entry.count, 0),
    );

    async function loadPart(part: StaticManifestPart): Promise<void> {
        if (loadedParts.has(part.id)) return;
        const running = inFlight.get(part.id);
        if (running) return running;

        const task = loadStaticPart(part)
            .then((data) => {
                loadedParts.add(part.id);
                if (data.series.length) {
                    series.value = [...series.value, ...data.series];
                }
                if (data.placements.length) {
                    placements.value = [...placements.value, ...data.placements];
                }
                if (data.pois.length) {
                    pois.value = [...pois.value, ...data.pois];
                }
            })
            .catch((cause: unknown) => {
                error.value = cause instanceof Error ? cause.message : String(cause);
            })
            .finally(() => {
                inFlight.delete(part.id);
                loading.value = inFlight.size > 0;
            });

        inFlight.set(part.id, task);
        loading.value = true;
        return task;
    }

    /** Loads every part backing the given layers; already loaded parts are skipped. */
    async function ensureLayers(layers: readonly StaticLayerKey[]): Promise<void> {
        const current = manifest.value;
        if (!current) return;
        const wanted = new Set(layers);
        await Promise.all(
            current.parts
                .filter((part) => wanted.has(part.layer))
                .map((part) => loadPart(part)),
        );
    }

    async function initialize(): Promise<void> {
        if (manifest.value) return;
        loading.value = true;
        try {
            manifest.value = await loadStaticManifest();
            available.value = true;
        } catch (cause: unknown) {
            available.value = false;
            error.value = cause instanceof Error ? cause.message : String(cause);
        } finally {
            loading.value = inFlight.size > 0;
        }
    }

    function pointIndexFor(
        entry: StaticPointSeries,
        indexes: Map<string, Map<string, number[]>>,
        cellSize: number,
    ): Map<string, number[]> {
        const cached = indexes.get(entry.key);
        if (cached) return cached;

        const index = new Map<string, number[]>();
        for (let position = 0; position < entry.count; position += 1) {
            const key = cellKey(entry.x[position], entry.y[position], cellSize);
            const bucket = index.get(key);
            if (bucket) bucket.push(position);
            else index.set(key, [position]);
        }
        indexes.set(entry.key, index);
        return index;
    }

    function findClosestInSeries(
        entry: StaticPointSeries,
        x: number,
        y: number,
        radius: number,
        indexes = matchIndexes,
        cellSize = MATCH_CELL_DM,
        accept?: (position: number) => boolean,
    ): number {
        const index = pointIndexFor(entry, indexes, cellSize);
        const baseX = Math.floor(x / cellSize);
        const baseY = Math.floor(y / cellSize);
        let best = -1;
        let bestDistance = radius * radius;

        const cellRadius = Math.ceil(radius / cellSize);
        for (let offsetX = -cellRadius; offsetX <= cellRadius; offsetX += 1) {
            for (let offsetY = -cellRadius; offsetY <= cellRadius; offsetY += 1) {
                const bucket = index.get(`${baseX + offsetX},${baseY + offsetY}`);
                if (!bucket) continue;
                for (const position of bucket) {
                    if (accept && !accept(position)) continue;
                    const deltaX = entry.x[position] - x;
                    const deltaY = entry.y[position] - y;
                    const distance = deltaX * deltaX + deltaY * deltaY;
                    if (distance <= bestDistance) {
                        bestDistance = distance;
                        best = position;
                    }
                }
            }
        }
        return best;
    }

    /**
     * Folds live plugin observations into the static catalog.
     *
     * A matched catalog point receives the observed state and keeps its own
     * filters. Only unmatched observations need a separate runtime marker.
     */
    function applyDynamicPois(livePois: readonly Poi[]): Set<string> {
        const matched = new Set<string>();

        for (const key of dirtyStateSeries) {
            const entry = series.value.find((candidate) => candidate.key === key);
            entry?.state.fill(0);
        }
        dirtyStateSeries.clear();

        if (!livePois.length || !series.value.length) {
            stateVersion.value += 1;
            return matched;
        }

        const labels = resourceLabelIndex.value;
        for (const poi of livePois) {
            if (poi.kind !== "plant_resource") continue;
            const typeId = labels.get((poi.resource ?? "").trim().toLowerCase());
            if (!typeId) continue;

            const x = Math.round(poi.world.x / 10);
            const y = Math.round(poi.world.y / 10);
            let bestSeries: StaticPointSeries | null = null;
            let bestIndex = -1;

            for (const entry of series.value) {
                if (entry.layer !== "resource" || entry.group !== typeId) continue;
                const position = findClosestInSeries(entry, x, y, DYNAMIC_MATCH_RADIUS_DM);
                if (position >= 0) {
                    bestSeries = entry;
                    bestIndex = position;
                    break;
                }
            }

            if (!bestSeries || bestIndex < 0) continue;
            bestSeries.state[bestIndex] = poi.depleted ? 2 : 1;
            dirtyStateSeries.add(bestSeries.key);
            matched.add(poi.unique_key);
        }

        stateVersion.value += 1;
        return matched;
    }

    /** Read the latest observation even when the point was selected before it arrived. */
    function getSelectionState(selection: StaticSelection): StaticElementState {
        if (selection.kind !== "resource" || selection.index === undefined) {
            return selection.state;
        }
        // The compact state arrays are not reactive; their revision invalidates
        // open details when applyDynamicPois updates the observed availability.
        void stateVersion.value;
        const entry = series.value.find((candidate) => candidate.key === selection.seriesKey);
        return STATE_CODES[entry?.state[selection.index] ?? 0] ?? "unknown";
    }

    /** Nearest catalog point to a world position, used for hover and selection. */
    function findNearest(
        x: number,
        y: number,
        radius: number,
        isSeriesVisible: (entry: StaticPointSeries) => boolean,
        isPlacementVisible: (entry: StaticPlacement) => boolean,
        isOrePurityVisible?: (code: number) => boolean,
    ): StaticSelection | null {
        let best: StaticSelection | null = null;
        let bestDistance = radius * radius;

        for (const entry of series.value) {
            if (!isSeriesVisible(entry)) continue;
            const purity = entry.purity;
            const position = findClosestInSeries(
                entry,
                x,
                y,
                Math.sqrt(bestDistance),
                pickIndexes,
                PICK_CELL_DM,
                purity && isOrePurityVisible
                    ? (candidate) => isOrePurityVisible(purity[candidate])
                    : undefined,
            );
            if (position < 0) continue;

            const deltaX = entry.x[position] - x;
            const deltaY = entry.y[position] - y;
            const distance = deltaX * deltaX + deltaY * deltaY;
            if (distance > bestDistance) continue;
            bestDistance = distance;
            best = {
                kind: "resource",
                key: `${entry.key}#${position}`,
                layer: entry.layer,
                group: entry.group,
                category: entry.category,
                representation: entry.kind,
                purity: entry.purity
                    ? STATIC_ORE_PURITY_LEVELS[entry.purity[position]] ?? "unknown"
                    : undefined,
                purityConfidence: entry.purity
                    ? entry.confidence
                        ? STATIC_ORE_CONFIDENCE_LEVELS[entry.confidence[position]] ??
                          "exact"
                        : "exact"
                    : undefined,
                seriesKey: entry.key,
                index: position,
                x: entry.x[position],
                y: entry.y[position],
                z: entry.z[position],
                state: STATE_CODES[entry.state[position]] ?? "unknown",
            };
        }

        for (const entry of placements.value) {
            if (!isPlacementVisible(entry)) continue;
            const deltaX = entry.x - x;
            const deltaY = entry.y - y;
            const distance = deltaX * deltaX + deltaY * deltaY;
            if (distance > bestDistance) continue;
            bestDistance = distance;
            best = {
                kind: "placement",
                key: entry.key,
                layer: entry.layer,
                group: entry.group,
                actorType: entry.actorType,
                label: entry.label,
                x: entry.x,
                y: entry.y,
                z: entry.z,
                state: "unknown",
            };
        }

        return best;
    }

    onMounted(() => {
        void initialize();
    });

    return {
        manifest,
        series,
        placements,
        pois,
        poiViews,
        loading,
        error,
        available,
        stateVersion,
        totalPointCount,
        resourceLabelIndex,
        ensureLayers,
        applyDynamicPois,
        getSelectionState,
        findNearest,
    };
}
