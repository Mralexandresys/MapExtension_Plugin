<script setup lang="ts">
import { computed } from "vue";

import teleporterSvg from "../../assets/teleporter.svg?raw";
import type {
    EntityToggleKey,
    FilterTabKey,
    MapFiltersPanelModel,
    StaticFilterToggle,
} from "../../lib/types";
import { MAP_PRESETS, PRESET_DEFINITIONS, type MapPreset } from "../../lib/mapPresets";
import { POI_SYMBOL_VIEWBOX, poiSymbol } from "../../lib/mapMarkers";
import MapStaticFilters from "./MapStaticFilters.vue";

const teleporterIconMarkup = teleporterSvg.replace(
    "<svg",
    '<svg class="filter-option-teleporter-svg"',
);

const props = defineProps<{
    panel: MapFiltersPanelModel;
}>();

const emit = defineEmits<{
    "toggle-collapse": [];
    "update:tab": [key: FilterTabKey];
    "clear": [];
    "toggle-entity": [key: EntityToggleKey];
    "update:preset": [value: MapPreset];
    "update:harvest-resource": [value: string | null];
    "update:show-all-links": [value: boolean];
    "update:highlight-orphans": [value: boolean];
    "update:user-annotations-only": [value: boolean];
    "toggle-focus": [];
    "static-toggle": [value: StaticFilterToggle];
    "update:static-search": [value: string];
    "static-show-all": [];
    "static-hide-all": [];
}>();

const behaviorOptions = computed(() => [
    {
        key: "showAllLinks",
        label: props.panel.ui.filters.showAllLinks,
        enabled: props.panel.showAllLinks,
        set: (value: boolean) => emit("update:show-all-links", value),
    },
    {
        key: "highlightOrphans",
        label: props.panel.ui.filters.highlightOrphans,
        enabled: props.panel.highlightOrphans,
        set: (value: boolean) => emit("update:highlight-orphans", value),
    },
    {
        key: "userAnnotationsOnly",
        label: props.panel.ui.filters.userAnnotationsOnly,
        enabled: props.panel.userAnnotationsOnly,
        set: (value: boolean) => emit("update:user-annotations-only", value),
    },
]);

const LOGISTICS_KEYS: EntityToggleKey[] = [
    "sender",
    "receiver",
    "teleporter",
    "player",
];
// `abandonedBase` is deliberately absent: it is now driven by the
// `abandoned_base` landmark chip, which holds the canonical count.
const RESOURCE_KEYS: EntityToggleKey[] = [
    "plantResource",
    "ignitium",
    "starTears",
];

const logisticsOptions = computed(() =>
    props.panel.entityToggleOptions.filter((option) =>
        LOGISTICS_KEYS.includes(option.key),
    ),
);

const resourceOptions = computed(() =>
    props.panel.entityToggleOptions.filter((option) =>
        RESOURCE_KEYS.includes(option.key),
    ),
);

/** The 241 canonical POI, promoted from three levels deep in the catalog. */
const landmarkOptions = computed(() => props.panel.staticFilters?.poiGroups ?? []);

const presetDefinition = computed(() => PRESET_DEFINITIONS[props.panel.preset]);

// ── Tabs ────────────────────────────────────────────────────────────────────
// One list at a time, each with the full height of the sidebar. Every tab
// carries its own summary so a hidden tab never hides the fact that filters are
// active inside it.

interface FilterTab {
    key: FilterTabKey;
    label: string;
    summary: string;
    /** True when the tab holds at least one non-default choice. */
    dirty: boolean;
}

const entityEnabledCount = computed(
    () =>
        [...logisticsOptions.value, ...resourceOptions.value].filter(
            (option) => props.panel.entityVisibility[option.key],
        ).length + landmarkOptions.value.filter((option) => option.enabled).length,
);

const entityTotalCount = computed(
    () =>
        logisticsOptions.value.length +
        resourceOptions.value.length +
        landmarkOptions.value.length,
);

const harvestSummary = computed(() => {
    const selected = props.panel.harvestOptions.find(
        (option) => option.id === props.panel.harvestResource,
    );
    return selected ? selected.label : "--";
});

const behaviorEnabledCount = computed(
    () =>
        [
            !props.panel.showAllLinks,
            props.panel.highlightOrphans,
            props.panel.userAnnotationsOnly,
        ].filter(Boolean).length,
);

