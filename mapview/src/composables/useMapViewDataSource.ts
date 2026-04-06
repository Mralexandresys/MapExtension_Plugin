import {
    computed,
    onBeforeUnmount,
    onMounted,
    reactive,
    ref,
    watch,
} from "vue";

import { fetchJson, normalizeEndpoint } from "../lib/api";
import { applyLanguage, getMessages, resolveInitialLanguage } from "../lang";
import type { Language } from "../lang";
import type {
    EntityVisibility,
    HealthResponse,
    CargoResponse,
    RuptureCycleResponse,
    ViewMode,
} from "../lib/types";

const DEFAULT_ENDPOINT = "http://127.0.0.1:9000";
const LIVE_REFRESH_MS = 2000;
const STORAGE_KEY = "starrupture-mapview:v3";

interface MapViewStatus {
    loading: boolean;
    online: boolean;
    text: string;
    error: string;
}

interface PersistedPreferences {
    endpoint?: string;
    autoRefresh?: boolean;
    showAllLinks?: boolean;
    highlightOrphans?: boolean;
    viewMode?: ViewMode;
    lang?: Language;
    entityVisibility?: Partial<EntityVisibility>;
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
    const lastUpdatedAt = ref(0);
    const now = ref(Date.now());
    const viewMode = ref<ViewMode>("network");

    const entityVisibility = reactive<EntityVisibility>({
        sender: true,
        receiver: true,
        teleporter: true,
        player: true,
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

    function loadPreferences(): void {
        if (typeof localStorage === "undefined") return;

        try {
            const raw = localStorage.getItem(STORAGE_KEY);
            if (!raw) return;

            const saved = JSON.parse(raw) as PersistedPreferences;

            endpoint.value = saved.endpoint || DEFAULT_ENDPOINT;
            endpointDraft.value = endpoint.value;
            autoRefresh.value = saved.autoRefresh ?? true;
            showAllLinks.value = saved.showAllLinks ?? true;
            highlightOrphans.value = saved.highlightOrphans ?? false;
            viewMode.value = saved.viewMode || "network";
            if (saved.lang === "en" || saved.lang === "fr") {
                lang.value = saved.lang;
            }
            entityVisibility.sender = saved.entityVisibility?.sender ?? true;
            entityVisibility.receiver = saved.entityVisibility?.receiver ?? true;
            entityVisibility.teleporter = saved.entityVisibility?.teleporter ?? true;
            entityVisibility.player = saved.entityVisibility?.player ?? true;
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
                showAllLinks: showAllLinks.value,
                highlightOrphans: highlightOrphans.value,
                viewMode: viewMode.value,
                lang: lang.value,
                entityVisibility: { ...entityVisibility },
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

    watch(autoRefresh, updateAutoRefresh, { immediate: true });

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
        lastUpdatedAt,
        now,
        viewMode,
        entityVisibility,
        status,
        ui,
        languageOptions,
        normalizedEndpoint,
        endpointHasPendingChanges,
        handleEndpointKeydown,
        refreshData,
        applyEndpoint,
    };
}
