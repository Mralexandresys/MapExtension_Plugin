<script setup lang="ts">
import { computed, ref } from 'vue';

import baseMapUrl from '../assets/base-map.webp';
import teleporterSvg from '../assets/teleporter.svg?raw';
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
import { useMapTooltip } from '../composables/useMapTooltip';
import { useMapPanZoom } from '../composables/useMapPanZoom';

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
  "select": [key: string];
  "clear-selection": [];
  "hover": [key: string | null];
}>();

const DEFAULT_MAP = {
  content_width: 3547,
  content_height: 3471,
  dst_x1: 380,
  dst_y1: 567,
  image_width: 4352,
  image_height: 5120,
};

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

const ui = computed(() => getMessages(props.lang));
const projection = computed(() => props.cargo?.map ?? DEFAULT_MAP);
const viewBoxWidth = computed(() => Number(projection.value.content_width) || 3547);
const viewBoxHeight = computed(() => Number(projection.value.content_height) || 3471);
const imageWidth = computed(() => Number(projection.value.image_width) || 4352);
const imageHeight = computed(() => Number(projection.value.image_height) || 5120);
const imageX = computed(() => -(Number(projection.value.dst_x1) || 380));
const imageY = computed(() => -(Number(projection.value.dst_y1) || 567));
const orphanKeySet = computed(() => new Set(props.orphanKeys));
const focusKeySet = computed(() => new Set(props.focusKeys));

const {
  tooltip,
  hideTooltip,
  showTooltip,
  showTooltipFromElement,
  moveTooltip,
} = useMapTooltip(mapShell);

const {
  transform,
  isDragging,
  resetView,
  centerOnPoint,
  handleMouseDown: panZoomHandleMouseDown,
  handleWheel,
} = useMapPanZoom(mapShell, viewBoxWidth, viewBoxHeight);

function focusSelection(): void {
  if (!props.selectedEntity) return;
  centerOnPoint(props.selectedEntity.raw.map.x, props.selectedEntity.raw.map.y);
}

