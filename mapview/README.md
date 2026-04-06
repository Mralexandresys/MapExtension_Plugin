# mapview

Frontend Vue 3 + Vite de l'interface web locale de `MapExtension_Plugin`.

## Objectif

- UI locale dediee a l'affichage de la carte, des entites et de leurs connexions.
- Build de production en un seul fichier `dist/MapExtensionViewer.html`.
- Utilisable directement contre le plugin local expose sur `http://127.0.0.1:9000` par defaut.
- V2 avec focus reseau, tri des elements et raccourcis clavier integres.
- V3 en layout `map-first` : carte dominante, overlays translucides, stats en panneau secondaire.

## Commandes

```bash
npm install
npm run dev
npm run check
npm run build
```

## Sortie de build

Le build de production genere un unique fichier :

```text
dist/MapExtensionViewer.html
```

Ce fichier contient :
- le JavaScript inline,
- le CSS inline,
- l'image de base inline.

## Stack retenue

- Vue 3
- TypeScript
- Vite
- vite-plugin-singlefile

## Notes d'integration

- L'endpoint plugin est editable dans l'UI.
- L'app consomme `GET /health`, `GET /cargo` et `GET /rupture-cycle`.
- Les labels d'interface utilisent `Cargo Dispatchers` et `Cargo Receivers`.
- Le fond de carte est embarque depuis `src/assets/base-map.webp` pour garder un build autonome.
- La timeline du cycle de rupture reste animee localement a partir de `elapsed_seconds` et `observed_at_unix_ms`.
- Raccourcis utiles : `?`, `/`, `R`, `L`, `G`, `E`, `S`, `F`, `0`, `Esc`.

## Licence tierce

Le fond de carte provient de `StarRuptureMap`.

Attention : le depot upstream est sous MIT, mais son fichier de licence indique
explicitement que `base_map.webp` n'est pas couvert par cette licence.

Voir `../THIRD_PARTY_NOTICES.md` et `THIRD_PARTY_NOTICES.md`.
