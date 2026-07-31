# Mapview

## What this project does

`mapview` is the local browser UI for `MapExtension_Plugin`. It displays StarRupture cargo networks, teleporters, players, user annotations, and the rupture-cycle timeline from the client plugin HTTP endpoint.

It is intended to be opened locally as `MapExtensionViewer.html` while the game/plugin is running.

## Technologies

- Vue 3 + Composition API.
- TypeScript.
- Vite.
- `vite-plugin-singlefile`.
- Node.js 20.19.0+ or 22.12.0+.

## Important structure

- `src/App.vue`: main app shell and panel wiring.
- `src/components/`: map canvas and UI panels.
- `src/composables/`: data loading, map state, pan/zoom, rupture timeline, annotations.
- `src/lib/`: API helpers, formatters, shared frontend types.
- `src/lang/`: English/French UI labels.
- `public/map-tiles/base_newmap_q70_2048/`: tiled map background.
- `local-test/`: mock API server and optional fixture payloads.
- Production output: `dist/MapExtensionViewer.html` plus `dist/map-tiles/`.

## Exact commands

Run these from `mapview/`.

- Install dependencies: `npm install`
- Start dev server: `npm run dev`
- Mock local API: `npm run mock-api`
- Type check: `npm run check`
- Production build: `npm run build`

For frontend-only changes, run `npm run check` and `npm run build`. Do not rebuild the C++ plugin for mapview-only changes.

## Project-specific rules

- The viewer reads `GET /health`, `GET /cargo`, and `GET /rupture-cycle` from the client plugin, defaulting to `http://127.0.0.1:9000`.
- Keep `MapExtensionViewer.html` and `map-tiles/` usable from any local folder as long as they stay side by side.
- Do not rename `public/map-tiles/base_newmap_q70_2048/` or tile filenames without updating `MapCanvas.vue`, packaging docs, and third-party notices.
- Preserve existing localStorage keys for preferences and annotations unless an intentional migration is implemented.
- Keep annotation JSON export/import backward-compatible when adding fields; default missing fields explicitly.
- Keep endpoint handling resilient: normalize user-entered endpoints and preserve the current display when a refresh fails.
- Keep rupture timeline animation derived from `elapsed_seconds` and `observed_at_unix_ms` so it continues smoothly between polls.
- If UI labels, shortcuts, endpoints, packaging, or asset paths change, update `mapview/README.md` and the root docs that mention mapview.
