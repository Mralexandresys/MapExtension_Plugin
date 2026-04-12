<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';

import baseMapUrl from '../assets/base-map.webp';
import teleporterSvg from '../assets/teleporter.svg?raw';
import { getMessages } from '../lang';
import type { Language } from '../lang';
import type {
  CargoConnection,
  CargoMarker,
  CargoResponse,
  Player,
  Rect2D,
  SelectedEntity,
  Teleporter,
  UserAnnotationEditMode,
  UserAnnotationSelection,
  UserMarker,
  UserZone,
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
  userMarkers?: UserMarker[];
  userZones?: UserZone[];
  annotationMode?: 'idle' | 'marker' | 'zone';
  annotationEditMode?: UserAnnotationEditMode;
  selectedAnnotation?: UserAnnotationSelection;
}>();

const emit = defineEmits<{
  "select": [key: string];
  "clear-selection": [];
  "hover": [key: string | null];
  "select-annotation": [sel: UserAnnotationSelection];
  "create-marker": [point: { x: number; y: number }];
  "create-zone": [rect: Rect2D];
  "move-marker": [point: { x: number; y: number }];
  "update-zone": [rect: Rect2D];
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
const lockedUserZones = computed(() => (props.userZones ?? []).filter((zone) => zone.locked));
const unlockedUserZones = computed(() => (props.userZones ?? []).filter((zone) => !zone.locked));

const {
  tooltip,
  hideTooltip,
  showTooltip,
  showTooltipFromElement,
  moveTooltip,
} = useMapTooltip(mapShell);

const panZoom = useMapPanZoom(mapShell, viewBoxWidth, viewBoxHeight);

const {
  mapScale,
  mapTranslateX,
  mapTranslateY,
  transform,
  isDragging,
  resetView,
  centerOnPoint,
  handleMouseDown: panZoomHandleMouseDown,
  handleWheel,
} = panZoom;

// ── annotation interaction ───────────────────────────────────────────────────

// Draft zone being drawn (map coords)
interface ZoneDraft { x: number; y: number; w: number; h: number; startX: number; startY: number }
const zoneDraft = ref<ZoneDraft | null>(null);
const ghostPoint = ref<{ x: number; y: number } | null>(null);
type ActiveDrag =
  | { type: 'marker'; id: string }
  | { type: 'zone'; id: string; originRect: Rect2D; originPoint: { x: number; y: number } };
const activeDrag = ref<ActiveDrag | null>(null);

// Keep zone labels readable regardless of zoom.
const zoneLabelFontSize = computed(() => Math.max(20, Math.round(26 / mapScale.value)));

/**
 * Convert a client mouse position to SVG viewBox (map) coordinates.
 * Takes into account the container bounding rect, the fit-scale that the SVG
 * applies automatically (xMidYMid meet) and the current pan/zoom transform.
 */
function screenToMapPoint(clientX: number, clientY: number): { x: number; y: number } {
  if (!mapShell.value) return { x: 0, y: 0 };
  const rect = mapShell.value.getBoundingClientRect();
  const fitScale = Math.min(rect.width / viewBoxWidth.value, rect.height / viewBoxHeight.value);
  // offset due to "meet" centering
  const svgLeft = rect.left + (rect.width - viewBoxWidth.value * fitScale) / 2;
  const svgTop  = rect.top  + (rect.height - viewBoxHeight.value * fitScale) / 2;
  // position in viewBox space (before pan/zoom transform)
  const vx = (clientX - svgLeft) / fitScale;
  const vy = (clientY - svgTop) / fitScale;
  // undo the pan/zoom transform: map = (viewBox - translate) / scale
  return {
    x: (vx - mapTranslateX.value) / mapScale.value,
    y: (vy - mapTranslateY.value) / mapScale.value,
  };
}

function handleWindowMouseMove(event: MouseEvent): void {
  if (activeDrag.value) {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    if (activeDrag.value.type === 'marker') {
      ghostPoint.value = pt;
      return;
    }

    const dx = pt.x - activeDrag.value.originPoint.x;
    const dy = pt.y - activeDrag.value.originPoint.y;
    zoneDraft.value = {
      x: activeDrag.value.originRect.x + dx,
      y: activeDrag.value.originRect.y + dy,
      w: activeDrag.value.originRect.width,
      h: activeDrag.value.originRect.height,
      startX: activeDrag.value.originRect.x + dx,
      startY: activeDrag.value.originRect.y + dy,
    };
    return;
  }

  if (zoneDraft.value) {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    const dx = pt.x - zoneDraft.value.startX;
    const dy = pt.y - zoneDraft.value.startY;
    zoneDraft.value = {
      ...zoneDraft.value,
      x: dx >= 0 ? zoneDraft.value.startX : pt.x,
      y: dy >= 0 ? zoneDraft.value.startY : pt.y,
      w: Math.abs(dx),
      h: Math.abs(dy),
    };
  }
}

function handleWindowMouseUp(event: MouseEvent): void {
  if (activeDrag.value) {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    if (activeDrag.value.type === 'marker') {
      emit('move-marker', pt);
      activeDrag.value = null;
      ghostPoint.value = null;
      return;
    }

    emit('update-zone', {
      x: activeDrag.value.originRect.x + (pt.x - activeDrag.value.originPoint.x),
      y: activeDrag.value.originRect.y + (pt.y - activeDrag.value.originPoint.y),
      width: activeDrag.value.originRect.width,
      height: activeDrag.value.originRect.height,
    });
    activeDrag.value = null;
    zoneDraft.value = null;
    return;
  }

  if (zoneDraft.value) {
    const { x, y, w, h } = zoneDraft.value;
    if (w > 10 && h > 10) {
      emit('create-zone', { x, y, width: w, height: h });
    }
    zoneDraft.value = null;
  }
}

function focusSelection(): void {
  if (!props.selectedEntity) return;
  centerOnPoint(props.selectedEntity.raw.map.x, props.selectedEntity.raw.map.y);
}

function focusPoint(mapX: number, mapY: number, desiredScale?: number): void {
  centerOnPoint(mapX, mapY, desiredScale);
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

function userMarkerLabel(marker: UserMarker): string {
  return marker.label || ui.value.notes.markerSingular;
}

function userZoneLabel(zone: UserZone): string {
  return zone.label || ui.value.notes.zoneSingular;
}

function showUserMarkerTooltip(marker: UserMarker, event: MouseEvent): void {
  showTooltip(userMarkerLabel(marker), [ui.value.map.clickToSelect], event);
}

function userZoneTooltipLines(zone: UserZone): string[] {
  const description = zone.description.trim();
  const label = userZoneLabel(zone);
  const lines = description && description !== label ? [description] : [];
  return [...lines, ui.value.map.clickToSelect];
}

function handleUserMarkerFocus(marker: UserMarker, event: FocusEvent): void {
  const target = event.target as Element | null;
  if (!target) return;
  showTooltipFromElement(userMarkerLabel(marker), [ui.value.map.clickToSelect], target);
}

function showUserZoneTooltip(zone: UserZone, event: MouseEvent): void {
  showTooltip(userZoneLabel(zone), userZoneTooltipLines(zone), event);
}

function handleUserZoneFocus(zone: UserZone, event: FocusEvent): void {
  const target = event.target as Element | null;
  if (!target) return;
  showTooltipFromElement(userZoneLabel(zone), userZoneTooltipLines(zone), target);
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

function handleMouseDown(event: MouseEvent): void {
  const target = event.target as Element | null;
  if (target?.closest('.map-marker, .connection-line, .user-marker, .user-zone')) return;

  const mode = props.annotationMode ?? 'idle';

  if (mode === 'zone') {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    zoneDraft.value = { x: pt.x, y: pt.y, w: 0, h: 0, startX: pt.x, startY: pt.y };
    return;
  }

  panZoomHandleMouseDown(event, hideTooltip);
}

function handleUserMarkerMouseDown(marker: UserMarker, event: MouseEvent): void {
  if (props.annotationEditMode !== 'move-marker' || !isAnnotationSelected('marker', marker.id)) return;
  event.stopPropagation();
  event.preventDefault();
  activeDrag.value = { type: 'marker', id: marker.id };
  ghostPoint.value = { ...marker.map };
  hideTooltip();
}

function handleUserZoneMouseDown(zone: UserZone, event: MouseEvent): void {
  if (props.annotationEditMode !== 'edit-zone' || !isAnnotationSelected('zone', zone.id)) return;
  event.stopPropagation();
  event.preventDefault();
  activeDrag.value = {
    type: 'zone',
    id: zone.id,
    originRect: { ...zone.rect },
    originPoint: screenToMapPoint(event.clientX, event.clientY),
  };
  zoneDraft.value = {
    x: zone.rect.x,
    y: zone.rect.y,
    w: zone.rect.width,
    h: zone.rect.height,
    startX: zone.rect.x,
    startY: zone.rect.y,
  };
  hideTooltip();
}

function handleCanvasClick(event: MouseEvent): void {
  const mode = props.annotationMode ?? 'idle';
  if (mode === 'marker') {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    emit('create-marker', pt);
  }
}

function handleMouseMove(event: MouseEvent): void {
  const mode = props.annotationMode ?? 'idle';
  if (mode === 'marker') {
    ghostPoint.value = screenToMapPoint(event.clientX, event.clientY);
  } else {
    ghostPoint.value = null;
  }
}

function handleMouseLeave(): void {
  ghostPoint.value = null;
}

function isAnnotationSelected(type: 'marker' | 'zone', id: string): boolean {
  const sel = props.selectedAnnotation;
  if (!sel) return false;
  return sel.type === type && sel.id === id;
}

function zoneLabelX(zone: UserZone): number {
  return zone.rect.x + 10 / mapScale.value;
}

function zoneLabelY(zone: UserZone): number {
  return zone.rect.y + 10 / mapScale.value;
}

function zoneGroupClass(zone: UserZone): Record<string, boolean> {
  return {
    active: isAnnotationSelected('zone', zone.id),
    locked: zone.locked,
  };
}

onMounted(() => {
  window.addEventListener('mousemove', handleWindowMouseMove);
  window.addEventListener('mouseup', handleWindowMouseUp);
});

onBeforeUnmount(() => {
  window.removeEventListener('mousemove', handleWindowMouseMove);
  window.removeEventListener('mouseup', handleWindowMouseUp);
});

defineExpose({
  focusSelection,
  focusPoint,
  resetView,
});
</script>

<template>
  <div
    ref="mapShell"
    class="map-canvas"
    :class="{
      dragging: isDragging,
      'is-placing-marker': (annotationMode ?? 'idle') === 'marker',
      'is-drawing-zone': (annotationMode ?? 'idle') === 'zone',
      'is-moving-annotation': (annotationEditMode ?? 'idle') !== 'idle',
    }"
    :aria-busy="loading"
    @mousedown="handleMouseDown"
    @wheel="handleWheel"
    @mousemove="handleMouseMove"
    @mouseleave="handleMouseLeave"
    @click="handleCanvasClick"
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

        <!-- Locked user zones under everything else -->
        <g v-if="lockedUserZones.length">
          <g
            v-for="zone in lockedUserZones"
            :key="zone.id"
            class="user-zone"
            :class="zoneGroupClass(zone)"
            :style="{ '--annotation-color': zone.color }"
            tabindex="0"
            role="button"
            @click.stop="emit('select-annotation', { type: 'zone', id: zone.id })"
            @focus.stop="handleUserZoneFocus(zone, $event)"
            @mouseenter.stop="showUserZoneTooltip(zone, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="hideTooltip"
            @mouseleave.stop="hideTooltip"
          >
            <rect
              :x="zone.rect.x"
              :y="zone.rect.y"
              :width="zone.rect.width"
              :height="zone.rect.height"
            />
            <text
              class="user-zone-label"
              :x="zoneLabelX(zone)"
              :y="zoneLabelY(zone)"
              :font-size="zoneLabelFontSize"
              text-anchor="start"
              dominant-baseline="hanging"
            >{{ zone.label }}</text>
          </g>
        </g>

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

        <!-- User zones -->
        <g v-if="unlockedUserZones.length">
          <g
            v-for="zone in unlockedUserZones"
            :key="zone.id"
            class="user-zone"
            :class="zoneGroupClass(zone)"
            :style="{ '--annotation-color': zone.color }"
            tabindex="0"
            role="button"
            @click.stop="emit('select-annotation', { type: 'zone', id: zone.id })"
            @mousedown.stop="handleUserZoneMouseDown(zone, $event)"
            @focus.stop="handleUserZoneFocus(zone, $event)"
            @mouseenter.stop="showUserZoneTooltip(zone, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="hideTooltip"
            @mouseleave.stop="hideTooltip"
          >
            <rect
              :x="zone.rect.x"
              :y="zone.rect.y"
              :width="zone.rect.width"
              :height="zone.rect.height"
            />
              <text
                class="user-zone-label"
                :x="zoneLabelX(zone)"
                :y="zoneLabelY(zone)"
                :font-size="zoneLabelFontSize"
                text-anchor="start"
                dominant-baseline="hanging"
             >{{ zone.label }}</text>
          </g>
        </g>

        <!-- Zone draft preview -->
        <rect
          v-if="zoneDraft && zoneDraft.w > 4 && zoneDraft.h > 4"
          class="zone-draft-preview"
          :x="zoneDraft.x"
          :y="zoneDraft.y"
          :width="zoneDraft.w"
          :height="zoneDraft.h"
        />

        <!-- User markers -->
        <g v-if="userMarkers && userMarkers.length">
          <g
            v-for="marker in userMarkers"
            :key="marker.id"
            class="map-marker user-marker"
            :class="{ active: isAnnotationSelected('marker', marker.id) }"
            :style="{ '--annotation-color': marker.color }"
            tabindex="0"
            role="button"
            @click.stop="emit('select-annotation', { type: 'marker', id: marker.id })"
            @mousedown.stop="handleUserMarkerMouseDown(marker, $event)"
            @focus.stop="handleUserMarkerFocus(marker, $event)"
            @mouseenter.stop="showUserMarkerTooltip(marker, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="hideTooltip"
            @mouseleave.stop="hideTooltip"
          >
            <circle :cx="marker.map.x" :cy="marker.map.y" r="9" class="user-marker-outer" />
            <circle :cx="marker.map.x" :cy="marker.map.y" r="4" class="user-marker-core" />
          </g>
        </g>

        <!-- Ghost marker (placement preview) -->
        <g v-if="ghostPoint" class="ghost-marker">
          <circle :cx="ghostPoint.x" :cy="ghostPoint.y" r="9" />
          <circle :cx="ghostPoint.x" :cy="ghostPoint.y" r="4" class="ghost-marker-core" />
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

.map-canvas.is-placing-marker {
    cursor: crosshair;
}

.map-canvas.is-drawing-zone {
    cursor: cell;
}

.map-canvas.is-moving-annotation {
    cursor: move;
}

:deep(.user-marker) {
    cursor: pointer;
}

.map-canvas.is-moving-annotation :deep(.user-marker.active),
.map-canvas.is-moving-annotation :deep(.user-zone.active) {
    cursor: move;
}

:deep(.user-marker-outer) {
    fill: color-mix(in srgb, var(--annotation-color, var(--amber)) 18%, transparent);
    stroke: var(--annotation-color, var(--amber));
    stroke-width: 1.5;
}

:deep(.user-marker-core) {
    fill: var(--annotation-color, var(--amber));
    stroke: none;
}

:deep(.user-marker.active .user-marker-outer) {
    stroke-width: 2.2;
    filter: drop-shadow(0 0 6px var(--annotation-color, var(--amber)));
}

:deep(.ghost-marker) {
    pointer-events: none;
    opacity: 0.55;
}

:deep(.ghost-marker circle) {
    fill: var(--amber-soft);
    stroke: var(--amber);
    stroke-width: 1.5;
    stroke-dasharray: 4 2;
}

:deep(.ghost-marker-core) {
    fill: var(--amber);
    stroke: none !important;
    stroke-dasharray: none !important;
}

:deep(.user-zone) {
    cursor: pointer;
}

:deep(.user-zone.locked rect) {
    stroke-dasharray: 3 4;
    opacity: 0.55;
}

:deep(.user-zone.locked .user-zone-label) {
    opacity: 0.72;
}

:deep(.user-zone rect) {
    fill: color-mix(in srgb, var(--annotation-color, var(--accent)) 10%, transparent);
    stroke: var(--annotation-color, var(--accent));
    stroke-width: 1.2;
    stroke-dasharray: 6 3;
}

:deep(.user-zone.active rect) {
    stroke: var(--annotation-color, var(--accent));
    stroke-width: 1.8;
    stroke-dasharray: none;
    fill: color-mix(in srgb, var(--annotation-color, var(--accent)) 16%, transparent);
}

:deep(.user-zone-label) {
    fill: var(--annotation-color, #fff4cf);
    font-family: var(--font-mono);
    pointer-events: none;
    paint-order: stroke fill;
    stroke: rgba(0, 0, 0, 0.78);
    stroke-width: 4px;
    font-weight: 700;
}

:deep(.zone-draft-preview) {
    fill: rgba(232, 184, 75, 0.08);
    stroke: var(--amber);
    stroke-width: 1.2;
    stroke-dasharray: 4 3;
    pointer-events: none;
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
