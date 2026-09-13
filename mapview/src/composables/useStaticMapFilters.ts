import { computed, reactive, ref, watch, type ComputedRef, type Ref } from "vue";

import type { Language, Messages } from "../lang";
import {
    DEFAULT_ENABLED_KINDS,
    DEFAULT_ENABLED_LAYERS,
    groupColor,
    orePurityColor,
    resourceColor,
    STATIC_LAYER_KEYS,
    STATIC_ORE_PURITY_LEVELS,
    STATIC_RESOURCE_KINDS,
    type StaticLayerKey,
    type StaticOrePurity,
    type StaticMapManifest,
    type StaticPlacement,
    type StaticPointSeries,
    type StaticResourceKind,
} from "../lib/staticMapCatalog";
import {
    PRESET_DEFINITIONS,
    RESOURCE_TYPES_HIDDEN_BY_DEFAULT,
    type MapPreset,
} from "../lib/mapPresets";
import type {
    MapStaticFiltersModel,
    StaticDepositTypeOption,
    StaticFilterCategory,
    StaticFilterOption,
    StaticFilterPlacementSection,
    StaticFilterToggle,
} from "../lib/types";

const STORAGE_KEY = "mapview.static-filters.v2";
const BUILDING_ACTOR_TYPE_FRAGMENTS = [
    "keycard",
    "coralion_egg",
    "spawner",
] as const;

/** A resource observed live that the catalog does not describe. */
export interface LiveResourceType {
    en: string;
    fr: string;
    count: number;
}

export interface StaticFilterState {
    layers: Record<string, boolean>;
    resourceCategories: Record<string, boolean>;
    resourceTypes: Record<string, boolean>;
    resourceKinds: Record<string, boolean>;
    poiGroups: Record<string, boolean>;
    /** Ore quality of extractor deposits, keyed by purity level. */
    orePurities: Record<string, boolean>;
    /** Keyed by `${layer}:${group}`. */
    placementGroups: Record<string, boolean>;
}

function createDefaultState(): StaticFilterState {
    const layers: Record<string, boolean> = {};
    for (const layer of STATIC_LAYER_KEYS) {
        layers[layer] = DEFAULT_ENABLED_LAYERS.includes(layer);
    }
    const resourceKinds: Record<string, boolean> = {};
    for (const kind of STATIC_RESOURCE_KINDS) {
        resourceKinds[kind] = DEFAULT_ENABLED_KINDS.includes(kind);
    }
    const orePurities: Record<string, boolean> = {};
    for (const level of STATIC_ORE_PURITY_LEVELS) orePurities[level] = true;
    return {
        layers,
        resourceCategories: {},
        resourceTypes: {},
        resourceKinds,
        poiGroups: {},
        orePurities,
        placementGroups: {},
    };
}

function readStoredState(): Partial<StaticFilterState> | null {
    try {
        const raw = window.localStorage.getItem(STORAGE_KEY);
        if (!raw) return null;
        const parsed = JSON.parse(raw) as Partial<StaticFilterState>;
        return parsed && typeof parsed === "object" ? parsed : null;
    } catch {
        return null;
    }
}

function mergeRecord(
    target: Record<string, boolean>,
    source: Record<string, boolean> | undefined,
): void {
    if (!source) return;
    for (const [key, value] of Object.entries(source)) {
        if (typeof value === "boolean") target[key] = value;
    }
}

