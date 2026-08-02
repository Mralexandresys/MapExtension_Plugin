# MapExtension_Plugin

French README: `README.fr.md`

`MapExtension_Plugin` exposes StarRupture map data through a local HTTP endpoint and includes `mapview`, a local web interface used to display the map, entities, their connections, and the rupture cycle timeline.

The plugin works in both single-player and multiplayer. For solo/local sessions, install the client build only. For dedicated-server sessions, install the client build on the player machine and the server build on the dedicated server so the server can send map and rupture-cycle data to connected clients.

## Features

- See which `Cargo Dispatchers` are linked to which `Cargo Receivers`, and vice versa
- View their positions directly on the map
- See the items currently travelling through the network
- Display the positions of `teleporters`
- Display the positions of `players`, with your own player highlighted in a distinct color
- Display abandoned bases and plant resources as points of interest (POIs)
- Distinguish available and depleted plant resources, with a stable color assigned per resource
- Use a compact rupture-cycle view with phase, remaining-time, legend, and timeline details
- Filter the map down to personal markers and zones only
- Center the map on your own player with the `Player` control or the `P` shortcut
- Expose `GET /health`, `GET /cargo`, and `GET /rupture-cycle` on the local HTTP server
- Receive authoritative map and rupture-cycle snapshots from the server build in dedicated-server sessions
- Fall back to the local `UCrEnviroWaveSubsystem` in solo/local sessions when no server snapshot is available

## Mapview

The included `mapview` is a local web UI designed to read the plugin data and display it in a browser by opening the generated `dist/MapExtensionViewer.html` file.

When packaged, keep the generated `map-tiles/` folder next to `MapExtensionViewer.html`; the viewer loads the map background from those tiles.

It consumes both cargo/map data and the rupture cycle endpoint to render the timeline shown in the HUD replacement UI.

Abandoned bases use their own map icon. Plant resources use a stable palette color derived from the resource name, so the same resource keeps the same color after refreshes; available resources use a solid marker, while depleted resources use a faded outlined marker. Separate filters control abandoned bases and plant resources.

The viewer can reduce the rupture timeline to a compact bar; hover it or focus it with the keyboard to show the current phase, remaining time, legend, and timeline ticks. The filters can hide every game entity and leave only personal markers and zones, and the `Player` control or `P` shortcut centers the map on the first reported player position.

## `/cargo` POI data

`GET /cargo` includes POI totals in `counts` and a `pois` array:

```json
{
  "counts": {
    "pois": 1,
    "abandoned_bases": 0,
    "plant_resources": 1
  },
  "pois": [
    {
      "kind": "plant_resource",
      "label": "Example Plant",
      "resource": "Example Resource",
      "depleted": false,
      "source": "actor_scan.gatherable",
      "unique_key": "example-plant-key",
      "world": { "x": 0.0, "y": 0.0, "z": 0.0 },
      "map": { "x": 0.0, "y": 0.0 }
    }
  ]
}
```

- `kind` is `abandoned_base` or `plant_resource`.
- `label` is the display label; `resource` is the detected resource name and can be empty for an abandoned base.
- `depleted` is the resource state: `false` means available, while `true` means depleted or permanently gathered. There is no separate `available` field.
- `source` identifies the capture path, `unique_key` identifies the POI to the viewer, `world` contains Unreal `x`/`y`/`z` coordinates, and `map` contains projected `x`/`y` coordinates.
- `counts.pois` is the total POI count; `counts.abandoned_bases` and `counts.plant_resources` contain the per-kind totals.

## Dedicated-server sync compatibility

Dedicated-server snapshots use sync protocol v3, which carries POIs in pages of up to 64 entries per request, flags each client's own player marker, and validates snapshot IDs, generations, item counts, chunk counts, and page layout before publishing a remote snapshot. Protocol versions must match exactly: a v3 client or server ignores packets from a different protocol version rather than attempting a downgrade.

**Update the client and dedicated-server builds together.** The modloader auto-updater replaces only the client DLL; the dedicated-server DLL must be replaced manually during the same update. Do not leave the two sides on different releases.

## Installation and updates

The client release archive contains:

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `MapExtensionViewer.html`
- `map-tiles/`

Copy the `Plugins/` content into `StarRupture/Binaries/Win64/Plugins/`, then keep `MapExtensionViewer.html` next to its `map-tiles/` folder anywhere on the machine.

`MapExtension_Plugin.json` is the modloader update sidecar. Its only field is `manifest_url`, pointing at the `latest/download` release manifest. When the sidecar is present, the modloader checks for a newer plugin version at startup and replaces `MapExtension_Plugin.dll` before loading any plugin. Installing only the standalone DLL asset disables automatic updates.

Two limits are worth knowing:

- The auto-updater replaces the DLL only. `MapExtensionViewer.html` and `map-tiles/` are never touched, since they live outside the game folder. The viewer detects incompatible payload contracts: when the plugin reports a contract newer than the one the local viewer was built with, a dialog offers a direct download of the matching `MapExtension_Plugin-<tag>-viewer.zip` asset, along with links to the GitHub release and the mod page. Backward-compatible viewer improvements may not trigger that dialog, so install the matching viewer archive manually to receive new UI features. Replace `MapExtensionViewer.html` and `map-tiles/` together, then reload the page.
- The server build is not covered by the sidecar. Sync protocol v3 requires matching client and server builds, so update both DLLs together and replace the dedicated-server DLL by hand.

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

[Experimental]
InGameMapProbe=0

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
- `InGameMapProbe`: enables the experimental client-only native map asset probe, one-segment preview, and progressive full-grid ImGui canvas (`1` or `0`, restart required). The probe may synchronously load the configured terrain asset and copy GPU textures on the game thread, causing brief test-time stalls; full-grid loading is capped at an estimated 128 MiB RGBA. The browser viewer remains the primary UI.
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
