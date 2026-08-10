/**
 * Marker silhouettes for the canonical points of interest.
 *
 * The 241 POI used to share one teardrop pin whose only distinguishing feature
 * was a colour — and four of the six group colours formed two near-identical
 * pairs. A shape per family is what actually makes them readable.
 *
 * Every symbol is authored in the same 24x24 box, anchored on its centre, and
 * consumed twice from here:
 *
 * - `MapCanvas.vue` emits them once as SVG `<symbol>` and references them with
 *   `<use>`, the pattern already used for the teleporter icon;
 * - `MapFiltersPanel.vue` inlines the same paths in the filter chips.
 *
 * Because both sides read this table, the filter list *is* the map legend and
 * cannot drift from it.
 */

export interface PoiSymbol {
    /** Filled silhouette. */
    body: string;
    /** Optional second path drawn over the body. */
    detail?: string;
    /** How `detail` is painted. */
    detailMode?: "fill" | "stroke";
}

export const POI_SYMBOL_VIEWBOX = "0 0 24 24";

/** Prefix shared by the `<symbol>` ids emitted into the map SVG. */
export const POI_SYMBOL_ID_PREFIX = "poi-symbol";

export function poiSymbolId(group: string): string {
    return `${POI_SYMBOL_ID_PREFIX}-${group}`;
}

const FALLBACK_SYMBOL: PoiSymbol = {
    body: "M 12 22 L 5 10 A 7 7 0 1 1 19 10 Z",
    detail: "M 9.4 9 A 2.6 2.6 0 1 0 14.6 9 A 2.6 2.6 0 1 0 9.4 9 Z",
    detailMode: "fill",
};

export const POI_SYMBOLS: Record<string, PoiSymbol> = {
    // Tunnel mouth: an entrance you walk into.
    cave: {
        body: "M 2.5 21 V 13 A 9.5 9.5 0 0 1 21.5 13 V 21 Z",
        detail: "M 8.5 21 V 15.5 A 3.5 3.5 0 0 1 15.5 15.5 V 21 Z",
        detailMode: "fill",
    },
    // Tall vertical diamond: reads as a standing monolith, and as a hazard.
    obelisk: {
        body: "M 12 1 L 17.5 12 L 12 23 L 6.5 12 Z",
    },
    // Emitter: a solid core with two emission arcs, both sized to survive the
    // 17px rendering used by the filter chips.
    antenna: {
        body: "M 7 12 A 5 5 0 1 0 17 12 A 5 5 0 1 0 7 12 Z",
        detail: "M 4.5 5 A 10 10 0 0 0 4.5 19 M 19.5 5 A 10 10 0 0 1 19.5 19",
        detailMode: "stroke",
    },
    // Same cracked-building silhouette already drawn for live abandoned bases,
    // rescaled into the 24x24 box.
    abandoned_base: {
        body: "M 2 20 V 7 H 7 V 11 H 12 V 4 H 20 V 20 Z",
        detail: "M 14 4 L 11 10 L 15 13 L 12 20",
        detailMode: "stroke",
    },
    // Hexagon with a hollow core: a single major structure.
    forgotten_engine: {
        body: "M 12 1.5 L 21.5 6.75 L 21.5 17.25 L 12 22.5 L 2.5 17.25 L 2.5 6.75 Z",
        detail: "M 12 8 L 16.5 10.5 L 16.5 15 L 12 17.5 L 7.5 15 L 7.5 10.5 Z",
        detailMode: "fill",
    },
    // Landing pod on legs. Deliberately not a triangle: the live player marker
    // is already a triangle, and sharing both shape and hue made the two
    // indistinguishable.
    orbital_lander: {
        body: "M 4.5 15.5 A 7.5 7.5 0 0 1 19.5 15.5 L 19.5 17.5 L 4.5 17.5 Z",
        detail: "M 7.5 17.5 L 4 22.5 M 16.5 17.5 L 20 22.5 M 12 17.5 L 12 22.5",
        detailMode: "stroke",
    },
};

export function poiSymbol(group: string): PoiSymbol {
    return POI_SYMBOLS[group] ?? FALLBACK_SYMBOL;
}

/** Groups that get their own symbol, in the order shown in the filter list. */
export const POI_SYMBOL_GROUPS: readonly string[] = [
    "cave",
    "obelisk",
    "antenna",
    "abandoned_base",
    "forgotten_engine",
    "orbital_lander",
];
