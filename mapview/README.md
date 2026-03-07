# MapExtension_Plugin mapview

Frontend Vue 3 + Vite du plugin `MapExtension_Plugin`.

## Objectif

- UI/UX refaite dans un vrai projet front modulaire.
- Build de production en un seul fichier `dist/index.html`.
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
dist/index.html
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
- L'app consomme `GET /health` et `GET /cargo`.
- Le fond de carte est embarque depuis `src/assets/base-map.webp` pour garder un build autonome.
- Raccourcis utiles : `?`, `/`, `R`, `L`, `G`, `E`, `S`, `F`, `0`, `Esc`.

## Licence tierce

Le fond de carte provient de `StarRuptureMap`.

Attention : le depot upstream est sous MIT, mais son fichier de licence indique
explicitement que `base_map.webp` n'est pas couvert par cette licence.

Voir `../THIRD_PARTY_NOTICES.md` et `THIRD_PARTY_NOTICES.md`.
