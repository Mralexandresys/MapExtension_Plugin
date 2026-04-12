<script setup lang="ts">
import type { MapCanvasToolbarModel } from "../../lib/types";

defineProps<{
    panel: MapCanvasToolbarModel;
}>();

const emit = defineEmits<{
    "reset": [];
    "center": [];
    "toggle-focus": [];
    "toggle-filters": [];
}>();
</script>

<template>
    <div class="overlay-layer overlay-bottom-center">
        <section class="floating-panel map-toolbar" aria-label="Map actions">
            <button class="toolbar-button" type="button" @click="emit('reset')">
                {{ panel.ui.buttons.reset }}
            </button>
            <button
                class="toolbar-button"
                type="button"
                :disabled="!panel.selectedEntityActive"
                @click="emit('center')"
            >
                {{ panel.ui.buttons.center }}
            </button>
            <button
                class="toolbar-button"
                :class="{ active: panel.filtersOpen }"
                type="button"
                :aria-pressed="panel.filtersOpen"
                @click="emit('toggle-filters')"
            >
                {{ panel.ui.handles.filters }}
            </button>
        </section>
    </div>
</template>

<style scoped>
.map-toolbar {
    display: flex;
    gap: 1px;
    background: var(--border);
    border-radius: 0;
    border: 1px solid var(--border-strong);
}

.toolbar-button {
    font-family: var(--font-mono);
    font-size: 0.70rem;
    text-transform: uppercase;
    letter-spacing: 0.1em;
    padding: 8px 18px;
    background: var(--panel-strong);
    color: var(--muted);
    border: 0;
    cursor: pointer;
    transition: color 0.15s, background 0.15s;
    min-height: 38px;
}

.toolbar-button.active,
.toolbar-button:hover { color: var(--accent); background: var(--accent-soft); }

.toolbar-button:disabled {
    opacity: 0.45;
    cursor: not-allowed;
    transform: none;
}

@media (max-width: 980px) {
    .map-toolbar {
        width: 100%;
        justify-content: center;
        border-radius: 20px;
    }
}
</style>