// What is drawn, not what was fetched.
const catalogSummary = computed(() => {
    const model = props.panel.staticFilters;
    if (!model || !model.available) return "--";
    return props.panel.staticVisibleCount.toLocaleString(props.panel.ui.locale);
});

const tabs = computed<FilterTab[]>(() => {
    const ui = props.panel.ui.filters;
    const list: FilterTab[] = [
        {
            key: "map",
            label: ui.tabs.map,
            summary: ui.groupCount(entityEnabledCount.value, entityTotalCount.value),
            dirty: entityEnabledCount.value < entityTotalCount.value,
        },
    ];

    if (presetDefinition.value.singleResource) {
        list.push({
            key: "harvest",
            label: ui.tabs.harvest,
            summary: harvestSummary.value,
            dirty: !!props.panel.harvestResource,
        });
    }

    // Nothing to refine in Network: that preset draws the canonical POI only,
    // and those live in the Map tab.
    if (props.panel.staticFilters && props.panel.preset !== "network") {
        list.push({
            key: "catalog",
            label: ui.tabs.catalog,
            summary: catalogSummary.value,
            dirty: false,
        });
    }

    list.push({
        key: "behavior",
        label: ui.tabs.behavior,
        summary: ui.groupCount(behaviorEnabledCount.value, 3),
        dirty: behaviorEnabledCount.value > 0,
    });

    return list;
});

/** Presets remove whole tabs; fall back rather than showing an empty body. */
const activeTab = computed<FilterTabKey>(() => {
    const available = tabs.value;
    return available.some((tab) => tab.key === props.panel.activeTab)
        ? props.panel.activeTab
        : (available[0]?.key ?? "map");
});

// A tablist is expected to move with the arrow keys, Home and End; without it
// only the selected tab is reachable and the others cannot be read at all.
function handleTabKeydown(event: KeyboardEvent, key: FilterTabKey): void {
    const keys = tabs.value.map((tab) => tab.key);
    const current = keys.indexOf(key);
    let next = -1;

    if (event.key === "ArrowRight" || event.key === "ArrowDown") {
        next = (current + 1) % keys.length;
    } else if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
        next = (current - 1 + keys.length) % keys.length;
    } else if (event.key === "Home") {
        next = 0;
    } else if (event.key === "End") {
        next = keys.length - 1;
    }

    if (next < 0) return;
    event.preventDefault();
    emit("update:tab", keys[next]);
    // The moved-to tab is the only focusable one once selected.
    const target = document.getElementById(`filter-tab-${keys[next]}`);
    target?.focus();
}

/** Catalog counts run into five digits; raw they read as one long number. */
function formatCount(value: number): string {
    return value.toLocaleString(props.panel.ui.locale);
}

// ── Group bulk actions ──────────────────────────────────────────────────────
// There is no bulk endpoint per group, so "All"/"None" replays the individual
// toggles that are not already in the requested state.

function setEntityGroup(
    options: Array<{ key: EntityToggleKey }>,
    enabled: boolean,
): void {
    for (const option of options) {
        if (props.panel.entityVisibility[option.key] !== enabled) {
            emit("toggle-entity", option.key);
        }
    }
}

function setLandmarkGroup(enabled: boolean): void {
    for (const option of landmarkOptions.value) {
        if (option.enabled !== enabled) {
            emit("static-toggle", { scope: "poiGroup", key: option.key });
        }
    }
}

function entityGroupCount(options: Array<{ key: EntityToggleKey }>): string {
    const enabled = options.filter(
        (option) => props.panel.entityVisibility[option.key],
    ).length;
    return props.panel.ui.filters.groupCount(enabled, options.length);
}

const commonHarvestOptions = computed(() =>
    props.panel.harvestOptions.filter((option) => option.common),
);

const rareHarvestOptions = computed(() =>
    props.panel.harvestOptions.filter((option) => !option.common),
);

// Rare first: those are the ones worth a per-point marker. Commons are shown
// but flagged, because each is tens of thousands of points.
const harvestGroups = computed(() => [
    {
        key: "rare",
        title: props.panel.ui.harvestRare,
        options: rareHarvestOptions.value,
    },
    {
        key: "common",
        title: props.panel.ui.harvestCommon,
        options: commonHarvestOptions.value,
    },
]);
</script>

