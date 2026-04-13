import { computed, onBeforeUnmount, onMounted, ref, type Ref } from "vue";

import { clamp } from "../lib/formatters";

interface DragState {
    x: number;
    y: number;
    tx: number;
    ty: number;
}

const IDLE_ZOOM = 1;
const FOCUS_ZOOM = 1.55;

export function useMapPanZoom(
    mapShell: Ref<HTMLElement | null>,
    viewBoxWidth: Ref<number>,
    viewBoxHeight: Ref<number>,
) {
    const mapScale = ref(IDLE_ZOOM);
    const mapTranslateX = ref(0);
    const mapTranslateY = ref(0);
    const drag = ref<DragState | null>(null);

    const transform = computed(
        () => `translate(${mapTranslateX.value},${mapTranslateY.value}) scale(${mapScale.value})`,
    );
    const isDragging = computed(() => drag.value !== null);

    function pixelToViewBox(deltaPx: number, deltaPy: number): { dx: number; dy: number } {
        if (!mapShell.value) return { dx: 0, dy: 0 };

        const rect = mapShell.value.getBoundingClientRect();
        if (!rect.width || !rect.height) {
            return { dx: 0, dy: 0 };
        }

        const fitScale = Math.min(rect.width / viewBoxWidth.value, rect.height / viewBoxHeight.value);

        return {
            dx: deltaPx / fitScale,
            dy: deltaPy / fitScale,
        };
    }

    function resetView(): void {
        mapScale.value = IDLE_ZOOM;
        mapTranslateX.value = 0;
        mapTranslateY.value = 0;
    }

    function centerOnPoint(mapX: number, mapY: number, desiredScale = FOCUS_ZOOM): void {
        const nextScale = Math.max(mapScale.value, desiredScale);
        mapScale.value = nextScale;
        mapTranslateX.value = viewBoxWidth.value / 2 - mapX * nextScale;
        mapTranslateY.value = viewBoxHeight.value / 2 - mapY * nextScale;
    }

    function handleMouseDown(event: MouseEvent, onDragStart?: () => void): void {
        if (event.button !== 0) return;
        const target = event.target as Element | null;
        if (target?.closest(".map-marker, .connection-line")) return;

        drag.value = {
            x: event.clientX,
            y: event.clientY,
            tx: mapTranslateX.value,
            ty: mapTranslateY.value,
        };
        onDragStart?.();
    }

    function handleWindowMove(event: MouseEvent): void {
        if (!drag.value) return;

        const delta = pixelToViewBox(event.clientX - drag.value.x, event.clientY - drag.value.y);
        mapTranslateX.value = drag.value.tx + delta.dx;
        mapTranslateY.value = drag.value.ty + delta.dy;
    }

    function handleWindowUp(): void {
        drag.value = null;
    }

    function handleWheel(event: WheelEvent): void {
        event.preventDefault();
        const factor = event.deltaY > 0 ? 0.9 : 1.1;
        const nextScale = clamp(mapScale.value * factor, 0.35, 12);
        if (nextScale === mapScale.value) return;

        const centerX = viewBoxWidth.value / 2;
        const centerY = viewBoxHeight.value / 2;
        mapTranslateX.value =
            mapTranslateX.value +
            (1 - nextScale / mapScale.value) * (centerX - mapTranslateX.value);
        mapTranslateY.value =
            mapTranslateY.value +
            (1 - nextScale / mapScale.value) * (centerY - mapTranslateY.value);
        mapScale.value = nextScale;
    }

    onMounted(() => {
        window.addEventListener("mousemove", handleWindowMove);
        window.addEventListener("mouseup", handleWindowUp);
    });

    onBeforeUnmount(() => {
        window.removeEventListener("mousemove", handleWindowMove);
        window.removeEventListener("mouseup", handleWindowUp);
    });

    return {
        mapScale,
        mapTranslateX,
        mapTranslateY,
        drag,
        transform,
        isDragging,
        resetView,
        centerOnPoint,
        handleMouseDown,
        handleWindowMove,
        handleWindowUp,
        handleWheel,
    };
}
