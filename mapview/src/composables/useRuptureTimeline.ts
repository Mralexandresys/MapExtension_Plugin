import { computed, type ComputedRef, type Ref } from "vue";

import { clamp, formatClockSeconds } from "../lib/formatters";
import type { Messages } from "../lang";
import type {
    RuptureCycleResponse,
    RupturePhaseKey,
    RupturePhaseView,
    RuptureTimelineTick,
} from "../lib/types";

const RUPTURE_INCOMING_MIN_WIDTH_PERCENT = 4.5;

interface RuptureVisualPhase {
    key: RupturePhaseKey;
    toneClass: string;
    startSeconds: number;
    endSeconds: number;
    visualStartPercent: number;
    visualEndPercent: number;
}

function mapRuptureStageToPhaseKey(
    stage: string,
    step: string,
): RupturePhaseKey | null {
    if (stage === "Moving") return "burning";
    if (stage === "Fadeout") {
        return step === "FireWave" ? "burning" : "cooling";
    }
    if (stage === "Growback") return "stabilizing";
    if (stage === "PreWave") return "stable";
    return null;
}

function getRuptureDurations(response: RuptureCycleResponse | null) {
    const timeline = response?.timeline;
    const burning = timeline?.phase_seconds?.burning ?? 30;
    const cooling = timeline?.phase_seconds?.cooling ?? 60;
    const stabilizing = timeline?.phase_seconds?.stabilizing ?? 600;
    const rawStable = timeline?.phase_seconds?.stable ?? 2550;
    const incoming = Math.min(15, Math.max(0, rawStable));
    const stable = Math.max(0, rawStable - incoming);

    return {
        burning,
        cooling,
        stabilizing,
        stable,
        incoming,
    };
}

function mapRupturePositionToPhaseKey(
    positionSeconds: number,
    durations: ReturnType<typeof getRuptureDurations>,
): RupturePhaseKey {
    const burningEnd = durations.burning;
    const coolingEnd = burningEnd + durations.cooling;
    const stabilizingEnd = coolingEnd + durations.stabilizing;
    const stableEnd = stabilizingEnd + durations.stable;

    if (positionSeconds < burningEnd) return "burning";
    if (positionSeconds < coolingEnd) return "cooling";
    if (positionSeconds < stabilizingEnd) return "stabilizing";
    if (positionSeconds < stableEnd) return "stable";
    return "incoming";
}

function buildRuptureVisualPhases(
    durations: ReturnType<typeof getRuptureDurations>,
    total: number,
): RuptureVisualPhase[] {
    const defs: Array<{
        key: RupturePhaseKey;
        toneClass: string;
        durationSeconds: number;
    }> = [
        { key: "burning", toneClass: "burning", durationSeconds: durations.burning },
        { key: "cooling", toneClass: "cooling", durationSeconds: durations.cooling },
        {
            key: "stabilizing",
            toneClass: "stabilizing",
            durationSeconds: durations.stabilizing,
        },
        { key: "stable", toneClass: "stable", durationSeconds: durations.stable },
        {
            key: "incoming",
            toneClass: "incoming",
            durationSeconds: durations.incoming,
        },
    ];

    const actualWidths = defs.map((phase) => (phase.durationSeconds / total) * 100);
    const visualWidths = [...actualWidths];
    const incomingIndex = defs.findIndex((phase) => phase.key === "incoming");
    const stableIndex = defs.findIndex((phase) => phase.key === "stable");

    if (
        incomingIndex >= 0 &&
        stableIndex >= 0 &&
        visualWidths[incomingIndex] < RUPTURE_INCOMING_MIN_WIDTH_PERCENT
    ) {
        const extraWidth =
            RUPTURE_INCOMING_MIN_WIDTH_PERCENT - visualWidths[incomingIndex];
        visualWidths[incomingIndex] += extraWidth;
        visualWidths[stableIndex] = Math.max(0, visualWidths[stableIndex] - extraWidth);
    }

    let actualCursor = 0;
    let visualCursor = 0;

    return defs.map((phase, index) => {
        const startSeconds = actualCursor;
        const endSeconds = startSeconds + phase.durationSeconds;
        const visualStartPercent = visualCursor;
        const visualEndPercent = visualStartPercent + visualWidths[index];

        actualCursor = endSeconds;
        visualCursor = visualEndPercent;

        return {
            key: phase.key,
            toneClass: phase.toneClass,
            startSeconds,
            endSeconds,
            visualStartPercent,
            visualEndPercent,
        };
    });
}