<template>
    <div class="overlay-layer overlay-left-sidebar">
        <button
            v-if="panel.collapsed"
            class="drawer-handle drawer-handle-left-rail"
            type="button"
            :aria-expanded="!panel.collapsed"
            @click="emit('toggle-collapse')"
        >
            <span class="collapse-arrow right" aria-hidden="true"></span>
            {{ panel.ui.handles.filters }}
        </button>

        <section
            class="floating-panel filters-panel filters-sidebar"
            :class="{ collapsed: panel.collapsed }"
        >
            <button
                class="panel-edge-toggle panel-edge-toggle-left-sidebar"
                type="button"
                :aria-label="panel.ui.buttons.collapse"
                :title="panel.ui.buttons.collapse"
                @click="emit('toggle-collapse')"
            >
                <span class="collapse-arrow left" aria-hidden="true"></span>
            </button>

            <!-- Kept out of the scrolling body: the active filters used to be
                 inserted above the current scroll position, which shifted every
                 section down the moment a filter was toggled. -->
            <div class="filters-sidebar-top">
                <div class="panel-top-row compact filters-sidebar-head">
                    <div>
                        <h2>{{ panel.ui.handles.filters }}</h2>
                        <p>
                            {{
                                panel.activeFilterCount
                                    ? panel.ui.format.activeFilterCount(panel.activeFilterCount)
                                    : panel.ui.filters.noneActive
                            }}
                        </p>
                    </div>
                    <button
                        v-if="panel.activeFilterCount"
                        class="button subtle small filters-reset-button"
                        type="button"
                        @click="emit('clear')"
                    >
                        {{ panel.ui.buttons.reset }}
                    </button>
                </div>

                <div class="preset-block">
                    <div class="preset-grid" role="radiogroup" :aria-label="panel.ui.presetsTitle">
                        <button
                            v-for="option in MAP_PRESETS"
                            :key="option"
                            class="preset-button"
                            :class="{ active: panel.preset === option }"
                            type="button"
                            role="radio"
                            :aria-checked="panel.preset === option"
                            :title="panel.ui.presets[option].help"
                            @click="emit('update:preset', option)"
                        >
                            {{ panel.ui.presets[option].label }}
                        </button>
                    </div>
                    <p class="preset-help">{{ panel.ui.presets[panel.preset].help }}</p>
                    <p v-if="presetDefinition.developerMode" class="preset-warning">
                        {{ panel.ui.developerModeWarning }}
                    </p>
                </div>

                <div
                    class="filter-tabs"
                    role="tablist"
                    :aria-label="panel.ui.filters.tabsLabel"
                >
                    <button
                        v-for="tab in tabs"
                        :id="`filter-tab-${tab.key}`"
                        :key="tab.key"
                        class="filter-tab"
                        :class="{ active: activeTab === tab.key, dirty: tab.dirty }"
                        type="button"
                        role="tab"
                        :aria-selected="activeTab === tab.key"
                        :aria-controls="`filter-tabpanel-${tab.key}`"
                        :tabindex="activeTab === tab.key ? 0 : -1"
                        @click="emit('update:tab', tab.key)"
                        @keydown="handleTabKeydown($event, tab.key)"
                    >
                        <span class="filter-tab-label">{{ tab.label }}</span>
                        <span class="filter-tab-summary">{{ tab.summary }}</span>
                    </button>
                </div>
            </div>

            <div
                :id="`filter-tabpanel-${activeTab}`"
                class="drawer-body filters-sidebar-body"
                role="tabpanel"
                :aria-labelledby="`filter-tab-${activeTab}`"
            >
                <div v-if="activeTab === 'map'" class="filter-tab-body">
                    <p class="filter-section-help">{{ panel.ui.filters.onMapHelp }}</p>

                    <section class="filter-group">
                        <header class="filter-group-head">
                            <h4>{{ panel.ui.filters.familyLogistics }}</h4>
                            <span class="filter-group-count">
                                {{ entityGroupCount(logisticsOptions) }}
                            </span>
                            <span class="filter-group-actions">
                                <button
                                    class="group-action"
                                    type="button"
                                    :aria-label="panel.ui.filters.selectAllIn(panel.ui.filters.familyLogistics)"
                                    @click="setEntityGroup(logisticsOptions, true)"
                                >
                                    {{ panel.ui.filters.selectAll }}
                                </button>
                                <button
                                    class="group-action"
                                    type="button"
                                    :aria-label="panel.ui.filters.selectNoneIn(panel.ui.filters.familyLogistics)"
                                    @click="setEntityGroup(logisticsOptions, false)"
                                >
                                    {{ panel.ui.filters.selectNone }}
                                </button>
                            </span>
                        </header>

                        <div class="filter-rows">
                            <button
                                v-for="option in logisticsOptions"
                                :key="option.key"
                                class="filter-row"
                                :class="{
                                    active: panel.entityVisibility[option.key],
                                    empty: option.count === 0,
                                }"
                                type="button"
                                :aria-pressed="panel.entityVisibility[option.key]"
                                @click="emit('toggle-entity', option.key)"
                            >
                                <span class="filter-row-check" aria-hidden="true"></span>
                                <span
                                    class="filter-option-icon"
                                    :class="option.key"
                                    aria-hidden="true"
                                    v-html="
                                        option.key === 'teleporter'
                                            ? teleporterIconMarkup
                                            : ''
                                    "
                                ></span>
                                <span class="filter-row-label" :title="option.label">
                                    {{ option.label }}
                                </span>
                                <span class="filter-row-count">
                                    {{ formatCount(option.count) }}
                                </span>
                            </button>
                        </div>
                    </section>

                    <!-- The 241 canonical POI. Each row carries the exact
                         silhouette used on the map, so this list is also the
                         legend. -->
                    <section v-if="landmarkOptions.length" class="filter-group">
                        <header class="filter-group-head">
                            <h4>{{ panel.ui.filters.familyLandmarks }}</h4>
                            <span class="filter-group-count">
                                {{
                                    panel.ui.filters.groupCount(
                                        landmarkOptions.filter((option) => option.enabled).length,
                                        landmarkOptions.length,
                                    )
                                }}
                            </span>
                            <span class="filter-group-actions">
                                <button
                                    class="group-action"
                                    type="button"
                                    :aria-label="panel.ui.filters.selectAllIn(panel.ui.filters.familyLandmarks)"
                                    @click="setLandmarkGroup(true)"
                                >
                                    {{ panel.ui.filters.selectAll }}
                                </button>
                                <button
                                    class="group-action"
                                    type="button"
                                    :aria-label="panel.ui.filters.selectNoneIn(panel.ui.filters.familyLandmarks)"
                                    @click="setLandmarkGroup(false)"
                                >
                                    {{ panel.ui.filters.selectNone }}
                                </button>
                            </span>
                        </header>

                        <div class="filter-rows">
                            <button
                                v-for="option in landmarkOptions"
                                :key="option.key"
                                class="filter-row"
                                :class="{ active: option.enabled, empty: option.count === 0 }"
                                type="button"
                                :aria-pressed="option.enabled"
                                @click="emit('static-toggle', { scope: 'poiGroup', key: option.key })"
                            >
                                <span class="filter-row-check" aria-hidden="true"></span>
                                <svg
                                    class="landmark-icon"
                                    :viewBox="POI_SYMBOL_VIEWBOX"
                                    width="18"
                                    height="18"
                                    aria-hidden="true"
                                    :style="{ color: option.color ?? '#94a3b8' }"
                                >
                                    <path class="landmark-body" :d="poiSymbol(option.key).body" />
                                    <path
                                        v-if="poiSymbol(option.key).detail"
                                        class="landmark-detail"
                                        :class="poiSymbol(option.key).detailMode"
                                        :d="poiSymbol(option.key).detail"
                                    />
                                </svg>
                                <span class="filter-row-label" :title="option.label">
                                    {{ option.label }}
                                </span>
                                <span class="filter-row-count">
                                    {{ formatCount(option.count) }}
                                </span>
                            </button>
                        </div>
                    </section>

                    <section class="filter-group">
                        <header class="filter-group-head">
                            <h4>{{ panel.ui.filters.familyResources }}</h4>
                            <span class="filter-group-count">
                                {{ entityGroupCount(resourceOptions) }}
                            </span>
                            <span class="filter-group-actions">
                                <button
                                    class="group-action"
                                    type="button"
                                    :aria-label="panel.ui.filters.selectAllIn(panel.ui.filters.familyResources)"
                                    @click="setEntityGroup(resourceOptions, true)"
                                >
                                    {{ panel.ui.filters.selectAll }}
                                </button>
                                <button
                                    class="group-action"
                                    type="button"
                                    :aria-label="panel.ui.filters.selectNoneIn(panel.ui.filters.familyResources)"
                                    @click="setEntityGroup(resourceOptions, false)"
                                >
                                    {{ panel.ui.filters.selectNone }}
                                </button>
                            </span>
                        </header>

                        <div class="filter-rows">
                            <button
                                v-for="option in resourceOptions"
                                :key="option.key"
                                class="filter-row"
                                :class="{
                                    active: panel.entityVisibility[option.key],
                                    empty: option.count === 0,
                                }"
                                type="button"
                                :aria-pressed="panel.entityVisibility[option.key]"
                                @click="emit('toggle-entity', option.key)"
                            >
                                <span class="filter-row-check" aria-hidden="true"></span>
                                <span
                                    class="filter-option-icon"
                                    :class="option.key"
                                    aria-hidden="true"
                                ></span>
                                <span class="filter-row-label" :title="option.label">
                                    {{ option.label }}
                                </span>
                                <span class="filter-row-count">
                                    {{ formatCount(option.count) }}
                                </span>
                            </button>
                        </div>
                    </section>
                </div>

                <div v-else-if="activeTab === 'harvest'" class="filter-tab-body">
                    <p class="filter-section-help">{{ panel.ui.harvestHelp }}</p>

                    <p v-if="!panel.harvestResource" class="filter-section-help harvest-empty">
                        {{ panel.ui.harvestPick }}
                    </p>

                    <section
                        v-for="group in harvestGroups"
                        v-show="group.options.length"
                        :key="group.key"
                        class="filter-group"
                    >
                        <header class="filter-group-head">
                            <h4>{{ group.title }}</h4>
                            <span class="filter-group-count">{{ group.options.length }}</span>
                        </header>

                        <div class="filter-rows" role="radiogroup" :aria-label="group.title">
                            <button
                                v-for="option in group.options"
                                :key="option.id"
                                class="filter-row radio"
                                :class="{ active: panel.harvestResource === option.id }"
                                type="button"
                                role="radio"
                                :aria-checked="panel.harvestResource === option.id"
                                @click="emit(
                                    'update:harvest-resource',
                                    panel.harvestResource === option.id ? null : option.id,
                                )"
                            >
                                <span class="filter-row-check" aria-hidden="true"></span>
                                <span
                                    class="filter-row-swatch"
                                    aria-hidden="true"
                                    :style="{ '--swatch': option.color }"
                                ></span>
                                <span class="filter-row-label" :title="option.label">
                                    {{ option.label }}
                                </span>
                                <span class="filter-row-count">
                                    {{ formatCount(option.count) }}
                                </span>
                            </button>
                        </div>
                    </section>
                </div>

                <MapStaticFilters
                    v-else-if="activeTab === 'catalog' && panel.staticFilters"
                    :model="panel.staticFilters"
                    :developer-mode="presetDefinition.developerMode"
                    @toggle="emit('static-toggle', $event)"
                    @update:search="emit('update:static-search', $event)"
                    @show-all="emit('static-show-all')"
                    @hide-all="emit('static-hide-all')"
                />

                <div v-else-if="activeTab === 'behavior'" class="filter-tab-body">
                    <p class="filter-section-help">{{ panel.ui.filters.behaviorHelp }}</p>

                    <!-- Same rows as every other list: these used to be native
                         checkboxes and read as a different kind of control. -->
                    <div class="filter-rows">
                        <button
                            v-for="option in behaviorOptions"
                            :key="option.key"
                            class="filter-row no-icon wrap"
                            :class="{ active: option.enabled }"
                            type="button"
                            role="switch"
                            :aria-checked="option.enabled"
                            @click="option.set(!option.enabled)"
                        >
                            <span class="filter-row-check" aria-hidden="true"></span>
                            <span class="filter-row-label">{{ option.label }}</span>
                        </button>
                    </div>

                    <div class="filters-sidebar-toggles">
                        <button
                            v-if="panel.canEnableFocusMode"
                            class="chip-button filters-focus-button"
                            :class="{ active: panel.focusMode }"
                            type="button"
                            :aria-pressed="panel.focusMode"
                            @click="emit('toggle-focus')"
                        >
                            {{
                                panel.focusMode
                                    ? panel.ui.buttons.showAll
                                    : panel.ui.buttons.focusSelection
                            }}
                        </button>
                    </div>
                </div>
            </div>
        </section>
    </div>
