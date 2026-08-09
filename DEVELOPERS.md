# MapExtension_Plugin developers

## Repository layout

- plugin source: `MapExtension_Plugin/`
- frontend source: `MapExtension_Plugin/mapview/`
- frontend build output: `MapExtension_Plugin/mapview/dist/MapExtensionViewer.html`, `map-tiles/`, and `map-data/`
- static world-catalog generator: `MapExtension_Plugin/tools/build_map_data.py`

## Runtime module layout

- `plugin.cpp`: plugin metadata, startup, shutdown, and hook registration
- `map_state_runtime.cpp`: public runtime facade used by the plugin entrypoints
- `map_state_capture.cpp` / `map_state_capture.h`: world scanning, snapshot refresh, and gameplay callbacks
- `map_state_full_scan.cpp` / `map_state_full_scan.h`: client-only progressive full-map World Partition scan controller
- `map_state_http.cpp` / `map_state_http.h`: local HTTP server and endpoint routing
- `map_state_json.cpp` / `map_state_json.h`: JSON serialization for `/health`, `/cargo`, and `/rupture-cycle`
- `map_state_poi_store.cpp` / `map_state_poi_store.h`: per-save-session disk persistence for discovered plant POIs
- `map_state_types.h`: shared snapshot types and map projection constants
- `client/map_sync_client.cpp` / `client/map_sync_client.h`: client-side snapshot requests and plugin-network handling
- `client/map_state_remote_cache.cpp` / `client/map_state_remote_cache.h`: client cache for remote rupture/cargo snapshots
- `server/map_sync_server.cpp` / `server/map_sync_server.h`: server-side snapshot capture and response streaming
- `shared/map_sync_protocol.h`: shared packet definitions for client/server snapshot sync

Keep the runtime split along these boundaries. Do not move HTTP or JSON formatting back into `map_state_runtime.cpp` unless the split is being intentionally reverted.

## Third-party code

- JSON serialization uses vendored `nlohmann/json` in `third_party/nlohmann/json.hpp`.
- License and notice files must stay in sync with that vendored header:
  - `licenses/nlohmann-json.MIT.txt`
  - `THIRD_PARTY_NOTICES.md`

## Prerequisites

- Visual Studio 2022 (17.8 or newer) with the Desktop development with C++ workload and the Windows 10 SDK.
- SDK layout: `StarRupture-Plugin-SDK` with `include/`, `Shared.props`, and `StarRupture SDK/`
- Node.js 20.19.0 or newer, or Node.js 22.12.0 or newer, and npm for the `mapview` build; run `node --version` before working to ensure you are not on an unsupported runtime.

## Plugin build

1. Open `MapExtension_Plugin.sln` in Visual Studio 2022.
2. Select `Client Debug|x64`, `Client Release|x64`, or `Server Release|x64`.
3. Build `MapExtension_Plugin`.

Use only `Server Release|x64` for server builds so the output matches the packaging flow.

The project resolves these SDK paths:

- `PluginSdkSharedProps=..\StarRupture-Plugin-SDK\Shared.props`
- `PluginApiIncludeDir=..\StarRupture-Plugin-SDK\include\`
- `StarRuptureSdkBaseDir=..\StarRupture-Plugin-SDK\StarRupture SDK\`

The helper scripts at the repository root set these properties for you.

The DLL is written to `build\\<Configuration>\\Plugins\\MapExtension_Plugin.dll`.

### Release identifier

`plugin.cpp` exports `MODLOADER_BUILD_TAG` and `PluginInfo.author`. For tagged builds, define the build tag at compile time so the loader and `/health` endpoint expose the correct version string. If needed, override the embedded author the same way.

- override the preprocessor definition in Visual Studio: **Project Properties → C/C++ → Preprocessor → Preprocessor Definitions**.
- or pass MSBuild properties from the helper script:
  - `./build_client.sh release --build-tag "ML-2026.04.04-214044-v0.2" --build-author "Mralexandresys"`
- or call MSBuild directly with properties such as `/p:ModLoaderBuildTag=ML-2026.04.04-214044-v0.2 /p:ModLoaderBuildAuthor=Mralexandresys`.

If the tag macro is not set, builds fall back to `"dev"`. If the author macro is not set, builds fall back to `"Mralexandresys"`.

## Standalone developer workflow

`MapExtension_Plugin` uses a standalone public-SDK workflow.

Expected checkout layout:

```text
workspace/
  MapExtension_Plugin/
  StarRupture-Plugin-SDK/
