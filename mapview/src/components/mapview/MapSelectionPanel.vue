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
