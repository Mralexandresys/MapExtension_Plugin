# MapExtension_Plugin

Filters, Display and Advanced are always available, regardless of the preset. Resource bulk actions only affect resources; Advanced contains the global layer controls. Reset restores both live and static filters to the current preset, including purity. Isolating a resource is a shortcut that can then be refined. Collapsed filters leave the keyboard focus order.

The viewer uses a compact session bar, a dark blue filter sidebar and cyan selection accents. Refresh timing is available in Settings; the rupture timeline stays visible above the map.

French README: `README.fr.md`

`MapExtension_Plugin` exposes StarRupture map data through a local HTTP endpoint and includes `mapview`, a local web interface used to display the map, entities, their connections, and the rupture cycle timeline.

The plugin works in both single-player and multiplayer. For solo/local sessions, install the client build only. For dedicated-server sessions, install the client build on the player machine and the server build on the dedicated server so the server can send map and rupture-cycle data to connected clients.

## Features

- See which `Cargo Dispatchers` are linked to which `Cargo Receivers`, and vice versa
- View their positions directly on the map
- See the items and amounts requested by cargo connections (without measuring actual throughput)
- Display the positions of `teleporters`
- Display the positions of `players`, with your own player highlighted in a distinct color
- Display abandoned bases, supported gatherable plants, Ignitium, and Star Tears as points of interest (POIs), including Hydrobulb, Polifruit, Oxallop, Purplant, Serpent Root, Prickler, Prism Herb, Sulheart, Gold Fruit, Thornfruit, Sikkim Rhubarb, and Nootka Lupine
- Browse a pre-generated world catalog containing major POIs, plants, minerals, animal resources, buildings, zones, and optional technical elements, with searchable persistent filters
- Accumulate live resource observations across streamed areas instead of dropping markers when the player leaves the current loading radius
- Keep depleted or permanently gathered resource positions observed during the active world on the map with a distinct depleted state
- Use a compact rupture-cycle view with phase, remaining-time, legend, and timeline details
- Filter the map down to personal markers and zones only
- Center the map on your own player with the `Player` control or the `P` shortcut
- Expose `GET /health`, `GET /cargo`, and `GET /rupture-cycle` on the local HTTP server
- Receive authoritative map and rupture-cycle snapshots from the server build in dedicated-server sessions
- Fall back to the local `UCrEnviroWaveSubsystem` in solo/local sessions when no server snapshot is available

## Mapview

The included `mapview` is a local web UI designed to read the plugin data and display it in a browser by opening the generated `dist/MapExtensionViewer.html` file.

When packaged, keep the generated `map-tiles/` and `map-data/` folders next to `MapExtensionViewer.html`; the viewer loads the map background from the tiles and the static world catalog from the compact data files.

It consumes both cargo/map data and the rupture cycle endpoint to render the timeline shown in the HUD replacement UI.

The viewer combines live plugin observations with a pre-generated static world catalog. Major POIs are rendered as selectable pins, while the larger resource, building, and zone layers use a canvas renderer. Searchable filters control layers, resource categories and types, representation sources, buildings, zones, and technical elements; filter choices persist in `localStorage`.

The frontend curates class-based placements in the `building` layer: only Unreal actor types whose names contain `KeyCard`, `Coralion_Egg`, or `Spawner` (case-insensitive) are rendered. This rule does not change the `zone` or `technical` layers.

Abandoned bases use their own map icon. Plant resources use a stable palette color derived from the resource name, while Ignitium and Star Tears have fixed resource-specific colors. Depleted resources remain visible as faded outlined markers. Separate filters control abandoned bases, plant resources, Ignitium, and Star Tears.

Ignitium and Star Tears filters use positions validated from runtime actors in areas loaded near players. The broad PCG inclusion/exclusion volumes are not displayed as resource locations. While the published timeline is in Arcadia stable (from 690 seconds in the 3240-second cycle, with `PreWave` as a fallback when elapsed time is unavailable), a validated, unharvested Ignitium site is exposed as Star Tears instead of Ignitium; an actual Star Tears actor observation is preferred at the same site. During the stabilizing transition, both resources can be displayed in the late-cycle overlap, with Star Tears drawn above the underlying Ignitium marker.

