<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue';

import MapCanvas from './components/MapCanvas.vue';
import { fetchJson, normalizeEndpoint } from './lib/api';
import { clamp, formatClockSeconds, formatRelativeAge, formatWorld } from './lib/formatters';
import { applyLanguage, getMessages, resolveInitialLanguage } from './lang';
import type { Language } from './lang';
import type {
  CargoConnection,
  CargoMarker,
  CargoResponse,
  EntityToggleKey,
  EntityVisibility,
  HealthResponse,
  Player,
  RuptureCycleResponse,
  SelectedEntity,
  SidebarTab,
  Teleporter,
  ViewMode,
} from './lib/types';

type RupturePhaseKey = 'burning' | 'cooling' | 'stabilizing' | 'stable';

interface DetailRow {
  label: string;
  value: string;
}

interface RupturePhaseView {
  key: RupturePhaseKey;
  label: string;
  durationSeconds: number;
  startSeconds: number;
  endSeconds: number;
  widthPercent: number;
  active: boolean;
  statusLabel: string;
  toneClass: string;
}

interface RuptureTimelineTick {
  key: string;
  label: string;
  leftPercent: number;
}

const DEFAULT_ENDPOINT = 'http://127.0.0.1:9000';
const LIVE_REFRESH_MS = 2000;
const STORAGE_KEY = 'starrupture-mapview:v3';

const mapCanvasRef = ref<InstanceType<typeof MapCanvas> | null>(null);
const cargo = ref<CargoResponse | null>(null);
const health = ref<HealthResponse | null>(null);
const ruptureCycle = ref<RuptureCycleResponse | null>(null);
const endpoint = ref(DEFAULT_ENDPOINT);
const lang = ref<Language>(resolveInitialLanguage(STORAGE_KEY));
const showAllLinks = ref(true);
const highlightOrphans = ref(false);
const focusMode = ref(false);
const selectedKey = ref<string | null>(null);
const hoveredKey = ref<string | null>(null);
const autoRefresh = ref(true);
const lastUpdatedAt = ref(0);
const now = ref(Date.now());
const viewMode = ref<ViewMode>('network');
const sidebarTab = ref<SidebarTab>('entities');
const heroPanelCollapsed = ref(true);
const rupturePanelCollapsed = ref(true);
const selectionPanelCollapsed = ref(true);
const detailsPanelExpanded = ref(false);
const filtersPanelCollapsed = ref(true);
const filtersPanelExpanded = ref(false);
const legendPanelCollapsed = ref(true);
const shortcutsOpen = ref(false);

const entityVisibility = reactive<EntityVisibility>({
  sender: true,
  receiver: true,
  teleporter: true,
  player: true,
});

const status = reactive({
  loading: false,
  online: false,
  text: getMessages(lang.value).status.connecting,
  error: '',
});

let liveTimer: number | null = null;
let clockTimer: number | null = null;

const ui = computed(() => getMessages(lang.value));
const languageOptions: Language[] = ['en', 'fr'];

const sidebarTabs = computed<Array<{ key: SidebarTab; label: string }>>(() => [
  { key: 'entities', label: ui.value.tabs.entities },
  { key: 'stats', label: ui.value.tabs.stats },
]);

const entityToggleOptions = computed<Array<{ key: EntityToggleKey; label: string }>>(() => [
  { key: 'sender', label: ui.value.entityLabels.sender },
  { key: 'receiver', label: ui.value.entityLabels.receiver },
  { key: 'teleporter', label: ui.value.entityLabels.teleporter },
  { key: 'player', label: ui.value.entityLabels.player },
]);

const shortcutItems = computed(() => [
  { keys: ['?'], label: ui.value.shortcuts.items.help.label, description: ui.value.shortcuts.items.help.description },
  { keys: ['R'], label: ui.value.shortcuts.items.refresh.label, description: ui.value.shortcuts.items.refresh.description },
  { keys: ['L'], label: ui.value.shortcuts.items.live.label, description: ui.value.shortcuts.items.live.description },
  { keys: ['G'], label: ui.value.shortcuts.items.focus.label, description: ui.value.shortcuts.items.focus.description },
  { keys: ['E'], label: ui.value.shortcuts.items.details.label, description: ui.value.shortcuts.items.details.description },
  { keys: ['F'], label: ui.value.shortcuts.items.filters.label, description: ui.value.shortcuts.items.filters.description },
  { keys: ['S'], label: ui.value.shortcuts.items.stats.label, description: ui.value.shortcuts.items.stats.description },
  { keys: ['C'], label: ui.value.shortcuts.items.center.label, description: ui.value.shortcuts.items.center.description },
  { keys: ['0'], label: ui.value.shortcuts.items.reset.label, description: ui.value.shortcuts.items.reset.description },
  { keys: ['Esc'], label: ui.value.shortcuts.items.close.label, description: ui.value.shortcuts.items.close.description },
] as const);

function loadPreferences(): void {
  if (typeof localStorage === 'undefined') return;

  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return;

    const saved = JSON.parse(raw) as Partial<{
      endpoint: string;
      autoRefresh: boolean;
      showAllLinks: boolean;
      highlightOrphans: boolean;
      viewMode: ViewMode;
      lang: Language;
      entityVisibility: Partial<EntityVisibility>;
    }>;

    endpoint.value = saved.endpoint || DEFAULT_ENDPOINT;
    autoRefresh.value = saved.autoRefresh ?? true;
    showAllLinks.value = saved.showAllLinks ?? true;
    highlightOrphans.value = saved.highlightOrphans ?? false;
    viewMode.value = saved.viewMode || 'network';
    if (saved.lang === 'en' || saved.lang === 'fr') {
      lang.value = saved.lang;
    }
    entityVisibility.sender = saved.entityVisibility?.sender ?? true;
    entityVisibility.receiver = saved.entityVisibility?.receiver ?? true;
    entityVisibility.teleporter = saved.entityVisibility?.teleporter ?? true;
    entityVisibility.player = saved.entityVisibility?.player ?? true;
  } catch {
    endpoint.value = DEFAULT_ENDPOINT;
  }
}

function savePreferences(): void {
  if (typeof localStorage === 'undefined') return;

  localStorage.setItem(
    STORAGE_KEY,
    JSON.stringify({
      endpoint: endpoint.value,
      autoRefresh: autoRefresh.value,
      showAllLinks: showAllLinks.value,
      highlightOrphans: highlightOrphans.value,
      viewMode: viewMode.value,
      lang: lang.value,
      entityVisibility: { ...entityVisibility },
    }),
  );
}

