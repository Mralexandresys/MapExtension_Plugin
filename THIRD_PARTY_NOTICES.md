# Third-party notices

Verification date: 2026-03-08

## StarRupture-ModLoader

- Source: `https://github.com/AlienXAXS/StarRupture-ModLoader`
- Upstream license: MIT
- Verified license file:
  `https://raw.githubusercontent.com/AlienXAXS/StarRupture-ModLoader/main/LICENSE`
- Usage in this project: build/runtime integration target and mod loader interface

## StarRuptureMap

- Source: `https://github.com/bithoarder/StarRuptureMap`
- Upstream license: MIT
- Verified license file:
  `https://raw.githubusercontent.com/bithoarder/StarRuptureMap/main/LICENSE`
- Important upstream note: the asset `base_map.webp` is explicitly stated as copyright
  `Creepy Jar S.A.` and is not covered by the MIT license.
- Usage in this project: reference for the static map image projection kept in
  `map_state_types.h` (unused since the HTTP API was removed in 0.8). The map tiles derived from
  `base_map.webp` shipped with the web viewer until 0.7; since 0.8 the viewer is
  removed and no such asset is distributed.

## StarRuptureSaveMap

- Source: `https://github.com/thanamatos/StarRuptureSaveMap`
- Usage in this project: inspiration/reference only
- License status checked on 2026-03-07: no `LICENSE` file found at
  `https://raw.githubusercontent.com/thanamatos/StarRuptureSaveMap/main/LICENSE`

No code or assets from `StarRuptureSaveMap` should be copied into this project
unless a clear license or permission is provided.

## nlohmann/json

- Source: `https://github.com/nlohmann/json`
- Upstream license: MIT
- Verified license file:
  `https://raw.githubusercontent.com/nlohmann/json/develop/LICENSE.MIT`
- Usage in this project: vendored single-header JSON library used to parse the
  static world catalog files
- Vendored files:
  - `third_party/nlohmann/json.hpp`
  - `licenses/nlohmann-json.MIT.txt`