export function useStaticMapFilters(manifest: Ref<StaticMapManifest | null>) {
    const state = reactive<StaticFilterState>(createDefaultState());
    const search = ref("");
    /**
     * Live plants share the catalog's per-type switches: the panel shows one
     * list of plants whether a given one comes from the export or from the
     * plugin, and Prickler or Prism Herb stop being all-or-nothing.
     */
    const liveResourceTypes = reactive<Record<string, LiveResourceType>>({});

    const stored = readStoredState();
    const hasStoredState = stored !== null;
    if (stored) {
        mergeRecord(state.layers, stored.layers);
        mergeRecord(state.resourceCategories, stored.resourceCategories);
        mergeRecord(state.resourceTypes, stored.resourceTypes);
        mergeRecord(state.resourceKinds, stored.resourceKinds);
        mergeRecord(state.poiGroups, stored.poiGroups);
        mergeRecord(state.orePurities, stored.orePurities);
        mergeRecord(state.placementGroups, stored.placementGroups);
    }

    // Entries missing from storage default to visible, so a catalog update that
    // adds a resource type or POI group shows up without resetting preferences.
    watch(
        manifest,
        (value) => {
            if (!value) return;
            for (const [typeId, entry] of Object.entries(value.resource_types)) {
                if (state.resourceTypes[typeId] === undefined) {
                    state.resourceTypes[typeId] =
                        !RESOURCE_TYPES_HIDDEN_BY_DEFAULT.includes(typeId);
                }
                if (state.resourceCategories[entry.category] === undefined) {
                    state.resourceCategories[entry.category] = true;
                }
            }
            for (const group of Object.keys(value.poi_groups ?? {})) {
                if (state.poiGroups[group] === undefined) {
                    state.poiGroups[group] = true;
                }
            }
            for (const [layer, groups] of Object.entries(value.placement_groups ?? {})) {
                for (const group of Object.keys(groups)) {
                    const key = `${layer}:${group}`;
                    if (state.placementGroups[key] === undefined) {
                        state.placementGroups[key] = true;
                    }
                }
            }
        },
        { immediate: true },
    );

    watch(
        state,
        (value) => {
            try {
                window.localStorage.setItem(STORAGE_KEY, JSON.stringify(value));
            } catch {
                // Storage can be unavailable (private mode); filters stay session-only.
            }
        },
        { deep: true },
    );

    const enabledLayers = computed<StaticLayerKey[]>(() =>
        STATIC_LAYER_KEYS.filter((layer) => state.layers[layer]),
    );

    const enabledKinds = computed<StaticResourceKind[]>(() =>
        STATIC_RESOURCE_KINDS.filter((kind) => state.resourceKinds[kind]),
    );

    function isLayerEnabled(layer: StaticLayerKey): boolean {
        return state.layers[layer] === true;
    }

    function isResourceTypeEnabled(typeId: string, category: string): boolean {
        return (
            state.resourceCategories[category] !== false &&
            state.resourceTypes[typeId] !== false
        );
    }

    function isSeriesVisible(series: StaticPointSeries): boolean {
        if (!isLayerEnabled(series.layer)) return false;
        if (series.layer !== "resource") return true;
        if (state.resourceKinds[series.kind] === false) return false;
        return isResourceTypeEnabled(series.group, series.category);
    }

    /** Which extractor a deposit takes, as published by the catalog. */
    function resourceExtractor(typeId: string): string | undefined {
        return manifest.value?.resource_types?.[typeId]?.extractor;
    }

    /**
     * Ore quality is a per-point attribute of the deposit series, so it is
     * filtered point by point by the renderer and the hit-test rather than by
     * dropping a whole series.
     */
    function isOrePurityVisible(code: number): boolean {
        const level = STATIC_ORE_PURITY_LEVELS[code] ?? "unknown";
        return state.orePurities[level] !== false;
    }

    function toggleOrePurity(level: StaticOrePurity): void {
        state.orePurities[level] = state.orePurities[level] === false;
    }

    function isPlacementGroupEnabled(layer: StaticLayerKey, group: string): boolean {
        return (
            isLayerEnabled(layer) &&
            state.placementGroups[`${layer}:${group}`] !== false
        );
    }

    function isPlacementVisible(placement: StaticPlacement): boolean {
        if (!isPlacementGroupEnabled(placement.layer, placement.group)) return false;
        if (placement.layer !== "building") return true;

        const actorType = placement.actorType.toLowerCase();
        return BUILDING_ACTOR_TYPE_FRAGMENTS.some((fragment) =>
            actorType.includes(fragment),
        );
    }

    function isPoiGroupEnabled(group: string): boolean {
        return isLayerEnabled("poi") && state.poiGroups[group] !== false;
    }

    /**
     * Registers the resource types currently observed live. Unknown ids default
     * to visible, exactly like a catalog type appearing after an update.
     */
    function syncLiveResourceTypes(entries: Record<string, LiveResourceType>): void {
        for (const key of Object.keys(liveResourceTypes)) {
            if (!(key in entries)) delete liveResourceTypes[key];
        }
        for (const [key, value] of Object.entries(entries)) {
            liveResourceTypes[key] = value;
            if (state.resourceTypes[key] === undefined) {
                state.resourceTypes[key] = true;
            }
        }
    }

    /**
     * Live observations are drawn by the entity layer, so they answer to their
     * resource type alone -- not to the catalog `resource` layer switch.
     */
    function isLiveResourceEnabled(typeId: string): boolean {
        return !typeId || state.resourceTypes[typeId] !== false;
    }

    function resolveGroupSelection(
        keys: readonly string[],
        selection: "all" | "none" | readonly string[],
    ): Record<string, boolean> {
        const next: Record<string, boolean> = {};
        for (const key of keys) {
            next[key] =
                selection === "all"
                    ? true
                    : selection === "none"
                      ? false
                      : selection.includes(key);
        }
        return next;
    }

    /**
     * Overwrites the catalog filters with a preset. Presets are deliberately
     * absolute rather than additive: their whole point is to give a predictable
     * starting state, and the user can refine afterwards.
     */
    function applyPreset(preset: MapPreset, harvestResource: string | null): void {
        const definition = PRESET_DEFINITIONS[preset];
        const catalog = manifest.value;

        for (const layer of STATIC_LAYER_KEYS) {
            state.layers[layer] = definition.layers[layer];
        }

        Object.assign(
            state.poiGroups,
            resolveGroupSelection(
                Object.keys(catalog?.poi_groups ?? {}),
                definition.poiGroups,
            ),
        );

        const categories = new Set<string>();
        for (const entry of Object.values(catalog?.resource_types ?? {})) {
            categories.add(entry.category);
        }
        Object.assign(
            state.resourceCategories,
            resolveGroupSelection([...categories], definition.resourceCategories),
        );

        const typeIds = new Set([
            ...Object.keys(catalog?.resource_types ?? {}),
            ...Object.keys(liveResourceTypes),
        ]);
        for (const typeId of typeIds) {
            if (definition.singleResource) {
                state.resourceTypes[typeId] = typeId === harvestResource;
            } else {
                state.resourceTypes[typeId] =
                    !RESOURCE_TYPES_HIDDEN_BY_DEFAULT.includes(typeId);
            }
        }

        for (const [layer, groups] of Object.entries(
            catalog?.placement_groups ?? {},
        )) {
            const resolved = resolveGroupSelection(
                Object.keys(groups),
                definition.placementGroups,
            );
            for (const [group, enabled] of Object.entries(resolved)) {
                state.placementGroups[`${layer}:${group}`] = enabled;
            }
        }

        for (const kind of STATIC_RESOURCE_KINDS) {
            state.resourceKinds[kind] = true;
        }
    }

    /** Harvest mode: exactly one resource type is drawn at a time. */
    function selectSingleResource(typeId: string | null): void {
        for (const key of Object.keys(state.resourceTypes)) {
            state.resourceTypes[key] = key === typeId;
        }
        for (const key of Object.keys(state.resourceCategories)) {
            state.resourceCategories[key] = true;
        }
        state.layers.resource = typeId !== null;
    }

    function toggleLayer(layer: StaticLayerKey): void {
        state.layers[layer] = !state.layers[layer];
    }

    function toggleResourceCategory(category: string): void {
        state.resourceCategories[category] = state.resourceCategories[category] === false;
    }

    function toggleResourceType(typeId: string): void {
        state.resourceTypes[typeId] = state.resourceTypes[typeId] === false;
    }

    function toggleResourceKind(kind: StaticResourceKind): void {
        state.resourceKinds[kind] = state.resourceKinds[kind] === false;
    }

    function togglePoiGroup(group: string): void {
        state.poiGroups[group] = state.poiGroups[group] === false;
    }

    function togglePlacementGroup(layer: StaticLayerKey, group: string): void {
        const key = `${layer}:${group}`;
        state.placementGroups[key] = state.placementGroups[key] === false;
    }

    function setAll(enabled: boolean): void {
        const value = manifest.value;
        for (const layer of STATIC_LAYER_KEYS) state.layers[layer] = enabled;
        for (const kind of STATIC_RESOURCE_KINDS) state.resourceKinds[kind] = enabled;
        for (const key of Object.keys(liveResourceTypes)) {
            state.resourceTypes[key] = enabled;
        }
        for (const level of STATIC_ORE_PURITY_LEVELS) {
            state.orePurities[level] = enabled;
        }
        if (!value) return;
        for (const [typeId, entry] of Object.entries(value.resource_types)) {
            state.resourceTypes[typeId] = enabled;
            state.resourceCategories[entry.category] = enabled;
        }
        for (const group of Object.keys(value.poi_groups ?? {})) {
            state.poiGroups[group] = enabled;
        }
        for (const [layer, groups] of Object.entries(value.placement_groups ?? {})) {
            for (const group of Object.keys(groups)) {
                state.placementGroups[`${layer}:${group}`] = enabled;
            }
        }
    }

    function resetFilters(): void {
        const defaults = createDefaultState();
        Object.assign(state.layers, defaults.layers);
        Object.assign(state.resourceKinds, defaults.resourceKinds);
        for (const key of Object.keys(state.resourceTypes)) state.resourceTypes[key] = true;
        for (const key of Object.keys(state.resourceCategories)) {
            state.resourceCategories[key] = true;
        }
        for (const key of Object.keys(state.poiGroups)) state.poiGroups[key] = true;
        Object.assign(state.orePurities, defaults.orePurities);
        for (const key of Object.keys(state.placementGroups)) {
            state.placementGroups[key] = true;
        }
        search.value = "";
    }

    /** Number of resource points enabled for a type, restricted to visible kinds. */
    function resourceTypeCount(typeId: string): number {
        const entry = manifest.value?.resource_types?.[typeId];
        if (!entry) return 0;
        let total = 0;
        for (const kind of STATIC_RESOURCE_KINDS) {
            if (state.resourceKinds[kind] === false) continue;
            total += entry.counts[kind] ?? 0;
        }
        return total;
    }

    const activeLayerCount = computed(() => enabledLayers.value.length);

    return {
        state,
        search,
        liveResourceTypes,
        syncLiveResourceTypes,
        isLiveResourceEnabled,
        enabledLayers,
        enabledKinds,
        activeLayerCount,
        isLayerEnabled,
        isSeriesVisible,
        isOrePurityVisible,
        resourceExtractor,
        toggleOrePurity,
        isPlacementGroupEnabled,
        isPlacementVisible,
        isPoiGroupEnabled,
        hasStoredState,
        applyPreset,
        selectSingleResource,
        isResourceTypeEnabled,
        resourceTypeCount,
        toggleLayer,
        toggleResourceCategory,
        toggleResourceType,
        toggleResourceKind,
        togglePoiGroup,
        togglePlacementGroup,
        setAll,
        resetFilters,
    };
}

