/**
 * Map presets.
 *
 * The catalog can technically draw ~80 000 static elements, but only a small
 * fraction answers a question a player actually asks while playing. A preset
 * is the answer to one such question, and it drives both the live plugin
 * entities and the static catalog filters in one go:
 *
 * - `network`     Where am I, where is my team, how do my resources move?
 * - `exploration` Where do I build, what is worth travelling to?
 * - `harvest`     Where is the one resource I am collecting right now?
 * - `technical`   Developer view over the raw exported layers.
 *
 * Defaults matter more than the filters themselves: the previous build enabled
 * the POI *and* resource layers at once, which meant 54 513 plant points were
 * drawn before the user had asked for anything.
 */

import type { StaticLayerKey } from "./staticMapCatalog";
import type { EntityVisibility } from "./types";

export type MapPreset = "network" | "exploration" | "harvest" | "technical";

export const MAP_PRESETS: readonly MapPreset[] = [
    "network",
    "exploration",
    "harvest",
    "technical",
];

export const DEFAULT_MAP_PRESET: MapPreset = "network";

/** `"all"` and `"none"` avoid restating group lists that the manifest owns. */
export type GroupSelection = "all" | "none" | readonly string[];

export interface PresetDefinition {
    /** Live entities published by the plugin. */
    entities: EntityVisibility;
    layers: Record<StaticLayerKey, boolean>;
    poiGroups: GroupSelection;
    resourceCategories: GroupSelection;
    placementGroups: GroupSelection;
    /**
     * Harvest shows one resource type at a time: 16 985 Polifruit points plus
     * 13 879 Hydrobulb points on the same screen is a dot cloud, not a map.
     */
    singleResource: boolean;
    /** Rendered as a developer-mode warning in the filters panel. */
    developerMode: boolean;
}

function entities(on: readonly (keyof EntityVisibility)[]): EntityVisibility {
    return {
        sender: on.includes("sender"),
        receiver: on.includes("receiver"),
        teleporter: on.includes("teleporter"),
        player: on.includes("player"),
        abandonedBase: on.includes("abandonedBase"),
        plantResource: on.includes("plantResource"),
        ignitium: on.includes("ignitium"),
        starTears: on.includes("starTears"),
    };
}

function layers(on: readonly StaticLayerKey[]): Record<StaticLayerKey, boolean> {
    return {
        poi: on.includes("poi"),
        resource: on.includes("resource"),
        building: on.includes("building"),
        zone: on.includes("zone"),
        technical: on.includes("technical"),
    };
}

export const PRESET_DEFINITIONS: Record<MapPreset, PresetDefinition> = {
    // Default view: every live entity the plugin publishes, plus the 241
    // canonical POI for orientation. No static resources at all.
    network: {
        entities: entities([
            "sender",
            "receiver",
            "teleporter",
            "player",
            "abandonedBase",
            "plantResource",
            "ignitium",
            "starTears",
        ]),
        layers: layers(["poi"]),
        poiGroups: "all",
        resourceCategories: "none",
        placementGroups: "none",
        singleResource: false,
        developerMode: false,
    },

    // Decluttered for scouting: canonical POI and industrial ore, no cargo
    // network on top of them.
    exploration: {
        entities: entities([
            "teleporter",
            "player",
            "abandonedBase",
            "ignitium",
            "starTears",
        ]),
        layers: layers(["poi", "resource"]),
        poiGroups: "all",
        resourceCategories: ["mineral"],
        placementGroups: "none",
        singleResource: false,
        developerMode: false,
    },

    // Caves stay on: Quartz is harvested inside them during the post-Rupture
    // window.
    harvest: {
        entities: entities([
            "teleporter",
            "player",
            "plantResource",
            "ignitium",
            "starTears",
        ]),
        layers: layers(["poi", "resource"]),
        poiGroups: ["cave"],
        resourceCategories: "all",
        placementGroups: "none",
        singleResource: true,
        developerMode: false,
    },

    technical: {
        entities: entities(["player"]),
        layers: layers(["poi", "building", "zone", "technical"]),
        poiGroups: "all",
        resourceCategories: "none",
        placementGroups: "all",
        singleResource: false,
        developerMode: true,
    },
};

/**
 * Resource types kept off even when their category is enabled.
 *
 * `unknown_ore` is 389 `BP_OreSocket` anchors, not an identified ore: showing
 * it as a mineral tells the player something false.
 */
export const RESOURCE_TYPES_HIDDEN_BY_DEFAULT: readonly string[] = ["unknown_ore"];

/**
 * Above this many points a resource type is treated as "common" and grouped
 * apart in the harvest picker. Count-based rather than a hardcoded list so a
 * catalog update classifies new types on its own.
 */
export const COMMON_RESOURCE_POINT_THRESHOLD = 5000;
