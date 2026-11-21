# MapExtension_Plugin

French README: `README.fr.md`

`MapExtension_Plugin` adds a logistics map inside StarRupture. Press `F8` in game to open a ModLoader panel that draws the game's own map terrain with your cargo network, teleporters, players, base cores, monoliths, antennas, resources and the rupture cycle.

Since version 0.8 the map lives in the game. The former browser viewer (`MapExtensionViewer.html`, `map-tiles/`) and its local HTTP API are removed; see [Upgrading from 0.7](#upgrading-from-07).

The plugin works in solo and in co-op; install the client build only. StarRupture ended dedicated-server support, so no server package is published any more (the last one, 0.8, is no longer maintained).

In co-op (one player hosts the game), every player installs the client build. The host can share their map with the joining players (experimental, not tested in a real co-op game yet; feedback is welcome): tick **Share map with co-op players (experimental)** in the in-game map toolbar (saved as `[Coop] ShareMapWithClients`). Joining players then see the host's cargo network, teleporters, players, points of interest and rupture cycle; the toolbar shows `Map: from host`, or `Map: local` when the host does not share or runs a different MapExtension version.

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

## Where the data comes from

- **Cargo, teleporters, players, rupture cycle**: read from the running game (or sent by the host in co-op when they share their map), refreshed every `RefreshIntervalMs`.
- **Antennas, abandoned bases, discovery states, base core names and levels**: read from the data the game replicates for its own map.
- **Monoliths and base core positions**: monolith state and charge only exist in the game's internal entity data, and the game publishes its base markers tens of seconds after a world loads. The client build reads both through a read-only Mass entity query whose functions and sizes all come from the mod loader's version map. If the loader does not cover a game build, it turns off only that feature ("In-game map: monolith state and charge"): monoliths lose their state, base cores appear with the game's own delay, and the rest of the plugin keeps working.
- **After a game update**: the client asks the mod loader for every game value by name (member offsets, struct sizes, game functions, enum values) instead of using the positions of the game build it was compiled for, so the same DLL keeps working after a game update, with no new MapExtension version. The mod loader updates its version map at each start. A value it cannot find on a new build turns off only the map feature that needs it.
- **Ore deposits, plants, creatures, points of interest**: a static catalog shipped with the client archive in `Plugins/MapExtension/map-data/`. Positions are pre-generated; they are not live observations.
- **Ignitium and Star Tears**: observed in areas loaded near players, then projected from the rupture phase.

The plugin only observes. It does not automate gameplay, change saves, or issue in-game commands.

## Ignitium and Star Tears

Positions come from runtime actors validated in loaded areas. While the published timeline is in Arcadia stable (from 690 seconds in the 3240-second cycle), a validated, unharvested Ignitium site is exposed as Star Tears instead of Ignitium; an actual Star Tears observation at the same site replaces that projection. During the stabilizing transition the in-game map shows Ignitium only where both are reported at the same site.

Markers distinguish available, not harvestable at the moment, depleted and estimated-from-cycle states. Unloading an area is not evidence of harvesting.

The timeline uses a calibrated 3240-second cycle (30/60/600/2430/120 seconds), animated between observations. It can drift from the game.

## Dedicated-server sync compatibility

No server package is published any more. The client still speaks sync protocol v6, so the last server DLL (0.8) keeps working with it, unmaintained. Protocol versions must match exactly: a v6 client ignores packets from another protocol version.

## Installation and updates

Requires [StarRupture-ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader).

The client release archive contains:

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `Plugins/MapExtension/map-data/` (static world catalog)

Install:

1. Copy the content of the archive's `Plugins/` folder into `StarRupture/Binaries/Win64/ModLoader/Plugins/`. Keep the `MapExtension/` folder next to the DLL: without it the map still opens, but ore deposits, plants, creatures and points of interest are missing.
2. Start the game, load a save, and press `F8`.

`MapExtension_Plugin.json` is the modloader update sidecar. Its only field is `manifest_url`, pointing at the `latest/download` release manifest. When the sidecar is present, the modloader checks for a newer plugin version at startup and replaces `MapExtension_Plugin.dll` before loading any plugin. Installing only the standalone DLL asset disables automatic updates.

One limit is worth knowing:

- The auto-updater replaces the DLL only. `Plugins/MapExtension/map-data/` is not updated; when a release changes the catalog, copy that folder again from the client archive.

Automatic updates can be disabled modloader-wide with `[AutoUpdate] Enabled=0` in `modloader.ini`.

### Upgrading from 0.9

- Replace the client DLL; the `Plugins/MapExtension/` folder does not change.
- The map now keeps working after a game update, without waiting for a new MapExtension version (see "After a game update" above). Keep the Mod Loader up to date.
- New diagnostic option `[Diagnostics] FieldAudit` (default `0`).
- Co-op (experimental, not tested in a real co-op game yet): new `[Coop] ShareMapWithClients` option (default `0`), toggled by the host from the in-game map. Host and joining players must run the same MapExtension version. Feedback is welcome.
- Dedicated server: StarRupture ended dedicated-server support, so no server package is published any more. Sync protocol is still v6; the 0.8 server DLL keeps working with this client.

### Upgrading from 0.8

- Replace the client DLL; the `Plugins/MapExtension/` folder does not change.
- A window now opens in the middle of the screen each time a game is loaded. It says the map opens in game with the toggle key, and asks players coming from a version older than 0.7 to download the mod again from Nexus Mods. Close it with OK; set `[InGameMap] ShowPopup=0` to stop showing it.
- `[InGameMap] Enabled` is removed: the in-game map is always on, and an old `Enabled=0` is ignored.
- Dedicated server: sync protocol is still v6 and the server build has no change; the 0.8 server DLL keeps working with the 0.9 client.

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
FieldAudit=0

[Runtime]
RefreshIntervalMs=2000

[Coop]
ShareMapWithClients=0

[InGameMap]
ToggleKey=F8
TileBudgetMiB=64
ShowPopup=1
```

- `Enabled`: enables or disables the plugin (`1` or `0`)
- `VerboseLifecycleLogs`: enables lifecycle logs (`1` or `0`)
- `LogRuntimePlanOnce`: logs the runtime strategy once (`1` or `0`)
- `LogCargoSnapshots`: logs cargo snapshots (`1` or `0`)
- `LogRuptureCycleEvents`: logs rupture cycle state changes and rupture-related world/server events (`1` or `0`)
- `LogResourceObservations`: logs observed resource state changes, local generation resets and changes in the number of ambiguous depletion records (`1` or `0`). Enable with `LogRuptureCycleEvents` on the solo/host/server authority to compare a harvest and a full cycle. These are observation times, not exact harvest times or player attribution; a short-lived actor may disappear between captures. Native harvest hooks still require executable-specific validation.
- `LogActorScanFallback`: logs actor scan fallback (`1` or `0`)
- `LogRefreshTimings`: logs per-phase refresh timings (`1` or `0`)
- `FieldAudit`: for troubleshooting after a game update. Also reads every game value the old way (at the position it had in the game build the mod was compiled for) and logs, at load and every 60 s, the values that moved or differ (`1` or `0`, default `0`; read at load)
- `RefreshIntervalMs`: sets the runtime refresh interval in milliseconds
- `Coop.ShareMapWithClients`: when you host a co-op game, sends your map to joining players running the same MapExtension version (default `0`). Experimental. Also toggled by the **Share map with co-op players (experimental)** box of the in-game map, shown to the host only
- `InGameMap.ToggleKey`: key that opens or closes that panel (default `F8`)
- `InGameMap.TileBudgetMiB`: approximate video memory allowed for full-resolution terrain tiles (default `64`; `0` keeps the low-resolution preview only)
- `InGameMap.ShowPopup`: each time a game is loaded, shows a centered window, closed with its OK button, saying the map opens in game with the toggle key and that players coming from a version older than 0.7 must download the mod again from Nexus Mods (default `1`; `0` hides it)

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
