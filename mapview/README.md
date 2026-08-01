# mapview

Frontend Vue 3 + Vite de l'interface web locale de `MapExtension_Plugin`.

## Objectif

- UI locale dediee a l'affichage de la carte, des entites et de leurs connexions.
- Build de production compose de `dist/MapExtensionViewer.html` et du dossier `dist/map-tiles/`.
- Utilisable directement contre le plugin local expose sur `http://127.0.0.1:9000` par defaut.
- V2 avec focus reseau, tri des elements et raccourcis clavier integres.
- V3 en layout `map-first` : carte dominante, overlays translucides, stats en panneau secondaire.

## Commandes

```bash
npm install
npm run dev
npm run mock-api
npm run check
npm run build
```

## Sortie de build

Le build de production genere l'entree HTML :

```text
dist/MapExtensionViewer.html
```

Et le fond de carte tuile :

```text
dist/map-tiles/
```

Pour utiliser ou distribuer l'interface, garder `map-tiles/` a cote de `MapExtensionViewer.html`.

## Stack retenue

- Vue 3
- TypeScript
- Vite
- vite-plugin-singlefile

## Notes d'integration

- L'endpoint plugin est editable dans l'UI.
- L'app consomme `GET /health`, `GET /cargo` et `GET /rupture-cycle`.
- Les labels d'interface utilisent `Cargo Dispatchers` et `Cargo Receivers`.
- Le fond de carte est charge depuis `map-tiles/base_newmap_q70_2048/`.
- La timeline du cycle de rupture reste animee localement a partir de `elapsed_seconds` et `observed_at_unix_ms`.
- Le plugin se met a jour tout seul via le sidecar du modloader, mais seule la DLL est remplacee : `MapExtensionViewer.html` et `map-tiles/` restent sur la version installee par l'utilisateur. Toute evolution de la forme des payloads doit donc rester retro-compatible avec une interface plus ancienne, ou etre annoncee comme mise a jour manuelle de l'interface.
- Pour rendre ce cas visible, l'interface consomme trois champs optionnels de `GET /health` : `plugin_version` (chaine), `viewer_contract_version` (entier) et `viewer_update` (`download_url`, `release_url`, `mod_page_url`, tous optionnels). Un plugin plus ancien n'envoie rien de tout cela et le comportement reste strictement inchange.
- Les URLs sont fabriquees par le plugin, jamais par l'interface : c'est le plugin qui est toujours a jour grace a l'auto-update. `download_url` et `release_url` sont absents sur un build local de developpement, et les liens correspondants ne sont alors pas affiches.
- L'interface embarque `VIEWER_CONTRACT_VERSION` dans `src/lib/viewerContract.ts`. Si `viewer_contract_version` renvoye par le plugin est strictement superieur a cette constante, une pop-up modale explique qu'il faut remplacer `MapExtensionViewer.html` et `map-tiles/` a la main. Aucune comparaison de numeros de version n'est faite.
- Regle de bump : incrementer `VIEWER_CONTRACT_VERSION` et `kViewerContractVersion` (`map_state_json.cpp`) dans le meme changement, uniquement quand une evolution de payload casse les interfaces plus anciennes. Un ajout de champ retro-compatible ne doit pas etre bumpe.
- Le rejet de la pop-up est memorise dans `localStorage` sous la cle dediee `starrupture-mapview:viewer-update-dismissed:v1`, par `plugin_version` : fermer la pop-up la masque pour cette version du plugin, et elle revient des que le plugin passe a une version plus recente. Les cles existantes (preferences, annotations) ne sont pas touchees.
- L'interface inclut des modes de vue reseau/ressources/teleporteurs/joueurs, des filtres, l'echelle d'icones et des annotations personnelles exportables/importables en JSON.
- Raccourcis utiles : `?`, `/`, `R`, `L`, `G`, `E`, `S`, `F`, `C`, `P`, `0`, `Esc`.

## POI et filtres de carte

Le tableau optionnel `pois` de `GET /cargo` alimente deux familles de points
d'interet :

- les bases abandonnees utilisent une icone de batiment fissure distincte ;
- les ressources vegetales utilisent des points dont la couleur HSL est derivee d'un
  hachage stable du nom de la ressource, puis du label ou de la cle unique, ce qui
  limite fortement les collisions de teinte entre ressources differentes ;
- une ressource `available` utilise un remplissage plein ; une ressource avec
  `depleted: true` est attenuee, avec un anneau pointille et un centre presque vide ;
- le survol ou le focus clavier affiche le type, le nom, la ressource et l'etat. Un POI
  peut etre selectionne a la souris ou avec `Entree`/`Espace`.

