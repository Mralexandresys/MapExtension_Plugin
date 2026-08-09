<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from "vue";

import droneProhibitedSvg from "../../assets/drone-prohibited-1-svgrepo-com.svg?raw";
import type { MapRupturePanelModel } from "../../lib/types";

const incomingDroneIconMarkup = droneProhibitedSvg
    .replace("fill:#000000;", "fill:currentColor;")
    .replace("<svg", '<svg class="rupture-legend-icon-svg"');

const props = defineProps<{
    panel: MapRupturePanelModel;
}>();

const emit = defineEmits<{
    "toggle-details": [];
    "close-details": [];
}>();

const rootRef = ref<HTMLElement | null>(null);

const activeToneClass = computed(
    () => props.panel.phases.find((phase) => phase.active)?.toneClass ?? "",
);

// The tick scale used to reserve a fixed height whatever the graduations
// needed. Derive it from the deepest stack level actually rendered instead.
const tickStackLevels = computed(() =>
    props.panel.timelineTicks.reduce(
        (deepest, tick) => Math.max(deepest, tick.stackLevel),
        0,
    ),
);

const tickScaleStyle = computed(() => ({
    "--rupture-tick-levels": String(tickStackLevels.value),
}));

function handleDocumentPointerDown(event: PointerEvent): void {
    const target = event.target as Node | null;
    if (target && rootRef.value?.contains(target)) return;
    emit("close-details");
}

function handleDocumentKeydown(event: KeyboardEvent): void {
    if (event.key !== "Escape") return;
    event.stopPropagation();
    emit("close-details");
}

watch(
    () => props.panel.detailsOpen,
    (open) => {
        if (open) {
            document.addEventListener("pointerdown", handleDocumentPointerDown);
            document.addEventListener("keydown", handleDocumentKeydown, true);
            return;
        }
        document.removeEventListener("pointerdown", handleDocumentPointerDown);
        document.removeEventListener("keydown", handleDocumentKeydown, true);
    },
);

onBeforeUnmount(() => {
    document.removeEventListener("pointerdown", handleDocumentPointerDown);
    document.removeEventListener("keydown", handleDocumentKeydown, true);
});
</script>

