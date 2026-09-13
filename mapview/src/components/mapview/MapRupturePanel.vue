<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref, watch } from "vue";

import droneProhibitedSvg from "../../assets/drone-prohibited-1-svgrepo-com.svg?raw";
import { formatClockSeconds } from "../../lib/formatters";
import type { MapRupturePanelModel, RupturePhaseView } from "../../lib/types";

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
const detailsRef = ref<HTMLElement | null>(null);
const detailsPosition = ref({ top: "12px", left: "12px" });

function positionDetails(): void {
    if (!rootRef.value || !detailsRef.value) return;
    const anchor = rootRef.value.getBoundingClientRect();
    const details = detailsRef.value.getBoundingClientRect();
    const margin = 12;
    detailsPosition.value = {
        top: `${Math.max(margin, Math.min(anchor.bottom + 8, window.innerHeight - details.height - margin))}px`,
        left: `${Math.max(margin, Math.min(anchor.right - details.width, window.innerWidth - details.width - margin))}px`,
    };
}

const activeToneClass = computed(
    () => props.panel.phases.find((phase) => phase.active)?.toneClass ?? "",
);

function phaseRange(phase: RupturePhaseView): string {
    return `${formatClockSeconds(phase.startSeconds)} - ${formatClockSeconds(phase.endSeconds)}`;
}

function handleDocumentPointerDown(event: PointerEvent): void {
    const target = event.target as Node | null;
    if (target && (rootRef.value?.contains(target) || detailsRef.value?.contains(target))) return;
    emit("close-details");
}

function handleDocumentKeydown(event: KeyboardEvent): void {
    if (event.key !== "Escape") return;
    event.stopPropagation();
    closeDetails();
}

function closeDetails(): void {
    emit("close-details");
    rootRef.value?.querySelector("button")?.focus();
}

watch(
    () => [props.panel.detailsOpen, props.panel.hasLiveData],
    async ([open], [wasOpen]) => {
        if (open) {
            document.addEventListener("pointerdown", handleDocumentPointerDown);
            document.addEventListener("keydown", handleDocumentKeydown, true);
            window.addEventListener("resize", positionDetails);
            window.addEventListener("scroll", positionDetails, true);
            await nextTick();
            positionDetails();
            if (!wasOpen) detailsRef.value?.querySelector("button")?.focus({ preventScroll: true });
            return;
        }
        document.removeEventListener("pointerdown", handleDocumentPointerDown);
        document.removeEventListener("keydown", handleDocumentKeydown, true);
        window.removeEventListener("resize", positionDetails);
        window.removeEventListener("scroll", positionDetails, true);
    },
);

onBeforeUnmount(() => {
    document.removeEventListener("pointerdown", handleDocumentPointerDown);
    document.removeEventListener("keydown", handleDocumentKeydown, true);
    window.removeEventListener("resize", positionDetails);
    window.removeEventListener("scroll", positionDetails, true);
});
</script>