watch(
  () => ({
    endpoint: endpoint.value,
    autoRefresh: autoRefresh.value,
    showAllLinks: showAllLinks.value,
    highlightOrphans: highlightOrphans.value,
    viewMode: viewMode.value,
    lang: lang.value,
    entityVisibility: { ...entityVisibility },
  }),
  savePreferences,
  { deep: true },
);

watch(
  lang,
  (value) => {
    applyLanguage(value);
    if (!status.loading && !cargo.value && !status.error) {
      status.text = getMessages(value).status.connecting;
    }
  },
  { immediate: true },
);

const normalizedEndpoint = computed(() => normalizeEndpoint(endpoint.value));
const isCargoViewMode = computed(
  () => viewMode.value === 'network' || viewMode.value === 'resources',
);

const allCargoMarkers = computed<CargoMarker[]>(() => cargo.value?.markers || []);
const allCargoConnections = computed<CargoConnection[]>(() => cargo.value?.connections || []);
const allTeleporters = computed<Teleporter[]>(() => cargo.value?.teleporters || []);
const allPlayers = computed<Player[]>(() => cargo.value?.players || []);

function isCargoMarkerAllowedByMode(marker: CargoMarker): boolean {
  if (!isCargoViewMode.value) return false;
  if (marker.kind === 'sender') return entityVisibility.sender;
  if (marker.kind === 'receiver') return entityVisibility.receiver;
  return true;
}

function cargoMarkerMatchesFilters(marker: CargoMarker): boolean {
  return isCargoMarkerAllowedByMode(marker);
}

const visibleCargoMarkers = computed(() => allCargoMarkers.value.filter(cargoMarkerMatchesFilters));

const renderableCargoConnections = computed(() => {
  if (!isCargoViewMode.value) return [];

  const visibleKeys = new Set(visibleCargoMarkers.value.map((marker) => marker.unique_key));

  return allCargoConnections.value.filter(
    (connection) =>
      visibleKeys.has(connection.sender_key) &&
      visibleKeys.has(connection.receiver_key),
  );
});

function getRelatedConnections(markerOrKey: CargoMarker | string | null): CargoConnection[] {
  const key = typeof markerOrKey === 'string' ? markerOrKey : markerOrKey?.unique_key;
  if (!key) return [];
  return renderableCargoConnections.value.filter(
    (connection) => connection.sender_key === key || connection.receiver_key === key,
  );
}

function teleporterMatchesFilters(): boolean {
  if (!entityVisibility.teleporter) return false;
  if (viewMode.value !== 'network' && viewMode.value !== 'teleporters') return false;
  return true;
}

const visibleTeleporters = computed(() => allTeleporters.value.filter(teleporterMatchesFilters));

function playerMatchesFilters(): boolean {
  if (!entityVisibility.player) return false;
  if (viewMode.value !== 'network' && viewMode.value !== 'players') return false;
  return true;
}

const visiblePlayers = computed(() => allPlayers.value.filter(playerMatchesFilters));

const visibleEntityKeys = computed(
  () =>
    new Set([
      ...visibleCargoMarkers.value.map((marker) => marker.unique_key),
      ...visibleTeleporters.value.map((entry) => entry.unique_key),
      ...visiblePlayers.value.map((entry) => entry.unique_key),
    ]),
);

const selectedEntity = computed<SelectedEntity | null>(() => {
  if (!selectedKey.value) return null;

  const cargoMarker = visibleCargoMarkers.value.find((marker) => marker.unique_key === selectedKey.value);
  if (cargoMarker) return { type: 'cargo', raw: cargoMarker };

  const teleporter = visibleTeleporters.value.find((entry) => entry.unique_key === selectedKey.value);
  if (teleporter) return { type: 'teleporter', raw: teleporter };

  const player = visiblePlayers.value.find((entry) => entry.unique_key === selectedKey.value);
  if (player) return { type: 'player', raw: player };

  return null;
});

const selectedCargo = computed(() => (selectedEntity.value?.type === 'cargo' ? selectedEntity.value.raw : null));
const selectedTeleporter = computed(() =>
  selectedEntity.value?.type === 'teleporter' ? selectedEntity.value.raw : null,
);
const selectedPlayer = computed(() => (selectedEntity.value?.type === 'player' ? selectedEntity.value.raw : null));

const hoveredCargoMarker = computed(() => {
  if (!hoveredKey.value) return null;

  return visibleCargoMarkers.value.find((marker) => marker.unique_key === hoveredKey.value) || null;
});

const orphanKeySet = computed(() => {
  if (!highlightOrphans.value || !isCargoViewMode.value) {
    return new Set<string>();
  }

  const linkedKeys = new Set<string>();
  renderableCargoConnections.value.forEach((connection) => {
    linkedKeys.add(connection.sender_key);
    linkedKeys.add(connection.receiver_key);
  });

  return new Set(
    visibleCargoMarkers.value
      .filter((marker) => !linkedKeys.has(marker.unique_key))
      .map((marker) => marker.unique_key),
  );
});

const focusKeys = computed(() => {
  const selection = selectedEntity.value;
  if (!selection) return new Set<string>();

  if (selection.type === 'cargo') {
    const keys = new Set<string>([selection.raw.unique_key]);
    getRelatedConnections(selection.raw.unique_key).forEach((connection) => {
      keys.add(connection.sender_key);
      keys.add(connection.receiver_key);
    });
    return keys;
  }

  return new Set<string>([selection.raw.unique_key]);
});

const canEnableFocusMode = computed(() => focusKeys.value.size > 0);

const focusCargoKey = computed(() => {
  if (selectedCargo.value) return selectedCargo.value.unique_key;
  return hoveredCargoMarker.value?.unique_key || null;
});

const displayedCargoMarkers = computed(() => {
  if (!focusMode.value || focusKeys.value.size === 0) return visibleCargoMarkers.value;
  return visibleCargoMarkers.value.filter((marker) => focusKeys.value.has(marker.unique_key));
});

const displayedTeleporters = computed(() => {
  if (!focusMode.value || focusKeys.value.size === 0) return visibleTeleporters.value;
  return visibleTeleporters.value.filter((teleporter) => focusKeys.value.has(teleporter.unique_key));
});

const displayedPlayers = computed(() => {
  if (!focusMode.value || focusKeys.value.size === 0) return visiblePlayers.value;
  return visiblePlayers.value.filter((player) => focusKeys.value.has(player.unique_key));
});

