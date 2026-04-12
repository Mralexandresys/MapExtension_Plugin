import { computed, ref } from "vue";

import type {
    Point2D,
    Rect2D,
    UserAnnotationDraft,
    UserAnnotationEditMode,
    UserAnnotationExport,
    UserAnnotationMode,
    UserAnnotationSelection,
    UserAnnotationSummary,
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

function load(): { markers: UserMarker[]; zones: UserZone[] } {
    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (!raw) return { markers: [], zones: [] };
        const parsed = JSON.parse(raw);
        return {
            markers: Array.isArray(parsed.markers)
                ? parsed.markers.map((marker: Partial<UserMarker>) => ({
                    id: marker.id ?? genId(),
                    label: marker.label ?? "",
                    description: marker.description ?? "",
                    color: normalizeColor(marker.color, DEFAULT_MARKER_COLOR),
                    map: marker.map ?? { x: 0, y: 0 },
                    createdAt: marker.createdAt ?? new Date().toISOString(),
                }))
                : [],
            zones: Array.isArray(parsed.zones)
                ? parsed.zones.map((zone: Partial<UserZone>) => ({
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
    } catch {
        return { markers: [], zones: [] };
    }
}

export function useUserAnnotations() {
    const stored = load();
    const markers = ref<UserMarker[]>(stored.markers);
    const zones = ref<UserZone[]>(stored.zones);

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

    const hasAnnotations = computed(
        () => markers.value.length > 0 || zones.value.length > 0,
    );
    const markersCount = computed(() => markers.value.length);
    const zonesCount = computed(() => zones.value.length);

    const annotations = computed<UserAnnotationSummary[]>(() => {
        const ms: UserAnnotationSummary[] = markers.value.map((m) => ({
            id: m.id,
            type: "marker" as const,
            label: m.label,
            description: m.description,
            meta: `${Math.round(m.map.x)}, ${Math.round(m.map.y)}`,
            createdAt: m.createdAt,
        }));
        const zs: UserAnnotationSummary[] = zones.value.map((z) => ({
            id: z.id,
            type: "zone" as const,
            label: z.label,
            description: z.description,
            meta: `${Math.round(z.rect.width)} × ${Math.round(z.rect.height)}`,
            createdAt: z.createdAt,
        }));
        return [...ms, ...zs].sort((a, b) => a.createdAt.localeCompare(b.createdAt));
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
            label: `Marker ${n}`,
            description: "",
            color: DEFAULT_MARKER_COLOR,
            map: { x: point.x, y: point.y },
            createdAt: new Date().toISOString(),
        };
        markers.value = [...markers.value, marker];
        selectedAnnotation.value = { type: "marker", id };
        annotationMode.value = "idle";
        annotationEditMode.value = "idle";
        persist(markers.value, zones.value);
    }

    function addZone(rect: Rect2D): void {
        const id = genId();
        const n = zones.value.length + 1;
        const zone: UserZone = {
            id,
            label: `Zone ${n}`,
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
        persist(markers.value, zones.value);
    }

    function moveSelectedMarker(point: Point2D): void {
        const sel = selectedAnnotation.value;
        if (!sel || sel.type !== "marker") return;
        markers.value = markers.value.map((marker) =>
            marker.id === sel.id ? { ...marker, map: { ...point } } : marker,
        );
        annotationEditMode.value = "idle";
        persist(markers.value, zones.value);
    }

    function updateSelectedZoneRect(rect: Rect2D): void {
        const sel = selectedAnnotation.value;
        if (!sel || sel.type !== "zone") return;
        zones.value = zones.value.map((zone) =>
            zone.id === sel.id ? { ...zone, rect: { ...rect } } : zone,
        );
        annotationEditMode.value = "idle";
        persist(markers.value, zones.value);
    }

    function toggleSelectedZoneLock(): void {
        const sel = selectedAnnotation.value;
        if (!sel || sel.type !== "zone") return;
        zones.value = zones.value.map((zone) =>
            zone.id === sel.id ? { ...zone, locked: !zone.locked } : zone,
        );
        annotationEditMode.value = "idle";
        persist(markers.value, zones.value);
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
        persist(markers.value, zones.value);
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
        persist(markers.value, zones.value);
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
        a.click();
        URL.revokeObjectURL(url);
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
            const incoming = parsed as UserAnnotationExport;
            if (hasAnnotations.value) {
                const ok = window.confirm(
                    "Importing will replace all current annotations. Continue?",
                );
                if (!ok) return;
            }
            markers.value = incoming.markers;
            zones.value = incoming.zones;
            annotationEditMode.value = "idle";
            selectedAnnotation.value = null;
            persist(markers.value, zones.value);
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
        hasAnnotations,
        markersCount,
        zonesCount,
        annotations,
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
