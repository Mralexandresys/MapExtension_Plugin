# mapview

Frontend Vue 3 + Vite de l'interface web locale de `MapExtension_Plugin`.

## Objectif

- UI locale dediee a l'affichage de la carte, des entites et de leurs connexions.
- Build de production compose de `dist/MapExtensionViewer.html` et des dossiers `dist/map-tiles/` et `dist/map-data/`.
- Utilisable directement contre le plugin local expose sur `http://127.0.0.1:9000` par defaut.
- V2 avec focus reseau, tri des elements et raccourcis clavier integres.
- V3 en layout `map-first` : carte dominante, overlays translucides, stats en panneau secondaire.

## Commandes

```bash
pnpm install
pnpm run dev
pnpm run mock-api
pnpm run check
pnpm run build
```

## Sortie de build

Le build de production genere l'entree HTML :

```text
dist/MapExtensionViewer.html
```

Et les assets externes utilises en ouverture locale :

```text
dist/map-tiles/
dist/map-data/
```

Pour utiliser ou distribuer l'interface, garder `map-tiles/` et `map-data/` a cote de `MapExtensionViewer.html`. Le HTML reste autonome pour le code Vue, mais ces deux dossiers sont necessaires au fond de carte et au catalogue du monde.

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
- Le catalogue statique est charge depuis `map-data/` par balises `<script>` JSONP afin de rester compatible avec une ouverture directe en `file://`.
- La timeline du cycle de rupture reste animee localement a partir de `elapsed_seconds` et `observed_at_unix_ms`.
- Le plugin se met a jour tout seul via le sidecar du modloader, mais seule la DLL est remplacee : `MapExtensionViewer.html`, `map-tiles/` et `map-data/` restent sur la version installee par l'utilisateur. Toute evolution de la forme des payloads doit donc rester retro-compatible avec une interface plus ancienne, ou etre annoncee comme mise a jour manuelle de l'interface.
- Pour rendre ce cas visible, l'interface consomme trois champs optionnels de `GET /health` : `plugin_version` (chaine), `viewer_contract_version` (entier) et `viewer_update` (`download_url`, `release_url`, `mod_page_url`, tous optionnels). Un plugin plus ancien n'envoie rien de tout cela et le comportement reste strictement inchange.
- Les URLs sont fabriquees par le plugin, jamais par l'interface : c'est le plugin qui est toujours a jour grace a l'auto-update. `download_url` et `release_url` sont absents sur un build local de developpement, et les liens correspondants ne sont alors pas affiches.
- L'interface embarque `VIEWER_CONTRACT_VERSION` dans `src/lib/viewerContract.ts`. Si `viewer_contract_version` renvoye par le plugin est strictement superieur a cette constante, une pop-up modale explique qu'il faut remplacer `MapExtensionViewer.html`, `map-tiles/` et `map-data/` a la main. Aucune comparaison de numeros de version n'est faite.
- Regle de bump : incrementer `VIEWER_CONTRACT_VERSION` et `kViewerContractVersion` (`map_state_json.cpp`) dans le meme changement, uniquement quand une evolution de payload casse les interfaces plus anciennes. Un ajout de champ retro-compatible ne doit pas etre bumpe.
- Le rejet de la pop-up est memorise dans `localStorage` sous la cle dediee `starrupture-mapview:viewer-update-dismissed:v1`, par `plugin_version` : fermer la pop-up la masque pour cette version du plugin, et elle revient des que le plugin passe a une version plus recente. Les cles existantes (preferences, annotations) ne sont pas touchees.
- L'interface inclut des modes de vue reseau/ressources/teleporteurs/joueurs, des filtres, l'echelle d'icones et des annotations personnelles exportables/importables en JSON.
- Raccourcis utiles : `?`, `/`, `R`, `L`, `G`, `E`, `S`, `F`, `C`, `P`, `0`, `Esc`.

## Catalogue statique du monde

Le viewer charge un catalogue compact pre-genere depuis `public/map-data/`. Il contient 241 POI principaux ainsi que les ressources, batiments, zones et elements techniques extraits localement des exports `analyse_map/map_v2_*`. Les POI restent dans le SVG interactif ; les centaines de milliers de ressources et placements sont dessines par `StaticMapCanvas.vue` afin d'eviter un DOM SVG trop volumineux.

Dans la couche `building`, le frontend rend uniquement les placements dont l'`actorType` contient `KeyCard`, `Coralion_Egg` ou `Spawner`, sans distinction de casse. Les couches `zone` et `technical` conservent tous leurs placements.

Le volet `Filtres` permet de rechercher et d'activer les couches, groupes de POI, categories et types de ressources, representations PCG/acteur, batiments, zones et elements techniques. Les choix sont persistants sous la cle `mapview.static-filters.v1`. Les couches POI et ressources sont actives par defaut ; les donnees techniques restent masquees.

### Zones et elements de generation