const visibleCargoConnections = computed(() => {
  const connections = renderableCargoConnections.value;
  if (!connections.length) return [];

  let nextConnections = connections;
  if (selectedCargo.value) {
    nextConnections = getRelatedConnections(selectedCargo.value.unique_key);
  } else if (viewMode.value === 'resources' || !showAllLinks.value) {
    nextConnections = hoveredCargoMarker.value ? getRelatedConnections(hoveredCargoMarker.value.unique_key) : [];
  }

  if (focusMode.value && focusKeys.value.size > 0) {
    return nextConnections.filter(
      (connection) =>
        focusKeys.value.has(connection.sender_key) &&
        focusKeys.value.has(connection.receiver_key),
    );
  }

  return nextConnections;
});

const totalCounts = computed(() => ({
  markers: cargo.value?.counts?.markers ?? health.value?.marker_count ?? allCargoMarkers.value.length,
  teleporters:
    cargo.value?.counts?.teleporters ??
    health.value?.teleporter_count ??
    allTeleporters.value.length,
  players: cargo.value?.counts?.players ?? health.value?.player_count ?? allPlayers.value.length,
}));

const filteredVisibleCount = computed(
  () =>
    visibleCargoMarkers.value.length +
    visibleTeleporters.value.length +
    visiblePlayers.value.length,
);

const activeFilterChips = computed(() => {
  const chips: string[] = [];

  if (viewMode.value !== 'network') {
    chips.push(ui.value.format.modeChip(ui.value.viewModes[viewMode.value]));
  }
  if (!showAllLinks.value) chips.push(ui.value.filters.linksFocus);
  if (highlightOrphans.value) chips.push(ui.value.filters.orphansVisible);
  if (focusMode.value && canEnableFocusMode.value) chips.push(ui.value.filters.focusOnly);

  const hidden = entityToggleOptions.value
    .filter((option) => !entityVisibility[option.key])
    .map((option) => option.label);
  if (hidden.length) chips.push(ui.value.format.hiddenLabels(hidden.join(', ')));

  return chips;
});

const liveAgeLabel = computed(() =>
  formatRelativeAge(
    lastUpdatedAt.value,
    now.value,
    ui.value.locale,
    ui.value.status.lastUpdatedPrefix,
    ui.value.status.lastUpdatedMissing,
  ),
);
const mapMetaLabel = computed(() => {
  if (!cargo.value) return ui.value.status.connecting;

  return ui.value.format.mapMeta(
    formatWorld(cargo.value.world, ui.value.selection.world),
    cargo.value.generation,
    ui.value.viewModes[viewMode.value],
  );
});

const statusTone = computed(() => {
  if (status.loading) return 'loading';
  if (status.online) return 'online';
  return cargo.value ? 'stale' : 'offline';
});
const statusBadgeLabel = computed(() => {
  if (status.loading) return ui.value.status.sync;
  if (status.online) return ui.value.format.liveAge(liveAgeLabel.value);
  if (cargo.value) return ui.value.format.cacheAge(liveAgeLabel.value);
  return ui.value.status.offline;
});

const selectedConnections = computed(() => (selectedCargo.value ? getRelatedConnections(selectedCargo.value) : []));
const selectedCargoIsOrphan = computed(
  () => !!selectedCargo.value && orphanKeySet.value.has(selectedCargo.value.unique_key),
);

function formatEntityType(type: 'sender' | 'receiver' | 'teleporter' | 'player'): string {
  if (type === 'sender') return ui.value.map.senderLabel;
  if (type === 'receiver') return ui.value.map.receiverLabel;
  if (type === 'teleporter') return ui.value.selection.teleporterFallback;
  return ui.value.selection.playerFallback;
}

const selectedEntitySummary = computed(() => {
  if (selectedCargo.value) {
    return ui.value.format.selectedSummaryCargo(
      formatEntityType(selectedCargo.value.kind),
      selectedConnections.value.length,
    );
  }
  if (selectedTeleporter.value) return ui.value.selection.teleporterFallback;
  if (selectedPlayer.value) return ui.value.selection.playerFallback;
  return ui.value.selection.summaryNone;
});
const selectedDisplayName = computed(() => {
  if (selectedCargo.value) {
    return selectedCargo.value.label || selectedCargo.value.display_name || ui.value.selection.cargoFallback;
  }
  if (selectedTeleporter.value) return selectedTeleporter.value.label || ui.value.selection.teleporterFallback;
  if (selectedPlayer.value) return selectedPlayer.value.label || ui.value.selection.playerFallback;
  return ui.value.selection.summaryNone;
});
const selectedDetailRows = computed<DetailRow[]>(() => {
  if (selectedCargo.value) {
    const rows: DetailRow[] = [
      { label: ui.value.selection.type, value: formatEntityType(selectedCargo.value.kind) },
      { label: ui.value.selection.resource, value: selectedCargo.value.resource || '--' },
      {
        label: ui.value.selection.network,
        value: selectedConnections.value.length
          ? ui.value.format.networkConnections(selectedConnections.value.length)
          : ui.value.selection.noVisibleConnection,
      },
    ];

    if (
      selectedCargo.value.display_name &&
      selectedCargo.value.label &&
      selectedCargo.value.display_name !== selectedCargo.value.label
    ) {
      rows.push({ label: ui.value.selection.name, value: selectedCargo.value.display_name });
    }

    if (selectedCargoIsOrphan.value) {
      rows.push({ label: ui.value.selection.state, value: ui.value.selection.orphan });
    }

    return rows;
  }

  if (selectedTeleporter.value) {
    return [
      { label: ui.value.selection.type, value: ui.value.selection.teleporterFallback },
      { label: ui.value.selection.source, value: selectedTeleporter.value.source || '--' },
    ];
  }

  if (selectedPlayer.value) {
    return [
      { label: ui.value.selection.type, value: ui.value.selection.playerFallback },
      { label: ui.value.selection.source, value: selectedPlayer.value.source || '--' },
    ];
  }

  return [];
});
const selectedPreviewFacts = computed(() => selectedDetailRows.value.slice(0, 2));
const statsOverview = computed<DetailRow[]>(() => [
  { label: ui.value.selection.endpoint, value: normalizedEndpoint.value || '--' },
  { label: ui.value.selection.world, value: formatWorld(cargo.value?.world, ui.value.selection.world) },
  { label: ui.value.selection.snapshot, value: String(cargo.value?.generation ?? '--') },
  { label: ui.value.selection.polling, value: autoRefresh.value ? ui.value.status.pollingOn : ui.value.status.pollingOff },
  { label: ui.value.selection.afterFilters, value: String(filteredVisibleCount.value) },
  { label: ui.value.selection.lastUpdate, value: liveAgeLabel.value },
]);