</template>

<style scoped>
.filters-panel {
    border-left: 0;
    border-bottom: 0;
    border-radius: 0 24px 0 0;
}

.filters-panel.collapsed {
    transform: translateY(calc(100% - 8px));
}

.filters-sidebar {
    width: 100%;
    height: 100%;
    max-height: 100%;
    padding: 16px 16px 14px;
    border: 0;
    border-right: 1px solid rgba(255, 255, 255, 0.10);
    border-radius: 0;
    background: linear-gradient(
        180deg,
        rgba(14, 22, 40, 0.99),
        rgba(12, 20, 36, 0.98)
    );
    box-shadow: none;
    backdrop-filter: none;
    grid-template-rows: auto minmax(0, 1fr);
}

.filters-sidebar.collapsed {
    transform: translateX(calc(-100% + 46px));
}

/* Every row above the tabs is height the filter lists do not get, so this
   block stays as tight as it can while keeping the preset help readable. */
.filters-sidebar-top {
    display: grid;
    gap: 8px;
}

.filters-sidebar-head {
    padding-right: 54px;
}

/* Sits on the title row: the removable-chip list it used to close is gone,
   every filter now being undone from the row that set it. */
.filters-reset-button {
    flex: 0 0 auto;
}

.preset-block {
    display: grid;
    gap: 6px;
}

