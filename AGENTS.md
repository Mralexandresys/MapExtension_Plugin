# MapExtension_Plugin

## What this project does

`MapExtension_Plugin` is a StarRupture mod-loader plugin that draws a logistics map inside the game (ModLoader ImGui panel, `F8`): cargo network, teleporters, players, base cores, monoliths, antennas, resources and rupture-cycle state. It is meant for players who need a clearer overview of their base logistics and world state than the native UI provides. The browser viewer (`mapview/`) was removed in 0.8.

Solo/local sessions use local game state. Dedicated-server sessions use the server build to send authoritative snapshots to the client build. The client build also exposes the same snapshot through a local HTTP API for external tools.

## Domain model and product intent

- The mod is a situational-awareness tool for StarRupture base logistics: it helps players understand where cargo endpoints, teleporters, players, and rupture-cycle state are on the world map.
- It does not automate gameplay, mutate saves, issue in-game commands, or manage cargo routes; it observes runtime state and publishes read-only snapshots.
- The main business object is a `CargoSnapshot`: cargo markers, cargo connections, teleporters, players, world name, generation/reason metadata, and the current rupture-cycle snapshot.
- Cargo markers represent `Cargo Dispatchers` and `Cargo Receivers`; cargo connections represent requested item flow from a dispatcher to a receiver, including item name, requested amount, and both world/map coordinates.
- The in-game map draws in Unreal world coordinates on the game's own terrain tiles (`client/ingame_map_projection.h`). The `map` coordinates of the HTTP payloads still use the static map image projection defined in `map_state_types.h`; keep them stable for external consumers.
- Rupture-cycle data is exposed as a timeline state (`wave`, `stage`, `step`, `elapsed_seconds`, `observed_at_unix_ms`) so a consumer can animate locally between plugin polls.
- In solo/local play, the client reads local game subsystems and actor state directly. In dedicated-server play, the server build is authoritative and streams snapshots to the client build, which then exposes the same local HTTP API to the browser.
- The in-game map panel is the only UI surface. It reads the published snapshot plus game state captured on the game thread; its render callback never touches UObjects.
- Some live state has no reflected source (monolith state and charge in solo). It is read natively through a loader-declared optional feature, read-only and guarded; see "Native reads" below.

## Technologies

- C++20 plugin built with Visual Studio/MSBuild and the v143 C++ toolset, matching CI.
- Local `StarRupture-Plugin-SDK/` checkout inside this project, used for StarRupture Mod Loader plugin interfaces and generated game SDK headers.
- Local HTTP endpoints: `/health`, `/cargo`, `/rupture-cycle`.
- Vendored `nlohmann/json` for JSON serialization.
- ImGui through the ModLoader interfaces (`IModLoaderImGui`, `IPluginImGuiTextures`) for the in-game map.

## Important structure

All plugin C++ lives under `src/`. Paths below that start with a source file name, `client/`, `server/` or `shared/` are relative to `src/`; `src/` is on the include path, so includes do not carry the prefix.

