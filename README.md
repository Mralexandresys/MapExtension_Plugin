# MapExtension_Plugin

French README: `README.fr.md`

`MapExtension_Plugin` exposes StarRupture map data through a local HTTP endpoint and includes `mapview`, a local web interface used to display the map, entities, their connections, and the rupture cycle timeline.

The plugin works in both single-player and multiplayer, but it only needs to be installed on the client side. For dedicated-server rupture cycle data, pair it with `RuptureCycleToChat_Plugin` on the server.

## Features

- See which `Cargo Dispatchers` are linked to which `Cargo Receivers`, and vice versa
- View their positions directly on the map
- See the items currently travelling through the network
- Display the positions of `teleporters`
- Display the positions of `players`
- Expose `GET /health`, `GET /cargo`, and `GET /rupture-cycle` on the local HTTP server
- Reconstruct rupture cycle state from dedicated-server chat payloads when `RuptureCycleToChat_Plugin` is present
- Fall back to the local `UCrEnviroWaveSubsystem` in solo/local sessions when no server chat payload is available

## Mapview

The included `mapview` is a standalone local web UI designed to read the plugin data and display it in a browser by opening the generated `dist/MapExtensionViewer.html` file.

It consumes both cargo/map data and the rupture cycle endpoint to render the timeline shown in the HUD replacement UI.

## Interface choice

The initial goal was a more direct integration into the in-game UI.

In practice, StarRupture's UI turned out to be too limited to produce something reliable and maintainable in good conditions. The project therefore moved to a local web interface.

This is not necessarily the ideal integration model, but it is currently the most effective way to iterate quickly, display the data correctly, and keep the tool usable.

## Project status

This is a first functional draft.

The project will continue to evolve soon, and feedback, fixes, and contributions are welcome.

The plugin now supports two build methods:

- `modloader-local`: build against the local modloader tree (`Version_Mod_Loader/plugins` + local `StarRupture SDK/`)
- `modloader-ng`: build against `StarRupture-Plugin-SDK`

Examples:

- `../build_client.sh release --build-method modloader-local`
- `../build_client.sh release --build-method modloader-ng`

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
LogRuptureCycleChat=0
LogActorScanFallback=0

[Http]
Port=9000

[Runtime]
RefreshIntervalMs=500

[Chat]
EnableRuptureCycleInfoRequest=1
RuptureCyclePrefix=[RUPTURE_CYCLE]
```

- `Enabled`: enables or disables the plugin (`1` or `0`)
- `VerboseLifecycleLogs`: enables lifecycle logs (`1` or `0`)
- `LogRuntimePlanOnce`: logs the runtime strategy once (`1` or `0`)
- `LogCargoSnapshots`: logs cargo snapshots (`1` or `0`)
- `LogRuptureCycleChat`: logs rupture cycle state parsed from server chat or recovered locally in solo sessions (`1` or `0`)
- `LogActorScanFallback`: logs actor scan fallback (`1` or `0`)
- `Port`: sets the local HTTP port used by the plugin
- `RefreshIntervalMs`: sets the runtime refresh interval in milliseconds
- `EnableRuptureCycleInfoRequest`: enables or disables the client chat request `get info arcadia` used in dedicated-server sessions (`1` or `0`)
- `RuptureCyclePrefix`: chat prefix expected from the server-side rupture plugin

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