```

Equivalent layouts are supported as long as the MSBuild include/props/SDK properties point at the right roots.

Workflow:

1. Clone `MapExtension_Plugin`.
2. Clone `StarRupture-Plugin-SDK` next to it.
3. Open `MapExtension_Plugin/MapExtension_Plugin.sln`.
4. Build the desired client or server configuration.

No manual edit of a parent solution is required if you use the helper scripts.

## Root helper scripts

From the repository root:

```bash
./build_client.sh release
./build_server.sh release
```

To inspect the latest build logs manually:

```bash
./summarize_build.sh client
./summarize_build.sh server
```

## Frontend build

From `MapExtension_Plugin/mapview/`:

```bash
npm install
npm run check
npm run build
```

The production build entry point is:

```text
MapExtension_Plugin/mapview/dist/MapExtensionViewer.html
```

The build output also includes `mapview/dist/map-tiles/` and `mapview/dist/map-data/` alongside the HTML entry point. Both folders must stay next to `MapExtensionViewer.html` for direct `file://` use.

## Static world catalog

`tools/build_map_data.py` converts local reverse-engineering exports into the compact files committed under `mapview/public/map-data/`. Its default inputs are:

- `analyse_map/map_v2_resources.jsonl`
- `analyse_map/map_v2_placements.jsonl`
- `analyse_map/map_v2_pois.geojson`
- `analyse_map/map_v2_catalog.json`

Run it from the repository root:

```bash
python3 tools/build_map_data.py
```

`analyse_map/` is a strictly local, ignored input and must not be committed. The generated `mapview/public/map-data/*.js` files are build inputs and must be committed because CI does not have the raw exports.

The format uses integer world decimetres, metre altitudes, dictionary/group indexes, locality sorting, and delta-encoded coordinate arrays. Multi-mesh HISM duplicates are merged within 50 cm; PCG points covered by nearby HISM instances are tagged separately so the viewer can hide them by default. Placements are split into building, zone, and technical parts, and supported box shapes retain a projected ground footprint.

The files contain compact JSON wrapped as `SRMAPDATA(<payload>);`. This JSONP wrapper is intentional: browsers commonly block `fetch()` from a page opened through `file://`, while classic relative `<script src>` loading remains available. Do not replace it with `fetch()` unless the distribution model changes to an HTTP-served viewer.

The viewer loads parts lazily by enabled layer. `StaticMapCanvas.vue` renders large point and placement layers on canvas; the existing SVG remains responsible for live entities, annotations, and selectable catalog POIs. World coordinates are projected with `mapProjection.ts`, whose defaults must stay synchronized with `map_state_types.h`; a reachable plugin can override those defaults through the `map` object in `/cargo`.

## Packaging a release

1. Set `MODLOADER_BUILD_TAG` to the version you want to publish and, if needed, `MODLOADER_BUILD_AUTHOR` to the release author (see above), then build both `Client Release|x64` and `Server Release|x64` so that these files are produced:
   - `build/Client Release/Plugins/MapExtension_Plugin.dll`
   - `build/Server Release/Plugins/MapExtension_Plugin.dll`
   - Build the client DLL with `./build_client.sh release` and the server DLL with `./build_server.sh release`.
2. Move to `mapview/`, ensure Node.js 20.19.0+ or 22.12.0+ is active, then run `npm install && npm run check && npm run build`. The bundle lands in `mapview/dist/MapExtensionViewer.html`.
3. Create a client archive containing:
   - `build/Client Release/Plugins/MapExtension_Plugin.dll`
   - `Plugins/MapExtension_Plugin.json`, the update sidecar whose only field is `manifest_url` (see the GitHub Actions release section below); omit it only if the archive is not meant to receive automatic updates
   - `mapview/dist/MapExtensionViewer.html`
   - `mapview/dist/map-tiles/`
   - `mapview/dist/map-data/`
4. Create a viewer-only archive containing `mapview/dist/MapExtensionViewer.html`, `mapview/dist/map-tiles/`, and `mapview/dist/map-data/`. This is what the in-app update dialog links to, so it must be named `MapExtension_Plugin-<tag>-viewer.zip`.
5. Create a server archive containing:
   - `build/Server Release/Plugins/MapExtension_Plugin.dll`
