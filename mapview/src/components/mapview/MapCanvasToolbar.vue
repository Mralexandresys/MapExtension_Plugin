<script setup lang="ts">
import type { MapCanvasToolbarModel } from "../../lib/types";

defineProps<{
    panel: MapCanvasToolbarModel;
}>();

const emit = defineEmits<{
    (event: "reset"): void;
    (event: "center"): void;
    (event: "toggle-focus"): void;
    (event: "toggle-filters"): void;
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