<template>
    <div ref="rootRef" class="rupture-strip">
        <button
            class="rupture-strip-trigger"
            type="button"
            :aria-expanded="panel.detailsOpen"
            :title="panel.ui.rupture.title"
            @click="emit('toggle-details')"
        >
            <span class="rupture-strip-kicker">{{ panel.ui.handles.timeline }}</span>

            <template v-if="panel.hasLiveData">
                <span class="rupture-strip-phase" :class="activeToneClass">
                    {{ panel.currentPhaseLabel }}
                </span>

                <span class="rupture-track rupture-track-strip" aria-hidden="true">
                    <span
                        v-for="phase in panel.phases"
                        :key="phase.key"
                        class="rupture-segment"
                        :class="[phase.toneClass, { active: phase.active }]"
                        :style="{ width: `${phase.widthPercent}%` }"
                    ></span>
                    <span
                        v-if="panel.markerPercent !== null"
                        class="rupture-strip-marker"
                        :style="{ left: `${panel.markerPercent}%` }"
                    ></span>
                </span>

                <strong class="rupture-strip-remaining">
                    {{ panel.currentPhaseRemainingLabel }}
                </strong>
            </template>

            <span v-else class="rupture-strip-nodata">
                {{ panel.ui.rupture.noDataShort }}
            </span>

            <span
                class="collapse-arrow rupture-strip-chevron"
                :class="panel.detailsOpen ? 'up' : 'down'"
                aria-hidden="true"
            ></span>
        </button>

        <div v-if="panel.detailsOpen" class="rupture-details">
            <div class="rupture-details-head">
                <div>
                    <h2>{{ panel.ui.rupture.title }}</h2>
                    <p>{{ panel.ui.rupture.subtitle }}</p>
                </div>
                <button
                    class="button subtle small"
                    type="button"
                    @click="emit('close-details')"
                >
                    {{ panel.ui.buttons.close }}
                </button>
            </div>

            <template v-if="panel.hasLiveData">
                <div class="rupture-focus-inline">
                    <span class="rupture-focus-pill">
                        <span class="rupture-focus-label">
                            {{ panel.ui.rupture.currentPhase }}
                        </span>
                        <strong class="rupture-focus-value">
                            {{ panel.currentPhaseLabel }}
                        </strong>
                    </span>
                    <span class="rupture-focus-pill">
                        <span class="rupture-focus-label">
                            {{ panel.ui.rupture.timeRemaining }}
                        </span>
                        <strong class="rupture-focus-value">
                            {{ panel.currentPhaseRemainingLabel }}
                        </strong>
                    </span>
                </div>

                <div class="rupture-timeline">
                    <div class="rupture-track">
                        <div
                            v-for="phase in panel.phases"
                            :key="phase.key"
                            class="rupture-segment"
                            :class="[phase.toneClass, { active: phase.active }]"
                            :style="{ width: `${phase.widthPercent}%` }"
                        ></div>
                        <div
                            v-if="panel.markerPercent !== null"
                            class="rupture-marker"
                            :style="{ left: `${panel.markerPercent}%` }"
                        >
                            <span>{{ panel.markerLabel }}</span>
                        </div>
                    </div>
                    <div class="rupture-track-scale" :style="tickScaleStyle">
                        <span
                            v-for="tick in panel.timelineTicks"
                            :key="tick.key"
                            class="rupture-track-tick"
                            :class="[tick.align, `stack-${tick.stackLevel}`]"
                            :style="tick.align === 'right'
                                ? {}
                                : { left: `${tick.leftPercent}%` }"
                        >
                            {{ tick.label }}
                        </span>
                    </div>
                </div>

                <div
                    class="rupture-legend-row"
                    :aria-label="panel.ui.handles.legend"
                >
                    <span
                        v-for="phase in panel.phases"
                        :key="phase.key"
                        class="rupture-legend-item"
                        :class="{ active: phase.active }"
                    >
                        <span
                            class="rupture-track-swatch"
                            :class="phase.toneClass"
                        ></span>
                        <span
                            v-if="phase.key === 'incoming'"
                            class="rupture-legend-icon incoming"
                            :title="panel.ui.rupture.incomingDroneDisabledTooltip"
                            :aria-label="panel.ui.rupture.incomingDroneDisabledTooltip"
                            v-html="incomingDroneIconMarkup"
                        ></span>
                        <strong>{{ phase.label }}</strong>
                    </span>
                </div>
            </template>

            <div v-else class="empty-state rupture-empty-state">
                {{ panel.ui.rupture.noData }}
            </div>
        </div>
    </div>
</template>

<style scoped>
/* ── header strip ─────────────────────────────────────────────────────────── */

.rupture-strip {
    position: relative;
    flex: 1;
    min-width: 0;
    display: flex;
    align-items: stretch;
}

.rupture-strip-trigger {
    display: flex;
    flex: 1;
    align-items: center;
    gap: 12px;
    min-width: 0;
    padding: 6px 12px;
    border: 1px solid var(--border);
    border-left: 2px solid var(--border-strong);
    background: rgba(12, 19, 35, 0.86);
    color: var(--text);
    font-family: var(--font-mono);
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    cursor: pointer;
    transition: background 0.15s, border-color 0.15s;
}

.rupture-strip-trigger:hover {
    background: rgba(34, 211, 238, 0.1);
    border-color: var(--border-strong);
}

.rupture-strip-kicker {
    color: var(--accent);
    font-weight: 700;
    letter-spacing: 0.1em;
    white-space: nowrap;
}

.rupture-strip-phase {
    color: var(--text);
    font-weight: 700;
    white-space: nowrap;
}

