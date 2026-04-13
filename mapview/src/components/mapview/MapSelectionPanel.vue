<script setup lang="ts">
import type { MapSelectionPanelModel } from "../../lib/types";

defineProps<{
    panel: MapSelectionPanelModel;
}>();

const emit = defineEmits<{
    "toggle-details": [];
    "center": [];
    "toggle-focus": [];
    "clear-selection": [];
    "open-shortcuts": [];
}>();
</script>

<template>
    <div class="overlay-layer overlay-top-right">
        <section
            class="floating-panel selection-panel"
            :class="{ expanded: panel.detailsExpanded }"
        >
            <div class="panel-top-row compact selection-top-row">
                <div class="selection-header-copy">
                    <div class="selection-identity-row">
                        <span class="selection-id">{{
                            panel.selectedEntityKeyLabel
                        }}</span>
                        <span
                            class="badge selection-type-pill"
                            :class="panel.selectedEntityTone"
                        >
                            {{ panel.selectedEntitySummary }}
                        </span>
                    </div>
                    <span class="panel-kicker">{{
                        panel.ui.handles.selection
                    }}</span>
                    <h2>{{ panel.selectedDisplayName }}</h2>
                    <p>
                        {{
                            panel.selectedEntityActive
                                ? panel.ui.selection.detailsCurrent
                                : panel.ui.selection.clickElement
                        }}
                    </p>
                </div>
                <div class="panel-top-actions">
                    <button
                        class="button subtle small"
                        type="button"
                        :disabled="!panel.selectedEntityActive"
                        @click="emit('toggle-details')"
                    >
                        {{
                            panel.detailsExpanded
                                ? panel.ui.buttons.hideDetails
                                : panel.ui.buttons.details
                        }}
                    </button>
                    <button
                        class="button subtle small"
                        type="button"
                        :disabled="!panel.selectedEntityActive"
                        @click="emit('clear-selection')"
                    >
                        {{ panel.ui.buttons.close }}
                    </button>
                </div>
            </div>

            <div class="drawer-body selection-body">
                <div
                    v-if="panel.selectedEntityActive"
                    class="selection-summary-grid"
                >
                    <div
                        v-for="item in panel.selectedPreviewFacts"
                        :key="item.label"
                        class="fact-card hero-fact-card"
                    >
                        <span>{{ item.label }}</span>
                        <strong>{{ item.value }}</strong>
                    </div>
                </div>

                <div v-else class="empty-state selection-empty-state">
                    <strong>{{ panel.ui.selection.detailsEmpty }}</strong>
                    <span>{{ panel.ui.selection.detailsHelp }}</span>
                </div>

                <div class="selection-actions compact-actions">
                    <button
                        class="button subtle small"
                        type="button"
                        :disabled="!panel.selectedEntityActive"
                        @click="emit('center')"
                    >
                        {{ panel.ui.buttons.center }}
                    </button>
                    <button
                        class="button subtle small"
                        type="button"
                        :disabled="!panel.canEnableFocusMode"
                        @click="emit('toggle-focus')"
                    >
                        {{
                            panel.focusMode
                                ? panel.ui.buttons.showAll
                                : panel.ui.buttons.focusSelection
                        }}
                    </button>
                </div>

                <div
                    v-if="panel.detailsExpanded && panel.selectedEntityActive"
                    class="secondary-panel selection-secondary-panel"
                >
                    <div class="panel-section">
                        <div class="section-header-row">
                            <div>
                                <h3>{{ panel.ui.selection.detailsTitle }}</h3>
                                <p>{{ panel.ui.selection.detailsCurrent }}</p>
                            </div>
                        </div>

                        <div
                            class="details-grid compact-grid selection-details-grid"
                        >
                            <div
                                v-for="item in panel.selectedDetailRows"
                                :key="item.label"
                            >
                                <strong>{{ item.label }}</strong>
                                <span>{{ item.value }}</span>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </section>
    </div>
</template>

<style scoped>
.selection-panel {
    width: min(420px, 100%);
    height: auto;
    max-height: min(72vh, calc(100vh - 124px));
    grid-template-rows: auto minmax(0, 1fr);
    border-radius: 24px;
    overflow: hidden;
    align-self: flex-start;
}

.selection-panel.expanded {
    transform: translateX(0);
    max-height: min(84vh, calc(100vh - 36px));
}

.selection-body {
    display: grid;
    gap: 12px;
    min-height: 0;
    overflow-y: auto;
    overflow-x: hidden;
    max-height: inherit;
    padding-right: 6px;
    scrollbar-gutter: stable;
}

.selection-header-copy {
    display: grid;
    gap: 6px;
}

.selection-header-copy h2 {
    font-size: 1.18rem;
    line-height: 1.15;
}

.selection-top-row {
    align-items: start;
    justify-content: space-between;
}

.selection-top-row .panel-top-actions {
    flex: 0 0 auto;
    align-self: flex-start;
}

.selection-identity-row {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 8px;
}

.selection-id {
    display: inline-flex;
    align-items: center;
    min-height: 24px;
    padding: 4px 9px;
    border-radius: 0;
    border: 1px solid var(--border-amber);
    background: var(--amber-soft);
    font-family: var(--font-mono);
    color: var(--amber);
    text-shadow: 0 0 8px rgba(232, 184, 75, 0.4);
    font-size: 0.72rem;
    font-weight: 700;
    line-height: 1.3;
    letter-spacing: 0.08em;
    max-width: 100%;
    white-space: normal;
    overflow-wrap: anywhere;
}

.selection-type-pill {
    border-left: 3px solid currentColor;
    border-radius: 0;
    padding: 2px 8px 2px 6px;
    font-family: var(--font-mono);
    font-size: 0.68rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    line-height: 1.3;
    max-width: 100%;
    white-space: normal;
    overflow-wrap: anywhere;
}

.selection-summary-grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px;
    align-items: stretch;
}

.selection-empty-state {
    display: grid;
    gap: 6px;
}

.selection-empty-state strong {
    color: var(--text);
}

.selection-actions .button {
    min-width: 0;
    white-space: normal;
    word-break: normal;
    overflow-wrap: normal;
    hyphens: none;
    text-align: center;
}

.selection-secondary-panel {
    display: grid;
    min-height: 0;
    overflow: auto;
    padding-top: 10px;
    padding-right: 4px;
    border-top: 1px solid rgba(255, 255, 255, 0.1);
    gap: 12px;
}

.selection-details-grid {
    grid-template-columns: 1fr;
}

.selection-actions {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
}

.selection-actions.compact-actions {
    grid-template-columns: repeat(2, minmax(0, 1fr));
}

.selection-actions .button {
    min-width: 0;
    white-space: normal;
    word-break: normal;
    overflow-wrap: normal;
    hyphens: none;
    text-align: center;
}

/* Contextual overrides */
.selection-panel .panel-top-row {
    padding-left: 0;
}

.selection-panel .panel-top-row.compact {
    align-items: flex-start;
    gap: 10px;
}

.selection-panel .panel-top-actions {
    max-width: 100%;
}

@media (max-width: 980px) {
    .selection-actions {
        grid-template-columns: repeat(2, minmax(0, 1fr));
        display: grid;
    }
}

@media (max-width: 720px) {
    .selection-panel {
        border-radius: 20px;
        width: 100%;
        max-height: min(44vh, calc(100vh - 136px));
    }

    .selection-actions {
        grid-template-columns: 1fr;
    }
}
</style>
