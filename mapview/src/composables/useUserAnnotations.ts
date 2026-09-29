import { computed, ref, watch, type ComputedRef } from "vue";

import type { Messages } from "../lang";
import type {
    Point2D,
    Rect2D,
    UserAnnotationDraft,
    UserAnnotationEditMode,
    UserAnnotationExport,
    UserAnnotationMode,
    UserAnnotationSelection,
    UserMarker,
    UserZone,
} from "../lib/types";

const STORAGE_KEY = "user-annotations";
const DEFAULT_MARKER_COLOR = "#e8b84b";
const DEFAULT_ZONE_COLOR = "#22d3ee";

function normalizeColor(value: unknown, fallback: string): string {
    return typeof value === "string" && /^#[0-9a-fA-F]{6}$/.test(value)
        ? value
        : fallback;
}

function genId(): string {
    if (typeof crypto !== "undefined" && crypto.randomUUID) {
        return crypto.randomUUID();
    }
    return `${Date.now()}-${Math.random().toString(36).slice(2)}`;
}

function persist(markers: UserMarker[], zones: UserZone[]): void {
    try {
        localStorage.setItem(
            STORAGE_KEY,
            JSON.stringify({ markers, zones }),
        );
    } catch {
        // storage unavailable — silently ignore
    }
}

/**
 * Stored and imported annotations go through the same defaults, so a file
 * written by an older build, or edited by hand, cannot break rendering.
 */
function normalizeAnnotations(parsed: unknown): { markers: UserMarker[]; zones: UserZone[] } {
    const source = (parsed ?? {}) as { markers?: unknown; zones?: unknown };
    return {
        markers: Array.isArray(source.markers)
            ? source.markers.map((marker: Partial<UserMarker>) => ({
                id: marker.id ?? genId(),
                label: marker.label ?? "",
                description: marker.description ?? "",
                color: normalizeColor(marker.color, DEFAULT_MARKER_COLOR),
                map: marker.map ?? { x: 0, y: 0 },
                createdAt: marker.createdAt ?? new Date().toISOString(),
            }))
            : [],
        zones: Array.isArray(source.zones)
            ? source.zones.map((zone: Partial<UserZone>) => ({
                id: zone.id ?? genId(),
                label: zone.label ?? "",
                description: zone.description ?? "",
                color: normalizeColor(zone.color, DEFAULT_ZONE_COLOR),
                locked: Boolean(zone.locked),
                rect: zone.rect ?? { x: 0, y: 0, width: 0, height: 0 },
                createdAt: zone.createdAt ?? new Date().toISOString(),
            }))
            : [],
    };
}

function load(): { markers: UserMarker[]; zones: UserZone[] } {
    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        return normalizeAnnotations(raw ? JSON.parse(raw) : null);
    } catch {
        return { markers: [], zones: [] };
    }
}

