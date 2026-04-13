import { computed, type ComputedRef, type Ref } from "vue";

import { formatRelativeAge, formatWorld } from "../lib/formatters";
import type { Messages } from "../lang";
import type {
    CargoConnection,
    CargoMarker,
    CargoResponse,
    CommandStat,
    DetailRow,
    EntityToggleKey,
    EntityVisibility,
    HealthResponse,
    Player,
    SelectedEntity,
    SelectionTone,
    StatusTone,
    Teleporter,
    ViewMode,
} from "../lib/types";

interface UseMapViewEntitiesOptions {
    cargo: Ref<CargoResponse | null>;
    health: Ref<HealthResponse | null>;
    ui: ComputedRef<Messages>;
    entityVisibility: EntityVisibility;
    viewMode: Ref<ViewMode>;
    showAllLinks: Ref<boolean>;
    highlightOrphans: Ref<boolean>;
    focusMode: Ref<boolean>;
    selectedKey: Ref<string | null>;
    hoveredKey: Ref<string | null>;
    entityToggleOptions: ComputedRef<
        Array<{ key: EntityToggleKey; label: string }>
    >;
    normalizedEndpoint: ComputedRef<string>;
    autoRefresh: Ref<boolean>;
    liveAgeLabel: ComputedRef<string>;
    statusTone: ComputedRef<StatusTone>;
    now: Ref<number>;
    lastUpdatedAt: Ref<number>;
}