<template>
    <div ref="rootRef" class="rupture-strip">
        <button
            class="rupture-strip-trigger"
            type="button"
            :aria-expanded="panel.detailsOpen"
            aria-controls="rupture-details"
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

        <Teleport to="body">
            <div
                v-if="panel.detailsOpen"
                id="rupture-details"
                ref="detailsRef"
                class="rupture-details"
                role="region"
                :aria-label="panel.ui.rupture.title"
                :style="detailsPosition"
            >
                <div class="rupture-details-head">
                    <div>
                        <h2>{{ panel.ui.rupture.title }}</h2>
                        <p>{{ panel.ui.rupture.subtitle }}</p>
                    </div>
                    <button
                        class="button subtle small"
                        type="button"
                        @click="closeDetails"
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
                        <!-- Position in the cycle. Previously a floating bubble above
                             the track, which overlapped this very row. -->
                        <span class="rupture-focus-pill">
                            <span class="rupture-focus-label">
                                {{ panel.ui.rupture.elapsed }}
                            </span>
                            <strong class="rupture-focus-value">
                                {{ panel.markerLabel }}
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
                            ></div>
                        </div>
                    </div>

                    <!-- Replaces the absolutely-positioned tick scale: phase
                         durations are wildly unequal, so the boundary labels piled
                         up at both ends of the track and left the middle empty.
                         A row per phase carries the same numbers, legibly, and
                         doubles as the legend. -->
                    <ul class="rupture-phase-list" :aria-label="panel.ui.handles.legend">
                        <li
                            v-for="phase in panel.phases"
                            :key="phase.key"
                            class="rupture-phase-row"
                            :class="{ active: phase.active }"
                        >
                            <span
                                class="rupture-track-swatch"
                                :class="phase.toneClass"
                                aria-hidden="true"
                            ></span>
                            <span class="rupture-phase-name">
                                {{ phase.label }}
                                <span
                                    v-if="phase.key === 'incoming'"
                                    class="rupture-legend-icon incoming"
                                    :title="panel.ui.rupture.incomingDroneDisabledTooltip"
                                    :aria-label="panel.ui.rupture.incomingDroneDisabledTooltip"
                                    v-html="incomingDroneIconMarkup"
                                ></span>
                            </span>
                            <span class="rupture-phase-range">{{ phaseRange(phase) }}</span>
                            <span class="rupture-phase-duration">{{ phase.durationLabel }}</span>
                            <span class="rupture-phase-status">{{ phase.statusLabel }}</span>
                        </li>
                    </ul>
                </template>

                <div v-else class="empty-state rupture-empty-state">
                    {{ panel.ui.rupture.noData }}
                </div>
            </div>
        </Teleport>
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
    position: fixed;
    z-index: 20;
    width: min(760px, calc(100vw - 24px));
    max-height: calc(100dvh - 24px);
    overflow: auto;
    overscroll-behavior: contain;
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
    top: -6px;
    bottom: -6px;
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

@keyframes marker-pulse {
    0%, 100% { filter: drop-shadow(0 0 8px var(--amber)); }
    50%      { filter: drop-shadow(0 0 20px var(--amber)); }
}

.rupture-phase-list {
    display: grid;
    gap: 4px;
    margin: 0;
    padding: 0;
    list-style: none;
}

.rupture-phase-row {
    display: grid;
    grid-template-columns: auto minmax(0, 1fr) auto auto auto;
    gap: 10px 14px;
    align-items: center;
    padding: 7px 10px;
    border: 1px solid transparent;
    border-left: 2px solid transparent;
    background: rgba(255, 255, 255, 0.03);
    color: var(--muted);
    font-family: var(--font-mono);
    font-size: 0.76rem;
}

.rupture-phase-row.active {
    color: var(--text);
    border-color: var(--border-strong);
    border-left-color: var(--amber);
    background: var(--amber-soft);
}

.rupture-phase-name {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    min-width: 0;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.rupture-phase-row.active .rupture-phase-name {
    font-weight: 700;
}

.rupture-phase-range,
.rupture-phase-duration,
.rupture-phase-status {
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
}

.rupture-phase-range {
    color: var(--dim);
}

.rupture-phase-duration {
    min-width: 3.5rem;
    text-align: right;
    color: var(--muted);
}

.rupture-phase-status {
    min-width: 9rem;
    text-align: right;
}

.rupture-phase-row.active .rupture-phase-status {
    color: var(--amber);
    font-weight: 700;
}

.rupture-empty-state {
    border-style: dashed;
}

@media (max-width: 980px) {
    .rupture-phase-row {
        grid-template-columns: auto minmax(0, 1fr) auto;
    }

    .rupture-phase-status {
        grid-column: 2 / -1;
        min-width: 0;
        text-align: left;
    }

    .rupture-strip-kicker,
    .rupture-track-strip {
        display: none;
    }

    .rupture-details {
        width: min(560px, calc(100vw - 24px));
    }
}

@media (max-width: 720px) {
    .rupture-phase-name {
        grid-column: 2 / -1;
        white-space: normal;
    }

    .rupture-phase-range {
        grid-column: 2;
    }
}
</style>
