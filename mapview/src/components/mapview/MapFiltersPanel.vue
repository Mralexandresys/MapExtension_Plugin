<script setup lang="ts">
import { computed } from "vue";

import teleporterSvg from "../../assets/teleporter.svg?raw";
import type {
    EntityToggleKey,
    FilterSectionKey,
    MapFiltersPanelModel,
    StaticFilterToggle,
    ViewMode,
} from "../../lib/types";
import MapStaticFilters from "./MapStaticFilters.vue";

const viewModeOptions: ViewMode[] = [
    "network",
    "resources",
    "teleporters",
    "players",
];

const teleporterIconMarkup = teleporterSvg.replace(
    "<svg",
    '<svg class="filter-option-teleporter-svg"',
);

const props = defineProps<{
    panel: MapFiltersPanelModel;
}>();

const emit = defineEmits<{
    "toggle-collapse": [];
    "toggle-section": [key: FilterSectionKey];
    "clear": [];
    "toggle-entity": [key: EntityToggleKey];
    "update:view-mode": [value: ViewMode];
    "update:show-all-links": [value: boolean];
    "update:highlight-orphans": [value: boolean];
    "update:user-annotations-only": [value: boolean];
    "toggle-focus": [];
    "static-toggle": [value: StaticFilterToggle];
    "update:static-search": [value: string];
    "static-show-all": [];
    "static-hide-all": [];
}>();

function handleShowAllLinksChange(event: Event): void {
    emit("update:show-all-links", (event.target as HTMLInputElement).checked);
}

function handleHighlightOrphansChange(event: Event): void {
    emit(
        "update:highlight-orphans",
        (event.target as HTMLInputElement).checked,
    );
}

function handleUserAnnotationsOnlyChange(event: Event): void {
    emit(
        "update:user-annotations-only",
        (event.target as HTMLInputElement).checked,
    );
}

// Every collapsed section still advertises its state in its header, so folding
// one never hides the fact that filters are active inside it.
const visibilitySummary = computed(() => {
    const options = props.panel.entityToggleOptions;
    const enabled = options.filter(
        (option) => props.panel.entityVisibility[option.key],
    ).length;
    return `${enabled}/${options.length}`;
});

const modeSummary = computed(() => props.panel.ui.viewModes[props.panel.viewMode]);

const behaviorSummary = computed(() => {
    const enabled = [
        !props.panel.showAllLinks,
        props.panel.highlightOrphans,
        props.panel.userAnnotationsOnly,
    ].filter(Boolean).length;
    return `${enabled}/3`;
});