6. (Optional) add `README.md`, `README.fr.md`, `LICENSE`, notice files, and `licenses/` content alongside the binaries if you want a fuller release bundle.
7. (Optional) include a sample `Plugins/config/MapExtension_Plugin.ini` if you want to ship defaults with instructions.
8. Verify that no files from `.gitignore` leaked into the packages, then sign or checksum the archives before publishing them.

## GitHub Actions release

`MapExtension_Plugin` also ships a manual GitHub Actions release workflow.

1. Open **Actions** in the `MapExtension_Plugin` repository.
2. Run the `Release` workflow.
3. Optionally provide `modloader_tag` to build against a specific `StarRupture-Plugin-SDK` tag. If empty, the workflow uses the latest published SDK release.
4. Provide `plugin_version` such as `v0.2`.
5. Optionally provide `build_author`. If left empty, the workflow uses the GitHub user who started it.
6. Choose whether the GitHub release should be created as a draft.

The workflow:

1. fetches the latest published release tag from `AlienXAXS/StarRupture-Plugin-SDK`
2. checks out `MapExtension_Plugin` into a local `plugin/` workspace path
3. checks out `StarRupture-Plugin-SDK` on the same release tag into a local `sdk/` workspace path
4. builds both the client and server plugin DLLs against that SDK checkout, then builds the `mapview` bundle
5. creates a client zip with the client DLL, `MapExtensionViewer.html`, and `Plugins/MapExtension_Plugin.json` sidecar whose only field is `manifest_url`; `.pdb` files are included when available
6. publishes `MapExtension_Plugin-client-manifest.json` and a direct client DLL asset for the modloader auto-updater
7. creates a separate server zip with the server DLL
8. creates a viewer-only zip `MapExtension_Plugin-<tag>-viewer.zip` with `MapExtensionViewer.html`, `map-tiles/`, and `map-data/`, which is the asset the viewer update dialog points users to
9. creates a plugin tag in the format `ML-<sdk-version>-vX.Y` (for example `ML-2026.04.09-200640-v0.2` or `ML-v1.2.0-v0.2`, depending on the selected SDK tag)
10. publishes a GitHub release in the plugin repository

The modloader auto-updater replaces `MapExtension_Plugin.dll` only. `MapExtensionViewer.html`, `map-tiles/`, and `map-data/` live outside the game folder and are never updated, so any change to the `/cargo`, `/health`, or `/rupture-cycle` payload shape must stay backward compatible with an older viewer, or bump the viewer contract version described below so the viewer prompts the user to download the viewer zip.

The server build ships no sidecar and is not auto-updated. Sync protocol v4 requires an exact protocol-version match, so releases using it must tell server admins to update the client and dedicated-server DLLs together. A mixed-version pair ignores incompatible packets and cannot publish a remote snapshot.

`interface_version_min`/`interface_version_max` in the manifest are read from `PLUGIN_INTERFACE_VERSION_MIN`/`PLUGIN_INTERFACE_VERSION_MAX` in the SDK header, matching the SDK's reference workflow. The loader only checks that this range overlaps its own, so the published range is wider than the single `PLUGIN_INTERFACE_VERSION` the DLL actually declares.

## Plant POI capture

Plant POIs are deliberately limited by the reward class in `ACrGatherableBaseActor::InteractionRewardResource`, not by localized display text. The accepted classes are `I_Hydrobulb_C`, `I_Polifruit_C`, `I_Oxallop_C`, `I_Purplant_C`, `I_SerpentRoot_C`, `I_Prickler_C`, `I_PrismHerb_C`, and `I_Sulheart_C`.

World Partition and PCG/Mass only materialize gatherable actors in loaded areas. `CapturePois` therefore merges available observations into a per-world cumulative catalog. A cell unloading does not remove its markers. Live `bIsDepleted`/`bIsPermanentlyGathered` state and `ACrGatherableSpawnersRepActor` depleted-location arrays remove gathered markers even when PCG has already removed the visual actor; removal matches the exact rounded-coordinate key first and then any catalog plant within a small world-space radius (`kDepletedPlantRemovalRadius`), because replicated depleted locations can differ slightly from the observed actor location. The catalog is cleared on world transitions and engine shutdown.

Discovered plants are persisted through `map_state_poi_store.cpp` as a per-save-session TSV cache under `Plugins/MapExtension_Plugin/poi_cache/`. The storage key combines the world name and the `UCrSaveSubsystem` save-session name (resolved with backoff, falling back to the world name alone if no session identity appears). Persisted entries are merged into the catalog on the first scan with source `persisted_catalog`, before depleted-location cleanup so stale entries are corrected by current game state; the cache file is rewritten whenever the POI revision changes.

