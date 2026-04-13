<script setup lang="ts">
import type { MapRupturePanelModel } from "../../lib/types";

defineProps<{
    panel: MapRupturePanelModel;
}>();

const emit = defineEmits<{
    "toggle-collapse": [];
}>();
</script>

<template>
    <div class="overlay-layer overlay-top-center timeline-layer">
        <button
            v-if="panel.collapsed"
            class="drawer-handle drawer-handle-top-center"
            type="button"
            :aria-expanded="!panel.collapsed"
            @click="emit('toggle-collapse')"
        >
            <span class="timeline-handle-pulse" aria-hidden="true"></span>
            <span class="timeline-handle-label">
                {{ panel.ui.handles.timeline }}
            </span>
            <span class="collapse-arrow down" aria-hidden="true"></span>
        </button>
        <section
            class="floating-panel timeline-panel"
            :class="{ collapsed: panel.collapsed }"
        >
            <button
                v-if="!panel.collapsed"
                class="panel-edge-toggle panel-edge-toggle-bottom-center"
                type="button"
                :aria-label="panel.ui.buttons.collapse"
                :title="panel.ui.buttons.collapse"
                @click="emit('toggle-collapse')"
            >
                <span class="collapse-arrow up" aria-hidden="true"></span>
            </button>
            <div class="panel-top-row">
                <div>
                    <span class="eyebrow">{{ panel.ui.handles.timeline }}</span>
                    <h2>{{ panel.ui.rupture.title }}</h2>
                    <p>{{ panel.ui.rupture.subtitle }}</p>
                </div>
            </div>

            <div class="drawer-body timeline-body">
                <section class="rupture-panel">
                    <div class="rupture-focus-inline">
                        <span class="rupture-focus-pill">
                            <span class="rupture-focus-label">{{
                                panel.ui.rupture.currentPhase
                            }}</span>
                            <strong class="rupture-focus-value">{{
                                panel.currentPhaseLabel
                            }}</strong>
                        </span>
                        <span class="rupture-focus-pill">
                            <span class="rupture-focus-label">{{
                                panel.ui.rupture.timeRemaining
                            }}</span>
                            <strong class="rupture-focus-value">{{
                                panel.currentPhaseRemainingLabel
                            }}</strong>
                        </span>
                    </div>

                    <div class="rupture-timeline">
                        <div class="rupture-track">
                            <div
                                v-for="phase in panel.phases"
                                :key="phase.key"
                                class="rupture-segment"
                                :class="[
                                    phase.toneClass,
                                    { active: phase.active },
                                ]"
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
                        <div class="rupture-track-scale">
                            <span
                                v-for="tick in panel.timelineTicks"
                                :key="tick.key"
                                class="rupture-track-tick"
                                :class="[
                                    tick.align,
                                    `stack-${tick.stackLevel}`,
                                ]"
                                :style="tick.align === 'right'
                                    ? {}
                                    : { left: `${tick.leftPercent}%` }"
                            >
                                {{ tick.label }}
                            </span>
                        </div>
                    </div>

                    <div class="rupture-legend-row" aria-label="Timeline legend">
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
                            <strong>{{ phase.label }}</strong>
                        </span>
                    </div>

                    <div
                        v-if="!panel.hasLiveData"
                        class="empty-state compact-empty rupture-empty-state"
                    >
                        {{ panel.ui.rupture.noData }}
                    </div>
                </section>
            </div>
        </section>
    </div>
</template>

<style scoped>
.timeline-body {
    display: grid;
    gap: 14px;
}

.rupture-panel {
    display: grid;
    gap: 16px;
    padding: 12px;
    border: 1px solid var(--border);
    border-radius: 0;
    background: rgba(12, 20, 38, 0.9);
}

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
    border-radius: 0;
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
    border-radius: 0;
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
    50%       { filter: drop-shadow(0 0 20px var(--amber)); }
}

.rupture-track-scale {
    position: relative;
    height: 86px;
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

@media (max-width: 1100px) {
    .rupture-track-scale { height: 96px; }
    .rupture-track-tick  { font-size: 0.72rem; }
    .rupture-track-tick.stack-1 { top: 36px; }
    .rupture-track-tick.stack-2 { top: 62px; }
    .rupture-track-tick.stack-1::before { height: 44px; }
    .rupture-track-tick.stack-2::before { height: 70px; }
}

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
    border-radius: 0;
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

</style>
