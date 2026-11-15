# MapExtension_Plugin developers

## Repository layout

- plugin source: `MapExtension_Plugin/src/` (paths in this document that start with a source file name, `client/`, `server/` or `shared/` are relative to it)
- static world catalog (committed build input): `MapExtension_Plugin/map-data/`
- static world-catalog generator: `MapExtension_Plugin/tools/build_map_data.py`

The browser viewer (`mapview/`, `MapExtensionViewer.html`, `map-tiles/`) was removed in 0.8. The local HTTP API (`/health`, `/cargo`, `/rupture-cycle`) and its JSON serialization were removed with it. The in-game map is the only consumer of the snapshot on the client.

## Runtime module layout

- `plugin.cpp`: plugin metadata, startup, shutdown, and hook registration
- `map_state_runtime.cpp`: public runtime facade used by the plugin entrypoints
- `map_state_capture.cpp` / `map_state_capture.h`: runtime capture, snapshot refresh, and gameplay callbacks
- `map_state_types.h`: shared snapshot types and map projection constants
- `client/map_sync_client.cpp` / `client/map_sync_client.h`: client-side snapshot requests and plugin-network handling
- `client/map_state_remote_cache.cpp` / `client/map_state_remote_cache.h`: client cache for remote rupture/cargo snapshots
- `server/map_sync_server.cpp` / `server/map_sync_server.h`: server-side snapshot capture and response streaming
- `shared/map_sync_protocol.h`: shared packet definitions for client/server snapshot sync

Keep the runtime split along these boundaries.

## In-game map

Client-only and always on. Original plan: `INGAME_MAP_POC_PLAN.md`; runtime measurements and decisions: `update/ingame_map_poc_journal.md`.

- `client/ingame_map_terrain_source.*` (game thread): terrain and textures. Resolves `UCrMapMenuDevSettings` CDO -> `TerrainData` soft reference -> `UCrMapMenuTerrainData` on every call and returns a POD `TerrainDescriptor`. Copies segment textures, item icons and marker textures (`LoadFromUTexture2D`).
- `client/ingame_map_live_state.*` (game thread): POI states, antennas, base cores and monoliths, including the native read. No UObject pointer is kept across calls, except the two native `UScriptStruct` used by the monolith read (rooted, never collected).
- `client/ingame_map_projection.h`: pure native-grid <-> world transform, with a compile-time round-trip check.
- `client/ingame_map_catalog.*`: static catalog loader (see below).
- `client/ingame_map_ui.*`: activation, panel and hook registration.
- `client/ingame_map_internal.h`: constants, the `Published` snapshot and the few variables shared across threads (`inline`); not included outside the in-game map.
- `client/ingame_map_render.cpp`: render-thread drawing, tooltips, filters and view state.
- `client/ingame_map_service.cpp`: game-thread service. It publishes an immutable snapshot (`Published`: terrain, tile handles, `CargoSnapshot`, catalog, icons, POI states, live structures) every 500 ms while the panel is open; the render callback only reads that published state. Filter and selection state lives on the render thread only. The cargo snapshot comes from `CopySnapshot()`, so solo and dedicated sessions use the existing data path.

Facts measured in game (CL-127004): `MapTerrainSegmentSize` is in metres while the pivot is in centimetres (hence `kSegmentSizeToWorldUnits`); grid index X follows world Y and index Y follows world X; the native map shows the `Radiation2` brushes at radiation level 1, so only that variant is loaded.

### Live game state