- `plugin.cpp`: plugin metadata, startup, shutdown, config/runtime registration, and the client-only `OnPluginLoadHooks` export.
- `plugin.h`, `dllmain.cpp`: exported plugin entry points and Windows DLL attach/detach plumbing.
- `plugin_config.h`: config schema and typed config accessors.
- `plugin_config.cpp`: storage for the config interface and plugin-self pointers; schema initialization lives in `plugin_config.h`.
- `plugin_helpers.h`: shared accessors for plugin hooks/self.
- `map_state_capture.cpp` / `.h`: world scanning, snapshot refresh, local fallback, gameplay callbacks.
- `map_state_http.cpp` / `.h`: client-only local HTTP server.
- `map_state_json.cpp` / `.h`: client-only JSON payload serialization.
- `map_state_runtime.cpp` / `.h`: callback registration, HTTP startup/shutdown, and client/server sync module lifecycle.
- `map_state_types.h`: shared snapshot types and map projection constants.
- `client/`: client snapshot requests and remote snapshot cache, plus the in-game map: `ingame_map_ui.*` activation and panel registration, `ingame_map_service.cpp` game-thread service, `ingame_map_render.cpp` render-thread drawing and filters, `ingame_map_internal.h` state shared by those three, `ingame_map_terrain_source.*` terrain tiles and icon textures, `ingame_map_live_state.*` replicated game state and native reads, `ingame_map_catalog.*` static catalog loader, `ingame_map_projection.h`.
- `server/`: server-side authoritative snapshot capture and response streaming.
- `shared/map_sync_protocol.h`: client/server packet definitions.
- `PropertySheet.props`: MSBuild property sheet for fallback build-tag definitions.
- `MapExtension_Plugin.vcxproj`: source list, client/server compile macros, and per-configuration exclusions.
- `map-data/`: committed static world catalog (`manifest.js`, `pois.js`, `resources-*.js`, `placements-*.js`), staged next to the client DLL.
- `update/ingame_map_poc_journal.md`: measurements and decisions for the in-game map; append to it when a fact is proven or disproven in game.
- `tools/build_map_data.py`: converts the local raw map exports in `analyse_map/` into the compact static catalog under `map-data/`.
- `build.sh`, `summarize_build.sh`: root build helpers; `./build.sh <client|server> <debug|release>`.
- `.github/workflows/release.yml`: PowerShell-based release packaging flow.
- `licenses/`, `THIRD_PARTY_NOTICES.md`: third-party attribution and license notices.
- `StarRupture-Plugin-SDK/include/plugin_interface.h`: plugin API, interfaces, callbacks, enums.
- `StarRupture-Plugin-SDK/include/plugin_network_helpers.h`: typed helpers for client/server plugin-network packets.
- `StarRupture-Plugin-SDK/StarRupture SDK/`: Dumper-7 generated UE5 SDK headers for client/server targets.
- `StarRupture-Plugin-SDK/Shared.props`: MSBuild SDK path/build property defaults.
- `StarRupture-Plugin-SDK/PluginDevelopment.md`: full plugin API reference and hook documentation.
- `StarRupture-Plugin-SDK/ExamplePlugin/`: minimal starter plugin reference.

## Static map-data conversion

- `tools/build_map_data.py` transforms the very large, local raw exports below into the compact JSONP (`SRMAPDATA(...)`) files read by the in-game map. The JSONP wrapper is a leftover of the removed web viewer; the catalog loader strips it.
- Raw inputs are intentionally ignored and must remain local; do not commit `analyse_map/`:
  - `analyse_map/map_v2_resources.jsonl` — very large resource-instance export.
  - `analyse_map/map_v2_placements.jsonl` — very large placement, volume, and technical-actor export.
  - `analyse_map/map_v2_pois.geojson` — canonical point-of-interest export.
  - `analyse_map/map_v2_catalog.json` — map metadata, data layers, and rupture rules. Kept for reference; the catalog build no longer reads it since the viewer never consumed the `rupture` block.
  - `analyse_map/map_v2_ore_veins.jsonl` — the 660 extractor ore veins, with their exact socket transform, resource, purity, extractor and the confidence of the purity join. Authoritative for the `deposit` points when present; the build falls back to joining sockets to nearby ore meshes when it is absent, which leaves 283 veins at unknown quality. The build also drops the `actor`/`pcg` points within 500 cm on each horizontal axis of a published deposit — the export describes the same vein up to three times — and never publishes `unknown_ore`.
- Run `python3 tools/build_map_data.py` from the repository root. It regenerates the versioned build inputs in `map-data/`: `manifest.js`, `resources-*.js`, `placements-*.js`, and `pois.js`.
- `manifest.js` resource types carry an optional `item` field (game item class path from the raw export, with fallbacks in `RESOURCE_ITEM_FALLBACK`). The in-game map reads it to load item icons; keep it when changing the manifest shape.
- Coordinates are compacted to world decimetres, with altitudes in metres, then grouped and delta-encoded. The in-game map scales them back to world centimetres in `client/ingame_map_catalog.cpp`; keep both sides synchronized.
- After regenerating data, run a client build so the staged copy under `build/<Configuration>/Plugins/MapExtension/map-data/` is refreshed. Only `manifest.js`, `pois.js` and `resources-*.js` are staged; `placements-*.js` are generated but not read.

## Exact commands

Run these from the repository root unless stated otherwise.

