import {
    computed,
    onBeforeUnmount,
    onMounted,
    reactive,
    ref,
    watch,
} from "vue";

import { fetchJson, normalizeEndpoint } from "../lib/api";
import { VIEWER_CONTRACT_VERSION } from "../lib/viewerContract";
import { applyLanguage, getMessages, resolveInitialLanguage } from "../lang";
import type { Language } from "../lang";
import {
    DEFAULT_MAP_PRESET,
    MAP_PRESETS,
    type MapPreset,
} from "../lib/mapPresets";
import type {
    EntityVisibility,
    FilterSectionsOpen,
    HealthResponse,
    CargoResponse,
    RuptureCycleResponse,
} from "../lib/types";

const DEFAULT_ENDPOINT = "http://127.0.0.1:9000";
const LIVE_REFRESH_MS = 2000;
const MIN_REFRESH_INTERVAL_MS = 500;
const MAX_REFRESH_INTERVAL_MS = 60000;
const STORAGE_KEY = "starrupture-mapview:v3";
const LANGUAGE_OPTIONS: Language[] = ["en", "fr"];
const DEFAULT_ICON_SCALE = 1;
const MIN_ICON_SCALE = 0.75;
const MAX_ICON_SCALE = 2;

/** Older builds stored one of four view modes; map them onto the presets. */
const LEGACY_VIEW_MODE_TO_PRESET: Record<string, MapPreset> = {
    network: "network",
    resources: "harvest",
    teleporters: "network",
    players: "network",
};

function resolvePreset(saved: PersistedPreferences): MapPreset {
    if (saved.preset && MAP_PRESETS.includes(saved.preset)) return saved.preset;
    if (saved.viewMode) {
        return LEGACY_VIEW_MODE_TO_PRESET[saved.viewMode] ?? DEFAULT_MAP_PRESET;
    }
    return DEFAULT_MAP_PRESET;
}

function clampIconScale(value: unknown): number {
    const numericValue = typeof value === "number" ? value : Number(value);
    if (!Number.isFinite(numericValue)) return DEFAULT_ICON_SCALE;
    return Math.min(MAX_ICON_SCALE, Math.max(MIN_ICON_SCALE, numericValue));
}

function clampRefreshIntervalMs(value: unknown): number {
    const numericValue = typeof value === "number" ? value : Number(value);
    if (!Number.isFinite(numericValue)) return LIVE_REFRESH_MS;
    return Math.min(
        MAX_REFRESH_INTERVAL_MS,
        Math.max(MIN_REFRESH_INTERVAL_MS, Math.round(numericValue)),
    );
}

interface MapViewStatus {
    loading: boolean;
    online: boolean;
    text: string;
    error: string;
}

interface PersistedPreferences {
    endpoint?: string;
    autoRefresh?: boolean;
    refreshIntervalMs?: number;
    iconScale?: number;
    showAllLinks?: boolean;
    highlightOrphans?: boolean;
    preset?: MapPreset;
    harvestResource?: string | null;
    /** Legacy key: the four view modes became presets. */
    viewMode?: string;
    lang?: Language;
    entityVisibility?: Partial<EntityVisibility>;
    filtersPanelCollapsed?: boolean;
    filterSectionsOpen?: Partial<FilterSectionsOpen>;
    /** Legacy: the rupture timeline is now a header strip with no compact mode. */
    ruptureCompact?: boolean;
}