| Data | Source | Notes |
| --- | --- | --- |
| POI discovery state | `ACrMapMenuDataReplicationHelper::PointOfInterestReplicationData` | joined to the catalog by marker GUID (`%08X-%08X-%08X-%08X`, A-B-C-D) |
| Abandoned base progress | `ACrAbandonBaseInfoReplicator::AbandonBaseDataContainer` | four collected-status arrays per base |
| Antennas | `ACrAntennasOwner::AntennaContainer` | replaces the catalog "Antenna" group when present |
| Base cores | native read of entities carrying `FCrMassBuildingBaseCoreFragment` (position, available at once), completed by `ACrMapMenuDataReplicationHelper::BuildingsMarkerDataContainer` entries whose unique name contains `core` (name; these markers arrive 16 to 56 s after the world loads) | level, cooling and infection from `ACrBaseCoreReplicationHelper::BaseCoreDataContainer`, joined on `BaseCore.Entity.CachedHandle` (entity index); displayed level is `UpgradeLevel + 1`. Without the native feature, base cores appear when the markers do |
| Monoliths | native read of `FCrAlienObeliskFragment` + `FTransformFragment` | see below; the replicated bubble and loaded actors only exist in multiplayer or near the player |
| Attack threshold | `UCrAlienObeliskTrait::Parameters.ChargeValueToActivateAttack` | 0.5 on CL-127004; percentage = charge / threshold |

POI states are captured every 2 s and live structures every 4 s while the panel is open.

### Native entity read (monoliths and base cores)

No reflected source lists monoliths in solo. `plugin.cpp` exports `OnPluginLoadHooks` (client build only) and calls `ResolveNativeAccess`, which declares the loader feature `ingame-map-monoliths` and resolves two keys:

- `FMassArchetypeData::GetFragmentDataForEntity`: the loader's map first, then the plugin's own 37-byte signature (one match in the CL-127004 executable), checked as a function start.
- `UMassEntitySubsystem::EntityManager`: native field row for CL-127004 (offset `0x38`), same row as the SDK's `plugin_map.h`.

`ScanEntitiesGuarded` walks the entity storage of `FMassEntityManager` (storage at `+0x20`, variant type at `+0x60`, vtable slots `+0x10` archetype and `+0x48` valid index, all verified by disassembling `IsEntityValid` in the installed executable) and asks the game function for the obelisk fragment, the base-core fragment and the transform, in one pass. It is read-only, installs no hook, and runs under `__try/__except`: a fault or an unexpected storage type disables the read for the session. A key that does not resolve turns off only this feature.

Rules when the game updates:

- The local `StarRupture-Game-IDADump/` is not guaranteed to match the installed executable (it did not on CL-127004). Verify bytes and offsets on the installed executable before trusting an address.
- Re-verify the signature count, the storage offsets and the vtable slots, then add the new changelist's field row. Do not widen the gate to an unverified build.

### Icons

- Item icons: `LoadItemIcon` (catalog types, by item class path from `manifest.js`) and `LoadItemIconsByName` (cargo items, one `GObjects` walk).
- Native map marker textures under `/Game/Chimera/UI/Map/Markers/` and the teleporter icon: `LoadTextureAsset`. A freshly loaded texture is not on the GPU yet, so `LoadFromUTexture2D` returns null; the loader forces its mips resident and retries on the next service pass.

### Static catalog

`client/ingame_map_catalog.*` reads `manifest.js`, `resources-*.js` and `pois.js` (the `SRMAPDATA(...)` JSONP files produced by `tools/build_map_data.py`) from `Plugins/MapExtension/map-data/` next to the client DLL, on a background task at activation. Placements are not read.

### Rupture strip and terrain tiles

The rupture strip derives the phase from the elapsed time in the 3240 s cycle, because `wave`/`stage`/`step` read `None` outside a wave.

Terrain tiles: a permanent preview (the resident mip of each of the 36 segments, 64 px) plus full resolution on demand. The render callback publishes a bitmask of the visible tiles closest to the view centre, capped by `InGameMap.TileBudgetMiB`; the service forces their mips resident, copies them once streamed in (2048 px), and frees them 10 s after they leave the view or as soon as the panel closes.

## Third-party code

- The static catalog loader (`client/ingame_map_catalog.cpp`) parses its files with vendored `nlohmann/json` in `third_party/nlohmann/json.hpp`.
- License and notice files must stay in sync with that vendored header:
  - `licenses/nlohmann-json.MIT.txt`
  - `THIRD_PARTY_NOTICES.md`

## Prerequisites

