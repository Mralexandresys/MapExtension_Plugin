# mapview

Frontend Vue 3 + Vite de l'interface web locale de `MapExtension_Plugin`.

## Objectif

- UI locale dediee a l'affichage de la carte, des entites et de leurs connexions.
- Build de production compose de `dist/MapExtensionViewer.html` et des dossiers `dist/map-tiles/` et `dist/map-data/`.
- Utilisable directement contre le plugin local expose sur `http://127.0.0.1:9000` par defaut.
- V2 avec focus reseau, tri des elements et raccourcis clavier integres.
- V3 en layout `map-first` : carte dominante, overlays translucides, stats en panneau secondaire.

## Commandes

Utiliser Node `^20.19.0 || >=22.12.0` et pnpm 10.14.0. Avec nvm,
executer `nvm install && nvm use` a la racine (`.nvmrc` : 22.22.1), puis revenir
dans `mapview/`. `corepack pnpm` permet de lancer la version declaree dans
`package.json` sans utiliser un ancien pnpm global.

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
- L'interface inclut quatre prereglages de carte (Reseau, Exploration, Recolte, Technique), des filtres, l'echelle d'icones et des annotations personnelles exportables/importables en JSON.
- Raccourcis utiles : `?`, `/`, `R`, `L`, `G`, `E`, `F`, `C`, `P`, `0`, `Esc`.

## Catalogue statique du monde

Le viewer charge un catalogue compact pre-genere depuis `public/map-data/`. Il contient 241 POI principaux ainsi que les ressources, batiments, zones et elements techniques extraits localement des exports `analyse_map/map_v2_*`. Les POI restent dans le SVG interactif ; les 57 147 ressources et 21 751 placements sont dessines par `StaticMapCanvas.vue` afin d'eviter un DOM SVG trop volumineux.

Dans la couche `building`, le frontend rend uniquement les placements dont l'`actorType` contient `KeyCard`, `Coralion_Egg` ou `Spawner`, sans distinction de casse. Les couches `zone` et `technical` conservent tous leurs placements.

Le volet `Filtres` s'ouvre sur un prereglage, puis sur des onglets : `Carte`, `Recolte` (mode Recolte
uniquement), `Catalogue` (masque en prereglage `Reseau`) et `Options`. Un seul onglet est affiche a la
fois, donc chaque liste dispose de toute la hauteur du volet au lieu d'une pile de sections repliables.
Chaque en-tete d'onglet porte son propre resume (elements actifs sur total, ressource choisie, elements
dessines), afin qu'un onglet hors ecran continue d'annoncer les filtres actifs qu'il contient. L'onglet
courant est persiste avec les autres preferences (`filterTab`), et les choix de filtres restent
persistants sous la cle `mapview.static-filters.v2`.

Chaque entree de liste est une ligne pleine largeur : case d'etat, icone de legende, libelle et
compteur (groupe par milliers selon la locale, un libelle tronque garde son nom complet en infobulle).
Chaque groupe propose `Tout` / `Aucun`, qui rejouent les bascules individuelles du groupe. Une
categorie du catalogue active dont certains types sont masques affiche un tiret plutot qu'une coche :
elle n'est ni tout affiche ni tout masque. Dans l'onglet `Catalogue`, le champ de recherche reste
epingle pendant que les listes defilent et ne filtre que les listes du catalogue.

La barre d'onglets se parcourt aux fleches, `Home` et `End`, comme un `tablist` standard. L'en-tete du
volet ne garde qu'un compteur de filtres actifs et `Reinitialiser` : la liste de chips retirables a ete
supprimee, chaque filtre s'annulant desormais depuis la ligne qui l'a pose (prereglage, onglet `Carte`
ou onglet `Options`).

L'onglet `Carte` regroupe en trois familles de sens ce qui etait auparavant reparti entre
la visibilite des entites live et le catalogue :

| Famille | Contenu |
|---|---|
| `Logistique` | Cargo Dispatchers, Cargo Receivers, Teleporteurs, Joueurs |
| `Reperes` | Grottes, Obelisques, Geoscanneurs, Bases abandonnees, Forgotten Engine, Orbital Lander |
| `Ressources` | Ressources vegetales, Ignitium, Star Tears |