export function useMapViewDataSource() {
    const cargo = ref<CargoResponse | null>(null);
    const health = ref<HealthResponse | null>(null);
    const ruptureCycle = ref<RuptureCycleResponse | null>(null);
    const endpoint = ref(DEFAULT_ENDPOINT);
    const endpointDraft = ref(DEFAULT_ENDPOINT);
    const lang = ref<Language>(resolveInitialLanguage(STORAGE_KEY));
    const showAllLinks = ref(true);
    const highlightOrphans = ref(false);
    const autoRefresh = ref(true);
    const refreshIntervalMs = ref(LIVE_REFRESH_MS);
    const iconScale = ref(DEFAULT_ICON_SCALE);
    const lastUpdatedAt = ref(0);
    const now = ref(Date.now());
    const preset = ref<MapPreset>(DEFAULT_MAP_PRESET);
    /** Harvest preset draws one resource type at a time; null means none picked. */
    const harvestResource = ref<string | null>(null);
    // Open on first run: the world catalog (241 POI and ~80k elements) was only
    // reachable through a 46px vertical rail, so most of the product was
    // invisible by default. The user's choice is persisted from then on.
    const filtersPanelCollapsed = ref(false);

    // Only the entity toggles start open. Every other section still advertises
    // its state through the count in its header, so nothing becomes hidden.
    const filterSectionsOpen = reactive<FilterSectionsOpen>({
        visibility: true,
        harvest: true,
        behavior: false,
        catalog: false,
    });

    const entityVisibility = reactive<EntityVisibility>({
        sender: true,
        receiver: true,
        teleporter: true,
        player: true,
        abandonedBase: true,
        plantResource: true,
        ignitium: true,
        starTears: true,
    });

    const status = reactive<MapViewStatus>({
        loading: false,
        online: false,
        text: getMessages(lang.value).status.connecting,
        error: "",
    });

    let liveTimer: number | null = null;
    let clockTimer: number | null = null;

    const ui = computed(() => getMessages(lang.value));
    const languageOptions: Language[] = ["en", "fr"];
    const normalizedEndpoint = computed(() => normalizeEndpoint(endpoint.value));
    const normalizedDraftEndpoint = computed(() =>
        normalizeEndpoint(endpointDraft.value),
    );
    const endpointHasPendingChanges = computed(
        () => normalizedDraftEndpoint.value !== normalizedEndpoint.value,
    );

    // ── Viewer contract ───────────────────────────────────────────────────────
    // The plugin auto-updates through the modloader sidecar, but only the DLL is
    // replaced: MapExtensionViewer.html and map-tiles/ stay on whatever version
    // the user installed by hand. A plugin that reports a contract version newer
    // than the one compiled in this build cannot be rendered reliably anymore.

    const pluginVersion = computed(() => health.value?.plugin_version ?? "");

    const viewerOutdated = computed(() => {
        const reported = health.value?.viewer_contract_version;
        return typeof reported === "number" && reported > VIEWER_CONTRACT_VERSION;
    });

    const viewerUpdateDownloadUrl = computed(
        () => health.value?.viewer_update?.download_url ?? "",
    );
    const viewerUpdateReleaseUrl = computed(
        () => health.value?.viewer_update?.release_url ?? "",
    );
    const viewerUpdateModPageUrl = computed(
        () => health.value?.viewer_update?.mod_page_url ?? "",
    );

    function loadPreferences(): void {
        if (typeof localStorage === "undefined") return;

        try {
            const raw = localStorage.getItem(STORAGE_KEY);
            if (!raw) return;

            const saved = JSON.parse(raw) as PersistedPreferences;

            endpoint.value = saved.endpoint || DEFAULT_ENDPOINT;
            endpointDraft.value = endpoint.value;
            autoRefresh.value = saved.autoRefresh ?? true;
            refreshIntervalMs.value = clampRefreshIntervalMs(
                saved.refreshIntervalMs,
            );
            iconScale.value = clampIconScale(saved.iconScale);
            showAllLinks.value = saved.showAllLinks ?? true;
            highlightOrphans.value = saved.highlightOrphans ?? false;
            preset.value = resolvePreset(saved);
            harvestResource.value =
                typeof saved.harvestResource === "string"
                    ? saved.harvestResource
                    : null;
            filtersPanelCollapsed.value = saved.filtersPanelCollapsed ?? false;
            filterSectionsOpen.visibility =
                saved.filterSectionsOpen?.visibility ?? true;
            filterSectionsOpen.harvest = saved.filterSectionsOpen?.harvest ?? true;
            filterSectionsOpen.behavior =
                saved.filterSectionsOpen?.behavior ?? false;
            filterSectionsOpen.catalog =
                saved.filterSectionsOpen?.catalog ?? false;
            if (saved.lang && LANGUAGE_OPTIONS.includes(saved.lang as Language)) {
                lang.value = saved.lang as Language;
            }
            entityVisibility.sender = saved.entityVisibility?.sender ?? true;
            entityVisibility.receiver = saved.entityVisibility?.receiver ?? true;
            entityVisibility.teleporter = saved.entityVisibility?.teleporter ?? true;
            entityVisibility.player = saved.entityVisibility?.player ?? true;
            entityVisibility.abandonedBase =
                saved.entityVisibility?.abandonedBase ?? true;
            entityVisibility.plantResource =
                saved.entityVisibility?.plantResource ?? true;
            entityVisibility.ignitium =
                saved.entityVisibility?.ignitium ?? true;
            entityVisibility.starTears =
                saved.entityVisibility?.starTears ?? true;
        } catch {
            endpoint.value = DEFAULT_ENDPOINT;
            endpointDraft.value = DEFAULT_ENDPOINT;
        }
    }

    function savePreferences(): void {
        if (typeof localStorage === "undefined") return;

        localStorage.setItem(
            STORAGE_KEY,
            JSON.stringify({
                endpoint: endpoint.value,
                autoRefresh: autoRefresh.value,
                refreshIntervalMs: refreshIntervalMs.value,
                iconScale: iconScale.value,
                showAllLinks: showAllLinks.value,
                highlightOrphans: highlightOrphans.value,
                preset: preset.value,
                harvestResource: harvestResource.value,
                lang: lang.value,
                entityVisibility: { ...entityVisibility },
                filtersPanelCollapsed: filtersPanelCollapsed.value,
                filterSectionsOpen: { ...filterSectionsOpen },
            }),
        );
    }

    function setStatus(online: boolean, text: string, error = ""): void {
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
            setStatus(
                false,
                ui.value.status.invalidEndpoint,
                ui.value.status.invalidEndpointHelp,
            );
            return;
        }

        if (status.loading) return;

        status.loading = true;
        setStatus(false, ui.value.status.contactingPlugin, "");

        try {
            const [nextHealth, nextCargo, nextRuptureCycle] = await Promise.all([
                fetchJson<HealthResponse>(currentEndpoint, "/health"),
                fetchJson<CargoResponse>(currentEndpoint, "/cargo"),
                fetchJson<RuptureCycleResponse>(currentEndpoint, "/rupture-cycle").catch(
                    () => null,
                ),
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
            const message = error instanceof Error ? error.message : "Unknown error";
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

    function applyEndpoint(refresh = true): void {
        endpoint.value = endpointDraft.value;
        if (refresh) {
            void refreshData();
        }
    }

    function handleEndpointKeydown(event: KeyboardEvent): void {
        if (event.key === "Enter") {
            event.preventDefault();
            applyEndpoint(true);
        }
    }

    function updateRefreshInterval(value: number): void {
        refreshIntervalMs.value = clampRefreshIntervalMs(value);
    }

    function updateAutoRefresh(): void {
        if (liveTimer !== null) {
            window.clearInterval(liveTimer);
            liveTimer = null;
        }

        if (autoRefresh.value) {
            liveTimer = window.setInterval(() => {
                void refreshData();
            }, refreshIntervalMs.value);
        }
    }

    watch(
        () => ({
            endpoint: endpoint.value,
            autoRefresh: autoRefresh.value,
            refreshIntervalMs: refreshIntervalMs.value,
            showAllLinks: showAllLinks.value,
            highlightOrphans: highlightOrphans.value,
            preset: preset.value,
            harvestResource: harvestResource.value,
            lang: lang.value,
            entityVisibility: { ...entityVisibility },
            filtersPanelCollapsed: filtersPanelCollapsed.value,
            filterSectionsOpen: { ...filterSectionsOpen },
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

    watch([autoRefresh, refreshIntervalMs], updateAutoRefresh);

    onMounted(() => {
        loadPreferences();
        updateAutoRefresh();
        clockTimer = window.setInterval(() => {
            now.value = Date.now();
        }, 1000);
        void refreshData();
    });

    onBeforeUnmount(() => {
        if (liveTimer !== null) window.clearInterval(liveTimer);
        if (clockTimer !== null) window.clearInterval(clockTimer);
    });

    return {
        DEFAULT_ENDPOINT,
        cargo,
        health,
        ruptureCycle,
        endpointDraft,
        lang,
        showAllLinks,
        highlightOrphans,
        autoRefresh,
        refreshIntervalMs,
        iconScale,
        lastUpdatedAt,
        now,
        preset,
        harvestResource,
        filtersPanelCollapsed,
        filterSectionsOpen,
        entityVisibility,
        status,
        ui,
        languageOptions,
        normalizedEndpoint,
        endpointHasPendingChanges,
        pluginVersion,
        viewerOutdated,
        viewerUpdateDownloadUrl,
        viewerUpdateReleaseUrl,
        viewerUpdateModPageUrl,
        handleEndpointKeydown,
        updateRefreshInterval,
        refreshData,
        applyEndpoint,
    };
}
