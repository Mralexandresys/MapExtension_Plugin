# MapExtension_Plugin developers

## Repository layout

- plugin source: `MapExtension_Plugin/`
- frontend source: `MapExtension_Plugin/mapview/`
- frontend build output: `MapExtension_Plugin/mapview/dist/MapExtensionViewer.html`

## Runtime module layout

- `plugin.cpp`: plugin metadata, startup, shutdown, and hook registration
- `map_state_runtime.cpp`: public runtime facade used by the plugin entrypoints
- `map_state_capture.cpp` / `map_state_capture.h`: world scanning, snapshot refresh, and gameplay callbacks
- `map_state_http.cpp` / `map_state_http.h`: local HTTP server and endpoint routing
- `map_state_json.cpp` / `map_state_json.h`: JSON serialization for `/health` and `/cargo`
- `map_state_types.h`: shared snapshot types and map projection constants

Keep the runtime split along these boundaries. Do not move HTTP or JSON formatting back into `map_state_runtime.cpp` unless the split is being intentionally reverted.

## Third-party code

- JSON serialization uses vendored `nlohmann/json` in `third_party/nlohmann/json.hpp`.
- License and notice files must stay in sync with that vendored header:
  - `licenses/nlohmann-json.MIT.txt`
  - `THIRD_PARTY_NOTICES.md`

## Prerequisites

- Visual Studio 2022 (17.8 or newer) with the Desktop development with C++ workload and the Windows 10 SDK.
- A checkout of `StarRupture-ModLoader` that sits next to this repository so that `Shared.props` and `StarRupture SDK/` are resolvable.
- Node.js 18 LTS (or newer) and npm for the `mapview` build; run `node --version` before working to ensure you are not on an unsupported runtime.

## Plugin build

1. Open `StarRupture-ModLoader.sln` in Visual Studio 2022.
2. Select `Client Debug|x64` or `Client Release|x64`.
3. Build `MapExtension_Plugin`.

The DLL is written to `build\\<Configuration>\\Plugins\\MapExtension_Plugin.dll`.

### Release identifier

`plugin.cpp` exports `MODLOADER_BUILD_TAG` and `PluginInfo.author`. For tagged builds, define the build tag at compile time so the loader and `/health` endpoint expose the correct version string. If needed, override the embedded author the same way.

- add `MODLOADER_BUILD_TAG=\"vX.Y.Z\"` to `Shared.props` (propagates to every configuration), or
- override the preprocessor definition in Visual Studio: **Project Properties → C/C++ → Preprocessor → Preprocessor Definitions**.
- or pass MSBuild properties from the helper script:
  - `../build_client.sh release --build-tag "ML-2026.04.04-214044-v0.2" --build-author "Mralexandresys"`

If the tag macro is not set, builds fall back to `"dev"`. If the author macro is not set, builds fall back to `"Mralexandresys"`.

## Standalone developer workflow

`MapExtension_Plugin` can also be built without editing `StarRupture-ModLoader.sln`.

Expected checkout layout:

```text
StarRupture-ModLoader/
  Shared.props
  Version_Mod_Loader/
  StarRupture SDK/
  MapExtension_Plugin/
```

Workflow:

1. Clone `StarRupture-ModLoader`.
2. Clone the plugin repository into `StarRupture-ModLoader/MapExtension_Plugin/`.
3. Open `MapExtension_Plugin/MapExtension_Plugin.sln`.
4. Build `Client Debug|x64` or `Client Release|x64`.

No manual edit of the parent `.sln` or other `.vcxproj` files is required for this workflow.

## Frontend build

From `MapExtension_Plugin/mapview/`:

```bash
npm install
npm run check
npm run build
```

The production build is a single self-contained HTML file:

```text
MapExtension_Plugin/mapview/dist/MapExtensionViewer.html
```

## Packaging a release

1. Set `MODLOADER_BUILD_TAG` to the version you want to publish and, if needed, `MODLOADER_BUILD_AUTHOR` to the release author (see above), then build `Client Release|x64` so that `build/Client Release/Plugins/MapExtension_Plugin.dll` is produced.
2. Move to `mapview/`, ensure Node.js ≥18 is active, then run `npm install && npm run check && npm run build`. The bundle lands in `mapview/dist/MapExtensionViewer.html`.
3. Copy the following into a staging directory or archive:
   - `build/Client Release/Plugins/MapExtension_Plugin.dll`
   - `mapview/dist/MapExtensionViewer.html`
   - `README.md`, `README.fr.md`, `LICENSE`, and both `THIRD_PARTY_NOTICES.md` files
   - `licenses/` and `mapview/licenses/` to keep third-party notices alongside the binaries
4. (Optional) include a sample `Plugins/config/MapExtension_Plugin.ini` if you want to ship defaults with instructions.
5. Verify that no files from `.gitignore` leaked into the package, then sign or checksum the archive before publishing it.

## GitHub Actions release

`MapExtension_Plugin` also ships a manual GitHub Actions release workflow.

1. Open **Actions** in the `MapExtension_Plugin` repository.
2. Run the `Release` workflow.
3. Provide `plugin_version` such as `v0.2`.
4. Optionally provide `build_author`. If left empty, the workflow uses the GitHub user who started it.
5. Choose whether the GitHub release should be created as a draft.

The workflow:

1. fetches the latest published release tag from `AlienXAXS/StarRupture-ModLoader`
2. checks out that modloader release
3. checks out `MapExtension_Plugin` into the expected `StarRupture-ModLoader/MapExtension_Plugin` path
4. builds the plugin and the `mapview` bundle against that modloader release
5. creates a plugin tag in the format `ML-YYYY.MM.DD-HHMMSS-vX.Y`
6. publishes a GitHub release in the plugin repository

## Runtime contract

- `GET /health`: status, world, generation, and entity counts
- `GET /cargo`: current snapshot payload used by the frontend

`/cargo` remains the compatibility endpoint consumed by the current frontend even though the payload now includes cargo links, teleporters, and players.

The frontend endpoint is editable in the UI, but defaults to `http://127.0.0.1:9000`.