const ruptureState = computed(() => ruptureCycle.value?.rupture_cycle ?? null);
const ruptureTimeline = computed(() => ruptureCycle.value?.timeline ?? null);
const ruptureCycleTotalSeconds = computed(() => ruptureTimeline.value?.cycle_total_seconds ?? 3240);
const rupturePhaseDurations = computed(() => ({
  burning: ruptureTimeline.value?.phase_seconds?.burning ?? 30,
  cooling: ruptureTimeline.value?.phase_seconds?.cooling ?? 60,
  stabilizing: ruptureTimeline.value?.phase_seconds?.stabilizing ?? 600,
  stable: ruptureTimeline.value?.phase_seconds?.stable ?? 2550,
}));

const ruptureElapsedSeconds = computed(() => {
  const value = ruptureState.value?.elapsed_seconds;
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
});

const ruptureObservedAtUnixMs = computed(() => {
  const value = ruptureState.value?.observed_at_unix_ms;
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
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

function mapRuptureStageToPhaseKey(stage: string, step: string): RupturePhaseKey | null {
  if (stage === 'Moving') return 'burning';
  if (stage === 'Fadeout') {
    return step === 'FireWave' ? 'burning' : 'cooling';
  }
  if (stage === 'Growback') return 'stabilizing';
  if (stage === 'PreWave') return 'stable';
  return null;
}

function mapRupturePositionToPhaseKey(positionSeconds: number, durations: typeof rupturePhaseDurations.value): RupturePhaseKey {
  const burningEnd = durations.burning;
  const coolingEnd = burningEnd + durations.cooling;
  const stabilizingEnd = coolingEnd + durations.stabilizing;

  if (positionSeconds < burningEnd) return 'burning';
  if (positionSeconds < coolingEnd) return 'cooling';
  if (positionSeconds < stabilizingEnd) return 'stabilizing';
  return 'stable';
}

const ruptureCyclePositionSeconds = computed(() => {
  const elapsed = ruptureLiveElapsedSeconds.value;
  const total = ruptureCycleTotalSeconds.value;
  if (elapsed == null || total <= 0) return null;

  const normalized = elapsed % total;
  return normalized >= 0 ? normalized : (normalized + total);
});

const ruptureCurrentPhaseKey = computed<RupturePhaseKey>(() => {
  const stage = ruptureState.value?.stage ?? 'None';
  const step = ruptureState.value?.step ?? 'None';
  const phaseFromStage = mapRuptureStageToPhaseKey(stage, step);
  if (phaseFromStage) return phaseFromStage;

  const positionSeconds = ruptureCyclePositionSeconds.value;
  if (positionSeconds != null) {
    return mapRupturePositionToPhaseKey(positionSeconds, rupturePhaseDurations.value);
  }

  return 'stable';
});

const ruptureCurrentPhaseLabel = computed(() => ui.value.rupture.phases[ruptureCurrentPhaseKey.value]);
const ruptureCurrentWaveLabel = computed(() => ruptureState.value?.wave || '--');
const ruptureCurrentStageLabel = computed(() => ruptureState.value?.stage || '--');
const ruptureCurrentStepLabel = computed(() => ruptureState.value?.step || '--');
const ruptureElapsedLabel = computed(() => formatClockSeconds(ruptureLiveElapsedSeconds.value));

const ruptureMarkerSeconds = computed(() => {
  const positionSeconds = ruptureCyclePositionSeconds.value;
  const cycleEnd = ruptureCycleTotalSeconds.value;
  if (positionSeconds == null || cycleEnd <= 0) return null;
  return clamp(positionSeconds, 0, cycleEnd);
});

const ruptureMarkerPercent = computed(() => {
  const marker = ruptureMarkerSeconds.value;
  const total = ruptureCycleTotalSeconds.value;
  if (marker == null || total <= 0) return null;
  return (marker / total) * 100;
});

const ruptureHasLiveData = computed(() => ruptureMarkerPercent.value !== null);

const ruptureTimelineTicks = computed<RuptureTimelineTick[]>(() => {
  const durations = rupturePhaseDurations.value;
  const total = ruptureCycleTotalSeconds.value || 1;
  const boundaries = [
    { key: 'start', seconds: 0 },
    { key: 'burning-end', seconds: durations.burning },
    { key: 'cooling-end', seconds: durations.burning + durations.cooling },
    { key: 'stabilizing-end', seconds: durations.burning + durations.cooling + durations.stabilizing },
    { key: 'end', seconds: total },
  ];

  return boundaries.map((boundary) => ({
    key: boundary.key,
    label: formatClockSeconds(boundary.seconds),
    leftPercent: (boundary.seconds / total) * 100,
  }));
});

const rupturePhases = computed<RupturePhaseView[]>(() => {
  const durations = rupturePhaseDurations.value;
  const phaseDefs: Array<{ key: RupturePhaseKey; toneClass: string }> = [
    { key: 'burning', toneClass: 'burning' },
    { key: 'cooling', toneClass: 'cooling' },
    { key: 'stabilizing', toneClass: 'stabilizing' },
    { key: 'stable', toneClass: 'stable' },
  ];

  const marker = ruptureMarkerSeconds.value ?? 0;
  const total = ruptureCycleTotalSeconds.value || 1;
  let cursor = 0;

  return phaseDefs.map((phase) => {
    const durationSeconds = durations[phase.key];
    const startSeconds = cursor;
    const endSeconds = startSeconds + durationSeconds;
    cursor = endSeconds;

    let statusLabel = ui.value.rupture.completed;
    if (ruptureMarkerSeconds.value == null) {
      statusLabel = ui.value.rupture.noDataShort;
    } else if (ruptureCurrentPhaseKey.value === phase.key) {
      statusLabel = ui.value.rupture.format.endsIn(formatClockSeconds(Math.max(0, endSeconds - marker)));
    } else if (marker < startSeconds) {
      statusLabel = ui.value.rupture.format.startsIn(formatClockSeconds(Math.max(0, startSeconds - marker)));
    }

    return {
      key: phase.key,
      label: ui.value.rupture.phases[phase.key],
      durationSeconds,
      startSeconds,
      endSeconds,
      widthPercent: (durationSeconds / total) * 100,
      active: ruptureCurrentPhaseKey.value === phase.key,
      statusLabel,
      toneClass: phase.toneClass,
    };
  });
});

function setStatus(online: boolean, text: string, error = ''): void {
  status.online = online;
  status.text = text;
  status.error = error;
}

async function refreshData(): Promise<void> {
  const currentEndpoint = normalizedEndpoint.value;
  if (!currentEndpoint) {
    cargo.value = null;
    health.value = null;
    ruptureCycle.value = null;
    lastUpdatedAt.value = 0;
    setStatus(false, ui.value.status.invalidEndpoint, ui.value.status.invalidEndpointHelp);
    return;
  }

  if (status.loading) return;

  status.loading = true;
  setStatus(false, ui.value.status.contactingPlugin, '');

  try {
    const [nextHealth, nextCargo, nextRuptureCycle] = await Promise.all([
      fetchJson<HealthResponse>(currentEndpoint, '/health'),
      fetchJson<CargoResponse>(currentEndpoint, '/cargo'),
      fetchJson<RuptureCycleResponse>(currentEndpoint, '/rupture-cycle').catch(() => null),
    ]);

    health.value = nextHealth;
    cargo.value = nextCargo;
    ruptureCycle.value = nextRuptureCycle;
    lastUpdatedAt.value = Date.now();
    setStatus(
      true,
      ui.value.format.countsSummary(
        nextHealth.marker_count,
        nextHealth.teleporter_count ?? 0,
        nextHealth.player_count ?? 0,
      ),
    );
  } catch (error) {
    const message = error instanceof Error ? error.message : 'Unknown error';
    if (!cargo.value) {
      health.value = null;
      lastUpdatedAt.value = 0;
    }
    setStatus(
      false,
      ui.value.format.endpointUnavailable(currentEndpoint),
      cargo.value
        ? ui.value.format.displayPreserved(message)
        : ui.value.format.fetchFailed(message),
    );
  } finally {
    status.loading = false;
  }
}

function updateAutoRefresh(): void {
  if (liveTimer !== null) {
    window.clearInterval(liveTimer);
    liveTimer = null;
  }

  if (autoRefresh.value) {
    liveTimer = window.setInterval(() => {
      void refreshData();
    }, LIVE_REFRESH_MS);
  }
}

function clearFilters(): void {
  showAllLinks.value = true;
  highlightOrphans.value = false;
  focusMode.value = false;
  viewMode.value = 'network';
  entityVisibility.sender = true;
  entityVisibility.receiver = true;
  entityVisibility.teleporter = true;
  entityVisibility.player = true;
}

function clearSelection(): void {
  selectedKey.value = null;
  hoveredKey.value = null;
  focusMode.value = false;
  detailsPanelExpanded.value = false;
  selectionPanelCollapsed.value = true;
}

function toggleEntity(key: EntityToggleKey): void {
  entityVisibility[key] = !entityVisibility[key];
}

function selectEntity(key: string): void {
  selectedKey.value = key;
  sidebarTab.value = 'entities';
  selectionPanelCollapsed.value = false;
}

function resetMapView(): void {
  mapCanvasRef.value?.resetView();
}

function centerSelection(): void {
  mapCanvasRef.value?.focusSelection();
}

function toggleFocusMode(): void {
  if (!canEnableFocusMode.value) return;
  focusMode.value = !focusMode.value;
}

function openPanel(tab?: SidebarTab): void {
  selectionPanelCollapsed.value = false;
  detailsPanelExpanded.value = true;
  if (tab) sidebarTab.value = tab;
}

function toggleHeroPanel(): void {
  heroPanelCollapsed.value = !heroPanelCollapsed.value;
}

function toggleRupturePanel(): void {
  rupturePanelCollapsed.value = !rupturePanelCollapsed.value;
}

function toggleSelectionPanel(): void {
  selectionPanelCollapsed.value = !selectionPanelCollapsed.value;
}

function toggleDetailsPanel(): void {
  selectionPanelCollapsed.value = false;
  sidebarTab.value = 'entities';
  detailsPanelExpanded.value = !detailsPanelExpanded.value;
}

function toggleFiltersPanel(): void {
  filtersPanelCollapsed.value = !filtersPanelCollapsed.value;
  if (filtersPanelCollapsed.value) {
    filtersPanelExpanded.value = false;
  }
}

function openFiltersPanel(expanded = false): void {
  filtersPanelCollapsed.value = false;
  filtersPanelExpanded.value = expanded;
}

function toggleFiltersExpanded(): void {
  if (filtersPanelCollapsed.value) {
    filtersPanelCollapsed.value = false;
    filtersPanelExpanded.value = true;
    return;
  }

  filtersPanelExpanded.value = !filtersPanelExpanded.value;
}

function toggleLegendPanel(): void {
  legendPanelCollapsed.value = !legendPanelCollapsed.value;
}

function isTypingTarget(target: EventTarget | null): boolean {
  const element = target as HTMLElement | null;
  if (!element) return false;
  const tagName = element.tagName;
  return (
    tagName === 'INPUT' ||
    tagName === 'TEXTAREA' ||
    tagName === 'SELECT' ||
    element.isContentEditable
  );
}

function handleKeydown(event: KeyboardEvent): void {
  if (event.metaKey || event.ctrlKey || event.altKey) return;

  if (event.key === 'Escape') {
    if (shortcutsOpen.value) {
      shortcutsOpen.value = false;
      return;
    }
    if (selectedKey.value) {
      clearSelection();
      return;
    }
    if (detailsPanelExpanded.value) {
      detailsPanelExpanded.value = false;
    }
    return;
  }

  if (event.key === '?') {
    event.preventDefault();
    shortcutsOpen.value = !shortcutsOpen.value;
    return;
  }

  if (isTypingTarget(event.target)) return;

  switch (event.key) {
    case '0':
      event.preventDefault();
      resetMapView();
      return;
  }

  switch (event.key.toLowerCase()) {
    case 'r':
      event.preventDefault();
      void refreshData();
      return;
    case 'l':
      event.preventDefault();
      autoRefresh.value = !autoRefresh.value;
      return;
    case 'g':
      event.preventDefault();
      toggleFocusMode();
      return;
    case 'e':
      event.preventDefault();
      openPanel('entities');
      return;
    case 'f':
      event.preventDefault();
      openFiltersPanel(true);
      return;
    case 's':
      event.preventDefault();
      openPanel('stats');
      return;
    case 'c':
      event.preventDefault();
      centerSelection();
      return;
    default:
      return;
  }
}

watch(autoRefresh, updateAutoRefresh, { immediate: true });
watch(
  [selectedKey, visibleEntityKeys],
  ([currentKey, keys]) => {
    if (currentKey && !keys.has(currentKey)) {
      clearSelection();
    }
  },
  { immediate: true },
);
watch(
  () => selectedEntity.value,
  (value) => {
    if (!value) {
      focusMode.value = false;
    }
  },
);

onMounted(() => {
  loadPreferences();
  updateAutoRefresh();
  clockTimer = window.setInterval(() => {
    now.value = Date.now();
  }, 1000);
  window.addEventListener('keydown', handleKeydown);
  void refreshData();
});

onBeforeUnmount(() => {
  if (liveTimer !== null) window.clearInterval(liveTimer);
  if (clockTimer !== null) window.clearInterval(clockTimer);
  window.removeEventListener('keydown', handleKeydown);
});
</script>

<template>
  <div class="app-shell">
    <section class="card map-stage">
      <MapCanvas
        ref="mapCanvasRef"
        :cargo="cargo"
        :cargo-markers="displayedCargoMarkers"
        :cargo-connections="visibleCargoConnections"
        :teleporters="displayedTeleporters"
        :players="displayedPlayers"
        :selected-key="selectedKey"
        :selected-entity="selectedEntity"
        :orphan-keys="Array.from(orphanKeySet)"
        :focus-keys="Array.from(focusKeys)"
        :focus-cargo-key="focusCargoKey"
        :lang="lang"
        @select="selectEntity"
        @clear-selection="clearSelection"
        @hover="hoveredKey = $event"
      />

      <div class="overlay-layer overlay-top-left">
        <button
          v-if="heroPanelCollapsed"
          class="drawer-handle drawer-handle-top-left"
          type="button"
          :aria-expanded="!heroPanelCollapsed"
          @click="toggleHeroPanel"
        >
          {{ ui.handles.controls }}
        </button>
        <section class="floating-panel hero-panel" :class="{ collapsed: heroPanelCollapsed }">
          <div class="panel-top-row">
            <div>
              <span class="eyebrow">{{ ui.hero.eyebrow }}</span>
              <h1>{{ ui.hero.title }}</h1>
              <p>{{ mapMetaLabel }}</p>
            </div>
            <div class="panel-top-actions hero-actions">
              <div class="hero-meta">
                <span class="status-inline" :class="statusTone">
                  <span class="status-dot small"></span>
                  {{ statusBadgeLabel }}
                </span>
              </div>
              <button
                v-if="!heroPanelCollapsed"
                class="button subtle small"
                type="button"
                @click="toggleHeroPanel"
              >
                {{ ui.buttons.collapse }}
              </button>
            </div>
          </div>

          <div class="drawer-body hero-body">
            <div class="floating-fields">
              <label class="field compact endpoint-field">
                <span>{{ ui.hero.endpoint }}</span>
                <input
                  v-model="endpoint"
                  type="text"
                  :placeholder="DEFAULT_ENDPOINT"
                  spellcheck="false"
                />
              </label>
              <label class="field compact language-field">
                <span>{{ ui.languageLabel }}</span>
                <select v-model="lang">
                  <option v-for="option in languageOptions" :key="option" :value="option">
                    {{ ui.languages[option] }}
                  </option>
                </select>
              </label>
            </div>

            <div class="floating-actions">
              <button class="button primary" type="button" :disabled="status.loading" @click="refreshData">
                {{ status.loading ? ui.buttons.refreshing : ui.buttons.refresh }}
              </button>
              <button class="button subtle" type="button" @click="autoRefresh = !autoRefresh">
                {{ autoRefresh ? ui.buttons.live : ui.buttons.pause }}
              </button>
              <button class="button subtle" type="button" @click="resetMapView">
                {{ ui.buttons.recenter }}
              </button>
              <button class="button subtle" type="button" @click="shortcutsOpen = true">
                {{ ui.buttons.help }}
              </button>
            </div>
          </div>
        </section>
      </div>

      <div class="overlay-layer overlay-top-center">
        <button
          v-if="rupturePanelCollapsed"
          class="drawer-handle drawer-handle-top-center"
          type="button"
          :aria-expanded="!rupturePanelCollapsed"
          @click="toggleRupturePanel"
        >
          {{ ui.handles.timeline }}
        </button>
        <section class="floating-panel timeline-panel" :class="{ collapsed: rupturePanelCollapsed }">
          <div class="panel-top-row">
            <div>
              <span class="eyebrow">{{ ui.handles.timeline }}</span>
              <h2>{{ ui.rupture.title }}</h2>
              <p>{{ ui.rupture.subtitle }}</p>
            </div>
            <div class="panel-top-actions">
              <span class="status-inline rupture-phase-badge" :class="ruptureCurrentPhaseKey">
                {{ ui.rupture.currentPhase }}: {{ ruptureCurrentPhaseLabel }}
              </span>
              <button
                v-if="!rupturePanelCollapsed"
                class="button subtle small"
                type="button"
                @click="toggleRupturePanel"
              >
                {{ ui.buttons.collapse }}
              </button>
            </div>
          </div>

          <div class="drawer-body timeline-body">
            <div class="rupture-summary-grid">
              <div class="fact-card">
                <strong>{{ ui.rupture.wave }}</strong>
                <span>{{ ruptureCurrentWaveLabel }}</span>
              </div>
              <div class="fact-card">
                <strong>{{ ui.rupture.stage }}</strong>
                <span>{{ ruptureCurrentStageLabel }}</span>
              </div>
              <div class="fact-card">
                <strong>{{ ui.rupture.step }}</strong>
                <span>{{ ruptureCurrentStepLabel }}</span>
              </div>
              <div class="fact-card">
                <strong>{{ ui.rupture.elapsed }}</strong>
                <span>{{ ruptureElapsedLabel }}</span>
              </div>
            </div>

            <section class="rupture-panel">
              <div class="rupture-timeline">
                <div class="rupture-track">
                  <div
                    v-for="phase in rupturePhases"
                    :key="phase.key"
                    class="rupture-segment"
                    :class="[phase.toneClass, { active: phase.active }]"
                    :style="{ width: `${phase.widthPercent}%` }"
                  ></div>
                  <div v-if="ruptureMarkerPercent !== null" class="rupture-marker" :style="{ left: `${ruptureMarkerPercent}%` }">
                    <span>{{ formatClockSeconds(ruptureMarkerSeconds) }}</span>
                  </div>
                </div>
                <div class="rupture-track-scale">
                  <span
                    v-for="tick in ruptureTimelineTicks"
                    :key="tick.key"
                    class="rupture-track-tick"
                    :style="{ left: `${tick.leftPercent}%` }"
                  >
                    {{ tick.label }}
                  </span>
                </div>
                <div class="rupture-track-legend">
                  <span
                    v-for="phase in rupturePhases"
                    :key="`${phase.key}-legend`"
                    class="rupture-track-legend-item"
                  >
                    <span class="rupture-track-swatch" :class="phase.toneClass"></span>
                    {{ phase.label }}
                  </span>
                </div>
              </div>
              <div v-if="!ruptureHasLiveData" class="empty-state compact-empty">
                {{ ui.rupture.noData }}
              </div>

              <div class="rupture-cards">
                <article v-for="phase in rupturePhases" :key="phase.key" class="rupture-card" :class="[phase.toneClass, { active: phase.active }]">
                  <div class="rupture-card-header">
                    <strong>{{ phase.label }}</strong>
                    <span>{{ phase.statusLabel }}</span>
                  </div>
                  <div class="details-grid compact-grid">
                    <div>
                      <strong>{{ ui.rupture.duration }}</strong>
                      <span>{{ formatClockSeconds(phase.durationSeconds) }}</span>
                    </div>
                    <div>
                      <strong>{{ ui.rupture.start }}</strong>
                      <span>{{ formatClockSeconds(phase.startSeconds) }}</span>
                    </div>
                    <div>
                      <strong>{{ ui.rupture.end }}</strong>
                      <span>{{ formatClockSeconds(phase.endSeconds) }}</span>
                    </div>
                    <div>
                      <strong>{{ ui.rupture.status }}</strong>
                      <span>{{ phase.statusLabel }}</span>
                    </div>
                  </div>
                </article>
              </div>
            </section>
          </div>
        </section>
      </div>

      <div class="overlay-layer overlay-top-right">
        <button
          v-if="selectionPanelCollapsed"
          class="drawer-handle drawer-handle-right"
          type="button"
          :aria-expanded="!selectionPanelCollapsed"
          @click="toggleSelectionPanel"
        >
          {{ ui.handles.selection }}
        </button>
        <section
          class="floating-panel selection-panel"
          :class="{ expanded: detailsPanelExpanded, collapsed: selectionPanelCollapsed }"
        >
          <div class="panel-top-row compact">
            <div>
              <span class="panel-kicker">{{ ui.handles.selection }}</span>
              <h2>{{ selectedDisplayName }}</h2>
              <p>{{ selectedEntitySummary }}</p>
            </div>
            <div class="panel-top-actions">
              <button class="button subtle small" type="button" @click="toggleDetailsPanel">
                {{ detailsPanelExpanded ? ui.buttons.hideDetails : ui.buttons.details }}
              </button>
              <button
                v-if="!selectionPanelCollapsed"
                class="button subtle small"
                type="button"
                @click="toggleSelectionPanel"
              >
                {{ ui.buttons.collapse }}
              </button>
            </div>
          </div>

          <div class="drawer-body selection-body">
            <div v-if="selectedPreviewFacts.length" class="selection-facts">
              <div v-for="item in selectedPreviewFacts" :key="item.label" class="fact-card">
                <strong>{{ item.label }}</strong>
                <span>{{ item.value }}</span>
              </div>
            </div>
            <div v-else class="empty-state compact-empty">
              {{ ui.selection.clickElement }}
            </div>

            <div class="selection-actions">
              <button class="button subtle small" type="button" :disabled="!selectedEntity" @click="centerSelection">
                {{ ui.buttons.center }}
              </button>
              <button
                class="button subtle small"
                type="button"
                :disabled="!canEnableFocusMode"
                @click="toggleFocusMode"
              >
                {{ focusMode ? ui.buttons.showAll : ui.buttons.focusSelection }}
              </button>
              <button class="button subtle small" type="button" :disabled="!selectedEntity" @click="clearSelection">
                {{ ui.buttons.deselect }}
              </button>
              <button class="button subtle small" type="button" :disabled="!selectedEntity" @click="toggleDetailsPanel">
                {{ detailsPanelExpanded ? ui.buttons.lessInfo : ui.buttons.moreInfo }}
              </button>
            </div>

            <div v-if="detailsPanelExpanded" class="secondary-panel">
              <div class="side-tabs">
                <button
                  v-for="tab in sidebarTabs"
                  :key="tab.key"
                  class="tab-button"
                  :class="{ active: sidebarTab === tab.key }"
                  type="button"
                  @click="sidebarTab = tab.key"
                >
                  {{ tab.label }}
                </button>
              </div>

              <div v-if="sidebarTab === 'entities'" class="panel-section">
                <div class="section-header-row">
                  <div>
                    <h3>{{ ui.selection.detailsTitle }}</h3>
                    <p v-if="selectedEntity">{{ ui.selection.detailsCurrent }}</p>
                    <p v-else>{{ ui.selection.detailsEmpty }}</p>
                  </div>
                </div>

                <div v-if="selectedDetailRows.length" class="details-grid compact-grid selection-details-grid">
                  <div v-for="item in selectedDetailRows" :key="item.label">
                    <strong>{{ item.label }}</strong>
                    <span>{{ item.value }}</span>
                  </div>
                </div>
                <div v-else class="empty-state compact-empty">
                  {{ ui.selection.detailsHelp }}
                </div>
              </div>
              <div v-else class="panel-section">
                <div class="section-header-row">
                  <div>
                    <h3>{{ ui.selection.observabilityTitle }}</h3>
                    <p>{{ ui.selection.observabilityHelp }}</p>
                  </div>
                  <button class="button subtle small" type="button" @click="shortcutsOpen = true">
                    {{ ui.buttons.viewShortcuts }}
                  </button>
                </div>

                <div class="stats-pills">
                  <span class="stat-pill">
                    <strong>{{ totalCounts.markers }}</strong>
                    <span>{{ ui.selection.totalCargo }}</span>
                  </span>
                  <span class="stat-pill">
                    <strong>{{ totalCounts.teleporters }}</strong>
                    <span>{{ ui.selection.totalTeleporters }}</span>
                  </span>
                  <span class="stat-pill">
                    <strong>{{ totalCounts.players }}</strong>
                    <span>{{ ui.selection.totalPlayers }}</span>
                  </span>
                  <span class="stat-pill">
                    <strong>{{ visibleCargoConnections.length }}</strong>
                    <span>{{ ui.selection.drawnLinks }}</span>
                  </span>
                </div>

                <div class="details-grid compact-grid">
                  <div v-for="item in statsOverview" :key="item.label">
                    <strong>{{ item.label }}</strong>
                    <span>{{ item.value }}</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </section>
      </div>

      <div
        class="overlay-layer overlay-bottom-left"
        :class="{ expanded: filtersPanelExpanded && !filtersPanelCollapsed }"
      >
        <button
          v-if="filtersPanelCollapsed"
          class="drawer-handle drawer-handle-bottom-left"
          type="button"
          :aria-expanded="!filtersPanelCollapsed"
          @click="toggleFiltersPanel"
        >
          {{ ui.handles.filters }}
        </button>
        <section
          class="floating-panel footer-panel filters-panel"
          :class="{ collapsed: filtersPanelCollapsed, expanded: filtersPanelExpanded }"
        >
          <div class="panel-top-row compact">
            <div>
              <span class="panel-kicker">{{ ui.handles.filters }}</span>
              <h2>{{ filtersPanelExpanded ? ui.filters.advancedTitle : ui.filters.activeTitle }}</h2>
              <p>{{ activeFilterChips.length ? ui.format.activeFilterCount(activeFilterChips.length) : ui.filters.none }}</p>
            </div>
            <div class="panel-top-actions">
              <button class="button subtle small" type="button" @click="toggleFiltersExpanded">
                {{ filtersPanelExpanded ? ui.buttons.compact : ui.buttons.advanced }}
              </button>
              <button
                v-if="!filtersPanelCollapsed"
                class="button subtle small"
                type="button"
                @click="toggleFiltersPanel"
              >
                {{ ui.buttons.collapse }}
              </button>
            </div>
          </div>

          <div class="drawer-body footer-panel-body">
            <div class="active-filters" :class="{ empty: activeFilterChips.length === 0 }">
              <template v-if="activeFilterChips.length">
                <span v-for="chip in activeFilterChips" :key="chip" class="filter-chip">
                  {{ chip }}
                </span>
              </template>
              <span v-else>{{ ui.filters.noneActive }}</span>
            </div>

            <div class="floating-actions footer-actions">
              <button class="button subtle small" type="button" @click="clearFilters">
                {{ ui.buttons.clear }}
              </button>
            </div>

            <div v-if="filtersPanelExpanded" class="secondary-panel filters-advanced-panel">
              <div class="panel-section">
                <div class="section-header-row">
                  <div>
                    <h3>{{ ui.filters.visibilityTitle }}</h3>
                    <p>{{ ui.filters.visibilityHelp }}</p>
                  </div>
                  <button class="button subtle small" type="button" @click="clearFilters">
                    {{ ui.buttons.reset }}
                  </button>
                </div>

                <div class="chip-group secondary-chip-group">
                  <button
                    v-for="option in entityToggleOptions"
                    :key="option.key"
                    class="chip-button"
                    :class="{ active: entityVisibility[option.key], muted: !entityVisibility[option.key] }"
                    type="button"
                    @click="toggleEntity(option.key)"
                  >
                    {{ option.label }}
                  </button>
                </div>
              </div>

              <div class="panel-section">
                <div class="section-header-row">
                  <div>
                    <h3>{{ ui.filters.behaviorTitle }}</h3>
                    <p>{{ ui.filters.behaviorHelp }}</p>
                  </div>
                </div>

                <div class="toggle-grid">
                  <label class="toggle-line">
                    <input v-model="showAllLinks" type="checkbox" />
                    <span>{{ ui.filters.showAllLinks }}</span>
                  </label>
                  <label class="toggle-line">
                    <input v-model="highlightOrphans" type="checkbox" />
                    <span>{{ ui.filters.highlightOrphans }}</span>
                  </label>
                </div>
              </div>
            </div>
          </div>
        </section>
      </div>

      <div class="overlay-layer overlay-bottom-right">
        <button
          v-if="legendPanelCollapsed"
          class="drawer-handle drawer-handle-bottom-right"
          type="button"
          :aria-expanded="!legendPanelCollapsed"
          @click="toggleLegendPanel"
        >
          {{ ui.handles.legend }}
        </button>
        <section class="floating-panel footer-panel legend-panel" :class="{ collapsed: legendPanelCollapsed }">
          <div class="panel-top-row compact">
            <div>
              <span class="panel-kicker">{{ ui.handles.legend }}</span>
              <h2>{{ ui.legend.title }}</h2>
            </div>
            <div class="panel-top-actions">
              <button
                v-if="!legendPanelCollapsed"
                class="button subtle small"
                type="button"
                @click="toggleLegendPanel"
              >
                {{ ui.buttons.collapse }}
              </button>
            </div>
          </div>

          <div class="drawer-body legend-panel-body">
            <div class="legend-row">
              <span class="legend-item"><span class="legend-swatch sender"></span> {{ ui.entityLabels.sender }}</span>
              <span class="legend-item"><span class="legend-swatch receiver"></span> {{ ui.entityLabels.receiver }}</span>
              <span class="legend-item"><span class="legend-swatch teleporter"></span> {{ ui.selection.teleporterFallback }}</span>
              <span class="legend-item"><span class="legend-swatch player"></span> {{ ui.selection.playerFallback }}</span>
            </div>
          </div>
        </section>
      </div>
    </section>

    <div v-if="shortcutsOpen" class="shortcut-backdrop" @click.self="shortcutsOpen = false">
      <section class="card shortcut-dialog">
        <div class="panel-top-row compact">
          <div>
            <span class="panel-kicker">{{ ui.shortcuts.kicker }}</span>
            <h2>{{ ui.shortcuts.title }}</h2>
            <p>{{ ui.shortcuts.subtitle }}</p>
          </div>
          <button class="button subtle small" type="button" @click="shortcutsOpen = false">
            {{ ui.buttons.close }}
          </button>
        </div>

        <div class="shortcut-list">
          <div v-for="item in shortcutItems" :key="item.label" class="shortcut-row">
            <div class="keycap-row">
              <kbd v-for="key in item.keys" :key="key" class="keycap">{{ key }}</kbd>
            </div>
            <div>
              <strong>{{ item.label }}</strong>
              <p>{{ item.description }}</p>
            </div>
          </div>
        </div>
      </section>
    </div>
  </div>
</template>