| Change type | Commands |
| --- | --- |
| Docs only | No build required |
| Client-only C++ | `./build.sh client debug --summary` or `./build.sh client release --summary`, then `./summarize_build.sh client` |
| Shared C++, config, protocol, or server-side C++ | Run a client build as above, then `./build.sh server release --summary` and `./summarize_build.sh server` |
| `map-data/` or `tools/build_map_data.py` | `python3 tools/build_map_data.py` (needs local `analyse_map/`), then a client build |

Server validation command: `./build.sh server release --summary`.

## Build mechanics

- C++ builds are launched from Linux/WSL with `build.sh`, but they execute Windows `MSBuild.exe`.
- The local helper script does not invoke PowerShell; the GitHub release workflow uses PowerShell, while local Linux/WSL validation calls `MSBuild.exe` directly.
- The script locates Visual Studio/MSBuild with `vswhere.exe` under `/mnt/c/Program Files (x86)/Microsoft Visual Studio/Installer/`, then falls back to common Visual Studio 2022/18 paths.
- Paths are converted with `wslpath`; `wslpath` and a Windows Visual Studio/MSBuild installation are required.
- The default SDK root is `./StarRupture-Plugin-SDK`, but it can be overridden with `--sdk-root <path>`.
- Client builds use `Client Debug|x64` or `Client Release|x64`; server builds use `Server Debug|x64` or `Server Release|x64`, with release server builds preferred for packaging/validation.
- Build outputs are written to `build/<Configuration>/Plugins/MapExtension_Plugin.dll`, for example `build/Client Release/Plugins/MapExtension_Plugin.dll` and `build/Server Release/Plugins/MapExtension_Plugin.dll`.
- A successful client build also stages `build/<Configuration>/Plugins/MapExtension/map-data/` (`manifest.js`, `pois.js`, `resources-*.js` copied from `map-data/`): the static catalog the in-game map reads from the folder next to the client DLL. Deploy that folder together with the DLL.
- The solution and `build.sh` both default to the SDK nested under this project. Install the v143 toolset even when using a newer Visual Studio.
- `--summary` replaces `build_client.log` or `build_server.log` for the current attempt (including launch failures) and runs `summarize_build.sh` on it; `summarize_build.sh client|server` can be rerun on an existing log.
- Keep shell scripts (`*.sh`) with LF line endings so they run correctly on Linux/WSL. Do not mass-normalize unrelated CRLF Visual Studio/project files unless that is the intended change.

## SDK update analysis and migration

- Treat the SDK version as a build input, not a source-level project constant. Local builds use the checkout selected by `--sdk-root` (or `./StarRupture-Plugin-SDK` by default), while the release workflow uses the requested `modloader_tag` or resolves the latest published SDK release.
- Never infer the SDK baseline from a MapExtension Git tag, `PropertySheet.props`, a build-tag fallback, documentation example, or plugin version string. Those values identify builds/releases and do not prove which SDK source revision should be compared.
- Before proposing an SDK migration, identify the exact baseline and target refs. Inspect `.github/workflows/release.yml` and the build helpers, resolve both SDK refs with `git rev-parse`, and use the nested SDK reflog when the question concerns a recently updated local checkout.
- Compare resolved commits before reading changelogs. If two SDK tags resolve to the same commit, report that there is no SDK source delta and do not modify plugin source, configuration, or compatibility documentation solely because the tag name changed.
- Separate the two SDK layers during comparison: diff `include/plugin_interface.h` and related public headers for plugin API changes, then compare the `StarRupture SDK` gitlink and relevant generated headers for game-layout changes. A large historical diff must not be attributed to the latest update without proving the selected baseline.
- `plugin.cpp` publishes the SDK-provided `PLUGIN_INTERFACE_VERSION`, so a build naturally advertises the interface of the SDK selected for that build. A `PLUGIN_INTERFACE_VERSION_MIN/MAX` bump alone does not justify pinning an SDK version, adding a compile-time version guard, or declaring a new minimum ModLoader version in project docs.
- Determine source impact by checking whether MapExtension actually calls a changed signature or reads a changed generated field/layout. Additive APIs that the plugin does not use are informational only and must not trigger unrelated adoption work.
- Preserve the dynamic SDK selection in local and release builds unless the user explicitly requests pinning or a reproducibility policy change. Rebuilding for a new ModLoader release and changing MapExtension source are separate decisions.
- Validate a real migration with the narrowest relevant diff first, then the required client/server builds. Do not make speculative migration edits merely to demonstrate use of a newly added SDK API.