The SDK has no intact-plant registry containing type and position for the entire procedural world. Normal capture is passive progressive coverage. The explicit solo/local full-map action in `map_state_full_scan.cpp` is the only exception: it creates a transient `ATargetPoint` with a `UWorldPartitionStreamingSourceComponent`, traverses an overlapping serpentine grid over the calibrated map bounds, waits for streaming and PCG, then requests the existing POI capture/persistence path. It never moves the player and never loads all zones simultaneously.

The controller is a game-thread state machine. ImGui only sets atomic start/cancel requests. During its first pass it temporarily enables `UMassLODSubsystem::bGatherStreamingSources` and `bAllowNonPlayerViwerActors`, restoring both original bits on cleanup. Capture reports live tracked actors separately from persisted markers. If the whole source-only pass sees no live tracked actor, the scanner repeats in player-fallback mode: it freezes the local character, disables collision, moves it with the source, and restores its immutable original transform and movement/input state before cleanup. Every zone has streaming and capture timeouts; timeout zones produce a partial result rather than a false complete result. Cancellation, world end, engine shutdown, and plugin shutdown must restore player/Mass state, disable the component, and destroy its owner exactly once. Keep this feature client-only and reject `bIsDedicatedServer` sessions.

The catalog is sorted by public key and assigned a stable 64-bit FNV-1a content revision over the fields sent on the wire. Identical content keeps the same revision across refreshes/processes; any addition, removal, state, coordinate, label, resource, source, or key change invalidates retained dedicated-server pages.

## Dedicated-server sync protocol v4

`shared/map_sync_protocol.h` defines the POD request, begin, chunk, rupture, and end packets shared by client and server builds. The protocol version is intentionally exact-match only; there is no downgrade path.

Protocol v2 added POIs to the existing rupture, player, teleporter, cargo-marker, and cargo-connection stream; protocol v3 added POI pagination and a per-recipient "self" player flag; protocol v4 adds exact POI-catalog revision tracking:

- `kRequestFlagPois` and `kSnapshotHasPois` identify POI content. All request flags are reserved; the server intentionally ignores `request_flags` and returns a complete snapshot.
- POIs are paginated: each `ClientSnapshotRequestPacket` carries a `poi_page`, and the server responds with at most `kPoiPageCapacity` POIs (currently 64, i.e. `kPoiChunksPerPage` = 16 chunks) for that page. `ServerSnapshotBeginPacket` declares the page slice via `pois_count`/`pois_chunk_count` plus `poi_page`, `poi_page_count`, `pois_total_count`, and `poi_revision`; `ServerSnapshotEndPacket` repeats the page counters, total, and revision.
- The client retains pages only for the exact `(world, poi_revision, poi_page_count, pois_total_count)` tuple. A changed revision clears every retained page before the new page is published, preventing removals or index shifts from leaving stale or duplicate markers. The client rejects duplicate/empty public keys and validates the fully assembled total.
- `ServerPlayerEntry` carries a `flags` byte; `kPlayerEntrySelf` marks the marker that belongs to the requesting player. The server matches the requesting player controller against the captured player keys, so each connected client sees its own marker flagged.
- `ServerPoiEntry` carries world coordinates, `kind`, the `kPoiEntryDepleted` flag, label, resource, source, and unique key.
- `ServerPoisChunkPacket` carries at most `kPoiChunkCapacity` entries (currently four). The packet remains trivially copyable and is statically limited to the recommended 1 KiB payload size.
- The server rejects collections that cannot be represented by the `uint16_t` wire counters. The client validates the snapshot ID, generation, POI revision, begin/end counts and totals, chunk counts, chunk indexes, per-chunk item counts, page layout, and merged public-key uniqueness before publishing the assembled snapshot.

**Always update the client and dedicated-server builds together when deploying protocol v4.** The client sidecar updates only `MapExtension_Plugin.dll` on player machines; it does not update the dedicated-server DLL. A mismatched pair will ignore each other's packets, so the server DLL must be replaced manually during the same rollout.

## Runtime contract

- `GET /health`: status, world, generation, and entity counts
- `GET /cargo`: current snapshot payload used by the frontend
- `GET /rupture-cycle`: current rupture-cycle payload used by the frontend timeline

`/cargo` remains the compatibility endpoint consumed by the current frontend even though the payload now includes cargo links, teleporters, players, and POIs.