export type StaticMapFilters = ReturnType<typeof useStaticMapFilters>;

// ── Filter panel model ───────────────────────────────────────────────────

const PLACEMENT_SECTION_LAYERS: readonly StaticLayerKey[] = [
    "building",
    "zone",
    "technical",
];

function matchesSearch(label: string, key: string, needle: string): boolean {
    if (!needle) return true;
    const lowered = needle.toLowerCase();
    return (
        label.toLowerCase().includes(lowered) || key.toLowerCase().includes(lowered)
    );
}

export function useStaticFiltersModel(options: {
    manifest: Ref<StaticMapManifest | null>;
    filters: StaticMapFilters;
    ui: Ref<Messages>;
    lang: Ref<Language>;
    loading: Ref<boolean>;
    error: Ref<string>;
    available: Ref<boolean>;
    loadedCount: Ref<number>;
}): ComputedRef<MapStaticFiltersModel> {
    const { manifest, filters, ui, lang, loading, error, available, loadedCount } =
        options;

    return computed<MapStaticFiltersModel>(() => {
        const messages = ui.value.staticFilters;
        const catalog = manifest.value;
        const needle = filters.search.value.trim();
        const french = lang.value === "fr";

        const layerCounts: Record<string, number> = {};
        for (const part of catalog?.parts ?? []) {
            layerCounts[part.layer] = (layerCounts[part.layer] ?? 0) + part.count;
        }

        const layers: StaticFilterOption[] = STATIC_LAYER_KEYS.map((layer) => ({
            key: layer,
            label: messages.layers[layer],
            count: layerCounts[layer] ?? 0,
            enabled: filters.isLayerEnabled(layer),
        }));

        const poiGroups: StaticFilterOption[] = Object.entries(
            catalog?.poi_groups ?? {},
        )
            .map(([group, count]) => ({
                key: group,
                label: messages.groups[group as keyof typeof messages.groups] ?? group,
                count,
                enabled: filters.state.poiGroups[group] !== false,
                color: groupColor(group),
            }))
            // Deliberately not filtered by `needle`: the search box lives in the
            // catalog tab, while these landmarks are listed in the map tab, and
            // typing there used to silently shorten a list on another tab.
            .sort((left, right) => right.count - left.count);

        const byCategory = new Map<string, StaticFilterOption[]>();
        for (const [typeId, entry] of Object.entries(catalog?.resource_types ?? {})) {
            // Veins live in the deposits block; a type is only listed here for
            // the points that are actually gathered by hand.
            const handCount = filters.resourceTypeCount(typeId) - (entry.counts.deposit ?? 0);
            if (handCount <= 0) continue;
            const option: StaticFilterOption = {
                key: typeId,
                label: french ? entry.fr : entry.en,
                count: handCount,
                enabled: filters.state.resourceTypes[typeId] !== false,
                color: resourceColor(typeId),
            };
            if (!matchesSearch(option.label, option.key, needle)) continue;
            const bucket = byCategory.get(entry.category);
            if (bucket) bucket.push(option);
            else byCategory.set(entry.category, [option]);
        }

        // Plants the plugin reports but the export never had; they only exist
        // once observed, and are listed next to the catalog plants.
        for (const [typeId, entry] of Object.entries(filters.liveResourceTypes)) {
            if (catalog?.resource_types?.[typeId]) continue;
            const option: StaticFilterOption = {
                key: typeId,
                label: french ? entry.fr : entry.en,
                count: entry.count,
                enabled: filters.state.resourceTypes[typeId] !== false,
                color: resourceColor(typeId),
            };
            if (!matchesSearch(option.label, option.key, needle)) continue;
            const bucket = byCategory.get("plant");
            if (bucket) bucket.push(option);
            else byCategory.set("plant", [option]);
        }

        const resourceCategories: StaticFilterCategory[] = [...byCategory.entries()]
            .map(([category, types]) => {
                types.sort((left, right) => right.count - left.count);
                return {
                    key: category,
                    label:
                        messages.categories[
                            category as keyof typeof messages.categories
                        ] ?? category,
                    count: types.reduce((total, type) => total + type.count, 0),
                    enabled: filters.state.resourceCategories[category] !== false,
                    types,
                };
            })
            .sort((left, right) => right.count - left.count);

        const representationCounts: Record<string, number> = {};
        for (const entry of Object.values(catalog?.resource_types ?? {})) {
            for (const kind of STATIC_RESOURCE_KINDS) {
                representationCounts[kind] =
                    (representationCounts[kind] ?? 0) + (entry.counts[kind] ?? 0);
            }
        }

        const representations: StaticFilterOption[] = STATIC_RESOURCE_KINDS.map(
            (kind) => ({
                key: kind,
                label: messages.representations[kind],
                count: representationCounts[kind] ?? 0,
                enabled: filters.state.resourceKinds[kind] !== false,
            }),
        ).filter((option) => option.count > 0);

        // ── Extractor deposits ────────────────────────────────────────────
        // One block, because "where do I put a drill, and on what quality?" is
        // one question. Quality used to be a section of its own at the bottom
        // of the panel, filtering only the deposit points while every other
        // mineral stayed on screen, which read as a filter doing nothing.
        const depositTypes: StaticDepositTypeOption[] = Object.entries(
            catalog?.resource_types ?? {},
        )
            .filter(([, entry]) => (entry.counts.deposit ?? 0) > 0)
            .map(([typeId, entry]) => ({
                key: typeId,
                label: french ? entry.fr : entry.en,
                count: entry.counts.deposit ?? 0,
                enabled: filters.state.resourceTypes[typeId] !== false,
                color: resourceColor(typeId),
                purityCounts: STATIC_ORE_PURITY_LEVELS.map((level) => ({
                    key: level,
                    count: entry.purity_counts?.[level] ?? 0,
                    color: orePurityColor(level),
                })).filter((purity) => purity.count > 0),
            }))
            .filter((option) => matchesSearch(option.label, option.key, needle))
            .sort((left, right) => right.count - left.count);

        // Counted over the ores actually enabled, so the chips describe what
        // ticking them would show rather than what the whole world contains.
        const purityCounts: Partial<Record<StaticOrePurity, number>> = {};
        for (const option of depositTypes) {
            if (!option.enabled) continue;
            for (const purity of option.purityCounts) {
                purityCounts[purity.key] = (purityCounts[purity.key] ?? 0) + purity.count;
            }
        }

        const deposits = {
            count: depositTypes.reduce((total, option) => total + option.count, 0),
            purities: STATIC_ORE_PURITY_LEVELS.map((level) => ({
                key: level,
                label: messages.purities[level],
                count: purityCounts[level] ?? 0,
                enabled: filters.state.orePurities[level] !== false,
                color: orePurityColor(level),
            })).filter(
                (option) =>
                    option.count > 0 ||
                    // A level filtered down to nothing still needs its way back.
                    !option.enabled,
            ),
            types: depositTypes,
        };

        const sectionTitles: Record<string, string> = {
            building: messages.buildingsTitle,
            zone: messages.zonesTitle,
            technical: messages.technicalTitle,
        };

        const placementSections: StaticFilterPlacementSection[] =
            PLACEMENT_SECTION_LAYERS.map((layer) => ({
                layer,
                title: sectionTitles[layer] ?? layer,
                options: Object.entries(catalog?.placement_groups?.[layer] ?? {})
                    .map(([group, count]) => ({
                        key: group,
                        label:
                            messages.groups[group as keyof typeof messages.groups] ??
                            group,
                        count,
                        enabled:
                            filters.state.placementGroups[`${layer}:${group}`] !== false,
                        color: groupColor(group),
                    }))
                    .filter((option) => matchesSearch(option.label, option.key, needle))
                    .sort((left, right) => right.count - left.count),
            })).filter((section) => section.options.length > 0);

        return {
            ui: ui.value,
            available: available.value,
            loading: loading.value,
            error: error.value,
            search: filters.search.value,
            loadedCount: loadedCount.value,
            layers,
            poiGroups,
            resourceCategories,
            deposits,
            representations,
            placementSections,
        };
    });
}

/** Applies a toggle emitted by the filters panel. */
export function applyStaticFilterToggle(
    filters: StaticMapFilters,
    toggle: StaticFilterToggle,
): void {
    switch (toggle.scope) {
        case "layer":
            filters.toggleLayer(toggle.key as StaticLayerKey);
            return;
        case "poiGroup":
            filters.togglePoiGroup(toggle.key);
            return;
        case "resourceCategory":
            filters.toggleResourceCategory(toggle.key);
            return;
        case "resourceType":
            filters.toggleResourceType(toggle.key);
            return;
        case "representation":
            filters.toggleResourceKind(toggle.key as StaticResourceKind);
            return;
        case "orePurity":
            filters.toggleOrePurity(toggle.key as StaticOrePurity);
            return;
        case "placementGroup":
            filters.togglePlacementGroup(
                (toggle.layer ?? "building") as StaticLayerKey,
                toggle.key,
            );
            return;
        default:
            return;
    }
}
