import { computed, type ComputedRef, type Ref } from "vue";

import { poiKindLabel, poiState, poiStateLabel } from "../lib/poiState";
import { formatRelativeAge, formatWorld } from "../lib/formatters";
import type { Messages } from "../lang";
import {
    DEFAULT_MAP_PRESET,
    PRESET_DEFINITIONS,
    type MapPreset,
} from "../lib/mapPresets";
import type {
    CargoConnection,
    CargoMarker,
    CargoResponse,
    CommandStat,
    DetailRow,
    EntityToggleKey,
    EntityVisibility,
    HealthResponse,
    NamedMapEntity,
    Player,
    Poi,
    PoiKind,
    RupturePhaseKey,
    SelectedEntity,
    SelectionTone,
    StatusTone,
    Teleporter,
} from "../lib/types";

interface UseMapViewEntitiesOptions {
    cargo: Ref<CargoResponse | null>;
    health: Ref<HealthResponse | null>;
    ui: ComputedRef<Messages>;
    entityVisibility: EntityVisibility;
    preset: Ref<MapPreset>;
    showAllLinks: Ref<boolean>;
    highlightOrphans: Ref<boolean>;
    userAnnotationsOnly: Ref<boolean>;
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
    ruptureCurrentPhaseKey: ComputedRef<RupturePhaseKey>;
    ruptureHasLiveData: ComputedRef<boolean>;
    /**
     * Shared resource filters for observed plants, including plants absent from
     * the catalog. These replace the legacy live-only visibility switch.
     */
    plantResourceFilter: Ref<((resource: string) => boolean) | null>;
}

