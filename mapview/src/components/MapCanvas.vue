<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';

import teleporterSvg from '../assets/teleporter.svg?raw';
import { getMessages } from '../lang';
import type { Language } from '../lang';
import type {
  CargoConnection,
  CargoMarker,
  CargoResponse,
  Player,
  Poi,
  Rect2D,
  SelectedEntity,
  Teleporter,
  UserAnnotationEditMode,
  UserAnnotationSelection,
  UserMarker,
  UserZone,
} from '../lib/types';
import { mapToWorld, resolveMapProjection } from '../lib/mapProjection';
import {
  groupColor,
  type StaticPlacement,
  type StaticPointSeries,
} from '../lib/staticMapCatalog';
import type {
  StaticMapPoiView,
  StaticSelection,
} from '../composables/useStaticMapData';
import { useMapTooltip } from '../composables/useMapTooltip';
import { useMapPanZoom } from '../composables/useMapPanZoom';
import StaticMapCanvas from './mapview/StaticMapCanvas.vue';

const props = defineProps<{
  loading: boolean;
  cargo: CargoResponse | null;
  cargoMarkers: CargoMarker[];
  cargoConnections: CargoConnection[];
  teleporters: Teleporter[];
  players: Player[];
  pois: Poi[];
  selectedKey: string | null;
  selectedEntity: SelectedEntity | null;
  orphanKeys: string[];
  focusKeys: string[];
  focusCargoKey: string | null;
  lang: Language;
  iconScale: number;
  userMarkers?: UserMarker[];
  userZones?: UserZone[];
  annotationMode?: 'idle' | 'marker' | 'zone';
  annotationEditMode?: UserAnnotationEditMode;
  selectedAnnotation?: UserAnnotationSelection;

  // ── Static world catalog (map-data/) ───────────────────────────────────────
  staticSeries?: StaticPointSeries[];
  staticPlacements?: StaticPlacement[];
  staticPois?: StaticMapPoiView[];
  staticStateVersion?: number;
  staticSelection?: StaticSelection | null;
  staticSeriesVisible?: (entry: StaticPointSeries) => boolean;
  staticPlacementVisible?: (entry: StaticPlacement) => boolean;
  staticPoiVisible?: (entry: StaticMapPoiView) => boolean;
  staticPick?: (x: number, y: number, radius: number) => StaticSelection | null;
  staticDescribe?: (selection: StaticSelection) => { title: string; lines: string[] };
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
  "select-static": [selection: StaticSelection | null];
}>(); 

const TELEPORTER_SYMBOL_ID = 'teleporter-marker-icon';
const TELEPORTER_ICON_SIZE = 28;
const TELEPORTER_ICON_HALF = TELEPORTER_ICON_SIZE / 2;
const TELEPORTER_HITBOX_SIZE = 36;
const TELEPORTER_HITBOX_HALF = TELEPORTER_HITBOX_SIZE / 2;
const POI_HITBOX_SIZE = 32;
const POI_HITBOX_HALF = POI_HITBOX_SIZE / 2;
const TILE_SOURCE_WIDTH = 9019;
const TILE_SOURCE_HEIGHT = 11691;
const TILE_SIZE = 2048;
const TILE_COLS = Math.ceil(TILE_SOURCE_WIDTH / TILE_SIZE);
const TILE_ROWS = Math.ceil(TILE_SOURCE_HEIGHT / TILE_SIZE);
const TILE_OVERSCAN = 1;
const TILE_BASE_PATH = 'map-tiles/base_newmap_q70_2048';
const teleporterSymbolHref = `#${TELEPORTER_SYMBOL_ID}`;
const teleporterSymbolMarkup = teleporterSvg
  .replace(/<\?xml[^>]*>\s*/i, '')
  .replace('<svg', `<symbol id="${TELEPORTER_SYMBOL_ID}"`)
  .replace('</svg>', '</symbol>')
  .replace(/\swidth="[^"]*"/i, '')
  .replace(/\sheight="[^"]*"/i, '');

const mapShell = ref<HTMLElement | null>(null);

