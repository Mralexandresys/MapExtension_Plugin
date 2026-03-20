<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';

import baseMapUrl from '../assets/base-map.webp';
import teleporterSvg from '../assets/teleporter.svg?raw';
import { clamp } from '../lib/formatters';
import { getMessages } from '../lang';
import type { Language } from '../lang';
import type {
  CargoConnection,
  CargoMarker,
  CargoResponse,
  Player,
  SelectedEntity,
  Teleporter,
} from '../lib/types';

interface TooltipState {
  visible: boolean;
  title: string;
  lines: string[];
  left: number;
  top: number;
}

interface DragState {
  x: number;
  y: number;
  tx: number;
  ty: number;
}

const props = defineProps<{
  loading: boolean;
  cargo: CargoResponse | null;
  cargoMarkers: CargoMarker[];
  cargoConnections: CargoConnection[];
  teleporters: Teleporter[];
  players: Player[];
  selectedKey: string | null;
  selectedEntity: SelectedEntity | null;
  orphanKeys: string[];
  focusKeys: string[];
  focusCargoKey: string | null;
  lang: Language;
}>();

const emit = defineEmits<{
  (event: 'select', key: string): void;
  (event: 'clear-selection'): void;
  (event: 'hover', key: string | null): void;
}>();

const DEFAULT_MAP = {
  content_width: 3547,
  content_height: 3471,
  dst_x1: 380,
  dst_y1: 567,
  image_width: 4352,
  image_height: 5120,
};

const IDLE_ZOOM = 1;
const FOCUS_ZOOM = 1.55;

const TELEPORTER_SYMBOL_ID = 'teleporter-marker-icon';
const TELEPORTER_ICON_SIZE = 20;
const TELEPORTER_ICON_HALF = TELEPORTER_ICON_SIZE / 2;
const TELEPORTER_HITBOX_SIZE = 28;
const TELEPORTER_HITBOX_HALF = TELEPORTER_HITBOX_SIZE / 2;
const teleporterSymbolHref = `#${TELEPORTER_SYMBOL_ID}`;
const teleporterSymbolMarkup = teleporterSvg
  .replace(/<\?xml[^>]*>\s*/i, '')
  .replace('<svg', `<symbol id="${TELEPORTER_SYMBOL_ID}"`)
  .replace('</svg>', '</symbol>')
  .replace(/\swidth="[^"]*"/i, '')
  .replace(/\sheight="[^"]*"/i, '');

const mapShell = ref<HTMLElement | null>(null);
const mapScale = ref(IDLE_ZOOM);
const mapTranslateX = ref(0);
const mapTranslateY = ref(0);
const drag = ref<DragState | null>(null);
const tooltip = ref<TooltipState>({
  visible: false,
  title: '',
  lines: [],
  left: 0,
  top: 0,
});

const ui = computed(() => getMessages(props.lang));
const projection = computed(() => props.cargo?.map ?? DEFAULT_MAP);
const viewBoxWidth = computed(() => Number(projection.value.content_width) || 3547);
const viewBoxHeight = computed(() => Number(projection.value.content_height) || 3471);
const imageWidth = computed(() => Number(projection.value.image_width) || 4352);
const imageHeight = computed(() => Number(projection.value.image_height) || 5120);
const imageX = computed(() => -(Number(projection.value.dst_x1) || 380));
const imageY = computed(() => -(Number(projection.value.dst_y1) || 567));
const transform = computed(() => `translate(${mapTranslateX.value},${mapTranslateY.value}) scale(${mapScale.value})`);
const orphanKeySet = computed(() => new Set(props.orphanKeys));
const focusKeySet = computed(() => new Set(props.focusKeys));
const isDragging = computed(() => drag.value !== null);

function hideTooltip(): void {
  tooltip.value.visible = false;
}

function updateTooltipPosition(clientX: number, clientY: number): void {
  if (!mapShell.value) return;

  const rect = mapShell.value.getBoundingClientRect();
  const estimatedWidth = 292;
  const estimatedHeight = 72 + tooltip.value.lines.length * 18;
  let left = clientX - rect.left + 16;
  let top = clientY - rect.top + 16;

  if (left + estimatedWidth > rect.width - 12) {
    left = clientX - rect.left - estimatedWidth - 16;
  }
  if (top + estimatedHeight > rect.height - 12) {
    top = clientY - rect.top - estimatedHeight - 16;
  }

  tooltip.value.left = clamp(left, 12, Math.max(12, rect.width - estimatedWidth));
  tooltip.value.top = clamp(top, 12, Math.max(12, rect.height - estimatedHeight));
}