function mapRuptureSecondsToVisualPercent(
    seconds: number,
    phases: RuptureVisualPhase[],
    total: number,
): number {
    if (!Number.isFinite(seconds) || total <= 0 || phases.length === 0) {
        return 0;
    }

    if (Math.abs(seconds - total) < 0.0001) {
        return 100;
    }

    const normalized = ((seconds % total) + total) % total;
    for (const phase of phases) {
        if (normalized <= phase.endSeconds || phase === phases[phases.length - 1]) {
            const duration = Math.max(phase.endSeconds - phase.startSeconds, 0.0001);
            const localProgress = Math.min(
                1,
                Math.max(0, (normalized - phase.startSeconds) / duration),
            );
            return (
                phase.visualStartPercent +
                (phase.visualEndPercent - phase.visualStartPercent) * localProgress
            );
        }
    }

    return 100;
}

export function useRuptureTimeline(
    ruptureCycle: Ref<RuptureCycleResponse | null>,
    now: Ref<number>,
    ui: ComputedRef<Messages>,
) {
    const ruptureState = computed(() => ruptureCycle.value?.rupture_cycle ?? null);
    const ruptureTimeline = computed(() => ruptureCycle.value?.timeline ?? null);
    const ruptureCycleTotalSeconds = computed(
        () => ruptureTimeline.value?.cycle_total_seconds ?? 3240,
    );
    const rupturePhaseDurations = computed(() =>
        getRuptureDurations(ruptureCycle.value),
    );

    const ruptureElapsedSeconds = computed(() => {
        const value = ruptureState.value?.elapsed_seconds;
        return typeof value === "number" && Number.isFinite(value) ? value : null;
    });

    const ruptureObservedAtUnixMs = computed(() => {
        const value = ruptureState.value?.observed_at_unix_ms;
        return typeof value === "number" && Number.isFinite(value) ? value : null;
    });

    const ruptureLiveElapsedSeconds = computed(() => {
        const elapsed = ruptureElapsedSeconds.value;
        if (elapsed == null) return null;

        const observedAtUnixMs = ruptureObservedAtUnixMs.value;
        if (observedAtUnixMs == null) {
            return elapsed;
        }

        const driftSeconds = Math.max(0, (now.value - observedAtUnixMs) / 1000);
        return elapsed + driftSeconds;
    });

    const ruptureCyclePositionSeconds = computed(() => {
        const elapsed = ruptureLiveElapsedSeconds.value;
        const total = ruptureCycleTotalSeconds.value;
        if (elapsed == null || total <= 0) return null;

        const normalized = elapsed % total;
        return normalized >= 0 ? normalized : normalized + total;
    });

    const ruptureCurrentPhaseKey = computed<RupturePhaseKey>(() => {
        const positionSeconds = ruptureCyclePositionSeconds.value;
        if (positionSeconds != null) {
            return mapRupturePositionToPhaseKey(
                positionSeconds,
                rupturePhaseDurations.value,
            );
        }

        const stage = ruptureState.value?.stage ?? "None";
        const step = ruptureState.value?.step ?? "None";
        const phaseFromStage = mapRuptureStageToPhaseKey(stage, step);
        if (phaseFromStage) return phaseFromStage;

        return "stable";
    });

    const ruptureCurrentPhaseLabel = computed(
        () => ui.value.rupture.phases[ruptureCurrentPhaseKey.value],
    );

    const ruptureMarkerSeconds = computed(() => {
        const positionSeconds = ruptureCyclePositionSeconds.value;
        const cycleEnd = ruptureCycleTotalSeconds.value;
        if (positionSeconds == null || cycleEnd <= 0) return null;
        return clamp(positionSeconds, 0, cycleEnd);
    });

    const rupturePhases = computed<RupturePhaseView[]>(() => {
        const durations = rupturePhaseDurations.value;
        const marker = ruptureMarkerSeconds.value ?? 0;
        const total = ruptureCycleTotalSeconds.value || 1;
        const phaseDefs = buildRuptureVisualPhases(durations, total);

        return phaseDefs.map((phase) => {
            const durationSeconds = phase.endSeconds - phase.startSeconds;
            const startSeconds = phase.startSeconds;
            const endSeconds = phase.endSeconds;

            let statusLabel = ui.value.rupture.completed;
            let shortStatusLabel = ui.value.rupture.completed;
            if (ruptureMarkerSeconds.value == null) {
                statusLabel = ui.value.rupture.noDataShort;
                shortStatusLabel = ui.value.rupture.noDataShort;
            } else if (ruptureCurrentPhaseKey.value === phase.key) {
                const remaining = formatClockSeconds(Math.max(0, endSeconds - marker));
                statusLabel = ui.value.rupture.format.endsIn(remaining);
                shortStatusLabel = ui.value.rupture.format.remainingShort(remaining);
            } else if (marker < startSeconds) {
                const remaining = formatClockSeconds(Math.max(0, startSeconds - marker));
                statusLabel = ui.value.rupture.format.startsIn(remaining);
                shortStatusLabel = ui.value.rupture.format.startsShort(remaining);
            }

            return {
                key: phase.key,
                label: ui.value.rupture.phases[phase.key],
                durationSeconds,
                durationLabel: formatClockSeconds(durationSeconds),
                startSeconds,
                endSeconds,
                visualStartPercent: phase.visualStartPercent,
                visualEndPercent: phase.visualEndPercent,
                widthPercent: phase.visualEndPercent - phase.visualStartPercent,
                active: ruptureCurrentPhaseKey.value === phase.key,
                statusLabel,
                shortStatusLabel,
                toneClass: phase.toneClass,
            };
        });
    });

    const ruptureMarkerPercent = computed(() => {
        const marker = ruptureMarkerSeconds.value;
        const total = ruptureCycleTotalSeconds.value;
        if (marker == null || total <= 0) return null;
        return mapRuptureSecondsToVisualPercent(
            marker,
            buildRuptureVisualPhases(rupturePhaseDurations.value, total),
            total,
        );
    });

    const ruptureHasLiveData = computed(() => ruptureMarkerPercent.value !== null);
    const ruptureCurrentPhaseView = computed(
        () =>
            rupturePhases.value.find((phase) => phase.active) ??
            rupturePhases.value[rupturePhases.value.length - 1] ??
            null,
    );
    const ruptureCurrentPhaseRemainingSeconds = computed(() => {
        const marker = ruptureMarkerSeconds.value;
        const phase = ruptureCurrentPhaseView.value;
        if (marker == null || !phase) return null;
        return Math.max(0, phase.endSeconds - marker);
    });
    const ruptureCurrentPhaseRemainingLabel = computed(() =>
        formatClockSeconds(ruptureCurrentPhaseRemainingSeconds.value),
    );
    const ruptureMarkerLabel = computed(() =>
        formatClockSeconds(ruptureMarkerSeconds.value),
    );

    const ruptureTimelineTicks = computed<RuptureTimelineTick[]>(() => {
        const durations = rupturePhaseDurations.value;
        const total = ruptureCycleTotalSeconds.value || 1;
        const phaseDefs = buildRuptureVisualPhases(durations, total);
        const boundaries = [
            { key: "start", seconds: 0 },
            { key: "burning-end", seconds: durations.burning },
            { key: "cooling-end", seconds: durations.burning + durations.cooling },
            {
                key: "stabilizing-end",
                seconds:
                    durations.burning + durations.cooling + durations.stabilizing,
            },
            {
                key: "stable-end",
                seconds:
                    durations.burning +
                    durations.cooling +
                    durations.stabilizing +
                    durations.stable,
            },
            { key: "end", seconds: total },
        ];

        let previousStackLevel = 0;

        return boundaries.map((boundary, index) => {
            const leftPercent = mapRuptureSecondsToVisualPercent(
                boundary.seconds,
                phaseDefs,
                total,
            );
            const previousLeftPercent =
                index > 0
                    ? mapRuptureSecondsToVisualPercent(
                          boundaries[index - 1].seconds,
                          phaseDefs,
                          total,
                      )
                    : null;
            const nextLeftPercent =
                index < boundaries.length - 1
                    ? mapRuptureSecondsToVisualPercent(
                          boundaries[index + 1].seconds,
                          phaseDefs,
                          total,
                      )
                    : null;
            const isCloseToPrevious =
                previousLeftPercent != null &&
                Math.abs(leftPercent - previousLeftPercent) < 10;
            const isCloseToNext =
                nextLeftPercent != null &&
                Math.abs(nextLeftPercent - leftPercent) < 10;

            let align: "left" | "center" | "right" = "center";
            if (index === boundaries.length - 1) align = "right";

            let stackLevel = 0;
            if (isCloseToPrevious) {
                stackLevel = Math.min(previousStackLevel + 1, 2);
            } else if (isCloseToNext && index % 2 === 1) {
                stackLevel = 1;
            }

            previousStackLevel = stackLevel;

            return {
                key: boundary.key,
                label: formatClockSeconds(boundary.seconds),
                leftPercent,
                align,
                stackLevel,
            };
        });
    });

    return {
        ruptureCurrentPhaseKey,
        ruptureCurrentPhaseLabel,
        ruptureCurrentPhaseRemainingLabel,
        rupturePhases,
        ruptureMarkerPercent,
        ruptureHasLiveData,
        ruptureTimelineTicks,
        ruptureMarkerLabel,
    };
}
