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