function showTooltip(title: string, lines: string[], event: MouseEvent): void {
  tooltip.value.visible = true;
  tooltip.value.title = title;
  tooltip.value.lines = lines;
  updateTooltipPosition(event.clientX, event.clientY);
}

function moveTooltip(event: MouseEvent): void {
  if (!tooltip.value.visible) return;
  updateTooltipPosition(event.clientX, event.clientY);
}

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

function focusSelection(): void {
  if (!props.selectedEntity) return;
  centerOnPoint(props.selectedEntity.raw.map.x, props.selectedEntity.raw.map.y);
}

function handleMouseDown(event: MouseEvent): void {
  if (event.button !== 0) return;
  const target = event.target as Element | null;
  if (target?.closest('.map-marker, .connection-line')) return;

  drag.value = {
    x: event.clientX,
    y: event.clientY,
    tx: mapTranslateX.value,
    ty: mapTranslateY.value,
  };
  hideTooltip();
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

function isDimmed(entityKey: string): boolean {
  return focusKeySet.value.size > 0 && !focusKeySet.value.has(entityKey);
}

function isConnectionActive(connection: CargoConnection): boolean {
  if (!props.focusCargoKey) return false;
  return connection.sender_key === props.focusCargoKey || connection.receiver_key === props.focusCargoKey;
}

function isConnectionMuted(connection: CargoConnection): boolean {
  return !!props.focusCargoKey && !isConnectionActive(connection);
}

function cargoLabel(marker: CargoMarker): string {
  return marker.label || marker.display_name || ui.value.selection.cargoFallback;
}

function relatedConnectionCount(markerKey: string): number {
  return props.cargoConnections.filter(
    (connection) => connection.sender_key === markerKey || connection.receiver_key === markerKey,
  ).length;
}

function markerTypeLabel(marker: CargoMarker): string {
  return marker.kind === 'sender' ? ui.value.map.senderLabel : ui.value.map.receiverLabel;
}

function showCargoTooltip(marker: CargoMarker, event: MouseEvent): void {
  emit('hover', marker.unique_key);
  showTooltip(
    cargoLabel(marker),
    [
      `${markerTypeLabel(marker)} | ${marker.resource || ui.value.map.noResource}`,
      ui.value.format.relatedConnections(relatedConnectionCount(marker.unique_key)),
      ui.value.map.clickToSelect,
    ],
    event,
  );
}

function showTeleporterTooltip(teleporter: Teleporter, event: MouseEvent): void {
  showTooltip(
    teleporter.label || ui.value.selection.teleporterFallback,
    [teleporter.source || ui.value.map.unknownSource, ui.value.map.clickToSelect],
    event,
  );
}

function showPlayerTooltip(player: Player, event: MouseEvent): void {
  showTooltip(
    player.label || ui.value.selection.playerFallback,
    [player.source || ui.value.map.unknownSource, ui.value.map.clickToSelect],
    event,
  );
}

function showConnectionTooltip(connection: CargoConnection, event: MouseEvent): void {
  showTooltip(
    ui.value.map.cargoConnection,
    [
      `${ui.value.map.senderLabel}: ${connection.sender_label || connection.sender_key}`,
      `${ui.value.map.receiverLabel}: ${connection.receiver_label || connection.receiver_key}`,
      `${ui.value.map.itemLabel}: ${connection.item || ui.value.map.unknownItem}`,
      `${ui.value.map.requestedLabel}: ${connection.requested_amount ?? '--'}`,
    ],
    event,
  );
}

onMounted(() => {
  window.addEventListener('mousemove', handleWindowMove);
  window.addEventListener('mouseup', handleWindowUp);
});

onBeforeUnmount(() => {
  window.removeEventListener('mousemove', handleWindowMove);
  window.removeEventListener('mouseup', handleWindowUp);
});

defineExpose({
  focusSelection,
  resetView,
});
</script>

<template>
  <div
    ref="mapShell"
    class="map-canvas"
    :class="{ dragging: isDragging }"
    @mousedown="handleMouseDown"
    @wheel="handleWheel"
  >
    <svg
      class="map-svg"
      :viewBox="`0 0 ${viewBoxWidth} ${viewBoxHeight}`"
      preserveAspectRatio="xMidYMid meet"
      @dblclick="emit('clear-selection')"
    >
      <defs v-html="teleporterSymbolMarkup"></defs>
      <g :transform="transform">
        <image
          class="base-map"
          :href="baseMapUrl"
          :x="imageX"
          :y="imageY"
          :width="imageWidth"
          :height="imageHeight"
        />

        <g>
          <line
            v-for="(connection, index) in cargoConnections"
            :key="`${connection.sender_key}-${connection.receiver_key}-${connection.item || 'item'}-${index}`"
            class="connection-line"
            :class="{
              active: isConnectionActive(connection),
              muted: isConnectionMuted(connection),
            }"
            :x1="connection.sender.map.x"
            :y1="connection.sender.map.y"
            :x2="connection.receiver.map.x"
            :y2="connection.receiver.map.y"
            @mouseenter.stop="showConnectionTooltip(connection, $event)"
            @mousemove.stop="moveTooltip($event)"
            @mouseleave.stop="hideTooltip"
          />
        </g>

        <g>
          <g
            v-for="marker in cargoMarkers"
            :key="marker.unique_key"
            class="map-marker"
            :class="[
              marker.kind,
              {
                orphan: orphanKeySet.has(marker.unique_key),
                active: selectedKey === marker.unique_key,
                dimmed: isDimmed(marker.unique_key),
              },
            ]"
            @click.stop="emit('select', marker.unique_key)"
            @dblclick.stop
            @mouseenter.stop="showCargoTooltip(marker, $event)"
            @mousemove.stop="moveTooltip($event)"
            @mouseleave.stop="emit('hover', null); hideTooltip()"
          >
            <rect
              v-if="marker.kind === 'sender'"
              :x="marker.map.x - 5"
              :y="marker.map.y - 5"
              width="10"
              height="10"
              rx="2"
            />
            <circle v-else :cx="marker.map.x" :cy="marker.map.y" r="5" />
          </g>

          <g
            v-for="teleporter in teleporters"
            :key="teleporter.unique_key"
            class="map-marker teleporter"
            :class="{
              active: selectedKey === teleporter.unique_key,
              dimmed: isDimmed(teleporter.unique_key),
            }"
            @click.stop="emit('select', teleporter.unique_key)"
            @dblclick.stop
            @mouseenter.stop="showTeleporterTooltip(teleporter, $event)"
            @mousemove.stop="moveTooltip($event)"
            @mouseleave.stop="hideTooltip"
          >
            <rect
              class="teleporter-hitbox"
              :x="teleporter.map.x - TELEPORTER_HITBOX_HALF"
              :y="teleporter.map.y - TELEPORTER_HITBOX_HALF"
              :width="TELEPORTER_HITBOX_SIZE"
              :height="TELEPORTER_HITBOX_SIZE"
            />
            <use
              :href="teleporterSymbolHref"
              :xlink:href="teleporterSymbolHref"
              :x="teleporter.map.x - TELEPORTER_ICON_HALF"
              :y="teleporter.map.y - TELEPORTER_ICON_HALF"
              :width="TELEPORTER_ICON_SIZE"
              :height="TELEPORTER_ICON_SIZE"
            />
          </g>

          <g
            v-for="player in players"
            :key="player.unique_key"
            class="map-marker player"
            :class="{
              active: selectedKey === player.unique_key,
              dimmed: isDimmed(player.unique_key),
            }"
            @click.stop="emit('select', player.unique_key)"
            @dblclick.stop
            @mouseenter.stop="showPlayerTooltip(player, $event)"
            @mousemove.stop="moveTooltip($event)"
            @mouseleave.stop="hideTooltip"
          >
            <path
              :d="`M ${player.map.x} ${player.map.y - 8} L ${player.map.x + 7} ${player.map.y + 6} L ${player.map.x - 7} ${player.map.y + 6} Z`"
            />
          </g>
        </g>
      </g>
    </svg>

    <div v-if="!cargo" class="map-empty-state">
      <strong>{{ ui.map.emptyTitle }}</strong>
      <span>{{ ui.map.emptyBody }}</span>
    </div>

    <div v-if="loading" class="map-loading-indicator">
      <span class="map-loading-dot"></span>
      <span>{{ ui.status.sync }}</span>
    </div>

    <div
      v-if="tooltip.visible"
      class="map-tooltip"
      :style="{
        left: `${tooltip.left}px`,
        top: `${tooltip.top}px`,
      }"
    >
      <strong>{{ tooltip.title }}</strong>
      <span v-for="line in tooltip.lines" :key="line">{{ line }}</span>
    </div>
  </div>
</template>