The rupture bar shows the phase and remaining time. Click it or press `Enter`/`Space` to open details; `Escape`, Close or an outside click closes them. On mobile, the bar stays visible without horizontal scrolling, details stay within the viewport, and the map controls have reserved space below the filters. The `Player` control or `P` shortcut centers on the `self` player, falling back to the first player when that flag is absent.

The timeline uses a calibrated 3240-second cycle (30/60/600/2550), animated between observations. It can drift from the game; raw Heat/Cold settings do not directly map to these phases. This calibration is deliberately retained.

## Plant coverage

The bundled static world catalog provides the complete pre-generated map data for plants and POIs. It is loaded directly by the viewer, so the plugin no longer drives a full World Partition scan, moves the player, pauses rupture activity, or writes a per-save POI cache.

The live snapshot can still capture supported `ACrGatherableBaseActor` and `ACrOreActor` instances in already loaded areas to reflect the active world. Those observations stay in memory only for the current world; live actor state and the game's replicated depleted-location data retain the last known positions of depleted resources. Ignitium and Star Tears observations are discarded when the replicated global PCG seed changes so points from the previous rupture generation do not leak into the new one.

Reward-class detection publishes Hydrobulb, Polifruit, Oxallop, Purplant, Serpent Root, Prickler, Prism Herb, and Sulheart. Explicit gatherable actor classes additionally cover Gold Fruit, Thornfruit, Sikkim Rhubarb, Nootka Lupine, and the game's generic `Plant_h` gatherable. Actors whose `InteractionRewardResource` is `I_StarTears_C` publish Star Tears; `ACrOreActor` instances whose `Resource` is `I_FireWaveOre_C` publish Ignitium.

## `/cargo` POI data

`GET /cargo` includes POI totals in `counts` and a `pois` array:

```json
{
  "counts": {
    "pois": 1,
    "abandoned_bases": 0,
    "plant_resources": 1,
    "ignitium": 0,
    "star_tears": 0
  },
  "pois": [
    {
      "kind": "plant_resource",
      "label": "Hydrobulb",
      "resource": "Hydrobulb",
      "depleted": false,
      "source": "actor_observation.gatherable",
      "unique_key": "example-plant-key",
      "world": { "x": 0.0, "y": 0.0, "z": 0.0 },
      "map": { "x": 0.0, "y": 0.0 }
    }
  ]
}
```

- `kind` is `abandoned_base`, `plant_resource`, `ignitium`, or `star_tears`.
- `label` is the display label; `resource` is the detected resource name and can be empty for an abandoned base.
- `depleted` is `true` for depleted resource actors observed during the active world. Their last known positions remain published so the viewer can render them as faded outlined markers.
- `source` identifies the capture path, `unique_key` identifies the POI to the viewer, `world` contains Unreal `x`/`y`/`z` coordinates, and `map` contains projected `x`/`y` coordinates.
- `counts.pois` is the total POI count; `counts.abandoned_bases`, `counts.plant_resources`, `counts.ignitium`, and `counts.star_tears` contain the per-kind totals.

## Dedicated-server sync compatibility

Dedicated-server snapshots use sync protocol v5, which carries POIs in pages of up to 64 entries per request, associates every page with a stable content revision, flags each client's own player marker, and validates snapshot IDs, generations, revisions, item counts, chunk counts, totals, and page layout before publishing a remote snapshot. Pages from different POI revisions are never merged. Protocol versions must match exactly: a v5 client or server ignores packets from a different protocol version rather than attempting a downgrade.

**Update the client and dedicated-server builds together.** The modloader auto-updater replaces only the client DLL; the dedicated-server DLL must be replaced manually during the same update. Do not leave the two sides on different releases.

## Installation and updates

The client release archive contains:

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `MapExtensionViewer.html`
- `map-tiles/`
- `map-data/`

Copy the `Plugins/` content into `StarRupture/Binaries/Win64/Plugins/`, then keep `MapExtensionViewer.html`, `map-tiles/`, and `map-data/` together anywhere on the machine.