const ui = computed(() => getMessages(props.lang));
const projection = computed(() => resolveMapProjection(props.cargo?.map));
const viewBoxWidth = computed(() => projection.value.content_width);
const viewBoxHeight = computed(() => projection.value.content_height);
const imageWidth = computed(() => projection.value.image_width);
const imageHeight = computed(() => projection.value.image_height);
const imageX = computed(() => -projection.value.dst_x1);
const imageY = computed(() => -projection.value.dst_y1);
const orphanKeySet = computed(() => new Set(props.orphanKeys));
const focusKeySet = computed(() => new Set(props.focusKeys));

const markerScaleTransform = computed(() => `scale(${props.iconScale})`);
const tileScaleX = computed(() => imageWidth.value / TILE_SOURCE_WIDTH);
const tileScaleY = computed(() => imageHeight.value / TILE_SOURCE_HEIGHT);

const visibleMapBounds = computed(() => {
  const left = (viewportBounds.value.left - mapTranslateX.value) / mapScale.value;
  const top = (viewportBounds.value.top - mapTranslateY.value) / mapScale.value;
  const right = (viewportBounds.value.right - mapTranslateX.value) / mapScale.value;
  const bottom = (viewportBounds.value.bottom - mapTranslateY.value) / mapScale.value;

  return {
    left: Math.min(left, right),
    right: Math.max(left, right),
    top: Math.min(top, bottom),
    bottom: Math.max(top, bottom),
  };
});

const visibleBaseMapTiles = computed(() => {
  const tileWidth = TILE_SIZE * tileScaleX.value;
  const tileHeight = TILE_SIZE * tileScaleY.value;

  if (tileWidth <= 0 || tileHeight <= 0) {
    return [];
  }

  const startCol = Math.max(
    0,
    Math.floor((visibleMapBounds.value.left - imageX.value) / tileWidth) - TILE_OVERSCAN,
  );
  const endCol = Math.min(
    TILE_COLS - 1,
    Math.floor((visibleMapBounds.value.right - imageX.value) / tileWidth) + TILE_OVERSCAN,
  );
  const startRow = Math.max(
    0,
    Math.floor((visibleMapBounds.value.top - imageY.value) / tileHeight) - TILE_OVERSCAN,
  );
  const endRow = Math.min(
    TILE_ROWS - 1,
    Math.floor((visibleMapBounds.value.bottom - imageY.value) / tileHeight) + TILE_OVERSCAN,
  );

  if (startCol > endCol || startRow > endRow) {
    return [];
  }

  const tiles = [] as Array<{
    key: string;
    href: string;
    x: number;
    y: number;
    width: number;
    height: number;
  }>;

  for (let row = startRow; row <= endRow; row += 1) {
    for (let col = startCol; col <= endCol; col += 1) {
      const sourceWidth = Math.min(TILE_SIZE, TILE_SOURCE_WIDTH - col * TILE_SIZE);
      const sourceHeight = Math.min(TILE_SIZE, TILE_SOURCE_HEIGHT - row * TILE_SIZE);

      tiles.push({
        key: `tile-${row}-${col}`,
        href: `${TILE_BASE_PATH}/tile_r${row}_c${col}.webp`,
        x: imageX.value + col * tileWidth,
        y: imageY.value + row * tileHeight,
        width: sourceWidth * tileScaleX.value,
        height: sourceHeight * tileScaleY.value,
      });
    }
  }

  return tiles;
});

function markerTranslateTransform(x: number, y: number): string {
  return `translate(${x} ${y}) ${markerScaleTransform.value} translate(${-x} ${-y})`;
}

const lockedUserZones = computed(() => (props.userZones ?? []).filter((zone) => zone.locked));
const unlockedUserZones = computed(() => (props.userZones ?? []).filter((zone) => !zone.locked));


const {
  tooltip,
  hideTooltip,
  showTooltip,
  showTooltipFromElement,
  moveTooltip,
} = useMapTooltip(mapShell);

const panZoom = useMapPanZoom(mapShell, viewBoxWidth, viewBoxHeight, {
  x: imageX,
  y: imageY,
  width: imageWidth,
  height: imageHeight,
});