- Visual Studio 2022 (17.8 or newer), or a newer Visual Studio with the v143 C++ build tools installed, the Desktop development with C++ workload and the Windows 10 SDK. The project and CI both use v143.
- SDK layout: `StarRupture-Plugin-SDK` with `include/`, `Shared.props`, and `StarRupture SDK/`

## Plugin build

1. Open `MapExtension_Plugin.sln` in Visual Studio 2022.
2. Select `Client Debug|x64`, `Client Release|x64`, or `Server Release|x64`.
3. Build `MapExtension_Plugin`.

Use only `Server Release|x64` for server builds so the output matches the packaging flow.

The project resolves these SDK paths:

- `PluginSdkSharedProps=StarRupture-Plugin-SDK\Shared.props`
- `PluginApiIncludeDir=StarRupture-Plugin-SDK\include\`
- `StarRuptureSdkBaseDir=StarRupture-Plugin-SDK\StarRupture SDK\`

These defaults are relative to the project directory and match `build.sh`. Use `./build.sh ... --sdk-root <path>` or override `PluginSdkRootDir` in MSBuild for another checkout. SDK tag selection remains independent of these paths and the compiler toolset.

The DLL is written to `build\\<Configuration>\\Plugins\\MapExtension_Plugin.dll`.

### Release identifier

`plugin.cpp` exports `MODLOADER_BUILD_TAG` and `PluginInfo.author`. For tagged builds, define the build tag at compile time so the loader and `/health` endpoint expose the correct version string. If needed, override the embedded author the same way.

- override the preprocessor definition in Visual Studio: **Project Properties → C/C++ → Preprocessor → Preprocessor Definitions**.
- or pass MSBuild properties from the helper script:
  - `./build.sh client release --build-tag "ML-2026.04.04-214044-v0.2" --build-author "Mralexandresys"`
- or call MSBuild directly with properties such as `/p:ModLoaderBuildTag=ML-2026.04.04-214044-v0.2 /p:ModLoaderBuildAuthor=Mralexandresys`.

If no build tag is supplied, `Client Release` can use the fallback in `PropertySheet.props`; configurations without that property sheet fall back to `"dev"` in the source. These identifiers do not establish the SDK revision used for the build. If the author macro is not set, builds fall back to `"Mralexandresys"`.

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
2. Clone `StarRupture-Plugin-SDK` inside it, at `MapExtension_Plugin/StarRupture-Plugin-SDK/`.
3. Open `MapExtension_Plugin/MapExtension_Plugin.sln`.
4. Build the desired client or server configuration.

No manual edit of a parent solution is required if you use the helper scripts.

## Root helper scripts

From the repository root:

```bash
./build.sh client release
./build.sh server release
```

To inspect the latest build logs manually:

```bash
./summarize_build.sh client
./summarize_build.sh server
```

## Static world catalog

`tools/build_map_data.py` converts local reverse-engineering exports into the compact files committed under `map-data/`. Its default inputs are:

- `analyse_map/map_v2_resources.jsonl`
- `analyse_map/map_v2_placements.jsonl`
- `analyse_map/map_v2_pois.geojson`
- `analyse_map/map_v2_ore_veins.jsonl` (preferred deposit source; optional fallback if absent)

`analyse_map/map_v2_catalog.json` remains useful reference material but is not a build input.

Run it from the repository root:

```bash
python3 tools/build_map_data.py
```

`analyse_map/` is a strictly local, ignored input and must not be committed. The generated `map-data/*.js` files are build inputs and must be committed because CI does not have the raw exports.

The format uses integer world decimetres, metre altitudes, dictionary/group indexes, locality sorting, and delta-encoded coordinate arrays. The preferred ore-vein export supplies 660 individual deposit sockets with resource, purity, extractor and join confidence.

The files contain compact JSON wrapped as `SRMAPDATA(<payload>);`. The wrapper dates from the removed web viewer, which loaded the parts with `<script>` tags from a `file://` page; the in-game map strips it. Resource types carry an optional `item` class path used to load the game's item icon.

`./build.sh client ...` stages `manifest.js`, `pois.js` and `resources-*.js` into `build/<Configuration>/Plugins/MapExtension/map-data/`. The `placements-*.js` parts are generated but not read by the in-game map.

## Packaging a release

1. Set `MODLOADER_BUILD_TAG` to the version you want to publish and, if needed, `MODLOADER_BUILD_AUTHOR` to the release author (see above), then build both `Client Release|x64` and `Server Release|x64` with `./build.sh client release` and `./build.sh server release`.
2. Create a client archive containing:
   - `Plugins/MapExtension_Plugin.dll` from `build/Client Release/Plugins/`
   - `Plugins/MapExtension_Plugin.json`, the update sidecar whose only field is `manifest_url` (see the GitHub Actions release section below); omit it only if the archive is not meant to receive automatic updates
   - `Plugins/MapExtension/map-data/` holding `manifest.js`, `pois.js` and `resources-*.js` from `map-data/` (`./build.sh client ...` stages this folder next to the built DLL)
3. Create a server archive containing `Plugins/MapExtension_Plugin.dll` from `build/Server Release/Plugins/`.
4. (Optional) add `README.md`, `README.fr.md`, `LICENSE`, notice files, and `licenses/` content alongside the binaries if you want a fuller release bundle.
5. Verify that no files from `.gitignore` leaked into the packages, then sign or checksum the archives before publishing them.

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
4. builds both the client and server plugin DLLs against that SDK checkout
5. creates a client zip with the client DLL, `Plugins/MapExtension/map-data/`, and `Plugins/MapExtension_Plugin.json` sidecar whose only field is `manifest_url`; `.pdb` files are included when available
6. publishes `MapExtension_Plugin-client-manifest.json` and a direct client DLL asset for the modloader auto-updater
7. creates a separate server zip with the server DLL
8. creates a plugin tag in the format `ML-<sdk-version>-vX.Y` (for example `ML-2026.04.09-200640-v0.2` or `ML-v1.2.0-v0.2`, depending on the selected SDK tag)
9. publishes a GitHub release in the plugin repository

The modloader auto-updater replaces `MapExtension_Plugin.dll` only. `Plugins/MapExtension/map-data/` is never updated by it, so a release that changes the catalog must tell users to copy that folder again.

The server build ships no sidecar and is not auto-updated. Sync protocol v6 requires an exact protocol-version match, so releases using it must tell server admins to update the client and dedicated-server DLLs together. A mixed-version pair ignores incompatible packets and cannot publish a remote snapshot.

`interface_version_min`/`interface_version_max` in the manifest are read from `PLUGIN_INTERFACE_VERSION_MIN`/`PLUGIN_INTERFACE_VERSION_MAX` in the SDK header, matching the SDK's reference workflow. The loader only checks that this range overlaps its own, so the published range is wider than the single `PLUGIN_INTERFACE_VERSION` the DLL actually declares.

## Plant and rupture-resource POI capture

Plant POIs are deliberately limited by the reward class in `ACrGatherableBaseActor::InteractionRewardResource`, not by localized display text. The accepted classes are `I_Hydrobulb_C`, `I_Polifruit_C`, `I_Oxallop_C`, `I_Purplant_C`, `I_SerpentRoot_C`, `I_Prickler_C`, `I_PrismHerb_C`, and `I_Sulheart_C`. Explicit gatherable actor classes additionally cover Grubbler (`BP_Gatherable_GoldFruitTree_*`), Prickler (`Thornfruit`), Glowcap (`SikkimRhubarb`), Prism Herb (`NootkaLupine`) and the generic Plant; these labels are the in-game item names. The same gatherable scan identifies Star Tears only when `InteractionRewardResource` is `I_StarTears_C`; the ore scan identifies Ignitium only when `ACrOreActor::Resource` is `I_FireWaveOre_C` (including `BP_FireWaveMeteOreChunk_C`).

The packaged static world catalog is the complete source for pre-generated plant and POI locations. The dynamic-resource inclusion/exclusion volumes do not contain exact runtime spawn points and must not be connected to the Ignitium or Star Tears visibility filters or turned into resource markers. Do not reintroduce a World Partition full-map scan, player movement, rupture-cycle pausing, or per-save POI cache to populate that data.

`CapturePois` remains a throttled live complement for already loaded actors. It merges observations into an in-memory catalog for the active world only. A cell unloading does not remove its markers during that world, while live `bIsDepleted` state (`bIsPermanentlyGathered` is a persistence policy, not runtime depletion), `ACrOreActor::OreData.bIsDepleted`, and `ACrGatherableSpawnersRepActor` depleted-location arrays mark gathered markers as depleted. The catalog is cleared on world transitions and engine shutdown; Ignitium and Star Tears observations are also cleared when `RepGlobalGatherablePCGSeed` changes, or on entry into Heat/Moving without a replica (normal solo cycles). Previously sampled actor identities are retired until removed, preventing their immediate reintroduction. BeginPlay queues identities on both builds; runtime properties are read on the next capture after initialization. Loaded remembered actors are refreshed even between the five-second full scans. A forced seed change with no observed phase transition remains outside the solo fallback. The dynamic-resource inclusion/exclusion volumes are not used as fixed positions; only generated runtime actors validate site coordinates. `ResolveRuptureResourcePhase` deliberately uses the same 30/60/600/2550-second timeline as the viewer before falling back to the raw stage, so the resource kind cannot disagree with the displayed Arcadia phase. In the stable interval, `ApplyRuptureResourcePhase` reclassifies a validated, non-depleted Ignitium site as Star Tears unless a real Star Tears actor has already been observed within the site tolerance. The burning/cooling interval suppresses stale Star Tears, while the stabilizing interval permits both kinds. Do not infer depletion from a nearby marker of the other rupture-resource kind because the two can coexist late in the cycle; the viewer gives Star Tears the higher SVG paint order at identical coordinates.

`CapturePois` excludes `rupture_phase.*` projections when retaining the previous snapshot. It then merges the raw observation catalog even on throttled scans, so `ApplyRuptureResourcePhase` derives the projection anew. A real Star Tears observation, including a depleted one, replaces the estimate without leaving an available duplicate; phase, world, and generation changes cannot retain the old projection as an observation.

The catalog is sorted by public key and assigned a stable 64-bit FNV-1a content revision over the fields sent on the wire. Identical content keeps the same revision across refreshes/processes; any addition, removal, state, coordinate, label, resource, source, or key change invalidates retained dedicated-server pages.

`IsMineableChunk()` supplies Ignitium harvestability independently of `OreData.bIsDepleted`. Star Tears are classified by explicit reward before generic plant classes. Their phase availability is read from the already loaded `UGatherableCropSettings::GatherableInteractivityData` class rules (stage, progress interval and allowed Fadeout/Growback substages). Missing asset/class/cycle data means unknown. `BP_IsGatherableDepleted(actor) == true` supplies extra depletion evidence; false alone never establishes availability. No gameplay interaction or mutation method is called.

Replicated depletion records have no resource type. Matching includes every observed plant/Ignitium/Star Tears candidate, including already depleted ones, within 150 cm of the location or bounds origin. Only a single candidate is accepted; overlapping candidates are left unchanged. Fresh actor/subsystem reads in the current capture take priority over untyped spatial history; this recovery path fills gaps for actors no longer loaded. The opaque Mass phase fragment and temporary clearing mask are not interpreted, so these records remain supporting observations, not a complete harvest history. Removed entries do not prove regrowth. `Diagnostics.LogResourceObservations=1` logs observation state changes, generation resets and ambiguity counts; enable `LogRuptureCycleEvents=1` alongside it for phase correlation. Times are observation times; no player attribution is inferred. Native harvest hooks and the exact Ignitium-to-Star Tears Blueprint relation remain pending executable/in-game validation; see [analysis and validation scenarios](IGNITIUM_STAR_TEARS_ANALYSIS.md).

## Dedicated-server sync protocol v6

`shared/map_sync_protocol.h` defines the POD request, begin, chunk, rupture, and end packets shared by client and server builds. The protocol version is intentionally exact-match only; there is no downgrade path.

Protocol v2 added POIs to the existing rupture, player, teleporter, cargo-marker, and cargo-connection stream; protocol v3 added POI pagination and a per-recipient "self" player flag; protocol v4 added exact POI-catalog revision tracking; protocol v5 extends the POI enum with `ignitium` and `star_tears` and keeps the exact-match requirement. Protocol v6 adds `kPoiEntryUnavailable` and `kPoiEntryUnknown` to the POI flags without changing packet layout; depletion remains an independent flag:

- `kSnapshotHasPois` identifies POI content in responses. All request flags are reserved; the server intentionally ignores `request_flags` and returns a complete snapshot.
- POIs are paginated: each `ClientSnapshotRequestPacket` carries a `poi_page`, and the server responds with at most `kPoiPageCapacity` POIs (currently 64, i.e. `kPoiChunksPerPage` = 16 chunks) for that page. `ServerSnapshotBeginPacket` declares the page slice via `pois_count`/`pois_chunk_count` plus `poi_page`, `poi_page_count`, `pois_total_count`, and `poi_revision`; `ServerSnapshotEndPacket` repeats the page counters, total, and revision.
- The client retains pages only for the exact `(world, poi_revision, poi_page_count, pois_total_count)` tuple. A changed revision clears every retained page before the new page is published, preventing removals or index shifts from leaving stale or duplicate markers. The client rejects duplicate/empty public keys and validates the fully assembled total.
- `ServerPlayerEntry` carries a `flags` byte; `kPlayerEntrySelf` marks the marker that belongs to the requesting player. The server matches the requesting player controller against the captured player keys, so each connected client sees its own marker flagged.
- `ServerPoiEntry` carries world coordinates, `kind`, the depletion/harvestability flags, label, resource, source, and unique key.
- `ServerPoisChunkPacket` carries at most `kPoiChunkCapacity` entries (currently four). The packet remains trivially copyable and is statically limited to the recommended 1 KiB payload size.
- The server rejects collections that cannot be represented by the `uint16_t` wire counters. The client validates the snapshot ID, generation, POI revision, begin/end counts and totals, chunk counts, chunk indexes, per-chunk item counts, page layout, and merged public-key uniqueness before publishing the assembled snapshot.

**Always update the client and dedicated-server builds together when deploying protocol v6.** The client sidecar updates only `MapExtension_Plugin.dll` on player machines; it does not update the dedicated-server DLL. A mismatched pair will ignore each other's packets, so the server DLL must be replaced manually during the same rollout.

## Snapshot semantics

`CargoSnapshot` (`map_state_types.h`) is the only contract between capture, dedicated-server sync and the in-game map; there is no external payload any more.

- `Generation` identifies refreshes, not content changes: it can advance while entities or a cached server snapshot stay unchanged. The POI content revision serves the separate purpose of invalidating paginated content.
- Cargo connections carry requested items and amounts, not measured throughput.
- POI `Kind` is abandoned base, plant resource, Ignitium or Star Tears; resource state is available, unavailable, unknown or depleted, and a depleted resource keeps its last known position.
- `MapLocation` fields (static map image projection of `map_state_types.h`) are still filled but no longer read by anything in the plugin.

## Resource observation regression checks

Run `python3 tools/tests/resource_observation_test.py` with Python 3 and a C++20-capable `g++`. It compiles the production phase, retention, depletion-match and wire-state code with minimal engine stand-ins. Scenarios cover normal solo cycles and replicated seeds, available/unavailable/unknown/depleted round trips, missing interaction data and progress/substage boundaries, an intact non-mineable Ignitium site, actual Star Tears replacing an estimate, ambiguous overlapping records, fresh observations overriding untyped records, removed records, and world/generation resets. It does not validate SDK runtime calls or actor lifetimes; complete the in-game scenarios in `IGNITIUM_STAR_TEARS_ANALYSIS.md` before claiming complete harvest coverage.
