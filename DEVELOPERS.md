# MapExtension_Plugin developers

## Repository layout

- plugin source: `MapExtension_Plugin/`
- frontend source: `MapExtension_Plugin/mapview/`
- frontend build output: `MapExtension_Plugin/mapview/dist/index.html`

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

`plugin.cpp` exports `MODLOADER_BUILD_TAG`. For tagged builds, define the macro at compile time so the loader and `/health` endpoint expose the correct version string. Recommended approaches:

- add `MODLOADER_BUILD_TAG=\"vX.Y.Z\"` to `Shared.props` (propagates to every configuration), or
- override the preprocessor definition in Visual Studio: **Project Properties → C/C++ → Preprocessor → Preprocessor Definitions**.

If the macro is not set, release builds fall back to the placeholder value `"dev"`.

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
MapExtension_Plugin/mapview/dist/index.html
```

## Packaging a release

1. Set `MODLOADER_BUILD_TAG` to the version you want to publish (see above) and build `Client Release|x64` so that `build/Client Release/Plugins/MapExtension_Plugin.dll` is produced.
2. Move to `mapview/`, ensure Node.js ≥18 is active, then run `npm install && npm run check && npm run build`. The bundle lands in `mapview/dist/index.html`.
3. Copy the following into a staging directory or archive:
   - `build/Client Release/Plugins/MapExtension_Plugin.dll`
   - `mapview/dist/index.html`
   - `README.md`, `README.fr.md`, `LICENSE`, and both `THIRD_PARTY_NOTICES.md` files
   - `licenses/` and `mapview/licenses/` to keep third-party notices alongside the binaries
4. (Optional) include a sample `Plugins/config/MapExtension_Plugin.ini` if you want to ship defaults with instructions.
5. Verify that no files from `.gitignore` leaked into the package, then sign or checksum the archive before publishing it.

## Runtime contract

- `GET /health`: status, world, generation, and entity counts
- `GET /cargo`: current snapshot payload used by the frontend

The frontend endpoint is editable in the UI, but defaults to `http://127.0.0.1:9000`.
