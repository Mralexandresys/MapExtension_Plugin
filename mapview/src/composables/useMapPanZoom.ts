import { computed, onBeforeUnmount, onMounted, ref, watch, type Ref } from "vue";

import { clamp } from "../lib/formatters";

interface DragState {
    startX: number;
    startY: number;
    lastX: number;
    lastY: number;
    tx: number;
    ty: number;
    distancePx: number;
    moved: boolean;
}

interface MapImageRefs {
    x: Ref<number>;
    y: Ref<number>;
    width: Ref<number>;
    height: Ref<number>;
}

interface ViewportBounds {
    left: number;
    right: number;
    top: number;
    bottom: number;
}

const IDLE_ZOOM = 1;
const FOCUS_ZOOM = 1.55;
const DRAG_DISTANCE_PX = 5;

export function useMapPanZoom(
    mapShell: Ref<HTMLElement | null>,
    viewBoxWidth: Ref<number>,
    viewBoxHeight: Ref<number>,
    image: MapImageRefs,
) {
    const mapScale = ref(IDLE_ZOOM);
    const mapTranslateX = ref(0);
    const mapTranslateY = ref(0);
    const drag = ref<DragState | null>(null);
    const completedDragMoved = ref(false);
    const shellWidth = ref(0);
    const shellHeight = ref(0);
    let resizeObserver: ResizeObserver | null = null;

    function finiteNumber(value: number, fallback = 0): number {
        return Number.isFinite(value) ? value : fallback;
    }

    function calculateViewportBounds(widthPx: number, heightPx: number): ViewportBounds {
        const width = finiteNumber(viewBoxWidth.value);
        const height = finiteNumber(viewBoxHeight.value);
        if (width <= 0 || height <= 0 || widthPx <= 0 || heightPx <= 0) {
            return { left: 0, right: width, top: 0, bottom: height };
        }

        const fitScale = Math.min(widthPx / width, heightPx / height);
        const visibleWidth = widthPx / fitScale;
        const visibleHeight = heightPx / fitScale;

        return {
            left: (width - visibleWidth) / 2,
            right: (width + visibleWidth) / 2,
            top: (height - visibleHeight) / 2,
            bottom: (height + visibleHeight) / 2,
        };
    }

    const viewportBounds = computed(() =>
        calculateViewportBounds(shellWidth.value, shellHeight.value),
    );
    const transform = computed(
        () => `translate(${mapTranslateX.value},${mapTranslateY.value}) scale(${mapScale.value})`,
    );
    const isDragging = computed(() => drag.value !== null);

    function measureViewport(): void {
        const rect = mapShell.value?.getBoundingClientRect();
        shellWidth.value = rect?.width ?? 0;
        shellHeight.value = rect?.height ?? 0;
    }

    function pixelToViewBox(deltaPx: number, deltaPy: number): { dx: number; dy: number } {
        if (!mapShell.value) return { dx: 0, dy: 0 };

        const rect = mapShell.value.getBoundingClientRect();
        const width = finiteNumber(viewBoxWidth.value);
        const height = finiteNumber(viewBoxHeight.value);
        if (!rect.width || !rect.height || width <= 0 || height <= 0) {
            return { dx: 0, dy: 0 };
        }

        const fitScale = Math.min(rect.width / width, rect.height / height);

        return {
            dx: deltaPx / fitScale,
            dy: deltaPy / fitScale,
        };
    }

    function screenToMapPoint(clientX: number, clientY: number): { x: number; y: number } | null {
        if (!mapShell.value) return null;

        const rect = mapShell.value.getBoundingClientRect();
        const width = finiteNumber(viewBoxWidth.value);
        const height = finiteNumber(viewBoxHeight.value);
        if (!rect.width || !rect.height || width <= 0 || height <= 0 || mapScale.value <= 0) {
            return null;
        }

        const bounds = calculateViewportBounds(rect.width, rect.height);
        const fitScale = Math.min(rect.width / width, rect.height / height);
        const viewBoxX = bounds.left + (clientX - rect.left) / fitScale;
        const viewBoxY = bounds.top + (clientY - rect.top) / fitScale;

        return {
            x: (viewBoxX - mapTranslateX.value) / mapScale.value,
            y: (viewBoxY - mapTranslateY.value) / mapScale.value,
        };
    }

    function constrainAxis(
        translation: number,
        scale: number,
        viewportCenter: number,
        imageStart: number,
        imageSize: number,
    ): number {
        const first = finiteNumber(imageStart);
        const second = first + finiteNumber(imageSize);
        const imageMin = Math.min(first, second);
        const imageMax = Math.max(first, second);
        if (imageMin === imageMax) return translation;

        // Keeping the viewport center inside the image prevents losing the map
        // while still allowing every image point, including each edge, to be centered.
        return clamp(
            translation,
            viewportCenter - imageMax * scale,
            viewportCenter - imageMin * scale,
        );
    }

    function setTransform(scale: number, translateX: number, translateY: number): void {
        const bounds = viewportBounds.value;
        const centerX = (bounds.left + bounds.right) / 2;
        const centerY = (bounds.top + bounds.bottom) / 2;
        const nextScale = finiteNumber(scale, IDLE_ZOOM);

        mapScale.value = nextScale;
        mapTranslateX.value = constrainAxis(
            finiteNumber(translateX),
            nextScale,
            centerX,
            image.x.value,
            image.width.value,
        );
        mapTranslateY.value = constrainAxis(
            finiteNumber(translateY),
            nextScale,
            centerY,
            image.y.value,
            image.height.value,
        );
    }

    function resetView(): void {
        setTransform(IDLE_ZOOM, 0, 0);
    }

    function centerOnPoint(mapX: number, mapY: number, desiredScale = FOCUS_ZOOM): void {
        const nextScale = Math.max(mapScale.value, desiredScale);
        const bounds = viewportBounds.value;
        const centerX = (bounds.left + bounds.right) / 2;
        const centerY = (bounds.top + bounds.bottom) / 2;
        setTransform(
            nextScale,
            centerX - mapX * nextScale,
            centerY - mapY * nextScale,
        );
    }

    function handleMouseDown(event: MouseEvent, onDragStart?: () => void): void {
        if (event.button !== 0) return;
        const target = event.target as Element | null;
        if (target?.closest(".map-marker, .connection-line")) return;

        completedDragMoved.value = false;
        drag.value = {
            startX: event.clientX,
            startY: event.clientY,
            lastX: event.clientX,
            lastY: event.clientY,
            tx: mapTranslateX.value,
            ty: mapTranslateY.value,
            distancePx: 0,
            moved: false,
        };
        onDragStart?.();
    }

    function handleWindowMove(event: MouseEvent): void {
        const currentDrag = drag.value;
        if (!currentDrag) return;

        currentDrag.distancePx += Math.hypot(
            event.clientX - currentDrag.lastX,
            event.clientY - currentDrag.lastY,
        );
        currentDrag.lastX = event.clientX;
        currentDrag.lastY = event.clientY;
        if (!currentDrag.moved && currentDrag.distancePx >= DRAG_DISTANCE_PX) {
            currentDrag.moved = true;
            completedDragMoved.value = true;
        }

        const delta = pixelToViewBox(
            event.clientX - currentDrag.startX,
            event.clientY - currentDrag.startY,
        );
        setTransform(
            mapScale.value,
            currentDrag.tx + delta.dx,
            currentDrag.ty + delta.dy,
        );
    }

    function handleWindowUp(): void {
        if (drag.value?.moved) {
            completedDragMoved.value = true;
        }
        drag.value = null;
    }

    function consumeDragMovement(): boolean {
        const moved = completedDragMoved.value;
        completedDragMoved.value = false;
        return moved;
    }

    function handleWheel(event: WheelEvent): void {
        event.preventDefault();
        const factor = event.deltaY > 0 ? 0.9 : 1.1;
        const nextScale = clamp(mapScale.value * factor, 0.35, 12);
        if (nextScale === mapScale.value) return;

        const bounds = viewportBounds.value;
        const centerX = (bounds.left + bounds.right) / 2;
        const centerY = (bounds.top + bounds.bottom) / 2;
        const translateX =
            mapTranslateX.value +
            (1 - nextScale / mapScale.value) * (centerX - mapTranslateX.value);
        const translateY =
            mapTranslateY.value +
            (1 - nextScale / mapScale.value) * (centerY - mapTranslateY.value);
        setTransform(nextScale, translateX, translateY);
    }

    watch(
        [viewBoxWidth, viewBoxHeight, image.x, image.y, image.width, image.height],
        () => setTransform(mapScale.value, mapTranslateX.value, mapTranslateY.value),
    );

    onMounted(() => {
        measureViewport();
        if (typeof ResizeObserver !== "undefined" && mapShell.value) {
            resizeObserver = new ResizeObserver(measureViewport);
            resizeObserver.observe(mapShell.value);
        }
        window.addEventListener("resize", measureViewport);
        window.addEventListener("mousemove", handleWindowMove);
        window.addEventListener("mouseup", handleWindowUp);
    });

    onBeforeUnmount(() => {
        resizeObserver?.disconnect();
        window.removeEventListener("resize", measureViewport);
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
        viewportBounds,
        resetView,
        centerOnPoint,
        screenToMapPoint,
        consumeDragMovement,
        handleMouseDown,
        handleWindowMove,
        handleWindowUp,
        handleWheel,
    };
}
