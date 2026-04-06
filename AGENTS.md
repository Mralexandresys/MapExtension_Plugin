# MapExtension_Plugin

## Scope
- This directory is its own git repository.
- `mapview/` has its own local `AGENTS.md`; follow it for frontend work.

## What to read first
1. `README.md`
2. `DEVELOPERS.md`
3. `mapview/README.md`

## Key rules
- Keep the plugin focused on client-side map data, local HTTP endpoints, and rupture-cycle reconstruction.
- Prefer the local `UCrEnviroWaveSubsystem` in solo/local sessions.
- Keep chat parsing resilient to rich-text markup.
- Do not edit generated SDK trees under `../StarRupture SDK/**` unless regeneration is intended.

## Build and validation
- If C++ files in this repo change, run `../build_client.sh debug --summary` or `../build_client.sh release --summary` from the root repo.
- If only `mapview/` changes, do not rebuild the C++ plugin; run `npm run check` and `npm run build` inside `mapview/`.
- After a client build, review `../summarize_build.sh client`.
- Validate solo/local sessions without any server chat bridge.
