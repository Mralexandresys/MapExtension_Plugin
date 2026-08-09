# MapExtension_Plugin

## What this project does

`MapExtension_Plugin` is a StarRupture mod-loader plugin that exposes live map, cargo, player, teleporter, and rupture-cycle data to a local browser viewer. It is meant for players who need a clearer overview of their base logistics and rupture-cycle state than the in-game UI provides.

Solo/local sessions use local game state. Dedicated-server sessions use the server build to send authoritative snapshots to the client build, which then exposes the local HTTP API consumed by `mapview`.

## Domain model and product intent

- The mod is a situational-awareness tool for StarRupture base logistics: it helps players understand where cargo endpoints, teleporters, players, and rupture-cycle state are on the world map.
- It does not automate gameplay, mutate saves, issue in-game commands, or manage cargo routes; it observes runtime state and publishes read-only snapshots.
- The main business object is a `CargoSnapshot`: cargo markers, cargo connections, teleporters, players, world name, generation/reason metadata, and the current rupture-cycle snapshot.
- Cargo markers represent `Cargo Dispatchers` and `Cargo Receivers`; cargo connections represent requested item flow from a dispatcher to a receiver, including item name, requested amount, and both world/map coordinates.
- Map rendering depends on converting Unreal world coordinates into the static StarRupture map image coordinate system defined in `map_state_types.h`; keep coordinate/projection changes synchronized with `mapview`.
- Rupture-cycle data is exposed as a timeline state (`wave`, `stage`, `step`, `elapsed_seconds`, `observed_at_unix_ms`) so the web UI can animate locally between plugin polls.
- In solo/local play, the client reads local game subsystems and actor state directly. In dedicated-server play, the server build is authoritative and streams snapshots to the client build, which then exposes the same local HTTP API to the browser.
- The browser viewer is the intended UI surface because the in-game UI was too limited for a reliable map/logistics overview; preserve the local-file `MapExtensionViewer.html` + `map-tiles/` distribution model.

## Technologies

- C++20 plugin built with Visual Studio/MSBuild.
- Local `StarRupture-Plugin-SDK/` checkout inside this project, used for StarRupture Mod Loader plugin interfaces and generated game SDK headers.
- Local HTTP endpoints: `/health`, `/cargo`, `/rupture-cycle`.
- Vendored `nlohmann/json` for JSON serialization.
- `mapview/`: Vue 3 + TypeScript + Vite + `vite-plugin-singlefile`.

## Important structure

- `plugin.cpp`: plugin metadata, startup, shutdown, config/runtime registration.
- `plugin.h`, `dllmain.cpp`: exported plugin entry points and Windows DLL attach/detach plumbing.
- `plugin_config.h`: config schema and typed config accessors.
- `plugin_config.cpp`: config hook registration and global config interface storage.
- `plugin_helpers.h`: shared accessors for plugin hooks/self.
- `map_state_capture.cpp` / `.h`: world scanning, snapshot refresh, local fallback, gameplay callbacks.
- `map_state_http.cpp` / `.h`: client-only local HTTP server.
- `map_state_json.cpp` / `.h`: client-only JSON payload serialization.
- `map_state_runtime.cpp` / `.h`: callback registration, HTTP startup/shutdown, and client/server sync module lifecycle.
- `map_state_types.h`: shared snapshot types and map projection constants.
- `client/`: client snapshot requests and remote snapshot cache.
- `server/`: server-side authoritative snapshot capture and response streaming.
- `shared/map_sync_protocol.h`: client/server packet definitions.
- `PropertySheet.props`: MSBuild property sheet for fallback build-tag definitions.
- `MapExtension_Plugin.vcxproj`: source list, client/server compile macros, and per-configuration exclusions.
- `mapview/`: local web viewer; follow `mapview/AGENTS.md` for frontend work.
- `mapview/local-test/`: mock HTTP API and fixture payloads for frontend development without the game/plugin.
- `mapview/public/map-tiles/`: packaged map tile assets copied next to `MapExtensionViewer.html`.
- `tools/build_map_data.py`: converts the local raw map exports in `analyse_map/` into the compact static catalog under `mapview/public/map-data/`.
- `build_client.sh`, `build_server.sh`, `summarize_build.sh`: root build helpers.
- `.github/workflows/release.yml`: PowerShell-based release packaging flow.
- `licenses/`, `THIRD_PARTY_NOTICES.md`, `mapview/THIRD_PARTY_NOTICES.md`: third-party attribution and license notices.
- `update/`: implementation notes for prior/planned improvement phases; use as context, not as active runtime code.
- `StarRupture-Plugin-SDK/include/plugin_interface.h`: plugin API, interfaces, callbacks, enums.
- `StarRupture-Plugin-SDK/include/plugin_network_helpers.h`: typed helpers for client/server plugin-network packets.
- `StarRupture-Plugin-SDK/StarRupture SDK/`: Dumper-7 generated UE5 SDK headers for client/server targets.
- `StarRupture-Plugin-SDK/Shared.props`: MSBuild SDK path/build property defaults.
- `StarRupture-Plugin-SDK/PluginDevelopment.md`: full plugin API reference and hook documentation.
- `StarRupture-Plugin-SDK/ExamplePlugin/`: minimal starter plugin reference.

## Static map-data conversion