function handleMouseDown(event: MouseEvent): void {
  panZoomHandleMouseDown(event, hideTooltip);
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

function cargoTooltipLines(marker: CargoMarker): string[] {
  return [
    `${markerTypeLabel(marker)} | ${marker.resource || ui.value.map.noResource}`,
    ui.value.format.relatedConnections(relatedConnectionCount(marker.unique_key)),
    ui.value.map.clickToSelect,
  ];
}

function teleporterLabel(teleporter: Teleporter): string {
  return teleporter.label || ui.value.selection.teleporterFallback;
}

function playerLabel(player: Player): string {
  return player.label || ui.value.selection.playerFallback;
}

function cargoAriaLabel(marker: CargoMarker): string {
  return `${cargoLabel(marker)}. ${markerTypeLabel(marker)}. ${ui.value.format.relatedConnections(relatedConnectionCount(marker.unique_key))}.`;
}

function teleporterAriaLabel(teleporter: Teleporter): string {
  return `${teleporterLabel(teleporter)}. ${teleporter.source || ui.value.map.unknownSource}.`;
}

function playerAriaLabel(player: Player): string {
  return `${playerLabel(player)}. ${player.source || ui.value.map.unknownSource}.`;
}

function handleMarkerKeydown(event: KeyboardEvent, key: string): void {
  if (event.key === 'Enter' || event.key === ' ') {
    event.preventDefault();
    emit('select', key);
    return;
  }

  if (event.key === 'Escape') {
    event.preventDefault();
    emit('clear-selection');
  }
}

function handleCargoFocus(marker: CargoMarker, event: FocusEvent): void {
  const target = event.target as Element | null;
  if (!target) return;

  emit('hover', marker.unique_key);
  showTooltipFromElement(cargoLabel(marker), cargoTooltipLines(marker), target);
}

function handleTeleporterFocus(teleporter: Teleporter, event: FocusEvent): void {
  const target = event.target as Element | null;
  if (!target) return;

  showTooltipFromElement(
    teleporterLabel(teleporter),
    [teleporter.source || ui.value.map.unknownSource, ui.value.map.clickToSelect],
    target,
  );
}

function handlePlayerFocus(player: Player, event: FocusEvent): void {
  const target = event.target as Element | null;
  if (!target) return;

  showTooltipFromElement(
    playerLabel(player),
    [player.source || ui.value.map.unknownSource, ui.value.map.clickToSelect],
    target,
  );
}

function handleCargoBlur(): void {
  emit('hover', null);
  hideTooltip();
}

function showCargoTooltip(marker: CargoMarker, event: MouseEvent): void {
  emit('hover', marker.unique_key);
  showTooltip(cargoLabel(marker), cargoTooltipLines(marker), event);
}

function showTeleporterTooltip(teleporter: Teleporter, event: MouseEvent): void {
  showTooltip(
    teleporterLabel(teleporter),
    [teleporter.source || ui.value.map.unknownSource, ui.value.map.clickToSelect],
    event,
  );
}

function showPlayerTooltip(player: Player, event: MouseEvent): void {
  showTooltip(
    playerLabel(player),
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
    :aria-busy="loading"
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
            tabindex="0"
            role="button"
            :aria-pressed="selectedKey === marker.unique_key"
            :aria-label="cargoAriaLabel(marker)"
            @click.stop="emit('select', marker.unique_key)"
            @dblclick.stop
            @keydown="handleMarkerKeydown($event, marker.unique_key)"
            @focus.stop="handleCargoFocus(marker, $event)"
            @mouseenter.stop="showCargoTooltip(marker, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="handleCargoBlur"
            @mouseleave.stop="handleCargoBlur"
          >
            <polygon
              v-if="marker.kind === 'sender'"
              :points="`${marker.map.x},${marker.map.y-7} ${marker.map.x+7},${marker.map.y} ${marker.map.x},${marker.map.y+7} ${marker.map.x-7},${marker.map.y}`"
            />
            <circle v-else :cx="marker.map.x" :cy="marker.map.y" r="6" stroke-width="1.5" />
          </g>

          <g
            v-for="teleporter in teleporters"
            :key="teleporter.unique_key"
            class="map-marker teleporter"
            :class="{
              active: selectedKey === teleporter.unique_key,
              dimmed: isDimmed(teleporter.unique_key),
            }"
            tabindex="0"
            role="button"
            :aria-pressed="selectedKey === teleporter.unique_key"
            :aria-label="teleporterAriaLabel(teleporter)"
            @click.stop="emit('select', teleporter.unique_key)"
            @dblclick.stop
            @keydown="handleMarkerKeydown($event, teleporter.unique_key)"
            @focus.stop="handleTeleporterFocus(teleporter, $event)"
            @mouseenter.stop="showTeleporterTooltip(teleporter, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="hideTooltip"
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
            tabindex="0"
            role="button"
            :aria-pressed="selectedKey === player.unique_key"
            :aria-label="playerAriaLabel(player)"
            @click.stop="emit('select', player.unique_key)"
            @dblclick.stop
            @keydown="handleMarkerKeydown($event, player.unique_key)"
            @focus.stop="handlePlayerFocus(player, $event)"
            @mouseenter.stop="showPlayerTooltip(player, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="hideTooltip"
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

    <div v-if="loading" class="map-loading-bar" role="progressbar" aria-label="Chargement…" />

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

<style scoped>
.map-canvas {
    position: relative;
    overflow: hidden;
    background: #0a1018;
    cursor: grab;
}

.map-canvas.dragging {
    cursor: grabbing;
}

.map-svg {
    display: block;
    width: 100%;
    height: 100%;
    user-select: none;
}

:deep(.base-map) {
    opacity: 0.92;
    pointer-events: none;
}

:deep(.connection-line) {
    stroke: var(--line);
    stroke-width: 1px;
    stroke-opacity: 0.35;
    stroke-dasharray: 6 3;
}

:deep(.connection-line.active) {
    stroke-opacity: 0.85;
    stroke-width: 1.5px;
    stroke-dasharray: none;
    filter: drop-shadow(0 0 3px var(--line));
}

:deep(.connection-line.muted) { stroke-opacity: 0.08; }

:deep(.map-marker) {
    cursor: pointer;
    transition: opacity 0.15s ease;
}

:deep(.map-marker:focus) {
    outline: none;
}

:deep(.map-marker:focus-visible rect),
:deep(.map-marker:focus-visible polygon),
:deep(.map-marker:focus-visible circle),
:deep(.map-marker:focus-visible path),
:deep(.map-marker:focus-visible use) {
    stroke-width: 2.8;
    filter: drop-shadow(0 0 10px rgba(255, 255, 255, 0.58));
}

:deep(.map-marker.sender rect),
:deep(.map-marker.sender polygon) {
    fill: var(--sender);
    stroke: #d8e8ff;
    stroke-width: 1.2;
}

:deep(.map-marker.receiver circle) {
    fill: var(--receiver);
    stroke: #fff2c7;
    stroke-width: 1.2;
}

:deep(.map-marker.teleporter) {
    color: var(--teleporter);
}

:deep(.map-marker.teleporter .teleporter-hitbox) {
    fill: currentColor;
    fill-opacity: 0;
    pointer-events: all;
}

:deep(.map-marker.teleporter use) {
    stroke: currentColor;
    fill: none;
}

:deep(.map-marker.player path) {
    fill: var(--player);
    stroke: #d8fff0;
    stroke-width: 1.2;
}

:deep(.map-marker.orphan rect),
:deep(.map-marker.orphan polygon),
:deep(.map-marker.orphan circle) {
    fill: var(--bad);
    stroke: #ffe1e1;
}

:deep(.map-marker.dimmed) {
    opacity: 0.18;
    filter: none;
}

:deep(.map-marker.sender.active polygon)  { filter: drop-shadow(0 0 6px var(--sender)); }
:deep(.map-marker.receiver.active circle) { filter: drop-shadow(0 0 6px var(--receiver)); }
:deep(.map-marker.player.active path)     { filter: drop-shadow(0 0 6px var(--player)); }
:deep(.map-marker.orphan rect),
:deep(.map-marker.orphan polygon),
:deep(.map-marker.orphan circle)          { stroke: var(--warn); stroke-width: 1.5; stroke-dasharray: 3 2; }

:deep(.map-marker.active rect),
:deep(.map-marker.active circle),
:deep(.map-marker.active polygon),
:deep(.map-marker.active path),
:deep(.map-marker.active use) {
    stroke-width: 2.6;
    filter: drop-shadow(0 0 8px rgba(255, 255, 255, 0.44));
}

.map-tooltip,
.map-empty-state {
    position: absolute;
    z-index: 6;
    padding: 12px 14px;
    border-radius: 16px;
    border: 1px solid var(--border-strong);
    background: rgba(7, 12, 24, 0.95);
    box-shadow: var(--shadow);
}

.map-loading-bar {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    height: 2px;
    overflow: hidden;
    z-index: 10;
}

.map-loading-bar::after {
    content: "";
    position: absolute;
    inset: 0;
    background: linear-gradient(90deg, transparent, var(--accent), transparent);
    animation: scan-pass 1.2s linear infinite;
}

@keyframes scan-pass {
    0%   { transform: translateX(-100%); }
    100% { transform: translateX(400%); }
}

.map-tooltip {
    display: grid;
    gap: 4px;
    pointer-events: none;
    min-width: 240px;
    max-width: 320px;
}

.map-tooltip span {
    color: #d7e4ff;
    font-size: 0.92rem;
}

.map-empty-state {
    inset: auto 20px 20px 20px;
    display: grid;
    gap: 6px;
    max-width: 420px;
}

.map-empty-state strong {
    font-size: 0.96rem;
}
</style>