Les six familles de reperes sont les POI canoniques : elles sont accessibles en un clic au premier
niveau, et non plus a trois niveaux de profondeur dans le catalogue. `Bases abandonnees` est un seul
controle : le compteur vient du catalogue (valeur canonique) et l'etat live s'y applique, au lieu des
deux controles qui se contredisaient.

Chaque repere possede une silhouette propre definie une seule fois dans `src/lib/mapMarkers.ts`,
emise en `<symbol>` par `MapCanvas.vue` et reprise a l'identique dans les lignes du volet : la liste
des filtres sert donc aussi de legende et ne peut pas diverger de la carte.

Le resume de l'onglet `Catalogue` indique le nombre d'elements reellement dessines, pas le nombre
charge. Les couches brutes de l'export (representations PCG/acteur, placements, zones, elements
techniques) ne sont listees qu'en prereglage `Technique`.

Les quatre prereglages repondent chacun a une question de joueur et pilotent d'un seul geste les entites live et les filtres du catalogue :

| Prereglage | Question | Couches actives |
|---|---|---|
| `Reseau` (defaut) | Ou suis-je, comment circulent mes ressources ? | Entites live completes + les 241 POI canoniques |
| `Exploration` | Ou construire, quel objectif vaut le deplacement ? | POI canoniques + minerais, reseau cargo masque |
| `Recolte` | Ou est la ressource que je collecte maintenant ? | Une seule ressource a la fois + grottes |
| `Technique` | Que contient l'export brut ? | Placements, zones, sockets et proxies (mode developpeur) |

Le defaut est volontairement `POI canoniques + donnees live` : les 54 513 points de plantes du catalogue ne sont plus actives sans demande explicite. Le type `unknown_ore` est exclu du catalogue. L'export dedie attribue les 12 anciens sockets non identifies au tungstene, qui compte ainsi 107 gisements. Le compteur de filtres actifs de l'en-tete ne compte que les ecarts par rapport au prereglage courant.

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

Les `resource_deposit_socket` ne figurent plus dans cette couche technique : ils
sont exposes comme ressources `deposit` dans la couche Minerais afin d'etre
visibles et filtrables avec le minerai correspondant.

`WorldSpawnerRegionExcluder` n'est pas present comme categorie dans le catalogue
exporte et n'est donc pas affiche. Les cellules World Partition et les Data Layers
sont des metadonnees techniques des exports bruts, non des couches du viewer. La
Rupture est exposee comme un etat et une timeline ; son front mobile n'est pas une
zone statique dessinee sur la carte.

### Gisements de minerai

Les zones de minerai qui demandent d'y placer un batiment sont publiees comme
points `deposit`, un par filon. Elles sont distinctes des rochers HISM minables a
la main, qui ne sont pas affiches.

Ces 660 filons viennent de l'export dedie `analyse_map/map_v2_ore_veins.jsonl`,
utilise par `tools/build_map_data.py` comme source autoritaire des gisements.
Chaque position y est une transformation de socket serialisee exacte. Quand ce
fichier est absent, le build retombe sur l'ancienne methode (jointure du socket au
mesh HISM le plus proche), qui laissait 283 filons en qualite `Inconnue` et 12
sockets en `Minerai inconnu`.

Chaque filon porte une qualite, les trois niveaux de `EOrePurityLevel` cote jeu :
`Impure`, `Normale` ou `Pure` : 201 impurs, 373 normaux, 86 purs. La goethite,
l'helium 3 et le soufre utilisent des classes de socket dediees et n'ont qu'un seul
grade de materiau physique, donc leur qualite est exacte. Le titane, le wolfram et
le calcium passent par des `BP_OreSocket` generiques : leur minerai et leur qualite
sont deduits en joignant le socket a l'ancrage de collision exporte le plus proche,
la geometrie des triangles de collision etant absente de l'export.

Le catalogue publie donc aussi la fiabilite de cette jointure, par filon :
`exact` (271), `elevee` (282), `moyenne` (100) et `faible` (7, tous du wolfram, a
des distances de jointure de 30 a 45 m contre ~1 m pour les jointures fiables). Cette fiabilite reste une information technique : elle est affichee dans le panneau
de selection en prereglage `Technique`, sans modifier le remplissage des marqueurs
ni ajouter une mention « estimee » a la purete dans les vues joueur.

