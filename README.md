# MapExtension_Plugin

French README: `README.fr.md`

`MapExtension_Plugin` adds a logistics map inside StarRupture. Press `F8` in game to open a ModLoader panel that draws the game's own map terrain with your cargo network, teleporters, players, base cores, monoliths, antennas, resources and the rupture cycle.

Since version 0.8 the map lives in the game. The former browser viewer (`MapExtensionViewer.html`, `map-tiles/`) and its local HTTP API are removed; see [Upgrading from 0.7](#upgrading-from-07).

The plugin works in both single-player and multiplayer. For solo/local sessions, install the client build only. For dedicated-server sessions, install the client build on the player machine and the server build on the dedicated server so the server can send map and rupture-cycle data to connected clients.

## Features

- Open the map with `F8` (configurable), close it with the same key or `Escape`; drag to pan, mouse wheel to zoom, optional "Follow me"
- See which `Cargo Dispatchers` are linked to which `Cargo Receivers`; hover a cargo or a link to see the transported items with their icons and requested amounts (requested amounts, not measured throughput)
- Click a cargo to isolate its links; the others fade out
- Filter the cargo network by transported item, with a search box, and optionally hide cargo points that have no link
- Display `teleporters` and `players`, with your own player highlighted
- Display your base cores with their name, level, cooling usage and infection state
- Display every monolith with its state (disabled, cooldown, enabled, charged, attack in progress), its charge as a percentage of the attack threshold, and the target of an attack in progress
- Display antennas (geo scanners) with their state
- Display abandoned bases with their discovery state and collection progress (items, blueprint chests, story items, infection cysts), plus caves and other points of interest
- Display Ignitium and Star Tears according to the rupture phase, with a rupture strip at the top of the map showing the phase, the time left and the resource of that phase
- Browse a pre-generated world catalog: ore deposits with purity, plants, creatures and points of interest, one filter per type, drawn with the game's own icons
- Receive authoritative map and rupture-cycle snapshots from the server build in dedicated-server sessions

## Where the data comes from

- **Cargo, teleporters, players, rupture cycle**: read from the running game (or streamed by the server build on a dedicated server), refreshed every `RefreshIntervalMs`.
- **Antennas, abandoned bases, discovery states, base core names and levels**: read from the data the game replicates for its own map.
- **Monoliths and base core positions**: monolith state and charge only exist in the game's internal entity data, and the game publishes its base markers tens of seconds after a world loads. The client build reads both natively, read-only, through a function located by signature. This is validated for game build CL-127004. After a game update that moves it, the mod loader turns off only that feature ("In-game map: monolith state and charge"): monoliths lose their state, base cores appear with the game's own delay, and the rest of the plugin keeps working.
- **Ore deposits, plants, creatures, points of interest**: a static catalog shipped with the client archive in `Plugins/MapExtension/map-data/`. Positions are pre-generated; they are not live observations.
- **Ignitium and Star Tears**: observed in areas loaded near players, then projected from the rupture phase.

The plugin only observes. It does not automate gameplay, change saves, or issue in-game commands.

## Ignitium and Star Tears

Positions come from runtime actors validated in loaded areas. While the published timeline is in Arcadia stable (from 690 seconds in the 3240-second cycle), a validated, unharvested Ignitium site is exposed as Star Tears instead of Ignitium; an actual Star Tears observation at the same site replaces that projection. During the stabilizing transition the in-game map shows Ignitium only where both are reported at the same site.

Markers distinguish available, not harvestable at the moment, depleted and estimated-from-cycle states. Unloading an area is not evidence of harvesting.

The timeline uses a calibrated 3240-second cycle (30/60/600/2430/120 seconds), animated between observations. It can drift from the game.

## Dedicated-server sync compatibility

Dedicated-server snapshots use sync protocol v6, which carries POIs in pages of up to 64 entries per request, associates every page with a stable content revision, flags each client's own player marker, and validates snapshot IDs, generations, revisions, item counts, chunk counts, totals, and page layout before publishing a remote snapshot. Pages from different POI revisions are never merged. Protocol versions must match exactly: a v6 client or server ignores packets from a different protocol version rather than attempting a downgrade.

**Update the client and dedicated-server builds together.** The modloader auto-updater replaces only the client DLL; the dedicated-server DLL must be replaced manually during the same update. Do not leave the two sides on different releases.

## Installation and updates

Requires [StarRupture-ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader).

The client release archive contains:

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `Plugins/MapExtension/map-data/` (static world catalog)

Install:

1. Copy the content of the archive's `Plugins/` folder into `StarRupture/Binaries/Win64/ModLoader/Plugins/`. Keep the `MapExtension/` folder next to the DLL: without it the map still opens, but ore deposits, plants, creatures and points of interest are missing.
2. Start the game, load a save, and press `F8`.
3. Dedicated server only: copy the server archive's `Plugins/MapExtension_Plugin.dll` into the server's `ModLoader/Plugins/` folder.

`MapExtension_Plugin.json` is the modloader update sidecar. Its only field is `manifest_url`, pointing at the `latest/download` release manifest. When the sidecar is present, the modloader checks for a newer plugin version at startup and replaces `MapExtension_Plugin.dll` before loading any plugin. Installing only the standalone DLL asset disables automatic updates.

Two limits are worth knowing:

- The auto-updater replaces the DLL only. `Plugins/MapExtension/map-data/` is not updated; when a release changes the catalog, copy that folder again from the client archive.
- The server build is not covered by the sidecar. Sync protocol v6 requires matching client and server builds, so update both DLLs together and replace the dedicated-server DLL by hand.

Automatic updates can be disabled modloader-wide with `[AutoUpdate] Enabled=0` in `modloader.ini`.

### Upgrading from 0.7

- Replace the client DLL and copy the new `Plugins/MapExtension/` folder next to it.
- `MapExtensionViewer.html`, `map-tiles/` and the `map-data/` folder you kept next to the viewer are no longer used; delete them.
- The in-game map is now enabled by default. An existing `MapExtension_Plugin.ini` keeps its own `[InGameMap] Enabled` value: set it to `1` if it is `0`.
- The local HTTP API (`/health`, `/cargo`, `/rupture-cycle`) is removed with the viewer. An older copy of the viewer no longer receives data, and the `[Http] Port` setting is ignored; the plugin opens no network port.
- Dedicated server: sync protocol is still v6; update the server DLL together with the client.

## Project status

The project keeps evolving; feedback, fixes, and contributions are welcome.

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
LogResourceObservations=0
LogActorScanFallback=0
LogRefreshTimings=0

[Runtime]
RefreshIntervalMs=2000

[InGameMap]
Enabled=1
ToggleKey=F8
TileBudgetMiB=64
```

- `Enabled`: enables or disables the plugin (`1` or `0`)
- `VerboseLifecycleLogs`: enables lifecycle logs (`1` or `0`)
- `LogRuntimePlanOnce`: logs the runtime strategy once (`1` or `0`)
- `LogCargoSnapshots`: logs cargo snapshots (`1` or `0`)
- `LogRuptureCycleEvents`: logs rupture cycle state changes and rupture-related world/server events (`1` or `0`)
- `LogResourceObservations`: logs observed resource state changes, local generation resets and changes in the number of ambiguous depletion records (`1` or `0`). Enable with `LogRuptureCycleEvents` on the solo/host/server authority to compare a harvest and a full cycle. These are observation times, not exact harvest times or player attribution; a short-lived actor may disappear between captures. Native harvest hooks still require executable-specific validation.
- `LogActorScanFallback`: logs actor scan fallback (`1` or `0`)
- `LogRefreshTimings`: logs per-phase refresh timings (`1` or `0`)
- `RefreshIntervalMs`: sets the runtime refresh interval in milliseconds
- `InGameMap.Enabled`: enables the in-game map panel, on by default (`1` or `0`). Applies immediately, no restart needed
- `InGameMap.ToggleKey`: key that opens or closes that panel (default `F8`)
- `InGameMap.TileBudgetMiB`: approximate video memory allowed for full-resolution terrain tiles (default `64`; `0` keeps the low-resolution preview only)

## Credits

- Thanks to `AlienXAXS` for `StarRupture-ModLoader`, the mod loader that made `MapExtension_Plugin` possible:
  `https://github.com/AlienXAXS/StarRupture-ModLoader`
- Thanks to `bithoarder` for `StarRuptureMap`, whose map was the background of the former web viewer:
  `https://github.com/bithoarder/StarRuptureMap/`
- This project is also inspired by `StarRuptureSaveMap`:
  `https://github.com/thanamatos/StarRuptureSaveMap`
- Thanks as well to the StarRupture developers for the game itself.

## Disclaimer

This is a modding tool. Use it at your own risk.

The authors cannot be held responsible for damage, regressions, or incompatibilities caused by its use. Every effort is made to reduce impact on save files and future compatibility, but that cannot be guaranteed.