`MapExtension_Plugin.json` is the modloader update sidecar. Its only field is `manifest_url`, pointing at the `latest/download` release manifest. When the sidecar is present, the modloader checks for a newer plugin version at startup and replaces `MapExtension_Plugin.dll` before loading any plugin. Installing only the standalone DLL asset disables automatic updates.

Two limits are worth knowing:

- The auto-updater replaces the DLL only. `MapExtensionViewer.html`, `map-tiles/`, and `map-data/` are never touched, since they live outside the game folder. The viewer detects incompatible payload contracts: when the plugin reports a contract newer than the one the local viewer was built with, a dialog offers a direct download of the matching `MapExtension_Plugin-<tag>-viewer.zip` asset, along with links to the GitHub release and the mod page. Backward-compatible viewer improvements may not trigger that dialog, so install the matching viewer archive manually to receive new UI features. Replace `MapExtensionViewer.html`, `map-tiles/`, and `map-data/` together, then reload the page.
- The server build is not covered by the sidecar. Sync protocol v5 requires matching client and server builds, so update both DLLs together and replace the dedicated-server DLL by hand.

Automatic updates can be disabled modloader-wide with `[AutoUpdate] Enabled=0` in `modloader.ini`.

## Interface choice

The initial goal was a more direct integration into the in-game UI.

In practice, StarRupture's UI turned out to be too limited to produce something reliable and maintainable in good conditions. The project therefore moved to a local web interface.

This is not necessarily the ideal integration model, but it is currently the most effective way to iterate quickly, display the data correctly, and keep the tool usable.

## Project status

This is a first functional draft.

The project will continue to evolve soon, and feedback, fixes, and contributions are welcome.

The plugin builds against `StarRupture-Plugin-SDK`.

Example:

- `./build.sh client release`

For build and workflow details, see `DEVELOPERS.md`.

## License

The `MapExtension_Plugin` code is distributed under the MIT license. See `LICENSE`.

Third-party components and references are documented in `THIRD_PARTY_NOTICES.md`.

Some third-party assets or references may be subject to rights separate from the MIT-licensed code.

## Plugin configuration

The plugin creates `Plugins/config/MapExtension_Plugin.ini` with:

```ini
[General]
Enabled=1

[Diagnostics]
VerboseLifecycleLogs=0
LogRuntimePlanOnce=0
LogCargoSnapshots=0
LogRuptureCycleEvents=0
LogActorScanFallback=0
LogRefreshTimings=0

[Http]
Port=9000

[Runtime]
RefreshIntervalMs=2000
```

- `Enabled`: enables or disables the plugin (`1` or `0`)
- `VerboseLifecycleLogs`: enables lifecycle logs (`1` or `0`)
- `LogRuntimePlanOnce`: logs the runtime strategy once (`1` or `0`)
- `LogCargoSnapshots`: logs cargo snapshots (`1` or `0`)
- `LogRuptureCycleEvents`: logs rupture cycle state changes and rupture-related world/server events (`1` or `0`)
- `LogActorScanFallback`: logs actor scan fallback (`1` or `0`)
- `LogRefreshTimings`: logs per-phase refresh timings (`1` or `0`)
- `Port`: sets the local HTTP port used by the plugin
- `RefreshIntervalMs`: sets the runtime refresh interval in milliseconds

## Credits

- Thanks to `AlienXAXS` for `StarRupture-ModLoader`, the mod loader that made `MapExtension_Plugin` possible:
  `https://github.com/AlienXAXS/StarRupture-ModLoader`
- Thanks to `bithoarder` for `StarRuptureMap`, used for the StarRupture game map:
  `https://github.com/bithoarder/StarRuptureMap/`
- This project is also inspired by `StarRuptureSaveMap`:
  `https://github.com/thanamatos/StarRuptureSaveMap`
- Thanks as well to the StarRupture developers for the game itself.

## Disclaimer

This is a modding tool. Use it at your own risk.

The authors cannot be held responsible for damage, regressions, or incompatibilities caused by its use. Every effort is made to reduce impact on save files and future compatibility, but that cannot be guaranteed.