Le volet `Filtres` propose des boutons de visibilite separes pour les bases abandonnees
et les ressources vegetales. Le mode `Reseau` peut afficher les deux familles, le mode
`Ressources` affiche uniquement les ressources vegetales, et les modes `Teleporteurs`
et `Joueurs` masquent les POI. `available` et `depleted` sont des etats visuels, pas des
filtres separes.

L'option `Afficher uniquement mes marqueurs et zones` masque toutes les donnees issues
du plugin (cargo, connexions, teleporteurs, joueurs et POI) pour ne conserver que les
marqueurs et zones crees par l'utilisateur, et efface la selection en cours. La
desactiver restaure les elements permis par le mode et les autres filtres actifs.

## Timeline Rupture et recentrage joueur

Le bouton de vue compacte du panneau Rupture remplace le panneau detaille par une barre
de timeline reduite ; le choix est conserve dans les preferences du navigateur. En
mode compact, survoler la barre ou lui donner le focus avec `Tab` ouvre les details :
phase actuelle, temps restant, graduations de la timeline et legende des phases. Le
meme contenu est donc disponible a la souris et au clavier.

Le bouton `Joueur` de la barre d'actions et le raccourci `P` recentrent la carte sur le
joueur marque `self: true` dans le payload courant (son propre joueur), ou a defaut sur
le premier joueur. Le marqueur `self` est rendu dans une couleur distincte. Le bouton
est desactive et le raccourci reste sans effet si aucun joueur n'est disponible. Comme
les autres raccourcis, `P` est actif en dehors des champs de saisie.

## Test local des POI et de la retrocompatibilite

Le payload `/cargo` interne de `npm run mock-api` contient une base abandonnee, une
ressource `Gold Fruit` disponible, la meme ressource en etat `depleted` et une ressource
`Plant Fiber` disponible. Il permet de verifier les icones, les deux etats, la stabilite
de couleur pour un meme nom de ressource, la palette entre ressources et les filtres.
Les scenarios et les commandes detaillees sont documentes dans
`local-test/README.md`.

Pour simuler un ancien plugin, copier la fixture fournie
`local-test/examples/cargo-legacy-no-pois.json` vers `local-test/data/cargo.json` :
elle contient un payload `/cargo` valide mais sans champ `pois`, sans compteurs
`pois`, `abandoned_bases` et `plant_resources`, et sans champ `self` sur les joueurs.
Le mock sert cet
override tel quel : apres un refresh, le viewer doit continuer a afficher la carte et
les anciennes entites, avec zero POI. Supprimer l'override permet de revenir au payload
POI interne.

## Test local de la pop-up de mise a jour

Le serveur mock (`npm run mock-api`, code dans `local-test/mock_server.py`) sert les
fichiers de fixtures deposes dans `local-test/data/` : `health.json`, `cargo.json` et
`rupture-cycle.json` (ou `rupture_cycle.json`). Si le fichier existe et n'est pas vide,
il est renvoye tel quel ; sinon le mock repond avec son payload interne par defaut.

Le payload `/health` par defaut declare `"viewer_contract_version": 1`, soit la meme
valeur que `VIEWER_CONTRACT_VERSION` : par defaut, la pop-up ne s'affiche donc pas.

Pour la declencher, creer `local-test/data/health.json` avec une valeur superieure :

```json
{
  "ok": true,
  "plugin": "mapview-local-mock",
  "world": "LocalTest",
  "snapshot_generation": 1,
  "marker_count": 0,
  "teleporter_count": 0,
  "player_count": 0,
  "plugin_version": "ML-v1.16.0-v0.5",
  "viewer_contract_version": 2,
  "viewer_update": {
    "download_url": "https://example.invalid/viewer.zip",
    "release_url": "https://example.invalid/release",
    "mod_page_url": "https://www.nexusmods.com/starrupture/mods/91"
  }
}
```

Puis relancer `npm run mock-api` et recharger l'interface.

Pour rejouer le cas "build local de developpement", retirer `download_url` et
`release_url` de `viewer_update` : les liens correspondants ne sont plus rendus.

Comme le rejet est persiste par `plugin_version`, il faut soit changer
`plugin_version` dans la fixture, soit supprimer la cle
`starrupture-mapview:viewer-update-dismissed:v1` du `localStorage` pour revoir la
pop-up apres l'avoir fermee.

## Licence tierce

Le fond de carte provient de `StarRuptureMap`.

Attention : le depot upstream est sous MIT, mais son fichier de licence indique
explicitement que `base_map.webp` n'est pas couvert par cette licence.

Voir `../THIRD_PARTY_NOTICES.md` et `THIRD_PARTY_NOTICES.md`.