La couche `Zones` contient les volumes exportes suivants. Lorsqu'un volume fournit
une boite, Map View en dessine l'empreinte rectangulaire au sol, avec sa rotation ;
sinon seul son marqueur central est affiche. Ces formes representent les limites
exportees, pas une garantie qu'une ressource soit actuellement disponible.

| Filtre Map View | Categorie exportee | Role |
|---|---|---|
| `Zones de generation` | `resource_spawn_zone` | Volume dans lequel le jeu peut generer des plantes, minerais ou meteorites. |
| `Zones d'exclusion` | `resource_exclusion_zone` | Volume ou la generation de ressources est interdite. |
| `Regions de spawn monde` | `world_spawn_region` | Region de generation generale du monde, notamment pour les creatures et ennemis ; ce n'est pas une zone de ressources. |

La couche `Elements techniques`, desactivee par defaut, contient egalement des
points utiles a la lecture de la generation :

| Filtre Map View | Categorie exportee | Role |
|---|---|---|
| `Marqueurs de ressources` | `resource_spawn_marker` | Point fixe servant de centre ou d'ancrage a une generation de ressource. |
| `Sockets de depots` | `resource_deposit_socket` | Emplacement fixe prevu pour accueillir un gisement de minerai. |

`WorldSpawnerRegionExcluder` n'est pas present comme categorie dans le catalogue
exporte et n'est donc pas affiche. Les cellules World Partition et les Data Layers
sont des metadonnees techniques des exports bruts, non des couches du viewer. La
Rupture est exposee comme un etat et une timeline ; son front mobile n'est pas une
zone statique dessinee sur la carte.

Les observations live de plantes recues dans `/cargo` sont appariees au point statique du meme type le plus proche dans un rayon de 150 cm. L'etat live est applique au point catalogue sans creer un doublon ; une observation sans correspondance reste un marqueur runtime distinct.

### Regeneration

Depuis la racine du depot, avec les quatre exports locaux presents dans `analyse_map/` :

```bash
python3 tools/build_map_data.py
```

Le script lit `map_v2_resources.jsonl`, `map_v2_placements.jsonl`, `map_v2_pois.geojson` et `map_v2_catalog.json`, puis reecrit `mapview/public/map-data/*.js`. `analyse_map/` est ignore par Git, mais les fichiers compacts generes doivent etre versionnes pour que les builds locaux et CI disposent du catalogue.

## POI live et filtres de carte

Le tableau optionnel `pois` de `GET /cargo` alimente deux familles de points
d'interet :

- les bases abandonnees utilisent une icone de batiment fissure distincte ;
- le plugin courant publie les plantes disponibles Hydrobulb, Polifruit, Oxallop,
  Purplant, Serpent Root, Prickler, Prism Herb et Sulheart ; il publie aussi les
  acteurs live Ignitium (`ACrOreActor::Resource == I_FireWaveOre_C`) et Star Tears
  (`InteractionRewardResource == I_StarTears_C`) avec leurs positions exactes ;
- les grands volumes PCG d'inclusion et d'exclusion ne sont pas relies a ces filtres :
  ils ne contiennent pas les points de spawn exacts. Un pin apparait uniquement quand
  son acteur runtime est effectivement charge pres d'un joueur ;
- quand la timeline indique Arcadia stable (a partir de 690 secondes dans le cycle,
  avec `PreWave` en secours), le plugin expose un site Ignitium valide et non recolte
  comme Star Tears. Une observation reelle de Star Tears au meme site est prioritaire ;
  pendant la stabilisation, les deux types peuvent coexister. Le viewer applique aussi
  cette projection depuis sa phase affichee pour neutraliser un payload `/cargo` en retard ;
- une ressource `available` utilise un remplissage plein. Pour rester compatible avec
  les anciens payloads, une ressource recue avec `depleted: true` reste attenuee,
  avec un anneau pointille et un centre presque vide ;
- le survol ou le focus clavier affiche le type, le nom, la ressource et l'etat. Un POI
  peut etre selectionne a la souris ou avec `Entree`/`Espace`.

Le volet `Filtres` propose des boutons de visibilite separes pour les bases abandonnees,
les ressources vegetales, Ignitium et Star Tears. Le mode `Reseau` peut afficher toutes
les familles, le mode `Ressources` affiche les trois types de ressources, et les modes
`Teleporteurs` et `Joueurs` masquent les POI. `available` et `depleted` restent des etats visuels,
pas des filtres separes ; les acteurs de ressource epuises peuvent rester publies avec leur derniere
position connue. Quand Ignitium et Star Tears partagent une position pendant la fenetre de fin de
cycle, Star Tears est rendue au-dessus sans marquer artificiellement l'une des deux ressources comme
epuisee.

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

Le payload `/cargo` interne de `pnpm run mock-api` contient une base abandonnee, une
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

Le serveur mock (`pnpm run mock-api`, code dans `local-test/mock_server.py`) sert les
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

Puis relancer `pnpm run mock-api` et recharger l'interface.

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
