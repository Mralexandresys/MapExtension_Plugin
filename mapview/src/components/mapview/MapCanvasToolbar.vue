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