export function useMapViewEntities(options: UseMapViewEntitiesOptions) {
    const {
        cargo,
        health,
        ui,
        entityVisibility,
        viewMode,
        showAllLinks,
        highlightOrphans,
        focusMode,
        selectedKey,
        hoveredKey,
        entityToggleOptions,
        normalizedEndpoint,
        autoRefresh,
        liveAgeLabel,
        statusTone,
        now,
        lastUpdatedAt,
    } = options;

    const liveAgeValue = computed(() =>
        formatRelativeAge(
            lastUpdatedAt.value,
            now.value,
            ui.value.locale,
            "",
            ui.value.status.lastUpdatedMissing,
        ),
    );

    const isCargoViewMode = computed(
        () => viewMode.value === "network" || viewMode.value === "resources",
    );

    const allCargoMarkers = computed<CargoMarker[]>(
        () => cargo.value?.markers || [],
    );
    const allCargoConnections = computed<CargoConnection[]>(
        () => cargo.value?.connections || [],
    );
    const allTeleporters = computed<Teleporter[]>(
        () => cargo.value?.teleporters || [],
    );
    const allPlayers = computed<Player[]>(() => cargo.value?.players || []);

    function isCargoMarkerAllowedByMode(marker: CargoMarker): boolean {
        if (!isCargoViewMode.value) return false;
        if (marker.kind === "sender") return entityVisibility.sender;
        if (marker.kind === "receiver") return entityVisibility.receiver;
        return true;
    }

    const visibleCargoMarkers = computed(() =>
        allCargoMarkers.value.filter(isCargoMarkerAllowedByMode),
    );

    const allCargoMarkerKeys = computed(
        () => new Set(allCargoMarkers.value.map((marker) => marker.unique_key)),
    );

    const visibleCargoMarkerKeys = computed(
        () => new Set(visibleCargoMarkers.value.map((marker) => marker.unique_key)),
    );

    function isMissingEndpointKindVisible(kind: "sender" | "receiver"): boolean {
        if (!isCargoViewMode.value) return false;
        return kind === "sender"
            ? entityVisibility.sender
            : entityVisibility.receiver;
    }

    const renderableCargoConnections = computed(() => {
        if (!isCargoViewMode.value) return [];

        return allCargoConnections.value.filter(
            (connection) => {
                const senderVisible = visibleCargoMarkerKeys.value.has(
                    connection.sender_key,
                );
                const receiverVisible = visibleCargoMarkerKeys.value.has(
                    connection.receiver_key,
                );

                const senderKnown = allCargoMarkerKeys.value.has(
                    connection.sender_key,
                );
                const receiverKnown = allCargoMarkerKeys.value.has(
                    connection.receiver_key,
                );

                const senderAllowed = senderVisible
                    ? true
                    : !senderKnown && isMissingEndpointKindVisible("sender");
                const receiverAllowed = receiverVisible
                    ? true
                    : !receiverKnown && isMissingEndpointKindVisible("receiver");

                return senderAllowed && receiverAllowed;
            },
        );
    });

    function getRelatedConnections(
        markerOrKey: CargoMarker | string | null,
    ): CargoConnection[] {
        const key =
            typeof markerOrKey === "string"
                ? markerOrKey
                : markerOrKey?.unique_key;
        if (!key) return [];
        return renderableCargoConnections.value.filter(
            (connection) =>
                connection.sender_key === key ||
                connection.receiver_key === key,
        );
    }

    const visibleTeleporters = computed(() =>
        allTeleporters.value.filter(() => {
            if (!entityVisibility.teleporter) return false;
            return (
                viewMode.value === "network" || viewMode.value === "teleporters"
            );
        }),
    );

    const visiblePlayers = computed(() =>
        allPlayers.value.filter(() => {
            if (!entityVisibility.player) return false;
            return viewMode.value === "network" || viewMode.value === "players";
        }),
    );

    const entityFilterCounts = computed<Record<EntityToggleKey, number>>(() => ({
        sender: isCargoViewMode.value
            ? allCargoMarkers.value.filter((marker) => marker.kind === "sender")
                  .length
            : 0,
        receiver: isCargoViewMode.value
            ? allCargoMarkers.value.filter((marker) => marker.kind === "receiver")
                  .length
            : 0,
        teleporter:
            viewMode.value === "network" || viewMode.value === "teleporters"
                ? allTeleporters.value.length
                : 0,
        player:
            viewMode.value === "network" || viewMode.value === "players"
                ? allPlayers.value.length
                : 0,
    }));

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

        const cargoMarker = visibleCargoMarkers.value.find(
            (marker) => marker.unique_key === selectedKey.value,
        );
        if (cargoMarker) return { type: "cargo", raw: cargoMarker };

        const teleporter = visibleTeleporters.value.find(
            (entry) => entry.unique_key === selectedKey.value,
        );
        if (teleporter) return { type: "teleporter", raw: teleporter };

        const player = visiblePlayers.value.find(
            (entry) => entry.unique_key === selectedKey.value,
        );
        if (player) return { type: "player", raw: player };

        return null;
    });

    const selectedCargo = computed(() =>
        selectedEntity.value?.type === "cargo"
            ? selectedEntity.value.raw
            : null,
    );
    const selectedTeleporter = computed(() =>
        selectedEntity.value?.type === "teleporter"
            ? selectedEntity.value.raw
            : null,
    );
    const selectedPlayer = computed(() =>
        selectedEntity.value?.type === "player"
            ? selectedEntity.value.raw
            : null,
    );

    const hoveredCargoMarker = computed(() => {
        if (!hoveredKey.value) return null;

        return (
            visibleCargoMarkers.value.find(
                (marker) => marker.unique_key === hoveredKey.value,
            ) || null
        );
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

        if (selection.type === "cargo") {
            const keys = new Set<string>([selection.raw.unique_key]);
            getRelatedConnections(selection.raw.unique_key).forEach(
                (connection) => {
                    keys.add(connection.sender_key);
                    keys.add(connection.receiver_key);
                },
            );
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
        if (!focusMode.value || focusKeys.value.size === 0) {
            return visibleCargoMarkers.value;
        }
        return visibleCargoMarkers.value.filter((marker) =>
            focusKeys.value.has(marker.unique_key),
        );
    });

    const displayedTeleporters = computed(() => {
        if (!focusMode.value || focusKeys.value.size === 0) {
            return visibleTeleporters.value;
        }
        return visibleTeleporters.value.filter((teleporter) =>
            focusKeys.value.has(teleporter.unique_key),
        );
    });

    const displayedPlayers = computed(() => {
        if (!focusMode.value || focusKeys.value.size === 0) {
            return visiblePlayers.value;
        }
        return visiblePlayers.value.filter((player) =>
            focusKeys.value.has(player.unique_key),
        );
    });

    const visibleCargoConnections = computed(() => {
        const connections = renderableCargoConnections.value;
        if (!connections.length) return [];

        let nextConnections = connections;
        if (selectedCargo.value) {
            nextConnections = getRelatedConnections(
                selectedCargo.value.unique_key,
            );
        } else if (viewMode.value === "resources" || !showAllLinks.value) {
            nextConnections = hoveredCargoMarker.value
                ? getRelatedConnections(hoveredCargoMarker.value.unique_key)
                : [];
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
        markers:
            cargo.value?.counts?.markers ??
            health.value?.marker_count ??
            allCargoMarkers.value.length,
        teleporters:
            cargo.value?.counts?.teleporters ??
            health.value?.teleporter_count ??
            allTeleporters.value.length,
        players:
            cargo.value?.counts?.players ??
            health.value?.player_count ??
            allPlayers.value.length,
    }));

    const filteredVisibleCount = computed(
        () =>
            visibleCargoMarkers.value.length +
            visibleTeleporters.value.length +
            visiblePlayers.value.length,
    );

    const activeFilterChips = computed(() => {
        const chips: string[] = [];

        if (viewMode.value !== "network") {
            chips.push(
                ui.value.format.modeChip(ui.value.viewModes[viewMode.value]),
            );
        }
        if (!showAllLinks.value) chips.push(ui.value.filters.linksFocus);
        if (highlightOrphans.value) chips.push(ui.value.filters.orphansVisible);
        if (focusMode.value && canEnableFocusMode.value) {
            chips.push(ui.value.filters.focusOnly);
        }

        const hidden = entityToggleOptions.value
            .filter((option) => !entityVisibility[option.key])
            .map((option) => option.label);
        if (hidden.length) {
            chips.push(ui.value.format.hiddenLabels(hidden.join(", ")));
        }

        return chips;
    });

    const currentTimeLabel = computed(() =>
        new Date(now.value).toLocaleTimeString(ui.value.locale, {
            hour12: false,
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit",
        }),
    );

    const commandStats = computed<CommandStat[]>(() => [
        {
            key: "entities",
            label: ui.value.tabs.entities,
            value: String(
                totalCounts.value.markers +
                    totalCounts.value.teleporters +
                    totalCounts.value.players,
            ),
            tone: "primary",
        },
        {
            key: "links",
            label: ui.value.selection.drawnLinks,
            value: String(visibleCargoConnections.value.length),
            tone: "neutral",
        },
        {
            key: "filters",
            label: ui.value.handles.filters,
            value: String(activeFilterChips.value.length),
            tone: activeFilterChips.value.length ? "warn" : "neutral",
        },
        {
            key: "freshness",
            label: ui.value.selection.lastUpdate,
            value: liveAgeValue.value,
            tone: statusTone.value === "online" ? "good" : "warn",
        },
    ]);

    const mapMetaLabel = computed(() => {
        if (!cargo.value) return ui.value.status.connecting;

        return ui.value.format.mapMeta(
            formatWorld(cargo.value.world, ui.value.selection.world),
            cargo.value.generation,
            ui.value.viewModes[viewMode.value],
        );
    });

    const selectedConnections = computed(() =>
        selectedCargo.value ? getRelatedConnections(selectedCargo.value) : [],
    );
    const selectedCargoIsOrphan = computed(
        () =>
            !!selectedCargo.value &&
            orphanKeySet.value.has(selectedCargo.value.unique_key),
    );

    function formatEntityType(
        type: "sender" | "receiver" | "teleporter" | "player",
    ): string {
        if (type === "sender") return ui.value.map.senderLabel;
        if (type === "receiver") return ui.value.map.receiverLabel;
        if (type === "teleporter") return ui.value.selection.teleporterFallback;
        return ui.value.selection.playerFallback;
    }

    const selectedEntitySummary = computed(() => {
        if (selectedCargo.value) {
            return ui.value.format.selectedSummaryCargo(
                formatEntityType(selectedCargo.value.kind),
                selectedConnections.value.length,
            );
        }
        if (selectedTeleporter.value)
            return ui.value.selection.teleporterFallback;
        if (selectedPlayer.value) return ui.value.selection.playerFallback;
        return ui.value.selection.summaryNone;
    });

    const selectedEntityKeyLabel = computed(() => {
        if (!selectedEntity.value) return "--";
        return selectedEntity.value.raw.unique_key;
    });

    const selectedEntityTone = computed<SelectionTone>(() => {
        if (selectedCargo.value) return selectedCargo.value.kind;
        if (selectedTeleporter.value) return "teleporter";
        if (selectedPlayer.value) return "player";
        return "neutral";
    });

    const selectedDisplayName = computed(() => {
        if (selectedCargo.value) {
            return (
                selectedCargo.value.label ||
                selectedCargo.value.display_name ||
                ui.value.selection.cargoFallback
            );
        }
        if (selectedTeleporter.value) {
            return (
                selectedTeleporter.value.label ||
                ui.value.selection.teleporterFallback
            );
        }
        if (selectedPlayer.value) {
            return (
                selectedPlayer.value.label || ui.value.selection.playerFallback
            );
        }
        return ui.value.selection.summaryNone;
    });

    const selectedDetailRows = computed<DetailRow[]>(() => {
        if (selectedCargo.value) {
            const rows: DetailRow[] = [
                {
                    label: ui.value.selection.type,
                    value: formatEntityType(selectedCargo.value.kind),
                },
                {
                    label: ui.value.selection.resource,
                    value: selectedCargo.value.resource || "--",
                },
                {
                    label: ui.value.selection.network,
                    value: selectedConnections.value.length
                        ? ui.value.format.networkConnections(
                              selectedConnections.value.length,
                          )
                        : ui.value.selection.noVisibleConnection,
                },
            ];

            if (
                selectedCargo.value.display_name &&
                selectedCargo.value.label &&
                selectedCargo.value.display_name !== selectedCargo.value.label
            ) {
                rows.push({
                    label: ui.value.selection.name,
                    value: selectedCargo.value.display_name,
                });
            }

            if (selectedCargoIsOrphan.value) {
                rows.push({
                    label: ui.value.selection.state,
                    value: ui.value.selection.orphan,
                });
            }

            return rows;
        }

        if (selectedTeleporter.value) {
            return [
                {
                    label: ui.value.selection.type,
                    value: ui.value.selection.teleporterFallback,
                },
                {
                    label: ui.value.selection.source,
                    value: selectedTeleporter.value.source || "--",
                },
            ];
        }

        if (selectedPlayer.value) {
            return [
                {
                    label: ui.value.selection.type,
                    value: ui.value.selection.playerFallback,
                },
                {
                    label: ui.value.selection.source,
                    value: selectedPlayer.value.source || "--",
                },
            ];
        }

        return [];
    });

    const selectedPreviewFacts = computed(() =>
        selectedDetailRows.value.slice(0, 2),
    );
    const statsOverview = computed<DetailRow[]>(() => [
        {
            label: ui.value.selection.endpoint,
            value: normalizedEndpoint.value || "--",
        },
        {
            label: ui.value.selection.world,
            value: formatWorld(cargo.value?.world, ui.value.selection.world),
        },
        {
            label: ui.value.selection.snapshot,
            value: String(cargo.value?.generation ?? "--"),
        },
        {
            label: ui.value.selection.polling,
            value: autoRefresh.value
                ? ui.value.status.pollingOn
                : ui.value.status.pollingOff,
        },
        {
            label: ui.value.selection.afterFilters,
            value: String(filteredVisibleCount.value),
        },
        { label: ui.value.selection.lastUpdate, value: liveAgeLabel.value },
    ]);

    return {
        displayedCargoMarkers,
        visibleCargoConnections,
        displayedTeleporters,
        displayedPlayers,
        entityFilterCounts,
        visibleEntityKeys,
        selectedEntity,
        orphanKeySet,
        focusKeys,
        focusCargoKey,
        commandStats,
        mapMetaLabel,
        selectedEntitySummary,
        selectedEntityKeyLabel,
        selectedEntityTone,
        selectedDisplayName,
        selectedPreviewFacts,
        canEnableFocusMode,
        selectedDetailRows,
        totalCounts,
        statsOverview,
        activeFilterChips,
        currentTimeLabel,
        selectedCargo,
    };
}