.preset-grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 5px;
}

.preset-button {
    min-height: 32px;
    padding: 5px 10px;
    border: 1px solid var(--border);
    border-left: 2px solid var(--border);
    background: rgba(12, 19, 35, 0.86);
    color: var(--muted);
    font-family: var(--font-mono);
    font-size: 0.72rem;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    cursor: pointer;
    transition: background 0.15s, color 0.15s, border-color 0.15s;
}

.preset-button:hover {
    color: var(--text);
    border-color: var(--border-strong);
}

.preset-button.active {
    color: var(--text);
    background: var(--accent-soft);
    border-color: rgba(34, 211, 238, 0.46);
    border-left-color: var(--accent);
}

.preset-help {
    color: var(--muted);
    font-size: 0.74rem;
    line-height: 1.35;
}

.preset-warning {
    padding: 5px 10px;
    border-left: 2px solid var(--amber);
    background: var(--amber-soft);
    color: var(--text);
    font-size: 0.76rem;
}

/* ── Tabs ─────────────────────────────────────────────────────────────── */

.filter-tabs {
    display: flex;
    gap: 2px;
    margin: 2px -16px 0;
    padding: 0 16px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.10);
}

.filter-tab {
    position: relative;
    flex: 1 1 0;
    min-width: 0;
    display: grid;
    justify-items: center;
    gap: 2px;
    padding: 8px 4px 9px;
    border: 0;
    border-bottom: 2px solid transparent;
    background: transparent;
    color: var(--muted);
    font-family: var(--font-mono);
    font-size: 0.72rem;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    cursor: pointer;
    transition: color 0.15s, background 0.15s, border-color 0.15s;
}

