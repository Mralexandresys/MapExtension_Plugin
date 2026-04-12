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
                                :style="{ left: `${tick.leftPercent}%` }"
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
