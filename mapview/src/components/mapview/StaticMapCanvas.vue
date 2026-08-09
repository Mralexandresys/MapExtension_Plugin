<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watchEffect } from 'vue';

import {
  mapProjectionScale,
  type MapProjectionConstants,
} from '../../lib/mapProjection';
import {
  STATE_COLORS,
  groupColor,
  type StaticPlacement,
  type StaticPointSeries,
} from '../../lib/staticMapCatalog';

/**
 * Canvas layer for the pre-generated world catalog.
 *
 * The SVG layer stays responsible for live plugin entities and annotations;
 * hundreds of thousands of static points are drawn here instead, directly into
 * an ImageData buffer, and only for the visible viewport.
 */
const props = defineProps<{
  series: StaticPointSeries[];
  placements: StaticPlacement[];
  isSeriesVisible: (entry: StaticPointSeries) => boolean;
  isPlacementVisible: (entry: StaticPlacement) => boolean;
  projection: MapProjectionConstants;
  mapScale: number;
  mapTranslateX: number;
  mapTranslateY: number;
  viewportBounds: { left: number; top: number; right: number; bottom: number };
  viewBoxWidth: number;
  viewBoxHeight: number;
  stateVersion: number;
  highlight?: { x: number; y: number } | null;
}>();

const canvasRef = ref<HTMLCanvasElement | null>(null);
const cssWidth = ref(0);
const cssHeight = ref(0);
const pixelRatio = ref(1);

let context: CanvasRenderingContext2D | null = null;
let imageData: ImageData | null = null;
let pixels: Uint32Array | null = null;
let frameHandle = 0;
let resizeObserver: ResizeObserver | null = null;

const visibleSeries = computed(() => props.series.filter(props.isSeriesVisible));
const visiblePlacements = computed(() =>
  props.placements.filter(props.isPlacementVisible),
);

function packColor(color: string): number {
  const hex = color.trim();
  let red = 255;
  let green = 255;
  let blue = 255;

  if (hex.startsWith('#') && (hex.length === 7 || hex.length === 4)) {
    if (hex.length === 7) {
      red = parseInt(hex.slice(1, 3), 16);
      green = parseInt(hex.slice(3, 5), 16);
      blue = parseInt(hex.slice(5, 7), 16);
    } else {
      red = parseInt(hex[1] + hex[1], 16);
      green = parseInt(hex[2] + hex[2], 16);
      blue = parseInt(hex[3] + hex[3], 16);
    }
  } else if (hex.startsWith('hsl')) {
    // hsl() fallbacks come from the palette hash; approximate with mid grey.
    red = 170;
    green = 170;
    blue = 200;
  }

  return (255 << 24) | (blue << 16) | (green << 8) | red;
}

const colorCache = new Map<string, number>();

function packedColor(color: string): number {
  const cached = colorCache.get(color);
  if (cached !== undefined) return cached;
  const packed = packColor(color);
  colorCache.set(color, packed);
  return packed;
}

interface DeviceTransform {
  ax: number;
  bx: number;
  ay: number;
  by: number;
  width: number;
  height: number;
}

/**
 * World decimetres -> device pixels, folding the map projection, the pan/zoom
 * transform and the SVG `preserveAspectRatio` fit into a single affine pass.
 */
function deviceTransform(): DeviceTransform | null {
  const width = Math.round(cssWidth.value * pixelRatio.value);
  const height = Math.round(cssHeight.value * pixelRatio.value);
  if (width <= 0 || height <= 0) return null;
  if (props.viewBoxWidth <= 0 || props.viewBoxHeight <= 0) return null;

  const fitScale = Math.min(
    cssWidth.value / props.viewBoxWidth,
    cssHeight.value / props.viewBoxHeight,
  );
  if (!Number.isFinite(fitScale) || fitScale <= 0) return null;

  const scale = mapProjectionScale(props.projection);
  const factor = fitScale * pixelRatio.value;

  return {
    ax: 10 * scale.x * props.mapScale * factor,
    bx:
      (-props.projection.src_x1 * scale.x * props.mapScale +
        props.mapTranslateX -
        props.viewportBounds.left) *
      factor,
    ay: 10 * scale.y * props.mapScale * factor,
    by:
      (-props.projection.src_y1 * scale.y * props.mapScale +
        props.mapTranslateY -
        props.viewportBounds.top) *
      factor,
    width,
    height,
  };
}

function ensureBuffer(width: number, height: number): void {
  if (!context) return;
  if (imageData && imageData.width === width && imageData.height === height) return;
  imageData = context.createImageData(width, height);
  pixels = new Uint32Array(imageData.data.buffer);
}

function drawResources(transform: DeviceTransform): void {
  if (!context || !pixels || !imageData) return;

  pixels.fill(0);

  const { ax, bx, ay, by, width, height } = transform;
  const dotSize = Math.max(
    1,
    Math.min(8, Math.round((1 + props.mapScale * 0.9) * pixelRatio.value)),
  );
  const half = Math.floor(dotSize / 2);

  const depleted = packedColor(STATE_COLORS.depleted ?? '#6b7280');
  const available = packedColor(STATE_COLORS.available ?? '#4ade80');

  for (const entry of visibleSeries.value) {
    const base = packedColor(entry.color);
    const { x, y, state, count } = entry;

    for (let index = 0; index < count; index += 1) {
      const px = (x[index] * ax + bx) | 0;
      if (px < -dotSize || px >= width + dotSize) continue;
      const py = (y[index] * ay + by) | 0;
      if (py < -dotSize || py >= height + dotSize) continue;

      let color = base;
      const observed = state[index];
      if (observed === 2) color = depleted;
      else if (observed === 1) color = available;

      const startX = Math.max(0, px - half);
      const endX = Math.min(width - 1, px - half + dotSize - 1);
      const startY = Math.max(0, py - half);
      const endY = Math.min(height - 1, py - half + dotSize - 1);

      for (let row = startY; row <= endY; row += 1) {
        const offset = row * width;
        for (let column = startX; column <= endX; column += 1) {
          pixels[offset + column] = color;
        }
      }
    }
  }

  context.putImageData(imageData, 0, 0);
}