## Source ownership and build split

- Client builds define `MODLOADER_CLIENT_BUILD`; server builds define `MODLOADER_SERVER_BUILD`.
- `map_state_http.cpp`, `map_state_json.cpp`, and `client/*.cpp` are excluded from server configurations in the project file.
- `server/map_sync_server.cpp` is excluded from client configurations in the project file.
- Shared C++ used by both builds lives directly in `src/` and in `src/shared/`; changes there require both client and server validation.
- The runtime startup path is `PluginInit` in `plugin.cpp` -> `MapExtensionPluginConfig::Config::Initialize` -> `MapStateRuntime::RegisterCallbacks`.
- `MapStateRuntime::RegisterCallbacks` owns hook registration, local HTTP startup, and initialization of both sync modules; keep lifecycle changes symmetric with shutdown.
- `/cargo` and `/rupture-cycle` HTTP requests trigger or use snapshots captured by `map_state_capture.cpp`; JSON shape changes belong in `map_state_json.cpp` and matching docs.
- Dedicated-server map/cycle data flows through `shared/map_sync_protocol.h`, `server/map_sync_server.cpp`, `client/map_sync_client.cpp`, and `client/map_state_remote_cache.cpp`.

## Project-specific rules

- Keep the plugin focused on the in-game map, client-side map data, local HTTP endpoints, and client/server snapshot sync.
- Prefer `UCrEnviroWaveSubsystem` for rupture-cycle state in solo/local sessions.
- Use the server build as the authoritative source in dedicated-server sessions; do not add a second data transport for rupture-cycle state.
- Keep HTTP and JSON code client-only unless the project file is intentionally changed too.
- Keep `shared/map_sync_protocol.h` packet structs POD/trivially-copyable and compatible across client/server builds.
- Reuse `map_state_types.h` and shared protocol helpers; do not duplicate snapshot types across client/server paths.
- `StarRupture-Plugin-SDK/` must be present in this project checkout for local builds; the root build scripts default to that path.
- `StarRupture-Plugin-SDK/` is a local nested checkout and is ignored by this repository; do not stage or vendor SDK contents into plugin commits.
- Do not edit generated SDK trees under `StarRupture-Plugin-SDK/StarRupture SDK/**` unless regeneration is intended.
- If config defaults/schema, endpoints, payloads, sync, packaging, third-party assets, or release behavior change, update the relevant docs (`README.md`, `README.fr.md`, `DEVELOPERS.md`, `descriptionbbcode`, notices/licenses).
- Keep `README.md` and `README.fr.md` semantically aligned.
- The in-game map source is ASCII-only (the project builds without `/utf-8`) and its UI labels are English.

## In-game map threading and native reads

- The game-thread service in `client/ingame_map_service.cpp` publishes an immutable snapshot every 500 ms while the panel is open; the render callback (`client/ingame_map_render.cpp`) reads only that. State shared between the two is declared `inline` in `client/ingame_map_internal.h`; everything else stays file-local. Do not read UObjects from the render callback, and do not keep UObject pointers across service passes.
- `LoadFromUTexture2D` returns null for a texture that is not on the GPU yet. Force its mips resident and retry on a later pass instead of treating it as missing.
- Native reads (`ResolveNativeAccess`, `ScanObelisksGuarded` in `client/ingame_map_live_state.cpp`) must stay behind a loader feature declared in `OnPluginLoadHooks`, so an unresolved key turns off that feature and never the plugin. Keep them read-only, hook-free and under `__try/__except`.
- Native offsets, vtable slots and signatures are valid for one game changelist (CL-127004 today). `StarRupture-Game-IDADump/` is a local, untracked reading aid that may not match the installed executable: verify bytes on the installed executable before using an address, and never widen the changelist gate to an unverified build.
- Prefer a reflected source over a native read whenever the game replicates the data (`ACrMapMenuDataReplicationHelper`, `ACrAbandonBaseInfoReplicator`, `ACrAntennasOwner`, `ACrBaseCoreReplicationHelper`).