.filter-tab:hover {
    color: var(--text);
    background: rgba(255, 255, 255, 0.04);
}

.filter-tab.active {
    color: var(--accent);
    border-bottom-color: var(--accent);
    background: var(--accent-soft);
}

.filter-tab-label {
    max-width: 100%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

/* Each tab keeps advertising its state, so switching tabs never hides the
   fact that filters are active in the ones that are not on screen. */
.filter-tab-summary {
    max-width: 100%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: 0.66rem;
    font-weight: 600;
    letter-spacing: 0.02em;
    text-transform: none;
    color: var(--dim);
}

.filter-tab.active .filter-tab-summary {
    color: #a9e7f5;
}

.filter-tab.dirty .filter-tab-summary {
    color: var(--amber);
}

/* ── Body ─────────────────────────────────────────────────────────────── */

/* No top padding: the catalog tab sticks its search bar to the very top of
   this scrollport, and any padding here would let rows show above it. Each
   other tab brings its own leading space through .filter-tab-body. */
.filters-sidebar-body {
    display: grid;
    align-content: start;
    min-height: 0;
    overflow-y: auto;
    overflow-x: hidden;
    padding: 0 6px 4px 0;
    scrollbar-gutter: stable;
}

.filter-tab-body {
    display: grid;
    align-content: start;
    gap: 14px;
    padding-top: 12px;
}

/* The row, group and action styles are global (styles/main.css) so the world
   catalog lists render exactly like the ones in this panel. */

.filter-row.empty .filter-option-icon,
.filter-row.empty .landmark-icon {
    opacity: 0.45;
}

.harvest-empty {
    color: var(--amber) !important;
}

.filters-panel .panel-top-row {
    padding-right: 46px;
}

.landmark-icon {
    flex: 0 0 auto;
}

.landmark-body {
    fill: currentColor;
    stroke: rgba(8, 14, 26, 0.9);
    stroke-width: 1.1;
    stroke-linejoin: round;
    paint-order: stroke fill;
}

.landmark-detail.fill {
    fill: #0b1220;
    stroke: none;
}

.landmark-detail.stroke {
    fill: none;
    stroke: rgba(8, 14, 26, 0.9);
    stroke-width: 1.6;
    stroke-linecap: round;
    stroke-linejoin: round;
}

.filter-option-icon {
    position: relative;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 18px;
    height: 18px;
    flex: 0 0 auto;
}

.filter-option-icon.sender::before {
    content: "";
    width: 12px;
    height: 12px;
    border-radius: 3px;
    background: var(--sender);
    box-shadow: 0 0 0 1px #d8e8ff;
}

.filter-option-icon.receiver::before {
    content: "";
    width: 12px;
    height: 12px;
    border-radius: 999px;
    background: var(--receiver);
    box-shadow: 0 0 0 1px #fff2c7;
}

.filter-option-icon.teleporter::before {
    content: none;
}

:deep(.filter-option-teleporter-svg) {
    width: 18px;
    height: 18px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
}

:deep(.filter-option-teleporter-svg) svg {
    width: 100%;
    height: 100%;
}

:deep(.filter-option-teleporter-svg) * {
    stroke: var(--teleporter);
    fill: none;
}

.filter-option-icon.player::before {
    content: "";
    width: 0;
    height: 0;
    border-left: 7px solid transparent;
    border-right: 7px solid transparent;
    border-bottom: 12px solid var(--player);
    filter: drop-shadow(0 0 0.5px #d8fff0);
}

.filter-option-icon.abandonedBase::before {
    content: "";
    width: 12px;
    height: 10px;
    border: 2px solid #cbd5e1;
    border-top-width: 4px;
    border-radius: 2px;
    transform: rotate(-8deg);
    opacity: 0.9;
}

.filter-option-icon.plantResource::before {
    content: "";
    width: 12px;
    height: 12px;
    border-radius: 10px 2px 10px 2px;
    background: #4ade80;
    box-shadow: 0 0 0 1px #dcfce7;
    transform: rotate(-35deg);
}

.filter-option-icon.ignitium::before {
    content: "";
    width: 12px;
    height: 12px;
    border-radius: 3px 9px 3px 9px;
    background: #f97316;
    box-shadow: 0 0 0 1px #ffedd5;
    transform: rotate(45deg);
}

.filter-option-icon.starTears::before {
    content: "";
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: #38bdf8;
    box-shadow: 0 0 0 1px #e0f2fe, 0 0 7px rgba(56, 189, 248, 0.62);
}

.filters-sidebar-toggles {
    display: grid;
    grid-template-columns: 1fr;
}

.filters-focus-button {
    justify-content: center;
}

@media (max-width: 720px) {
    .filters-panel {
        border-radius: 0 20px 0 0;
    }

    .filters-sidebar {
        height: auto;
        max-height: min(58vh, calc(100vh - 128px));
        border-right: 0;
        border-radius: 0 20px 0 0;
        box-shadow: 0 20px 54px rgba(0, 0, 0, 0.42);
    }

    .filters-panel.collapsed {
        transform: translateY(calc(100% - 8px));
    }

    .filters-sidebar.collapsed {
        transform: translateY(calc(100% - 8px));
    }

    /* As a bottom drawer the panel is short: the preset help and the panel
       subtitle are advisory, and the tab lists need those rows more. */
    .preset-help,
    .filters-sidebar-head p {
        display: none;
    }

    .filter-tabs {
        margin-left: -14px;
        margin-right: -14px;
        padding: 0 14px;
    }
}
</style>