- `tools/build_map_data.py` transforms the very large, local raw exports below into the compact JSONP (`SRMAPDATA(...)`) files used by `mapview`. The JSONP wrapper permits loading data from a viewer opened with `file://`.
- Raw inputs are intentionally ignored and must remain local; do not commit `analyse_map/`:
  - `analyse_map/map_v2_resources.jsonl` — very large resource-instance export.
  - `analyse_map/map_v2_placements.jsonl` — very large placement, volume, and technical-actor export.
  - `analyse_map/map_v2_pois.geojson` — canonical point-of-interest export.
  - `analyse_map/map_v2_catalog.json` — map metadata, data layers, and rupture rules.
- Run `python3 tools/build_map_data.py` from the repository root. It regenerates the versioned build inputs in `mapview/public/map-data/`: `manifest.js`, `resources-*.js`, `placements-*.js`, and `pois.js`.
- Coordinates are compacted to world decimetres, with altitudes in metres, then grouped and delta-encoded. Keep projection assumptions synchronized with `map_state_types.h` and `mapview`.
- After regenerating data, validate/package the viewer with `cd mapview && pnpm run check && pnpm run build`.

## Exact commands

Run these from the repository root unless stated otherwise.

| Change type | Commands |
| --- | --- |
| Docs only | No build required |
| Client-only C++ | `./build_client.sh debug --summary` or `./build_client.sh release --summary`, then `./summarize_build.sh client` |
| Shared C++, config, protocol, or server-side C++ | Run a client build as above, then `./build_server.sh release --summary` and `./summarize_build.sh server` |
| `mapview/` only | `cd mapview && pnpm run check && pnpm run build` |

Server validation command: `./build_server.sh release --summary`.

## Build mechanics

- C++ builds are launched from Linux/WSL with `build_client.sh` and `build_server.sh`, but they execute Windows `MSBuild.exe`.
- The local helper scripts do not invoke PowerShell; the GitHub release workflow uses PowerShell, while local Linux/WSL validation calls `MSBuild.exe` directly.
- The scripts locate Visual Studio/MSBuild with `vswhere.exe` under `/mnt/c/Program Files (x86)/Microsoft Visual Studio/Installer/`, then fall back to common Visual Studio 2022/18 paths.
- Paths are converted with `wslpath`; `wslpath` and a Windows Visual Studio/MSBuild installation are required.
- The default SDK root is `./StarRupture-Plugin-SDK`, but it can be overridden with `--sdk-root <path>`.
- Client builds use `Client Debug|x64` or `Client Release|x64`; server builds use `Server Debug|x64` or `Server Release|x64`, with release server builds preferred for packaging/validation.
- Build outputs are written to `build/<Configuration>/Plugins/MapExtension_Plugin.dll`, for example `build/Client Release/Plugins/MapExtension_Plugin.dll` and `build/Server Release/Plugins/MapExtension_Plugin.dll`.
- `--summary` writes `build_client.log` or `build_server.log` and runs `summarize_build.sh` on it; `summarize_build.sh client|server` can be rerun on an existing log.
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
- Shared C++ used by both builds lives at the repository root and in `shared/`; changes there require both client and server validation.
- The runtime startup path is `PluginInit` in `plugin.cpp` -> `MapExtensionPluginConfig::Config::Initialize` -> `MapStateRuntime::RegisterCallbacks`.
- `MapStateRuntime::RegisterCallbacks` owns hook registration, local HTTP startup, and initialization of both sync modules; keep lifecycle changes symmetric with shutdown.
- `/cargo` and `/rupture-cycle` HTTP requests trigger or use snapshots captured by `map_state_capture.cpp`; JSON shape changes belong in `map_state_json.cpp` and matching docs/frontend types.
- Dedicated-server map/cycle data flows through `shared/map_sync_protocol.h`, `server/map_sync_server.cpp`, `client/map_sync_client.cpp`, and `client/map_state_remote_cache.cpp`.

## Project-specific rules

- Keep the plugin focused on client-side map data, local HTTP endpoints, and client/server snapshot sync.
- Prefer `UCrEnviroWaveSubsystem` for rupture-cycle state in solo/local sessions.
- Use the server build as the authoritative source in dedicated-server sessions; do not add a second data transport for rupture-cycle state.
- Keep HTTP and JSON code client-only unless the project file is intentionally changed too.
- Keep `shared/map_sync_protocol.h` packet structs POD/trivially-copyable and compatible across client/server builds.
- Reuse `map_state_types.h` and shared protocol helpers; do not duplicate snapshot types across client/server paths.
- `StarRupture-Plugin-SDK/` must be present in this project checkout for local builds; the root build scripts default to that path.
- `StarRupture-Plugin-SDK/` is a local nested checkout and is ignored by this repository; do not stage or vendor SDK contents into plugin commits.
- Do not edit generated SDK trees under `StarRupture-Plugin-SDK/StarRupture SDK/**` unless regeneration is intended.
- If config defaults/schema, endpoints, payloads, sync, packaging, third-party assets, or release behavior change, update the relevant docs (`README.md`, `README.fr.md`, `DEVELOPERS.md`, `mapview/README.md`, `descriptionbbcode`, notices/licenses).
- Keep `README.md` and `README.fr.md` semantically aligned.
