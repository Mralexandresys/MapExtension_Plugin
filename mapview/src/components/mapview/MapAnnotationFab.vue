<script setup lang="ts">
import type { UserAnnotationMode } from "../../lib/types";

const props = defineProps<{
    annotationMode: UserAnnotationMode;
}>();

const emit = defineEmits<{
    "toggle-mode": [mode: "marker" | "zone"];
}>();
</script>

<template>
    <div class="annotation-fab">
        <!-- Marker button -->
        <button
            class="fab-btn"
            :class="{ active: props.annotationMode === 'marker' }"
            type="button"
            title="Place marker"
            aria-label="Place personal marker"
            @click="emit('toggle-mode', 'marker')"
        >
            <!-- Pin icon -->
            <svg viewBox="0 0 20 20" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                <circle cx="10" cy="8" r="4" />
                <line x1="10" y1="12" x2="10" y2="18" />
            </svg>
        </button>

        <div class="fab-divider"></div>

        <!-- Zone button -->
        <button
            class="fab-btn"
            :class="{ active: props.annotationMode === 'zone' }"
            type="button"
            title="Draw zone"
            aria-label="Draw personal zone"
            @click="emit('toggle-mode', 'zone')"
        >
            <!-- Rect icon -->
            <svg viewBox="0 0 20 20" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                <rect x="3" y="3" width="14" height="14" rx="1" stroke-dasharray="3 2" />
            </svg>
        </button>
    </div>
</template>

<style scoped>
.annotation-fab {
    --cut: 8px;
    position: absolute;
    right: 14px;
    top: 50%;
    transform: translateY(-50%);
    z-index: 12;

    display: flex;
    flex-direction: column;
    align-items: stretch;
    pointer-events: auto;

    clip-path: polygon(
        var(--cut) 0%, 100% 0%, 100% calc(100% - var(--cut)),
        calc(100% - var(--cut)) 100%, 0% 100%, 0% var(--cut)
    );
    border: 1px solid var(--border);
    background: var(--panel-strong);
    box-shadow: var(--shadow-panel);
    backdrop-filter: blur(24px);
}

.fab-btn {
    width: 40px;
    height: 44px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: transparent;
    border: 0;
    color: var(--muted);
    cursor: pointer;
    transition:
        background 0.15s ease,
        color 0.15s ease;
}

.fab-btn:hover {
    background: rgba(34, 211, 238, 0.08);
    color: var(--text);
}

.fab-btn.active {
    background: var(--amber-soft);
    color: var(--amber);
}

.fab-divider {
    height: 1px;
    background: var(--border);
    margin: 0;
}
</style>