const catalogSummary = computed(() => {
    const model = props.panel.staticFilters;
    if (!model || !model.available) return "--";
    return model.loadedCount.toLocaleString(props.panel.ui.locale);
});
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

            <div class="panel-top-row compact filters-sidebar-head">
                <div>
                    <h2>{{ panel.ui.handles.filters }}</h2>
                    <p>
                        {{
                            panel.activeFilterChips.length
                                ? panel.ui.format.activeFilterCount(panel.activeFilterChips.length)
                                : panel.ui.filters.noneActive
                        }}
                    </p>
                </div>
            </div>

            <div class="drawer-body filters-sidebar-body">
                <!-- Shown only when there is something to show: the empty case is
                     already stated once by the panel header above. -->
                <div v-if="panel.activeFilterChips.length" class="panel-section">
                    <div>
                        <h3>{{ panel.ui.filters.activeTitle }}</h3>
                        <p>{{ panel.ui.filters.summaryHelp }}</p>
                    </div>

                    <div class="active-filters">
                        <span v-for="chip in panel.activeFilterChips" :key="chip" class="filter-chip">
                            {{ chip }}
                        </span>
                    </div>

                    <div class="floating-actions footer-actions filters-sidebar-actions">
                        <button class="button subtle small" type="button" @click="emit('clear')">
                            {{ panel.ui.buttons.reset }}
                        </button>
                    </div>
                </div>

                <section class="filter-section">
                    <button
                        class="filter-section-head"
                        type="button"
                        :aria-expanded="panel.sectionsOpen.visibility"
                        @click="emit('toggle-section', 'visibility')"
                    >
                        <span
                            class="collapse-arrow"
                            :class="panel.sectionsOpen.visibility ? 'down' : 'right'"
                            aria-hidden="true"
                        ></span>
                        <span class="filter-section-title">
                            {{ panel.ui.filters.visibilityTitle }}
                        </span>
                        <span class="filter-section-summary">{{ visibilitySummary }}</span>
                    </button>

                    <div v-if="panel.sectionsOpen.visibility" class="filter-section-body">
                    <p class="filter-section-help">{{ panel.ui.filters.visibilityHelp }}</p>

                    <div class="chip-group quick-filter-group filters-sidebar-chips">
                        <button
                            v-for="option in panel.entityToggleOptions"
                            :key="option.key"
                            class="chip-button"
                            :class="{
                                active: panel.entityVisibility[option.key],
                                muted: !panel.entityVisibility[option.key],
                                empty: option.count === 0,
                            }"
                            type="button"
                            :aria-pressed="panel.entityVisibility[option.key]"
                            @click="emit('toggle-entity', option.key)"
                        >
                            <span class="filter-option-label">
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
                                <span>{{ option.label }}</span>
                            </span>
                            <strong class="filter-option-count">
                                {{ option.count }}
                            </strong>
                        </button>
                    </div>
                    </div>
                </section>

                <section class="filter-section">
                    <button
                        class="filter-section-head"
                        type="button"
                        :aria-expanded="panel.sectionsOpen.mode"
                        @click="emit('toggle-section', 'mode')"
                    >
                        <span
                            class="collapse-arrow"
                            :class="panel.sectionsOpen.mode ? 'down' : 'right'"
                            aria-hidden="true"
                        ></span>
                        <span class="filter-section-title">
                            {{ panel.ui.filters.modeTitle }}
                        </span>
                        <span class="filter-section-summary">{{ modeSummary }}</span>
                    </button>

                    <div v-if="panel.sectionsOpen.mode" class="filter-section-body">
                    <p class="filter-section-help">{{ panel.ui.filters.modeHelp }}</p>

                    <div class="side-tabs filters-sidebar-modes">
                        <button
                            v-for="mode in viewModeOptions"
                            :key="mode"
                            class="tab-button"
                            :class="{ active: panel.viewMode === mode }"
                            type="button"
                            :aria-pressed="panel.viewMode === mode"
                            @click="emit('update:view-mode', mode)"
                        >
                            {{ panel.ui.viewModes[mode] }}
                        </button>
                    </div>
                    </div>
                </section>

                <section class="filter-section">
                    <button
                        class="filter-section-head"
                        type="button"
                        :aria-expanded="panel.sectionsOpen.behavior"
                        @click="emit('toggle-section', 'behavior')"
                    >
                        <span
                            class="collapse-arrow"
                            :class="panel.sectionsOpen.behavior ? 'down' : 'right'"
                            aria-hidden="true"
                        ></span>
                        <span class="filter-section-title">
                            {{ panel.ui.filters.behaviorTitle }}
                        </span>
                        <span class="filter-section-summary">{{ behaviorSummary }}</span>
                    </button>

                    <div v-if="panel.sectionsOpen.behavior" class="filter-section-body">
                    <p class="filter-section-help">{{ panel.ui.filters.behaviorHelp }}</p>

                    <div class="toggle-grid compact-toggle-grid filters-sidebar-toggles">
                        <label class="toggle-line">
                            <input :checked="panel.showAllLinks" type="checkbox" @change="handleShowAllLinksChange" />
                            <span>{{ panel.ui.filters.showAllLinks }}</span>
                        </label>
                        <label class="toggle-line">
                            <input :checked="panel.highlightOrphans" type="checkbox" @change="handleHighlightOrphansChange" />
                            <span>{{ panel.ui.filters.highlightOrphans }}</span>
                        </label>
                        <label class="toggle-line">
                            <input :checked="panel.userAnnotationsOnly" type="checkbox" @change="handleUserAnnotationsOnlyChange" />
                            <span>{{ panel.ui.filters.userAnnotationsOnly }}</span>
                        </label>
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
                </section>

                <section v-if="panel.staticFilters" class="filter-section">
                    <button
                        class="filter-section-head"
                        type="button"
                        :aria-expanded="panel.sectionsOpen.catalog"
                        @click="emit('toggle-section', 'catalog')"
                    >
                        <span
                            class="collapse-arrow"
                            :class="panel.sectionsOpen.catalog ? 'down' : 'right'"
                            aria-hidden="true"
                        ></span>
                        <span class="filter-section-title">
                            {{ panel.ui.staticFilters.title }}
                        </span>
                        <span class="filter-section-summary">{{ catalogSummary }}</span>
                    </button>

                    <div v-if="panel.sectionsOpen.catalog" class="filter-section-body">
                        <MapStaticFilters
                            :model="panel.staticFilters"
                            @toggle="emit('static-toggle', $event)"
                            @update:search="emit('update:static-search', $event)"
                            @show-all="emit('static-show-all')"
                            @hide-all="emit('static-hide-all')"
                        />
                    </div>
                </section>
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

