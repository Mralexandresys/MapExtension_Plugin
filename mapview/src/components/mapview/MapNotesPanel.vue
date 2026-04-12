<script setup lang="ts">
import { computed } from "vue";

import type { MapNotesPanelModel, UserAnnotationDraft } from "../../lib/types";

const props = defineProps<{
    panel: MapNotesPanelModel;
}>();

const emit = defineEmits<{
    "toggle-mode": [mode: "marker" | "zone"];
    "update:draft": [draft: UserAnnotationDraft];
    "clear-selection": [];
    "delete-selected": [];
    "center-selected": [];
    "toggle-zone-lock": [];
}>();

const selectedTypePillClass = computed(() =>
    props.panel.selectedAnnotation?.type === "marker" ? "sender" : "teleporter",
);

const selectedTypeLabel = computed(() =>
    props.panel.selectedAnnotation?.type === "marker"
        ? props.panel.ui.notes.markerSingular
        : props.panel.ui.notes.zoneSingular,
);

const selectedIdLabel = computed(() =>
    props.panel.selectedAnnotation?.id.slice(0, 8).toUpperCase() ?? "",
);

const editActionLabel = computed(() =>
    "Deplacer",
);

const editActionActive = computed(() =>
    props.panel.annotationEditMode === "move-marker"
    || props.panel.annotationEditMode === "edit-zone",
);

const isZoneSelected = computed(() => props.panel.selectedAnnotation?.type === "zone");

const zoneLockLabel = computed(() =>
    props.panel.selectedZoneLocked ? "Deverrouiller" : "Verrouiller",
);

const panelTitle = computed(() => {
    if (props.panel.selectedAnnotation) {
        return props.panel.draft.label || props.panel.ui.notes.title;
    }
    if (props.panel.annotationMode === "marker") {
        return props.panel.ui.notes.addMarker;
    }
    if (props.panel.annotationMode === "zone") {
        return props.panel.ui.notes.addZone;
    }
    return props.panel.ui.notes.title;
});

const panelHelp = computed(() => {
    if (props.panel.annotationEditMode === "move-marker") {
        return "Clique sur la carte pour deplacer le point.";
    }
    if (props.panel.annotationEditMode === "edit-zone") {
        return "Glisse sur la zone pour la deplacer.";
    }
    if (props.panel.annotationMode === "marker") {
        return props.panel.ui.notes.markerHelp;
    }
    if (props.panel.annotationMode === "zone") {
        return props.panel.ui.notes.zoneHelp;
    }
    return "";
});

function emitDraft(patch: Partial<UserAnnotationDraft>): void {
    emit("update:draft", {
        label: patch.label ?? props.panel.draft.label,
        description: patch.description ?? props.panel.draft.description,
        color: patch.color ?? props.panel.draft.color,
    });
}

function handleLabelInput(event: Event): void {
    emitDraft({ label: (event.target as HTMLInputElement).value });
}

function handleDescInput(event: Event): void {
    emitDraft({ description: (event.target as HTMLTextAreaElement).value });
}

function selectColor(color: string): void {
    emitDraft({ color });
}

function handleColorInput(event: Event): void {
    selectColor((event.target as HTMLInputElement).value);
}
</script>