.rupture-strip-phase.burning     { color: #fca5a5; }
.rupture-strip-phase.cooling     { color: var(--warn); }
.rupture-strip-phase.stabilizing { color: #e5e7eb; }
.rupture-strip-phase.stable      { color: var(--good); }
.rupture-strip-phase.incoming    { color: #c48dff; }

.rupture-track-strip {
    flex: 1;
    height: 10px;
    min-width: 80px;
    clip-path: none;
}

.rupture-strip-marker {
    position: absolute;
    top: -2px;
    bottom: -2px;
    width: 2px;
    background: var(--amber);
    box-shadow: 0 0 6px var(--amber);
    transform: translateX(-50%);
}

.rupture-strip-remaining {
    color: var(--amber);
    font-size: 0.8rem;
    font-weight: 700;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
}

.rupture-strip-nodata {
    flex: 1;
    color: var(--dim);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.rupture-strip-chevron {
    flex: 0 0 auto;
    color: var(--muted);
}

/* ── detail dropdown ──────────────────────────────────────────────────────── */

.rupture-details {
    position: absolute;
    top: calc(100% + 8px);
    right: 0;
    z-index: 20;
    width: min(760px, calc(100vw - 32px));
    display: grid;
    gap: 14px;
    padding: 16px;
    border: 1px solid var(--border-strong);
    background: rgba(12, 20, 38, 0.99);
    box-shadow: 0 22px 46px rgba(0, 0, 0, 0.45);
}

.rupture-details-head {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 14px;
}

.rupture-details-head h2 {
    font-size: 1.05rem;
}

.rupture-details-head p {
    color: var(--muted);
    font-size: 0.82rem;
}

/* ── shared track pieces ──────────────────────────────────────────────────── */

.rupture-focus-inline {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
    align-items: center;
}

.rupture-focus-pill {
    display: inline-flex;
    align-items: baseline;
    gap: 8px;
    padding: 6px 10px;
    border: 1px solid var(--border);
    background: rgba(255, 255, 255, 0.04);
    font-family: var(--font-mono);
}

.rupture-focus-label {
    font-size: 0.74rem;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--muted);
}

.rupture-focus-value {
    font-size: 0.95rem;
    color: var(--text);
    font-family: var(--font-mono);
}

.rupture-timeline {
    display: grid;
    gap: 14px;
}

.rupture-track {
    position: relative;
    display: flex;
    height: 20px;
    overflow: hidden;
    clip-path: polygon(4px 0%, calc(100% - 4px) 0%, 100% 4px, 100% 100%, 0% 100%, 0% 4px);
    border: 1px solid var(--border);
    background: rgba(255, 255, 255, 0.04);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
}

.rupture-segment {
    height: 100%;
    opacity: 0.8;
    min-width: 2px;
    border-right: 1px solid rgba(7, 13, 24, 0.45);
}

.rupture-segment.active {
    opacity: 1;
    filter: brightness(1.25);
    box-shadow: inset 0 0 0 2px rgba(255, 255, 255, 0.18);
}

.rupture-segment.burning {
    background: rgba(248, 113, 113, 0.18);
    border-color: rgba(248, 113, 113, 0.34);
}

.rupture-segment.cooling {
    background: rgba(245, 158, 11, 0.18);
    border-color: rgba(245, 158, 11, 0.34);
}

.rupture-segment.stabilizing {
    background: rgba(229, 231, 235, 0.14);
    border-color: rgba(229, 231, 235, 0.28);
}

.rupture-segment.stable {
    background: rgba(49, 196, 141, 0.16);
    border-color: rgba(49, 196, 141, 0.32);
}

.rupture-segment.incoming {
    background: rgba(168, 85, 247, 0.18);
    border-color: rgba(168, 85, 247, 0.34);
}

.rupture-marker {
    position: absolute;
    top: -34px;
    bottom: -10px;
    transform: translateX(-50%);
    display: inline-flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
    border-left: 2px solid var(--amber);
    box-shadow: var(--glow-amber);
    animation: marker-pulse 2s ease-in-out infinite;
}

.rupture-marker::after {
    content: "";
    position: absolute;
    top: 100%;
    left: -7px;
    width: 0;
    height: 0;
    border-left: 6px solid transparent;
    border-right: 6px solid transparent;
    border-top: 8px solid var(--amber);
}

.rupture-marker span {
    position: absolute;
    top: 0;
    left: 4px;
    transform: translateY(-100%);
    padding: 3px 8px;
    background: rgba(232, 184, 75, 0.18);
    color: var(--amber);
    font-size: 0.78rem;
    font-weight: 700;
    font-family: var(--font-mono);
    border: 1px solid var(--border-amber);
    white-space: nowrap;
}

@keyframes marker-pulse {
    0%, 100% { filter: drop-shadow(0 0 8px var(--amber)); }
    50%      { filter: drop-shadow(0 0 20px var(--amber)); }
}

.rupture-track-scale {
    position: relative;
    /* Ticks sit at top 10 / 32 / 54 depending on their stack level; reserve only
       the depth actually used. */
    height: calc(30px + var(--rupture-tick-levels, 2) * 22px);
    padding-top: 10px;
}

.rupture-track-swatch {
    width: 12px;
    height: 12px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, 0.18);
}

.rupture-track-swatch.burning    { background: rgba(248, 113, 113, 0.88); }
.rupture-track-swatch.cooling    { background: rgba(245, 158, 11, 0.88); }
.rupture-track-swatch.stabilizing{ background: rgba(229, 231, 235, 0.72); }
.rupture-track-swatch.stable     { background: rgba(49, 196, 141, 0.84); }
.rupture-track-swatch.incoming   { background: rgba(168, 85, 247, 0.88); }

.rupture-legend-icon {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 20px;
    height: 20px;
    color: var(--muted);
}

.rupture-legend-item.active .rupture-legend-icon,
.rupture-legend-icon.incoming {
    color: rgba(196, 141, 255, 0.95);
}

.rupture-legend-icon :deep(.rupture-legend-icon-svg) {
    display: block;
    width: 100%;
    height: 100%;
}

.rupture-track-tick {
    position: absolute;
    top: 10px;
    font-size: 0.74rem;
    color: var(--muted);
    white-space: nowrap;
    line-height: 1;
}

.rupture-track-tick.left   { transform: translateX(0); }
.rupture-track-tick.center { transform: translateX(-50%); }
.rupture-track-tick.right  { left: auto; right: 0; transform: none; text-align: right; }

.rupture-track-tick.right::before { left: auto; right: 0; transform: none; }

.rupture-track-tick.stack-1 { top: 32px; }
.rupture-track-tick.stack-2 { top: 54px; }

.rupture-track-tick.right.stack-1,
.rupture-track-tick.right.stack-2 {
    left: auto;
    right: 0;
    transform: none;
}

.rupture-track-tick.center.stack-1,
.rupture-track-tick.center.stack-2 {
    transform: translateX(-50%);
}

.rupture-track-tick::before {
    content: "";
    position: absolute;
    left: 50%;
    bottom: calc(100% + 6px);
    transform: translateX(-50%);
    width: 1px;
    height: 18px;
    background: rgba(255, 255, 255, 0.24);
}

.rupture-track-tick.stack-1::before { height: 40px; }
.rupture-track-tick.stack-2::before { height: 62px; }

.rupture-legend-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px 12px;
    align-items: center;
}

.rupture-legend-item {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    min-height: 30px;
    padding: 5px 10px;
    border: 1px solid var(--border);
    background: rgba(255, 255, 255, 0.04);
    color: var(--muted);
    font-family: var(--font-mono);
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
}

.rupture-legend-item.active {
    color: var(--text);
    border-color: var(--border-strong);
    background: var(--amber-soft);
}

.rupture-legend-item strong {
    font-size: 0.8rem;
}

.rupture-empty-state {
    border-style: dashed;
}

@media (max-width: 980px) {
    .rupture-strip-kicker,
    .rupture-track-strip {
        display: none;
    }

    .rupture-details {
        right: auto;
        left: 0;
        width: min(560px, calc(100vw - 32px));
    }
}
</style>