Le catalogue publie enfin l'extracteur a poser sur chaque minerai
(`Excavatrice de minerai` 389 filons, `Foreuse laser` 102, `Extracteur de soufre`
95, `Extracteur d'helium 3` 74). L'extracteur figure dans la selection du filon,
sans filtre separe : chaque minerai correspond deja a une seule machine.

La qualite se lit directement sur la carte, sans ouvrir le panneau : le marqueur
garde la couleur de son minerai et la qualite joue sur sa luminosite et sa taille
(pur = plus clair et plus gros, impur = plus sombre et plus petit). Elle est aussi
filtrable dans le bloc `Gisements pour extracteurs` de l'onglet `Catalogue`, au-dessus
des types de minerai. Les compteurs de purete suivent les minerais actives et la recherche.
Le filtre agit sur les gisements ; les ressources recoltees a la main restent dans un bloc
separe. La recherche prend en compte les deux blocs, sans afficher « aucun resultat »
lorsque seuls des gisements correspondent. Le panneau de selection indique la purete.

Le generateur supprime les representations acteur/PCG du meme minerai situees a
moins de 500 cm sur chacun des deux axes horizontaux d'un gisement. Le catalogue
courant retire ainsi 366 doublons sans retirer les 660 gisements.

Les libelles EN/FR viennent des tables d'items du jeu embarquees dans l'export
(`item.item_name`), donc le catalogue affiche les noms officiels : `Minerai de
tungstene` pour `wolfram`, `Ophidine` pour `serpent_root`, `Pourprier` pour
`purplant`. Seuls les noms absents des tables, ou errones ("Polufruit"), sont
surcharges dans `tools/build_map_data.py`.

Les observations live de plantes recues dans `/cargo` sont appariees au point statique du meme type le plus proche dans un rayon de 150 cm. L'etat live est applique au point catalogue sans creer un doublon ; une observation sans correspondance reste un marqueur runtime distinct.

Ces observations obeissent aux memes interrupteurs par type que le catalogue :
`Ressources vegetales` reste l'interrupteur global, mais chaque plante possede sa
propre case. Les plantes que le plugin publie sans que l'export les contienne
(`Prickler`, `Prism Herb`, le gatherable generique `Plant`) apparaissent dans la
liste des plantes des qu'elles sont observees.

Cliquer un POI ecrit son nom sur la carte, sous son icone : les 29 bases
abandonnees se lisent par leur nom (`FRO "Mantis Head"`, `SMB "Purple Haze"`) et
non par leur silhouette. La description complete (`Future Health Solutions
Research Outpost`) reste dans le panneau de selection.

### Regeneration

Depuis la racine du depot, avec les exports locaux presents dans `analyse_map/` :

```bash
python3 tools/build_map_data.py
```

Le script lit `map_v2_resources.jsonl`, `map_v2_placements.jsonl` et `map_v2_pois.geojson`,
avec `map_v2_ore_veins.jsonl` comme source privilegiee des gisements lorsqu'il est present.
`map_v2_catalog.json` est une reference locale et n'est plus lu ; son ancien bloc
`rupture` n'est plus publie dans le manifeste. Le script reecrit `mapview/public/map-data/*.js`.
`analyse_map/` est ignore et conserve localement, tandis que les fichiers compacts generes
doivent etre versionnes pour les builds locaux et CI. Apres regeneration, lancer
`pnpm run check && pnpm run build` dans `mapview/`.

## POI live et filtres de carte

Le tableau optionnel `pois` de `GET /cargo` alimente quatre categories de points
d'interet :