export function useUserAnnotations(ui: ComputedRef<Messages>) {
    const stored = load();
    const markers = ref<UserMarker[]>(stored.markers);
    const zones = ref<UserZone[]>(stored.zones);
    // Every mutation replaces the arrays, so a shallow watch sees them all.
    watch([markers, zones], ([nextMarkers, nextZones]) => persist(nextMarkers, nextZones));

    const annotationMode = ref<UserAnnotationMode>("idle");
    const annotationEditMode = ref<UserAnnotationEditMode>("idle");
    const selectedAnnotation = ref<UserAnnotationSelection>(null);
    const importError = ref("");

    // ── derived ──────────────────────────────────────────────────────────────

    const draft = computed<UserAnnotationDraft>(() => {
        const sel = selectedAnnotation.value;
        if (!sel) return { label: "", description: "", color: DEFAULT_MARKER_COLOR };
        if (sel.type === "marker") {
            const m = markers.value.find((x) => x.id === sel.id);
            return {
                label: m?.label ?? "",
                description: m?.description ?? "",
                color: m?.color ?? DEFAULT_MARKER_COLOR,
            };
        }
        const z = zones.value.find((x) => x.id === sel.id);
        return {
            label: z?.label ?? "",
            description: z?.description ?? "",
            color: z?.color ?? DEFAULT_ZONE_COLOR,
        };
    });

    const selectedZoneLocked = computed(() => {
        const sel = selectedAnnotation.value;
        if (!sel || sel.type !== "zone") return false;
        return zones.value.find((zone) => zone.id === sel.id)?.locked ?? false;
    });

    // ── mode ─────────────────────────────────────────────────────────────────

    function setAnnotationMode(mode: UserAnnotationMode): void {
        annotationEditMode.value = "idle";
        annotationMode.value = annotationMode.value === mode ? "idle" : mode;
    }

    function setAnnotationEditMode(mode: UserAnnotationEditMode): void {
        annotationMode.value = "idle";
        annotationEditMode.value = annotationEditMode.value === mode ? "idle" : mode;
    }

    // ── selection ─────────────────────────────────────────────────────────────

    function selectMarker(id: string): void {
        annotationEditMode.value = "idle";
        selectedAnnotation.value = { type: "marker", id };
    }

    function selectZone(id: string): void {
        annotationEditMode.value = "idle";
        selectedAnnotation.value = { type: "zone", id };
    }

    function clearAnnotationSelection(): void {
        annotationEditMode.value = "idle";
        selectedAnnotation.value = null;
    }

    // ── mutations ─────────────────────────────────────────────────────────────

    function addMarker(point: Point2D): void {
        const id = genId();
        const n = markers.value.length + 1;
        const marker: UserMarker = {
            id,
            label: ui.value.notes.markerDefaultLabel(n),
            description: "",
            color: DEFAULT_MARKER_COLOR,
            map: { x: point.x, y: point.y },
            createdAt: new Date().toISOString(),
        };
        markers.value = [...markers.value, marker];
        selectedAnnotation.value = { type: "marker", id };
        annotationMode.value = "idle";
        annotationEditMode.value = "idle";
    }

    function addZone(rect: Rect2D): void {
        const id = genId();
        const n = zones.value.length + 1;
        const zone: UserZone = {
            id,
            label: ui.value.notes.zoneDefaultLabel(n),
            description: "",
            color: DEFAULT_ZONE_COLOR,
            locked: false,
            rect: { ...rect },
            createdAt: new Date().toISOString(),
        };
        zones.value = [...zones.value, zone];
        selectedAnnotation.value = { type: "zone", id };
        annotationMode.value = "idle";
        annotationEditMode.value = "idle";
    }

    function moveSelectedMarker(point: Point2D): void {
        const sel = selectedAnnotation.value;
        if (!sel || sel.type !== "marker") return;
        markers.value = markers.value.map((marker) =>
            marker.id === sel.id ? { ...marker, map: { ...point } } : marker,
        );
        annotationEditMode.value = "idle";
    }

    function updateSelectedZoneRect(rect: Rect2D): void {
        const sel = selectedAnnotation.value;
        if (!sel || sel.type !== "zone") return;
        zones.value = zones.value.map((zone) =>
            zone.id === sel.id ? { ...zone, rect: { ...rect } } : zone,
        );
        annotationEditMode.value = "idle";
    }

    function toggleSelectedZoneLock(): void {
        const sel = selectedAnnotation.value;
        if (!sel || sel.type !== "zone") return;
        zones.value = zones.value.map((zone) =>
            zone.id === sel.id ? { ...zone, locked: !zone.locked } : zone,
        );
        annotationEditMode.value = "idle";
    }

    function updateSelectedDraft(d: UserAnnotationDraft): void {
        const sel = selectedAnnotation.value;
        if (!sel) return;
        if (sel.type === "marker") {
            markers.value = markers.value.map((m) =>
                m.id === sel.id
                    ? {
                        ...m,
                        label: d.label,
                        description: d.description,
                        color: normalizeColor(d.color, DEFAULT_MARKER_COLOR),
                    }
                    : m,
            );
        } else {
            zones.value = zones.value.map((z) =>
                z.id === sel.id
                    ? {
                        ...z,
                        label: d.label,
                        description: d.description,
                        color: normalizeColor(d.color, DEFAULT_ZONE_COLOR),
                    }
                    : z,
            );
        }
    }

    function deleteSelectedAnnotation(): void {
        const sel = selectedAnnotation.value;
        if (!sel) return;
        if (sel.type === "marker") {
            markers.value = markers.value.filter((m) => m.id !== sel.id);
        } else {
            zones.value = zones.value.filter((z) => z.id !== sel.id);
        }
        annotationEditMode.value = "idle";
        selectedAnnotation.value = null;
    }

    // ── import / export ───────────────────────────────────────────────────────

    function exportAnnotations(): void {
        const data: UserAnnotationExport = {
            version: 1,
            exportedAt: new Date().toISOString(),
            markers: markers.value,
            zones: zones.value,
        };
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = `starrupture-annotations-${Date.now()}.json`;
        // Firefox ignores clicks on a detached anchor, and revoking the URL in
        // the same tick can cancel the download before it starts.
        a.style.display = "none";
        document.body.appendChild(a);
        a.click();
        setTimeout(() => {
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }, 0);
    }

    async function importAnnotations(file: File): Promise<void> {
        importError.value = "";
        try {
            const text = await file.text();
            const parsed: unknown = JSON.parse(text);
            if (
                typeof parsed !== "object" ||
                parsed === null ||
                (parsed as UserAnnotationExport).version !== 1 ||
                !Array.isArray((parsed as UserAnnotationExport).markers) ||
                !Array.isArray((parsed as UserAnnotationExport).zones)
            ) {
                importError.value = "invalid";
                return;
            }
            const incoming = normalizeAnnotations(parsed);
            if (markers.value.length > 0 || zones.value.length > 0) {
                const ok = window.confirm(ui.value.notes.importReplaceConfirm);
                if (!ok) return;
            }
            markers.value = incoming.markers;
            zones.value = incoming.zones;
            annotationEditMode.value = "idle";
            selectedAnnotation.value = null;
            } catch {
            importError.value = "invalid";
        }
    }

    return {
        markers,
        zones,
        annotationMode,
        annotationEditMode,
        selectedAnnotation,
        selectedZoneLocked,
        importError,
        draft,
        setAnnotationMode,
        setAnnotationEditMode,
        selectMarker,
        selectZone,
        clearAnnotationSelection,
        addMarker,
        addZone,
        moveSelectedMarker,
        updateSelectedZoneRect,
        toggleSelectedZoneLock,
        updateSelectedDraft,
        deleteSelectedAnnotation,
        exportAnnotations,
        importAnnotations,
    };
}
