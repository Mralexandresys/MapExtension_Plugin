# MapExtension_Plugin

French README: `README.fr.md`

`MapExtension_Plugin` exposes StarRupture map data through a local HTTP endpoint and includes `mapview`, a local web interface used to display the map, entities, their connections, and the rupture cycle timeline.

The plugin works in both single-player and multiplayer. For solo/local sessions, install the client build only. For dedicated-server sessions, install the client build on the player machine and the server build on the dedicated server so the server can send map and rupture-cycle data to connected clients.

## Features

- See which `Cargo Dispatchers` are linked to which `Cargo Receivers`, and vice versa
- View their positions directly on the map
- See the items currently travelling through the network
- Display the positions of `teleporters`
- Display the positions of `players`
- Expose `GET /health`, `GET /cargo`, and `GET /rupture-cycle` on the local HTTP server
- Receive authoritative map and rupture-cycle snapshots from the server build in dedicated-server sessions
- Fall back to the local `UCrEnviroWaveSubsystem` in solo/local sessions when no server snapshot is available

## Mapview

The included `mapview` is a local web UI designed to read the plugin data and display it in a browser by opening the generated `dist/MapExtensionViewer.html` file.

When packaged, keep the generated `map-tiles/` folder next to `MapExtensionViewer.html`; the viewer loads the map background from those tiles.

It consumes both cargo/map data and the rupture cycle endpoint to render the timeline shown in the HUD replacement UI.

## Installation and updates

The client release archive contains:

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `MapExtensionViewer.html`
- `map-tiles/`

Copy the `Plugins/` content into `StarRupture/Binaries/Win64/Plugins/`, then keep `MapExtensionViewer.html` next to its `map-tiles/` folder anywhere on the machine.

`MapExtension_Plugin.json` is the modloader update sidecar. Its only field is `manifest_url`, pointing at the `latest/download` release manifest. When the sidecar is present, the modloader checks for a newer plugin version at startup and replaces `MapExtension_Plugin.dll` before loading any plugin. Installing only the standalone DLL asset disables automatic updates.

Two limits are worth knowing:

- The auto-updater replaces the DLL only. `MapExtensionViewer.html` and `map-tiles/` are never touched, since they live outside the game folder. The viewer detects this on its own: when the plugin reports a payload contract newer than the one the local viewer was built with, a dialog offers a direct download of the matching `MapExtension_Plugin-<tag>-viewer.zip` asset, along with links to the GitHub release and the mod page. Replace `MapExtensionViewer.html` and `map-tiles/` together, then reload the page.
- The server build is not covered by the sidecar. Update it by hand and keep it on the same version as the client, since both sides share `shared/map_sync_protocol.h`.

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

- `./build_client.sh release`

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