`/rupture-cycle` remains a separate endpoint consumed by the frontend for the timeline view.

The frontend endpoint is editable in the UI, but defaults to `http://127.0.0.1:9000`.

### `/cargo` POI contract

The payload includes `counts.pois`, `counts.abandoned_bases`, and `counts.plant_resources`, plus a top-level `pois` array:

```json
{
  "counts": {
    "pois": 2,
    "abandoned_bases": 1,
    "plant_resources": 1
  },
  "pois": [
    {
      "kind": "abandoned_base",
      "label": "Abandoned Base",
      "resource": "",
      "depleted": false,
      "source": "actor_scan.abandoned_base",
      "unique_key": "example-abandoned-base-key",
      "world": { "x": 0.0, "y": 0.0, "z": 0.0 },
      "map": { "x": 0.0, "y": 0.0 }
    },
    {
      "kind": "plant_resource",
      "label": "Hydrobulb",
      "resource": "Hydrobulb",
      "depleted": false,
      "source": "actor_scan.gatherable",
      "unique_key": "example-plant-key",
      "world": { "x": 0.0, "y": 0.0, "z": 0.0 },
      "map": { "x": 0.0, "y": 0.0 }
    }
  ]
}
```

| Field | Contract |
| --- | --- |
| `kind` | `abandoned_base` or `plant_resource`. |
| `label` | Canonical display label for tracked plants, or the fixed abandoned-base label. |
| `resource` | Canonical tracked plant name; empty when no resource applies, including abandoned bases. |
| `depleted` | Backward-compatible boolean field. The current catalog emits only available plants (`false`) and omits `bIsDepleted`/`bIsPermanentlyGathered` actors. Abandoned bases also emit `false`. |
| `source` | Capture-path identifier such as `actor_scan.abandoned_base` or `actor_scan.gatherable`. |
| `unique_key` | Public identity used by the viewer for selection and rendering. |
| `world` | Unreal coordinates as numeric `x`, `y`, and `z`. |
| `map` | Projected map coordinates as numeric `x` and `y`. |

`counts.pois` equals the length of `pois`; `counts.abandoned_bases` and `counts.plant_resources` are the per-kind totals and add up to that total.

The viewer renders abandoned bases with a dedicated fixed-color icon. Plant resources use a stable FNV-1a hash of `resource || label || unique_key`, after trimming and lowercasing, to derive an HSL color (hue from the full hash range, saturation 62–82%, lightness 60–72%). The mapping is independent of payload order, and distinct resource names rarely share the same hue. Current plugin payloads contain only available plants and use a solid core; the viewer keeps the faded dashed/outlined rendering for backward compatibility with older payloads that contain `depleted: true`.

Player entries in `/cargo` carry an optional boolean `self`, set to `true` on the marker representing the local viewer's own player (the primary local player in solo sessions, or the marker flagged by the dedicated server for this client). The viewer renders the `self` player with a distinct color and prefers it when centering on the player. Older plugins omit the field.

## Viewer contract version

Because the auto-updater replaces `MapExtension_Plugin.dll` only, a recent plugin can end up serving payloads to an older `MapExtensionViewer.html`. `/health` therefore also returns:

- `plugin_version`: the build tag baked in through `/p:ModLoaderBuildTag`
- `viewer_contract_version`: `kViewerContractVersion` in `map_state_json.cpp`
- `viewer_update`: `mod_page_url`, plus `release_url` and `download_url` on published builds

The viewer compares `viewer_contract_version` against `VIEWER_CONTRACT_VERSION` in `mapview/src/lib/viewerContract.ts` and shows an update dialog when the plugin reports a higher value.

Rules:

- Bump `kViewerContractVersion` and `VIEWER_CONTRACT_VERSION` together, in the same change, whenever a payload change breaks older viewers (removed or renamed fields, changed semantics, changed coordinate projection).
- Do not bump for purely additive, backward-compatible fields, or every user gets an update prompt for nothing.
- The plugin builds the download URL, not the viewer. The viewer that shows the prompt is by definition the outdated one, so it must not carry a hardcoded URL pattern. `MapExtension_Plugin-<tag>-viewer.zip` in `.github/workflows/release.yml` and `BuildViewerUpdateJson` in `map_state_json.cpp` must stay in sync.
- Local builds define `MAPEXTENSION_LOCAL_BUILD` through `PropertySheet.props` and advertise no download URL, since their fallback tag has no published assets.