function drawPlacements(transform: DeviceTransform): void {
  if (!context) return;

  const { ax, bx, ay, by, width, height } = transform;
  const size = Math.max(3, Math.min(10, 3 + props.mapScale)) * pixelRatio.value;
  const half = size / 2;
  const byColor = new Map<string, StaticPlacement[]>();

  for (const placement of visiblePlacements.value) {
    const color = groupColor(placement.group);
    const bucket = byColor.get(color);
    if (bucket) bucket.push(placement);
    else byColor.set(color, [placement]);
  }

  for (const [color, bucket] of byColor) {
    context.beginPath();
    for (const placement of bucket) {
      const px = placement.x * ax + bx;
      if (px < -size || px > width + size) continue;
      const py = placement.y * ay + by;
      if (py < -size || py > height + size) continue;
      context.moveTo(px, py - half);
      context.lineTo(px + half, py);
      context.lineTo(px, py + half);
      context.lineTo(px - half, py);
      context.closePath();
    }
    context.fillStyle = color;
    context.globalAlpha = 0.92;
    context.fill();
  }
  context.globalAlpha = 1;

  // Volumes are drawn as their ground footprint.
  context.lineWidth = Math.max(1, pixelRatio.value);
  for (const placement of visiblePlacements.value) {
    if (!placement.boxes?.length) continue;
    const color = groupColor(placement.group);
    for (const box of placement.boxes) {
      const px = box.x * ax + bx;
      const py = box.y * ay + by;
      const extentX = box.extentX * ax;
      const extentY = box.extentY * ay;
      if (
        px + Math.abs(extentX) < 0 ||
        px - Math.abs(extentX) > width ||
        py + Math.abs(extentY) < 0 ||
        py - Math.abs(extentY) > height
      ) {
        continue;
      }

      context.save();
      context.translate(px, py);
      context.rotate((box.yaw * Math.PI) / 180);
      context.beginPath();
      context.rect(-extentX, -extentY, extentX * 2, extentY * 2);
      context.globalAlpha = 0.12;
      context.fillStyle = color;
      context.fill();
      context.globalAlpha = 0.7;
      context.strokeStyle = color;
      context.stroke();
      context.restore();
    }
  }
  context.globalAlpha = 1;
}

function drawHighlight(transform: DeviceTransform): void {
  if (!context || !props.highlight) return;
  const { ax, bx, ay, by } = transform;
  const px = props.highlight.x * ax + bx;
  const py = props.highlight.y * ay + by;
  const radius = 9 * pixelRatio.value;

  context.beginPath();
  context.arc(px, py, radius, 0, Math.PI * 2);
  context.lineWidth = 2 * pixelRatio.value;
  context.strokeStyle = '#22d3ee';
  context.stroke();
}

function draw(): void {
  const canvas = canvasRef.value;
  if (!canvas) return;
  if (!context) context = canvas.getContext('2d');
  if (!context) return;

  const transform = deviceTransform();
  if (!transform) return;

  if (canvas.width !== transform.width || canvas.height !== transform.height) {
    canvas.width = transform.width;
    canvas.height = transform.height;
    imageData = null;
    pixels = null;
  }
  ensureBuffer(transform.width, transform.height);

  drawResources(transform);
  drawPlacements(transform);
  drawHighlight(transform);
}

function schedule(): void {
  if (frameHandle) return;
  frameHandle = requestAnimationFrame(() => {
    frameHandle = 0;
    draw();
  });
}

function measure(): void {
  const canvas = canvasRef.value;
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  cssWidth.value = rect.width;
  cssHeight.value = rect.height;
  pixelRatio.value = Math.min(window.devicePixelRatio || 1, 2);
  schedule();
}

watchEffect(() => {
  // Touch every input so the canvas repaints on data, filter or view changes.
  void visibleSeries.value;
  void visiblePlacements.value;
  void props.stateVersion;
  void props.mapScale;
  void props.mapTranslateX;
  void props.mapTranslateY;
  void props.viewportBounds;
  void props.viewBoxWidth;
  void props.viewBoxHeight;
  void props.projection;
  void props.highlight;
  void cssWidth.value;
  void cssHeight.value;
  schedule();
});

onMounted(() => {
  measure();
  if (typeof ResizeObserver !== 'undefined' && canvasRef.value) {
    resizeObserver = new ResizeObserver(measure);
    resizeObserver.observe(canvasRef.value);
  }
  window.addEventListener('resize', measure);
});

onBeforeUnmount(() => {
  if (frameHandle) cancelAnimationFrame(frameHandle);
  resizeObserver?.disconnect();
  window.removeEventListener('resize', measure);
});

defineExpose({ redraw: schedule });
</script>

<template>
  <canvas ref="canvasRef" class="static-map-canvas" aria-hidden="true"></canvas>
</template>

<style scoped>
.static-map-canvas {
  position: absolute;
  inset: 0;
  z-index: 1;
  width: 100%;
  height: 100%;
  pointer-events: none;
  /* Pre-generated catalog: tens of thousands of points that otherwise carry the
     same visual weight as the handful of live plugin entities drawn above. Held
     back so the live layer reads first; the points stay perfectly legible and
     hoverable. */
  opacity: 0.72;
}
</style>
