import { computed, reactive, ref, watch, type ComputedRef, type Ref } from "vue";

import type { Language, Messages } from "../lang";
import {
    DEFAULT_ENABLED_KINDS,
    DEFAULT_ENABLED_LAYERS,
    groupColor,
    resourceColor,
    STATIC_LAYER_KEYS,
    STATIC_RESOURCE_KINDS,
    type StaticLayerKey,
    type StaticMapManifest,
    type StaticPlacement,
    type StaticPointSeries,
    type StaticResourceKind,
} from "../lib/staticMapCatalog";
import type {
    MapStaticFiltersModel,
    StaticFilterCategory,
    StaticFilterOption,
    StaticFilterPlacementSection,
    StaticFilterToggle,
} from "../lib/types";

const STORAGE_KEY = "mapview.static-filters.v1";
const BUILDING_ACTOR_TYPE_FRAGMENTS = [
    "keycard",
    "coralion_egg",
    "spawner",
] as const;

export interface StaticFilterState {
    layers: Record<string, boolean>;
    resourceCategories: Record<string, boolean>;
    resourceTypes: Record<string, boolean>;
    resourceKinds: Record<string, boolean>;
    poiGroups: Record<string, boolean>;
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
    return {
        layers,
        resourceCategories: {},
        resourceTypes: {},
        resourceKinds,
        poiGroups: {},
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

    const stored = readStoredState();
    if (stored) {
        mergeRecord(state.layers, stored.layers);
        mergeRecord(state.resourceCategories, stored.resourceCategories);
        mergeRecord(state.resourceTypes, stored.resourceTypes);
        mergeRecord(state.resourceKinds, stored.resourceKinds);
        mergeRecord(state.poiGroups, stored.poiGroups);
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
                    state.resourceTypes[typeId] = true;
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
        enabledLayers,
        enabledKinds,
        activeLayerCount,
        isLayerEnabled,
        isSeriesVisible,
        isPlacementGroupEnabled,
        isPlacementVisible,
        isPoiGroupEnabled,
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
            .filter((option) => matchesSearch(option.label, option.key, needle))
            .sort((left, right) => right.count - left.count);

        const byCategory = new Map<string, StaticFilterOption[]>();
        for (const [typeId, entry] of Object.entries(catalog?.resource_types ?? {})) {
            const option: StaticFilterOption = {
                key: typeId,
                label: french ? entry.fr : entry.en,
                count: filters.resourceTypeCount(typeId),
                enabled: filters.state.resourceTypes[typeId] !== false,
                color: resourceColor(typeId),
            };
            if (!matchesSearch(option.label, option.key, needle)) continue;
            const bucket = byCategory.get(entry.category);
            if (bucket) bucket.push(option);
            else byCategory.set(entry.category, [option]);
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