const {
  mapScale,
  mapTranslateX,
  mapTranslateY,
  transform,
  mapPixelsPerUnit,
  isDragging,
  viewportBounds,
  resetView,
  centerOnPoint,
  screenToMapPoint,
  consumeDragMovement,
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

function isMapPointWithinImage(point: { x: number; y: number }): boolean {
  const imageRight = imageX.value + imageWidth.value;
  const imageBottom = imageY.value + imageHeight.value;
  const left = Math.min(imageX.value, imageRight);
  const right = Math.max(imageX.value, imageRight);
  const top = Math.min(imageY.value, imageBottom);
  const bottom = Math.max(imageY.value, imageBottom);

  return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

function handleWindowMouseMove(event: MouseEvent): void {
  if (activeDrag.value) {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    if (!pt) return;

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
    if (!pt) return;

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
    if (!pt) {
      activeDrag.value = null;
      ghostPoint.value = null;
      zoneDraft.value = null;
      return;
    }

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

function stableStringHash(value: string): number {
  let hash = 0x811c9dc5;

  for (let index = 0; index < value.length; index += 1) {
    hash ^= value.charCodeAt(index);
    hash = Math.imul(hash, 0x01000193);
  }

  return hash >>> 0;
}

function poiKindLabel(poi: Poi): string {
  switch (poi.kind) {
    case 'abandoned_base':
      return ui.value.map.abandonedBaseLabel;
    case 'plant_resource':
      return ui.value.map.plantResourceLabel;
    case 'ignitium':
      return ui.value.map.ignitiumLabel;
    case 'star_tears':
      return ui.value.map.starTearsLabel;
  }
}

function poiLabel(poi: Poi): string {
  return poi.label || poi.resource || poiKindLabel(poi);
}

function poiStateLabel(poi: Poi): string {
  return poi.depleted === true
    ? ui.value.map.depletedLabel
    : ui.value.map.availableLabel;
}

function poiTooltipLines(poi: Poi): string[] {
  return [
    `${ui.value.selection.type}: ${poiKindLabel(poi)}`,
    `${ui.value.selection.name}: ${poiLabel(poi)}`,
    `${ui.value.selection.resource}: ${poi.resource || ui.value.map.noResource}`,
    `${ui.value.selection.state}: ${poiStateLabel(poi)}`,
    ui.value.map.clickToSelect,
  ];
}

function poiColorStyle(poi: Poi): Record<string, string> {
  if (poi.kind === 'ignitium') return { '--poi-color': '#f97316' };
  if (poi.kind === 'star_tears') return { '--poi-color': '#38bdf8' };

  const colorKey = (poi.resource || poi.label || poi.unique_key).trim().toLowerCase();
  const hash = stableStringHash(colorKey);
  // Derive a stable HSL color from the full hash instead of a small fixed
  // palette so distinct resources rarely share the same hue.
  const hue = hash % 360;
  const saturation = 62 + (Math.floor(hash / 360) % 21); // 62–82%
  const lightness = 60 + (Math.floor(hash / 7560) % 13); // 60–72%
  return { '--poi-color': `hsl(${hue}, ${saturation}%, ${lightness}%)` };
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

function poiAriaLabel(poi: Poi): string {
  return `${poiLabel(poi)}. ${poiKindLabel(poi)}. ${poi.resource || ui.value.map.noResource}. ${poiStateLabel(poi)}.`;
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

function handlePoiFocus(poi: Poi, event: FocusEvent): void {
  const target = event.target as Element | null;
  if (!target) return;

  emit('hover', poi.unique_key);
  showTooltipFromElement(poiLabel(poi), poiTooltipLines(poi), target);
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

function showPoiTooltip(poi: Poi, event: MouseEvent): void {
  emit('hover', poi.unique_key);
  showTooltip(poiLabel(poi), poiTooltipLines(poi), event);
}

function handlePoiBlur(): void {
  emit('hover', null);
  hideTooltip();
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
  if (!target?.closest('.map-svg')) return;
  if (target.closest('.map-marker, .connection-line, .user-marker, .user-zone')) return;

  const mode = props.annotationMode ?? 'idle';

  if (mode === 'zone') {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    if (!pt || !isMapPointWithinImage(pt)) return;

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
  const originPoint = screenToMapPoint(event.clientX, event.clientY);
  if (!originPoint) return;

  activeDrag.value = {
    type: 'zone',
    id: zone.id,
    originRect: { ...zone.rect },
    originPoint,
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
  const target = event.target as Element | null;
  if (!target?.closest('.map-svg')) return;

  const mode = props.annotationMode ?? 'idle';
  if (mode === 'idle') {
    if (consumeDragMovement()) return;
    if (target.closest('.map-marker, .connection-line, .user-zone')) return;
    const found = pickStaticAt(event.clientX, event.clientY);
    if (found) emit('select-static', found);
    return;
  }
  if (mode !== 'marker' || consumeDragMovement()) return;

  const pt = screenToMapPoint(event.clientX, event.clientY);
  if (!pt || !isMapPointWithinImage(pt)) return;

  emit('create-marker', pt);
}

function handleMouseMove(event: MouseEvent): void {
  const target = event.target as Element | null;
  const mode = props.annotationMode ?? 'idle';
  if (mode === 'marker' && target?.closest('.map-svg')) {
    const pt = screenToMapPoint(event.clientX, event.clientY);
    ghostPoint.value = pt && isMapPointWithinImage(pt) ? pt : null;
  } else {
    ghostPoint.value = null;
  }

  if (mode !== 'idle' || target?.closest('.map-marker, .connection-line, .user-zone')) {
    clearStaticHover();
    return;
  }
  updateStaticHover(event);
}

function handleMouseLeave(): void {
  ghostPoint.value = null;
  clearStaticHover();
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

// ── static world catalog ─────────────────────────────────────────────────

const staticSeriesList = computed(() => props.staticSeries ?? []);
const staticPlacementList = computed(() => props.staticPlacements ?? []);
const noStaticSeries = () => false;
const noStaticPlacement = () => false;
const staticSeriesVisible = computed(() => props.staticSeriesVisible ?? noStaticSeries);
const staticPlacementVisible = computed(
  () => props.staticPlacementVisible ?? noStaticPlacement,
);
const visibleStaticPois = computed(() => {
  const entries = props.staticPois ?? [];
  const isVisible = props.staticPoiVisible;
  return isVisible ? entries.filter(isVisible) : entries;
});

const staticHover = ref<StaticSelection | null>(null);
let staticPickHandle = 0;
let staticPickEvent: MouseEvent | null = null;

const staticHighlight = computed(() => {
  const target = staticHover.value ?? props.staticSelection ?? null;
  return target ? { x: target.x, y: target.y } : null;
});

/** Converts a screen position to world decimetres and queries the catalog. */
function pickStaticAt(clientX: number, clientY: number): StaticSelection | null {
  const pick = props.staticPick;
  if (!pick) return null;

  const point = screenToMapPoint(clientX, clientY);
  if (!point) return null;

  const world = mapToWorld(point, projection.value);
  const scaleX =
    (projection.value.dst_x2 - projection.value.dst_x1) /
    (projection.value.src_x2 - projection.value.src_x1);
  // Keep the hit radius at roughly 9 screen pixels whatever the viewport or zoom.
  const radiusDm =
    Math.abs(9 / Math.max(mapPixelsPerUnit.value, Number.EPSILON) / scaleX) / 10;

  return pick(world.x / 10, world.y / 10, radiusDm);
}

function clearStaticHover(): void {
  if (!staticHover.value) return;
  staticHover.value = null;
  hideTooltip();
}

function updateStaticHover(event: MouseEvent): void {
  if (!props.staticPick) return;

  staticPickEvent = event;
  if (staticPickHandle) return;

  staticPickHandle = requestAnimationFrame(() => {
    staticPickHandle = 0;
    const pointer = staticPickEvent;
    if (!pointer) return;

    const found = pickStaticAt(pointer.clientX, pointer.clientY);
    const previousKey = staticHover.value?.key ?? null;
    staticHover.value = found;

    if (!found) {
      if (previousKey) hideTooltip();
      return;
    }
    if (found.key === previousKey) {
      moveTooltip(pointer);
      return;
    }

    const description = props.staticDescribe?.(found);
    if (description) {
      showTooltip(description.title, description.lines, pointer);
    }
  });
}

function staticPoiLabel(poi: StaticMapPoiView): string {
  return props.lang === 'fr' ? poi.nameFr || poi.nameEn : poi.nameEn || poi.nameFr;
}

function staticPoiColorStyle(poi: StaticMapPoiView): Record<string, string> {
  return { '--static-poi-color': groupColor(poi.group) };
}

function staticPoiTransform(poi: StaticMapPoiView): string {
  const screenConstantScale =
    props.iconScale / Math.max(mapPixelsPerUnit.value, Number.EPSILON);
  return `translate(${poi.map.x} ${poi.map.y}) scale(${screenConstantScale}) translate(${-poi.map.x} ${-poi.map.y})`;
}

function staticPoiSelection(poi: StaticMapPoiView): StaticSelection {
  return {
    kind: 'poi',
    key: poi.key,
    layer: 'poi',
    group: poi.group,
    x: poi.x,
    y: poi.y,
    z: poi.z,
    state: 'unknown',
    label: staticPoiLabel(poi),
    guid: poi.guid,
    poi,
  };
}

function showStaticPoiTooltip(poi: StaticMapPoiView, event: MouseEvent): void {
  const description = props.staticDescribe?.(staticPoiSelection(poi));
  if (description) showTooltip(description.title, description.lines, event);
}

function handleStaticPoiFocus(poi: StaticMapPoiView, event: FocusEvent): void {
  const target = event.currentTarget as Element | null;
  const description = props.staticDescribe?.(staticPoiSelection(poi));
  if (target && description) {
    showTooltipFromElement(description.title, description.lines, target);
  }
}

function handleStaticPoiKeydown(event: KeyboardEvent, poi: StaticMapPoiView): void {
  if (event.key === 'Enter' || event.key === ' ') {
    event.preventDefault();
    emit('select-static', staticPoiSelection(poi));
    return;
  }

  if (event.key === 'Escape') {
    event.preventDefault();
    emit('select-static', null);
  }
}

onMounted(() => {
  window.addEventListener('mousemove', handleWindowMouseMove);
  window.addEventListener('mouseup', handleWindowMouseUp);
});

onBeforeUnmount(() => {
  if (staticPickHandle) cancelAnimationFrame(staticPickHandle);
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
      class="map-svg map-svg-base"
      :viewBox="`0 0 ${viewBoxWidth} ${viewBoxHeight}`"
      preserveAspectRatio="xMidYMid meet"
      aria-hidden="true"
    >
      <g :transform="transform">
        <!-- preserveAspectRatio="none" is required: the projection makes each
             tile slot non-square (ratio ~1.10) while the tile images are square,
             so the default "xMidYMid meet" shrank every tile to fit its slot
             height and left a black gap at each seam. x/y/width/height already
             place each tile exactly, so filling the slot is the correct mapping. -->
        <image
          v-for="tile in visibleBaseMapTiles"
          :key="tile.key"
          class="base-map"
          preserveAspectRatio="none"
          :href="tile.href"
          :x="tile.x"
          :y="tile.y"
          :width="tile.width"
          :height="tile.height"
        />
      </g>
    </svg>

    <StaticMapCanvas
      :series="staticSeriesList"
      :placements="staticPlacementList"
      :is-series-visible="staticSeriesVisible"
      :is-placement-visible="staticPlacementVisible"
      :projection="projection"
      :map-scale="mapScale"
      :map-translate-x="mapTranslateX"
      :map-translate-y="mapTranslateY"
      :viewport-bounds="viewportBounds"
      :view-box-width="viewBoxWidth"
      :view-box-height="viewBoxHeight"
      :state-version="staticStateVersion ?? 0"
      :highlight="staticHighlight"
    />

    <svg
      class="map-svg map-svg-entities"
      :viewBox="`0 0 ${viewBoxWidth} ${viewBoxHeight}`"
      preserveAspectRatio="xMidYMid meet"
      @dblclick="emit('clear-selection')"
    >
      <defs v-html="teleporterSymbolMarkup"></defs>
      <g :transform="transform">

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
          <!-- Catalog points of interest, selectable like live entities -->
          <g
            v-for="poi in visibleStaticPois"
            :key="poi.key"
            class="map-marker static-poi"
            :class="{ active: staticSelection?.key === poi.key }"
            :style="staticPoiColorStyle(poi)"
            tabindex="0"
            role="button"
            :aria-pressed="staticSelection?.key === poi.key"
            :aria-label="staticPoiLabel(poi)"
            :transform="staticPoiTransform(poi)"
            @click.stop="emit('select-static', staticPoiSelection(poi))"
            @keydown="handleStaticPoiKeydown($event, poi)"
            @dblclick.stop
            @focus.stop="handleStaticPoiFocus(poi, $event)"
            @mouseenter.stop="showStaticPoiTooltip(poi, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="hideTooltip"
            @mouseleave.stop="hideTooltip"
          >
            <rect
              class="poi-hitbox"
              :x="poi.map.x - POI_HITBOX_HALF"
              :y="poi.map.y - POI_HITBOX_HALF"
              :width="POI_HITBOX_SIZE"
              :height="POI_HITBOX_SIZE"
              rx="4"
            />
            <path
              class="static-poi-pin"
              :d="`M ${poi.map.x} ${poi.map.y + 9} L ${poi.map.x - 7} ${poi.map.y - 3} A 7 7 0 1 1 ${poi.map.x + 7} ${poi.map.y - 3} Z`"
            />
            <circle class="static-poi-core" :cx="poi.map.x" :cy="poi.map.y - 4" r="2.6" />
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
            :transform="markerTranslateTransform(marker.map.x, marker.map.y)"
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
            v-for="poi in pois"
            :key="poi.unique_key"
            class="map-marker poi"
            :class="[
              poi.kind,
              {
                active: selectedKey === poi.unique_key,
                dimmed: isDimmed(poi.unique_key),
                depleted: poi.depleted === true,
                available: poi.depleted !== true,
              },
            ]"
            :style="poi.kind !== 'abandoned_base' ? poiColorStyle(poi) : undefined"
            tabindex="0"
            role="button"
            :aria-pressed="selectedKey === poi.unique_key"
            :aria-label="poiAriaLabel(poi)"
            :transform="markerTranslateTransform(poi.map.x, poi.map.y)"
            @click.stop="emit('select', poi.unique_key)"
            @dblclick.stop
            @keydown="handleMarkerKeydown($event, poi.unique_key)"
            @focus.stop="handlePoiFocus(poi, $event)"
            @mouseenter.stop="showPoiTooltip(poi, $event)"
            @mousemove.stop="moveTooltip($event)"
            @blur.stop="handlePoiBlur"
            @mouseleave.stop="handlePoiBlur"
          >
            <rect
              class="poi-hitbox"
              :x="poi.map.x - POI_HITBOX_HALF"
              :y="poi.map.y - POI_HITBOX_HALF"
              :width="POI_HITBOX_SIZE"
              :height="POI_HITBOX_SIZE"
              rx="4"
            />
            <template v-if="poi.kind === 'abandoned_base'">
              <path
                class="abandoned-base-shell"
                :d="`M ${poi.map.x - 10} ${poi.map.y + 8} V ${poi.map.y - 5} H ${poi.map.x - 5} V ${poi.map.y - 1} H ${poi.map.x} V ${poi.map.y - 8} H ${poi.map.x + 8} V ${poi.map.y + 8} Z`"
              />
              <path
                class="abandoned-base-crack"
                :d="`M ${poi.map.x + 2} ${poi.map.y - 8} L ${poi.map.x - 1} ${poi.map.y - 2} L ${poi.map.x + 3} ${poi.map.y + 1} L ${poi.map.x} ${poi.map.y + 8}`"
              />
            </template>
            <template v-else>
              <circle class="poi-resource-ring" :cx="poi.map.x" :cy="poi.map.y" r="9" />
              <circle class="poi-resource-core" :cx="poi.map.x" :cy="poi.map.y" r="5.5" />
            </template>
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
            :transform="markerTranslateTransform(teleporter.map.x, teleporter.map.y)"
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
              self: player.self === true,
              active: selectedKey === player.unique_key,
              dimmed: isDimmed(player.unique_key),
            }"
            tabindex="0"
            role="button"
            :aria-pressed="selectedKey === player.unique_key"
            :aria-label="playerAriaLabel(player)"
            :transform="markerTranslateTransform(player.map.x, player.map.y)"
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

    <div
      v-if="loading"
      class="map-loading-bar"
      role="progressbar"
      :aria-label="ui.map.loading"
    />

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
    position: absolute;
    inset: 0;
    display: block;
    width: 100%;
    height: 100%;
    user-select: none;
}

.map-svg-base {
    z-index: 0;
    pointer-events: none;
}

.map-svg-entities {
    z-index: 2;
}

:deep(.map-marker.static-poi .poi-hitbox) {
    fill: transparent;
    stroke: none;
}

:deep(.map-marker.static-poi .static-poi-pin) {
    fill: var(--static-poi-color, #94a3b8);
    stroke: rgba(8, 14, 26, 0.85);
    stroke-width: 1.2;
    opacity: 0.9;
}

:deep(.map-marker.static-poi .static-poi-core) {
    fill: #0b1220;
}

:deep(.map-marker.static-poi:hover .static-poi-pin),
:deep(.map-marker.static-poi:focus-visible .static-poi-pin) {
    opacity: 1;
}

:deep(.map-marker.static-poi.active .static-poi-pin) {
    stroke: #22d3ee;
    stroke-width: 2;
    opacity: 1;
}

:deep(.base-map) {
    opacity: 0.92;
    pointer-events: none;
}

/* The cargo network is the headline feature but used to render as faint cyan
   scratches over the green terrain. Thicker, more opaque, and outlined with a
   dark halo so it reads at any zoom. */
:deep(.connection-line) {
    stroke: var(--line);
    stroke-width: 1.6px;
    stroke-opacity: 0.62;
    stroke-dasharray: 6 3;
    filter: drop-shadow(0 0 1.5px rgba(2, 8, 18, 0.95));
}

:deep(.connection-line.active) {
    stroke-opacity: 1;
    stroke-width: 2.2px;
    stroke-dasharray: none;
    filter: drop-shadow(0 0 4px var(--line));
}

:deep(.connection-line.muted) {
    stroke-opacity: 0.1;
    filter: none;
}

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

:deep(.map-marker.player.self path) {
    fill: var(--player-self);
    stroke: #fff4d6;
    filter: drop-shadow(0 0 5px var(--player-self));
}

:deep(.map-marker.poi .poi-hitbox) {
    fill: transparent;
    stroke: none;
    pointer-events: all;
}

:deep(.map-marker.poi.abandoned_base .abandoned-base-shell) {
    fill: #b9774c;
    stroke: #ffe1b5;
    stroke-width: 1.3;
    stroke-linejoin: round;
}

:deep(.map-marker.poi.abandoned_base .abandoned-base-crack) {
    fill: none;
    stroke: #442418;
    stroke-width: 1.5;
    stroke-linecap: round;
    stroke-linejoin: round;
}

:deep(.map-marker.poi:not(.abandoned_base) .poi-resource-ring) {
    fill: var(--poi-color);
    fill-opacity: 0.18;
    stroke: var(--poi-color);
    stroke-width: 1.2;
    stroke-opacity: 0.72;
}

:deep(.map-marker.poi:not(.abandoned_base) .poi-resource-core) {
    fill: var(--poi-color);
    stroke: #f7fbff;
    stroke-width: 1.2;
}

:deep(.map-marker.poi:not(.abandoned_base).depleted .poi-resource-ring) {
    fill-opacity: 0;
    stroke-opacity: 0.38;
    stroke-dasharray: 2 2;
}

:deep(.map-marker.poi:not(.abandoned_base).depleted .poi-resource-core) {
    fill-opacity: 0.12;
    stroke: var(--poi-color);
    stroke-width: 1.8;
    stroke-opacity: 0.58;
}

:deep(.map-marker.poi.active .poi-hitbox),
:deep(.map-marker.poi:focus-visible .poi-hitbox) {
    stroke: none;
    filter: none;
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
:deep(.map-marker.player.self.active path) { filter: drop-shadow(0 0 6px var(--player-self)); }
:deep(.map-marker.poi.abandoned_base.active .abandoned-base-shell) { filter: drop-shadow(0 0 7px #ffb36b); }
:deep(.map-marker.poi:not(.abandoned_base).active .poi-resource-core)  { filter: drop-shadow(0 0 7px var(--poi-color)); }
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

/* Centred rather than bottom-left: that corner is occupied by the filters
   sidebar and the map toolbar, which clipped this message exactly when it
   mattered most. */
.map-empty-state {
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    display: grid;
    gap: 6px;
    width: min(420px, calc(100% - 40px));
    text-align: center;
    justify-items: center;
}

.map-empty-state strong {
    font-size: 0.96rem;
}
</style>