export function useMapViewEntities(options: UseMapViewEntitiesOptions) {
    const {
        cargo,
        health,
        ui,
        entityVisibility,
        preset,
        showAllLinks,
        highlightOrphans,
        userAnnotationsOnly,
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
        ruptureCurrentPhaseKey,
        ruptureHasLiveData,
        plantResourceFilter,
    } = options;

    // Bare value: this feeds a stat card that already carries its own "last
    // update" label, so the prefixed variant would repeat it.
    const liveAgeValue = computed(() =>
        formatRelativeAge(lastUpdatedAt.value, now.value, ui.value.locale, "", "--"),
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
    const observedPois = computed<Poi[]>(() =>
        (cargo.value?.pois ?? []).map((poi) => ({
            ...poi,
            depleted: poi.depleted ?? false,
        })),
    );

    const allPois = computed<Poi[]>(() => {
        const pois = observedPois.value;
        if (!ruptureHasLiveData.value) return pois;

        const phase = ruptureCurrentPhaseKey.value;
        if (phase === "burning" || phase === "cooling") {
            return pois.filter((poi) => poi.kind !== "star_tears");
        }
        if (phase === "stabilizing") return pois;

        const starTears = pois.filter((poi) => poi.kind === "star_tears");
        const siteRadiusSquared = 300 * 300;
        return pois.flatMap((poi): Poi[] => {
            if (poi.kind !== "ignitium") return [poi];
            if (poiState(poi) === "depleted") return [];

            const hasObservedStarTears = starTears.some((starTear) => {
                const deltaX = starTear.world.x - poi.world.x;
                const deltaY = starTear.world.y - poi.world.y;
                return deltaX * deltaX + deltaY * deltaY <= siteRadiusSquared;
            });
            if (hasObservedStarTears) return [];

            return [
                {
                    ...poi,
                    unique_key: `rupture-phase:${poi.unique_key}`,
                    kind: "star_tears",
                    label: ui.value.map.starTearsLabel,
                    resource: "Star Tears",
                    source: "rupture_phase.ignitium_to_star_tears",
                    state: "available",
                },
            ];
        });
    });

    function isCargoMarkerAllowedByMode(marker: CargoMarker): boolean {
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

    /** A visible endpoint, or one the snapshot does not list but whose kind is shown. */
    function isEndpointAllowed(key: string, kind: "sender" | "receiver"): boolean {
        return (
            visibleCargoMarkerKeys.value.has(key) ||
            (!allCargoMarkerKeys.value.has(key) && entityVisibility[kind])
        );
    }

    const renderableCargoConnections = computed(() =>
        allCargoConnections.value.filter(
            (connection) =>
                isEndpointAllowed(connection.sender_key, "sender") &&
                isEndpointAllowed(connection.receiver_key, "receiver"),
        ),
    );

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
        entityVisibility.teleporter ? allTeleporters.value : [],
    );

    const visiblePlayers = computed(() =>
        entityVisibility.player ? allPlayers.value : [],
    );

    function isPoiKindVisible(poi: Poi): boolean {
        switch (poi.kind) {
            case "abandoned_base":
                return entityVisibility.abandonedBase;
            case "plant_resource": {
                const filter = plantResourceFilter.value;
                return filter
                    ? filter(poi.resource ?? "")
                    : entityVisibility.plantResource;
            }
            case "ignitium":
                return entityVisibility.ignitium;
            case "star_tears":
                return entityVisibility.starTears;
        }
    }

    // Presets write straight into `entityVisibility`, so a second mode-based
    // mask on top of it would only be a way for the two to disagree.
    const visiblePois = computed<Poi[]>(() =>
        allPois.value.filter(isPoiKindVisible),
    );

    /** Observed plant resources, by the label the plugin publishes. */
    const livePlantResourceCounts = computed<Record<string, number>>(() => {
        const counts: Record<string, number> = {};
        for (const poi of allPois.value) {
            if (poi.kind !== "plant_resource") continue;
            const resource = (poi.resource ?? "").trim();
            if (!resource) continue;
            counts[resource] = (counts[resource] ?? 0) + 1;
        }
        return counts;
    });

    const poiCountsByKind = computed(() => {
        const counts: Record<PoiKind, number> = {
            abandoned_base: 0,
            plant_resource: 0,
            ignitium: 0,
            star_tears: 0,
        };
        for (const poi of allPois.value) counts[poi.kind] += 1;
        return counts;
    });

    const entityFilterCounts = computed<Record<EntityToggleKey, number>>(() => ({
        sender: allCargoMarkers.value.filter((m) => m.kind === "sender").length,
        receiver: allCargoMarkers.value.filter((m) => m.kind === "receiver").length,
        teleporter: allTeleporters.value.length,
        player: allPlayers.value.length,
        abandonedBase: poiCountsByKind.value.abandoned_base,
        plantResource: poiCountsByKind.value.plant_resource,
        ignitium: poiCountsByKind.value.ignitium,
        starTears: poiCountsByKind.value.star_tears,
    }));

    const selectedEntity = computed<SelectedEntity | null>(() => {
        if (!selectedKey.value || userAnnotationsOnly.value) return null;

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

        const poi = visiblePois.value.find(
            (entry) => entry.unique_key === selectedKey.value,
        );
        if (poi) return { type: "poi", raw: poi };

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
    const selectedPoi = computed(() =>
        selectedEntity.value?.type === "poi" ? selectedEntity.value.raw : null,
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
        if (!highlightOrphans.value) {
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

    /** What is drawn once "annotations only" and focus mode are applied. */
    function applyFocus<T extends { unique_key: string }>(entries: T[]): T[] {
        if (userAnnotationsOnly.value) return [];
        if (!focusMode.value || focusKeys.value.size === 0) return entries;
        return entries.filter((entry) => focusKeys.value.has(entry.unique_key));
    }

    const displayedCargoMarkers = computed(() => applyFocus(visibleCargoMarkers.value));
    const displayedTeleporters = computed(() => applyFocus(visibleTeleporters.value));
    const displayedPlayers = computed(() => applyFocus(visiblePlayers.value));

    function poiRenderPriority(poi: Poi): number {
        const availability = poiState(poi) === "available" ? 10 : 0;
        switch (poi.kind) {
            case "ignitium":
                return availability + 1;
            case "star_tears":
                return availability + 2;
            case "plant_resource":
                return availability + 3;
            case "abandoned_base":
                return availability + 4;
        }
    }

    const displayedPois = computed<Poi[]>(() =>
        // SVG paints later siblings on top. Star Tears therefore stay visible
        // when the underlying Ignitium actor legitimately shares the same site.
        [...applyFocus(visiblePois.value)].sort(
            (left, right) => poiRenderPriority(left) - poiRenderPriority(right),
        ),
    );

    const visibleEntityKeys = computed(
        () =>
            new Set([
                ...displayedCargoMarkers.value.map((marker) => marker.unique_key),
                ...displayedTeleporters.value.map((entry) => entry.unique_key),
                ...displayedPlayers.value.map((entry) => entry.unique_key),
                ...displayedPois.value.map((entry) => entry.unique_key),
            ]),
    );

    const visibleCargoConnections = computed(() => {
        if (userAnnotationsOnly.value) return [];
        const connections = renderableCargoConnections.value;
        if (!connections.length) return [];

        let nextConnections = connections;
        if (selectedCargo.value) {
            nextConnections = getRelatedConnections(
                selectedCargo.value.unique_key,
            );
        } else if (!showAllLinks.value) {
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
        pois: allPois.value.length,
        abandonedBases: poiCountsByKind.value.abandoned_base,
        plantResources: poiCountsByKind.value.plant_resource,
        ignitium: poiCountsByKind.value.ignitium,
        starTears: poiCountsByKind.value.star_tears,
    }));

    const filteredVisibleCount = computed(
        () =>
            displayedCargoMarkers.value.length +
            displayedTeleporters.value.length +
            displayedPlayers.value.length +
            displayedPois.value.length,
    );

    /**
     * Whether the view deviates from the current preset's own defaults. A
     * preset hiding the cargo network is its documented behaviour, not a filter
     * the user set. `abandonedBase` follows the `abandoned_base` landmark.
     */
    const hasActiveFilters = computed(() => {
        if (
            preset.value !== DEFAULT_MAP_PRESET ||
            !showAllLinks.value ||
            highlightOrphans.value ||
            userAnnotationsOnly.value ||
            (focusMode.value && canEnableFocusMode.value)
        ) {
            return true;
        }
        const presetEntities = PRESET_DEFINITIONS[preset.value].entities;
        return entityToggleOptions.value.some(
            (option) =>
                option.key !== "abandonedBase" &&
                entityVisibility[option.key] !== presetEntities[option.key],
        );
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
                    totalCounts.value.players +
                    totalCounts.value.pois,
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
            ui.value.presets[preset.value].label,
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

    function cargoKindLabel(kind: CargoMarker["kind"]): string {
        return kind === "sender" ? ui.value.map.senderLabel : ui.value.map.receiverLabel;
    }

    const selectedEntitySummary = computed(() => {
        if (selectedCargo.value) {
            return ui.value.format.selectedSummaryCargo(
                cargoKindLabel(selectedCargo.value.kind),
                selectedConnections.value.length,
            );
        }
        if (selectedTeleporter.value)
            return ui.value.selection.teleporterFallback;
        if (selectedPlayer.value) return ui.value.selection.playerFallback;
        if (selectedPoi.value) return poiKindLabel(selectedPoi.value.kind, ui.value.map);
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
        if (selectedPoi.value) {
            return (
                selectedPoi.value.label ||
                selectedPoi.value.resource ||
                poiKindLabel(selectedPoi.value.kind, ui.value.map)
            );
        }
        return ui.value.selection.summaryNone;
    });

    /** Live positions are Unreal centimetres; the panel speaks metres. */
    function worldPositionRows(entity: NamedMapEntity): DetailRow[] {
        return [
            {
                label: ui.value.staticFilters.details.position,
                value: `X ${Math.round(entity.world.x / 100)} m | Y ${Math.round(
                    entity.world.y / 100,
                )} m`,
            },
            {
                label: ui.value.staticFilters.details.altitude,
                value: `${Math.round(entity.world.z / 100)} m`,
            },
        ];
    }

    const selectedDetailRows = computed<DetailRow[]>(() => {
        if (selectedCargo.value) {
            const marker = selectedCargo.value;
            const rows: DetailRow[] = [
                {
                    label: ui.value.selection.resource,
                    value: marker.resource || "--",
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
                marker.display_name &&
                marker.label &&
                marker.display_name !== marker.label
            ) {
                rows.push({
                    label: ui.value.selection.name,
                    value: marker.display_name,
                });
            }

            if (selectedCargoIsOrphan.value) {
                rows.push({
                    label: ui.value.selection.state,
                    value: ui.value.selection.orphan,
                });
            }

            // What the building actually moves. The plugin has always sent the
            // item and the requested amount, but they only ever showed in the
            // tooltip of a hovered link, one link at a time.
            for (const connection of selectedConnections.value) {
                const outgoing = connection.sender_key === marker.unique_key;
                const other =
                    (outgoing ? connection.receiver_label : connection.sender_label) ||
                    ui.value.selection.cargoFallback;
                const amount = connection.requested_amount;
                rows.push({
                    label: connection.item || ui.value.map.unknownItem,
                    value:
                        amount == null
                            ? `${outgoing ? "->" : "<-"} ${other}`
                            : `${outgoing ? "->" : "<-"} ${other} (${amount})`,
                });
            }

            return rows;
        }

        if (selectedTeleporter.value) {
            return worldPositionRows(selectedTeleporter.value);
        }

        if (selectedPlayer.value) {
            const rows: DetailRow[] = [];
            if (selectedPlayer.value.self) {
                rows.push({
                    label: ui.value.selection.type,
                    value: ui.value.selection.selfPlayer,
                });
            }
            return [...rows, ...worldPositionRows(selectedPlayer.value)];
        }

        if (selectedPoi.value) {
            const rows: DetailRow[] = [];

            if (selectedPoi.value.kind !== "abandoned_base") {
                rows.push({
                    label: ui.value.selection.resource,
                    value:
                        selectedPoi.value.resource ||
                        selectedPoi.value.label ||
                        "--",
                });
            }

            rows.push({
                label: ui.value.selection.state,
                value: poiStateLabel(selectedPoi.value, ui.value.map),
            });

            return [...rows, ...worldPositionRows(selectedPoi.value)];
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
        displayedPois,
        livePlantResourceCounts,
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
        hasActiveFilters,
        currentTimeLabel,
    };
}
