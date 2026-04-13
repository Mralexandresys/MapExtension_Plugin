<script setup lang="ts">
import teleporterSvg from "../../assets/teleporter.svg?raw";
import type {
    EntityToggleKey,
    MapFiltersPanelModel,
    ViewMode,
} from "../../lib/types";

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
    "clear": [];
    "toggle-entity": [key: EntityToggleKey];
    "update:view-mode": [value: ViewMode];
    "update:show-all-links": [value: boolean];
    "update:highlight-orphans": [value: boolean];
    "toggle-focus": [];
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
                    <span class="panel-kicker">{{ panel.ui.handles.filters }}</span>
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
                <div class="panel-section">
                    <div>
                        <h3>{{ panel.ui.filters.activeTitle }}</h3>
                        <p>
                            {{
                                panel.activeFilterChips.length
                                    ? panel.ui.filters.summaryHelp
                                    : panel.ui.filters.noneActive
                            }}
                        </p>
                    </div>

                    <div class="active-filters" :class="{ empty: panel.activeFilterChips.length === 0 }">
                        <template v-if="panel.activeFilterChips.length">
                            <span v-for="chip in panel.activeFilterChips" :key="chip" class="filter-chip">
                                {{ chip }}
                            </span>
                        </template>
                        <span v-else>{{ panel.ui.filters.noneActive }}</span>
                    </div>

                    <div class="floating-actions footer-actions filters-sidebar-actions">
                        <button class="button subtle small" type="button" @click="emit('clear')">
                            {{ panel.ui.buttons.reset }}
                        </button>
                    </div>
                </div>

                <div class="panel-section">
                    <div>
                        <h3>{{ panel.ui.filters.visibilityTitle }}</h3>
                        <p>{{ panel.ui.filters.visibilityHelp }}</p>
                    </div>

                    <div class="chip-group quick-filter-group filters-sidebar-chips">
                        <button
                            v-for="option in panel.entityToggleOptions"
                            :key="option.key"
                            class="chip-button"
                            :class="{
                                active: panel.entityVisibility[option.key],
                                muted: !panel.entityVisibility[option.key],
                            }"
                            type="button"
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

                <div class="panel-section">
                    <div>
                        <h3>{{ panel.ui.filters.modeTitle }}</h3>
                        <p>{{ panel.ui.filters.modeHelp }}</p>
                    </div>

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

                <div class="panel-section">
                    <div>
                        <h3>{{ panel.ui.filters.behaviorTitle }}</h3>
                        <p>{{ panel.ui.filters.behaviorHelp }}</p>
                    </div>

                    <div class="toggle-grid compact-toggle-grid filters-sidebar-toggles">
                        <label class="toggle-line">
                            <input :checked="panel.showAllLinks" type="checkbox" @change="handleShowAllLinksChange" />
                            <span>{{ panel.ui.filters.showAllLinks }}</span>
                        </label>
                        <label class="toggle-line">
                            <input :checked="panel.highlightOrphans" type="checkbox" @change="handleHighlightOrphansChange" />
                            <span>{{ panel.ui.filters.highlightOrphans }}</span>
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

.filters-sidebar .panel-section + .panel-section {
    padding-top: 12px;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
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