.filters-sidebar-head {
    padding-right: 54px;
    padding-bottom: 10px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}

/* filters-panel .panel-top-row contextual */
.filters-panel .panel-top-row {
    padding-right: 46px;
}

.filters-sidebar-body {
    display: grid;
    gap: 14px;
    min-height: 0;
    overflow-y: auto;
    overflow-x: hidden;
    padding-right: 6px;
    scrollbar-gutter: stable;
}

.filters-sidebar .panel-section {
    padding: 0 0 2px;
}

.filter-section + .filter-section,
.panel-section + .filter-section {
    border-top: 1px solid rgba(255, 255, 255, 0.06);
}

.filter-section-head {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
    padding: 11px 2px;
    border: 0;
    background: transparent;
    color: var(--text);
    font-family: var(--font-mono);
    font-size: 0.74rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    text-align: left;
    cursor: pointer;
}

.filter-section-head:hover {
    color: var(--accent);
}

.filter-section-head .collapse-arrow {
    color: var(--muted);
}

.filter-section-title {
    flex: 1;
    min-width: 0;
}

.filter-section-summary {
    flex: 0 0 auto;
    padding: 2px 8px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.08);
    color: #dbe6ff;
    font-size: 0.72rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    text-transform: none;
}

.filter-section-body {
    display: grid;
    gap: 10px;
    padding: 0 0 12px;
}

.filter-section-help {
    color: var(--muted);
    font-size: 0.78rem;
}

.filters-sidebar-chips {
    display: grid;
    grid-template-columns: 1fr;
}

.filters-sidebar-chips .chip-button {
    width: 100%;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    text-align: left;
}

.filter-option-label {
    display: inline-flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
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

.filter-option-count {
    flex: 0 0 auto;
    min-width: 2.25rem;
    padding: 2px 8px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.08);
    color: #dbe6ff;
    text-align: center;
    font-size: 0.76rem;
    line-height: 1.2;
}

.filters-sidebar-chips .chip-button.active .filter-option-count {
    background: rgba(34, 211, 238, 0.18);
}

/* "Enabled but nothing to show" used to look exactly like "enabled with
   results", so a filter listing 0 elements read as active content. */
.filters-sidebar-chips .chip-button.empty .filter-option-label {
    opacity: 0.5;
}

.filters-sidebar-chips .chip-button.empty .filter-option-count {
    background: rgba(255, 255, 255, 0.04);
    color: var(--dim);
}

.filters-sidebar-toggles {
    grid-template-columns: 1fr;
}

.filters-sidebar-modes {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px;
}

.filters-sidebar-modes .tab-button {
    min-width: 0;
    white-space: normal;
    text-align: center;
}

.filters-focus-button {
    justify-content: center;
}

.filters-sidebar-actions {
    display: grid;
    grid-template-columns: 1fr;
}

.active-filters {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    align-items: center;
    padding: 10px 12px;
    border-radius: 14px;
    border: 1px solid var(--border);
    background: rgba(14, 22, 40, 0.75);
    max-height: 104px;
    overflow: auto;
}

.active-filters.empty {
    border-style: dashed;
}

.filter-chip {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    min-height: 30px;
    padding: 5px 10px;
    border-radius: 999px;
    border: 1px solid rgba(34, 211, 238, 0.22);
    background: var(--accent-soft);
    color: #deebff;
    max-width: 100%;
    white-space: normal;
    word-break: break-word;
}

.inline-legend {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px 10px;
    padding: 10px 12px;
    border-radius: 16px;
    border: 1px solid var(--border);
    background: rgba(8, 14, 26, 0.46);
}

.filters-advanced-panel {
    padding-top: 12px;
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

    .inline-legend {
        grid-template-columns: 1fr;
    }
}
</style>
