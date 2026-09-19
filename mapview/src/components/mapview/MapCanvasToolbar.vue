<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from "vue";
import type { MapCanvasToolbarModel } from "../../lib/types";

defineProps<{
    panel: MapCanvasToolbarModel;
}>();

const emit = defineEmits<{
    "reset": [];
    "center": [];
    "center-player": [];
    "toggle-focus": [];
    "toggle-filters": [];
    resize: [height: number];
}>();

const toolbarRef = ref<HTMLElement | null>(null);
let resizeObserver: ResizeObserver | undefined;
onMounted(() => {
    const toolbar = toolbarRef.value;
    if (!toolbar) return;
    resizeObserver = new ResizeObserver(() => {
        emit("resize", Math.ceil(toolbar.getBoundingClientRect().height));
    });
    resizeObserver.observe(toolbar);
});
onBeforeUnmount(() => resizeObserver?.disconnect());
</script>

<template>
    <div class="overlay-layer overlay-bottom-center">
        <section ref="toolbarRef" class="floating-panel map-toolbar" :aria-label="panel.ui.map.actionsLabel">
            <!-- Resets the map view only. Clearing filters stays in the filters
                 panel, where it is contextual: the two used to share a label. -->
            <button class="toolbar-button" type="button" @click="emit('reset')">
                {{ panel.ui.buttons.recenter }}
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
                type="button"
                :disabled="!panel.canCenterOnPlayer"
                :title="panel.ui.shortcuts.items.centerPlayer.label"
                :aria-label="panel.ui.shortcuts.items.centerPlayer.label"
                @click="emit('center-player')"
            >
                {{ panel.ui.buttons.centerPlayer }}
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
    gap: 4px;
    padding: 5px;
    background: var(--panel-strong);
    border-radius: 10px;
    border: 1px solid var(--border-strong);
}

.toolbar-button {
    font-family: var(--font-body);
    font-size: 0.84rem;
    text-transform: none;
    letter-spacing: 0;
    padding: 8px 14px;
    border-radius: 6px;
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

@media (max-width: 720px) {
    .map-toolbar {
        flex-wrap: wrap;
    }

    .toolbar-button {
        flex: 1 1 auto;
        padding: 8px 10px;
        letter-spacing: 0.06em;
    }
}
</style>