- les bases abandonnees utilisent une icone de batiment fissure distincte ;
- le plugin courant publie les plantes disponibles Hydrobulb, Polifruit, Oxallop,
  Purplant, Serpent Root, Prickler, Prism Herb et Sulheart, ainsi que Gold Fruit,
  Thornfruit, Sikkim Rhubarb, Nootka Lupine et Plant par classes d'acteur ; il publie aussi les
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
les ressources vegetales, Ignitium et Star Tears. Chaque prereglage definit son propre
jeu de familles visibles : `Reseau` les affiche toutes, `Exploration` masque le reseau
cargo, `Recolte` se concentre sur les ressources et `Technique` ne garde que le joueur.
Les boutons restent utilisables pour s'ecarter du prereglage, et cet ecart est alors
compte dans l'en-tete du volet. `available` et `depleted` restent des etats visuels,
pas des filtres separes ; les acteurs de ressource epuises peuvent rester publies avec leur derniere
position connue. Quand Ignitium et Star Tears partagent une position pendant la fenetre de fin de
cycle, Star Tears est rendue au-dessus sans marquer artificiellement l'une des deux ressources comme
epuisee.

L'option `Afficher uniquement mes marqueurs et zones` masque toutes les donnees issues
du plugin (cargo, connexions, teleporteurs, joueurs et POI) pour ne conserver que les
marqueurs et zones crees par l'utilisateur, et efface la selection en cours. La
desactiver restaure les elements permis par le mode et les autres filtres actifs.

## Timeline Rupture et recentrage joueur

La barre Rupture affiche la phase et le temps restant. Un clic ou `Entree`/`Espace`
ouvre les details ; `Echap`, Fermer ou un clic exterieur les referme. Les details sont
rendus hors du bandeau, limites a la fenetre et defilables si necessaire. Sur mobile,
la barre du cycle reste visible sans defilement horizontal et le tiroir de filtres
reserve la hauteur reelle des commandes de carte, y compris quand elles occupent plusieurs lignes.

Les durees sont une calibration conservee de 3240 secondes (30/60/600/2550), pas une
lecture exacte des reglages Heat/Cold. L'animation entre observations et cette calibration
peuvent diverger du jeu ; brancher directement les durees brutes ne corrigerait pas ce decalage.

Le bouton `Joueur` de la barre d'actions et le raccourci `P` recentrent la carte sur le
joueur marque `self: true` dans le payload courant (son propre joueur), ou a defaut sur
le premier joueur. Le marqueur `self` est rendu dans une couleur distincte. Le bouton
est desactive et le raccourci reste sans effet si aucun joueur n'est disponible. Comme
les autres raccourcis, `P` est actif en dehors des champs de saisie.

## Test local des POI et de la retrocompatibilite

Le payload `/cargo` interne de `pnpm run mock-api` contient une base abandonnee, une
ressource `Gold Fruit` disponible, la meme ressource en etat `depleted` et une ressource
`Plant Fiber` disponible, un Ignitium et une Star Tears. Il permet de verifier les icones, les deux etats, la stabilite
de couleur pour un meme nom de ressource, la palette entre ressources et les filtres.
Les scenarios et les commandes detaillees sont documentes dans
`local-test/README.md`.

Pour simuler un ancien plugin, copier la fixture fournie
`local-test/examples/cargo-legacy-no-pois.json` vers `local-test/data/cargo.json` :
elle contient un payload `/cargo` valide mais sans champ `pois`, sans compteurs
`pois`, `abandoned_bases` et `plant_resources`, et sans champ `self` sur les joueurs.
Le mock sert cet
override tel quel : apres un refresh, le viewer doit continuer a afficher la carte et
les anciennes entites, avec zero POI observes (les POI du catalogue restent independants). Supprimer l'override permet de revenir au payload
POI interne.

## Test local de la pop-up de mise a jour

Le serveur mock (`pnpm run mock-api`, code dans `local-test/mock_server.py`) sert les
fichiers de fixtures deposes dans `local-test/data/` : `health.json`, `cargo.json` et
`rupture-cycle.json` (ou `rupture_cycle.json`). Si le fichier existe et n'est pas vide,
il est renvoye tel quel ; sinon le mock repond avec son payload interne par defaut.

Le payload `/health` par defaut declare `"viewer_contract_version": 2`, soit la meme
valeur que `VIEWER_CONTRACT_VERSION` : par defaut, la pop-up ne s'affiche donc pas.

Pour la declencher, creer `local-test/data/health.json` avec une valeur strictement
superieure a `VIEWER_CONTRACT_VERSION` (3 pour le viewer actuel a 2) :

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
  "viewer_contract_version": 3,
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