<template>
    <div class="overlay-layer overlay-top-right notes-selection-overlay">
        <section class="floating-panel selection-panel notes-selection-panel">
            <div class="panel-top-row compact selection-top-row">
                <div class="selection-header-copy">
                    <div v-if="panel.selectedAnnotation" class="selection-identity-row">
                        <span class="selection-id">{{ selectedIdLabel }}</span>
                        <span
                            class="badge selection-type-pill"
                            :class="selectedTypePillClass"
                        >
                            {{ selectedTypeLabel }}
                        </span>
                    </div>
                    <span class="panel-kicker">{{ panel.ui.handles.selection }}</span>
                    <h2>{{ panelTitle }}</h2>
                    <p v-if="panelHelp">{{ panelHelp }}</p>
                </div>
                <div class="panel-top-actions">
                    <button
                        class="button subtle small"
                        type="button"
                        @click="emit('clear-selection')"
                    >
                        {{ panel.ui.buttons.close }}
                    </button>
                </div>
            </div>

            <div class="drawer-body selection-body">
                <template v-if="panel.selectedAnnotation">
                    <label class="field compact notes-field">
                        <span>{{ panel.ui.notes.nameLabel }}</span>
                        <input
                            type="text"
                            :value="panel.draft.label"
                            :placeholder="panel.ui.notes.nameLabel"
                            @input="handleLabelInput"
                        />
                    </label>

                    <label class="field compact notes-field">
                        <span>{{ panel.ui.notes.descriptionLabel }}</span>
                        <textarea
                            class="notes-textarea"
                            :value="panel.draft.description"
                            :placeholder="panel.ui.notes.descriptionLabel"
                            rows="4"
                            @input="handleDescInput"
                        ></textarea>
                    </label>

                    <label class="field compact notes-field notes-color-field">
                        <span>{{ panel.ui.notes.colorLabel }}</span>
                        <div class="notes-color-input-row">
                            <input
                                class="notes-color-picker"
                                type="color"
                                :value="panel.draft.color"
                                @input="handleColorInput"
                            />
                            <span class="notes-color-value">{{ panel.draft.color }}</span>
                        </div>
                    </label>
                </template>

                <div v-else class="empty-state selection-empty-state notes-empty-state">
                    <strong>{{ panelTitle }}</strong>
                    <span>{{ panelHelp }}</span>
                </div>

                <div v-if="panel.importError" class="inline-note error notes-error">
                    <strong>{{ panel.ui.notes.importFailed }}</strong>
                    <span>{{ panel.ui.notes.importFailedHelp }}</span>
                </div>

                <div
                    v-if="panel.selectedAnnotation"
                    class="selection-actions compact-actions notes-edit-actions"
                >
                    <button
                        class="button subtle small notes-move-btn"
                        :class="{ active: editActionActive }"
                        type="button"
                        @click="emit('center-selected')"
                    >
                        {{ editActionLabel }}
                    </button>
                    <button
                        v-if="isZoneSelected"
                        class="button subtle small notes-lock-btn"
                        :class="{ active: panel.selectedZoneLocked }"
                        type="button"
                        @click="emit('toggle-zone-lock')"
                    >
                        {{ zoneLockLabel }}
                    </button>
                    <button
                        class="button subtle small notes-delete-btn"
                        type="button"
                        @click="emit('delete-selected')"
                    >
                        {{ panel.ui.notes.deleteSelection }}
                    </button>
                </div>
            </div>
        </section>
    </div>
</template>

<style scoped>
.notes-selection-overlay {
    z-index: 13;
}

.notes-selection-panel {
    width: min(420px, 100%);
}

.notes-field {
    display: grid;
    gap: 6px;
}

.notes-textarea {
    width: 100%;
    min-height: 96px;
    padding: 9px 12px;
    border-radius: 14px;
    border: 1px solid var(--border);
    background: rgba(8, 13, 24, 0.84);
    color: var(--text);
    font: inherit;
    outline: none;
    resize: vertical;
}

.notes-textarea:focus {
    border-color: var(--border-strong);
    box-shadow: 0 0 0 3px rgba(34, 211, 238, 0.16);
}

.notes-color-field {
    gap: 8px;
}

.notes-color-input-row {
    display: flex;
    align-items: center;
    gap: 10px;
}

.notes-color-picker {
    width: 52px;
    height: 36px;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: 8px;
    background: rgba(8, 13, 24, 0.84);
    cursor: pointer;
}

.notes-color-value {
    font-family: var(--font-mono);
    color: var(--muted);
    font-size: 0.8rem;
    text-transform: uppercase;
}

.notes-empty-state {
    padding: 12px 14px;
}

.notes-edit-actions {
    grid-template-columns: repeat(2, minmax(0, 1fr));
}

.notes-move-btn.active {
    color: var(--accent) !important;
    border-left-color: var(--accent) !important;
    background: rgba(34, 211, 238, 0.12) !important;
}

.notes-move-btn.active:hover {
    background: rgba(34, 211, 238, 0.18) !important;
}

.notes-lock-btn.active {
    color: var(--amber) !important;
    border-left-color: var(--amber) !important;
    background: rgba(245, 158, 11, 0.12) !important;
}

.notes-delete-btn {
    color: var(--bad) !important;
    border-left-color: var(--bad) !important;
}

.notes-delete-btn:hover {
    background: rgba(248, 113, 113, 0.1) !important;
}

.notes-error {
    border-color: rgba(248, 113, 113, 0.4);
}

@media (max-width: 720px) {
    .notes-selection-panel {
        width: 100%;
    }

    .notes-edit-actions {
        grid-template-columns: 1fr;
    }
}
</style>
