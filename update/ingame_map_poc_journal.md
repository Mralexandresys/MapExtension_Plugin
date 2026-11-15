# Journal — POC carte logistique in-game

Plan : `INGAME_MAP_POC_PLAN.md`. Branche : `feature/ingame-map-poc` (depuis `feature/0.7`).

## 2026-11-13 — J0 et code J1

### J0 (atteint)

- SDK plugin `v1.23.2`, sous-module SDK de jeu `659656c` : vérifiés.
- Config : section `InGameMap`, `Enabled=0` par défaut, `ToggleKey=F8`.
- Enregistrement/désenregistrement symétrique dans `map_state_runtime.cpp` (client seulement).
- Builds : `client debug` et `server release` verts, 0 erreur, aucun warning hors SDK généré.
  Les deux `.cpp` du POC sont exclus des configurations serveur.

Écarts volontaires par rapport au plan :

- `ingame_map_tiles.*` et `ingame_map_texture_reader.*` ne sont pas créés : ils n'ont de contenu qu'à partir de J2.
- `TileBudgetMiB` n'est pas ajouté à la config tant que rien ne le lit (J2).
- La fenêtre est un **panneau** (`RegisterPanel`), pas encore un widget : il fournit gratuitement le bouton
  d'ouverture, la capture d'entrée (donc `GetMouseWheel()` fonctionne) et la barre de titre. Le passage en
  widget + overlay sans capture reste le sujet de J3.
- Libellés UI en anglais ASCII : le projet compile sans `/utf-8`, des accents dans les littéraux
  s'afficheraient mal dans ImGui.

### J1 (code livré, mesures runtime en attente)

Ce que fait le build :

- À la première publication en monde `ChimeraMain`, panneau ouvert, le log reçoit une fois (préfixe `[InGameMap]`) :
  pivot, taille de segment, étendue de grille, niveau de radiation, zoom min/défaut/max, table mip/zoom,
  puis pour chaque segment les noms R1/R2 avec `ImageSize` et `UVRegion` (utile pour l'inconnue R2 `(2,5)`),
  l'étendue monde de la grille sous les deux hypothèses d'axes, et pour 10 joueurs/téléporteurs :
  position monde, pixel web (`WorldToMap`) et case native sous les deux hypothèses.
- Chaque changement de `CurrentRadiationLevelReplicated` est loggé (`radiation level a -> b`).
- Le panneau dessine la grille (cadres, index, nom de ressource), le cadre de l'export web 9019×11691
  et les surcouches cargo / liens / téléporteurs / joueurs ; survol = nom, position monde, case native.

Choix de projection : l'écran suit la convention du viewer web (x écran ∝ X monde, y écran ∝ Y monde),
déjà validée par `mapview`. Les surcouches ne dépendent donc pas de l'orientation native ; seule la
position des **tuiles** en dépend. L'unique degré de liberté restant est « quel index de grille suit quel
axe monde », exposé par la case `Swap grid axes` (défaut : inversé, hypothèse issue de `feature/new-ui`).
Les signes viennent de `MapTerrainSegmentSize`.

Repère calculé hors jeu : l'export web couvre environ X ∈ [-432 000, 4 100], Y ∈ [-373 600, 191 400] (cm).
La bonne hypothèse d'axes est celle dont l'étendue de grille englobe ce rectangle de façon cohérente.

Limite connue : l'« erreur de placement en mètres » demandée en sortie de J1 ne peut pas se mesurer sans
pixels de terrain. Avec la grille seule on prouve l'orientation et l'étendue ; l'erreur métrique se mesurera
à J2.1 (aperçu basse résolution) en comparant un repère visible du terrain à un marqueur.

### Inconnues

| # | Inconnue | État |
| --- | --- | --- |
| 1 | Orientation / signes de la projection native | à lire dans le log (lignes `native grid world extent`) |
| 2 | Règle radiation → R1/R2 | à observer ; l'UI suppose R2 si niveau ≥ 2 (libellé seulement) |
| 5 | R2 `(2,5)` | `UVRegion`/`ImageSize` loggés par segment |
| 6 | `RegisterOnTick` côté client | fonctionnait sur un ancien log local (CL-119405) ; à reconfirmer en 1.23.x |

### Checklist runtime (à exécuter en jeu)

1. Mettre `InGameMap.Enabled=1` dans la config du plugin, déployer `build/Client Debug/Plugins/MapExtension_Plugin.dll`.
2. Charger une partie solo, ouvrir le panneau (`F8` ou bouton « Logistics map » du ModLoader).
3. Vérifier : le statut affiche `Terrain: ok (36 segments, radiation N)` ; pan au glisser ; zoom molette sans défilement du panneau.
4. Cocher/décocher `Swap grid axes` : noter quelle position fait coïncider la grille avec le cadre jaune de l'export web.
5. Ouvrir la carte native, noter si elle montre R1 ou R2 et le niveau de radiation affiché dans le statut ; si possible changer de zone de radiation et recommencer.
6. Fournir `ModLoader.log` (lignes `[InGameMap]` et `[EngineTick]`).

### Mesures

_À compléter avec le log._

## 2026-11-13 (soir) — premier essai en jeu : le panneau ne s'ouvre pas

Log fourni (`logs/ModLoader.log`, loader 1.23.x, CL-127004) :

- Aucune ligne `[InGameMap]` : le module ne s'est jamais activé. `PluginInit` a tourné à 23:32:14 alors que
  `InGameMap.Enabled` valait encore `0` ; l'option a été passée à `1` à 23:37 (fichier de config), sans rechargement.
  Ce n'était donc pas un bug d'ouverture mais l'option lue une seule fois au chargement.
- Correctif : `InGameMap.Enabled` s'applique maintenant à chaud via `RegisterOnConfigChanged`
  (activation/désactivation sans recharger le plugin). Builds client debug et serveur release verts.
- Inconnue 6 levée côté installation : `[EngineTick] UGameEngine::Tick found ... Hook installed successfully`
  sur le client en CL-127004. Reste à voir le service tourner (lignes `[InGameMap] terrain:`).

Non encore testé en jeu après correctif.

## 2026-11-13 (23:47) — premières mesures J1 en jeu + aperçu des tuiles (J2.1)

Retour utilisateur : panneau affiché, marqueurs au bon endroit, pas de tuiles (normal : non codées à ce stade).

### Mesures lues dans `logs/ModLoader.log` (CL-127004, solo)

| Donnée | Valeur |
| --- | --- |
| `MapTerrainTopLeftPivotPoint` | (-480000, -380000, 0) — en cm, égal à `MapAreaPivotPoint` |
| `MapTerrainSegmentSize` | (1000, 1000, 0) — **en mètres** |
| Grille | 36 segments, index [0..5]×[0..5] |
| Brushes | `ImageSize` 2048×2048 partout, `UVRegion` invalide (non utilisé) partout |
| Zoom min / défaut / max | 0.005 / 0.015 / 0.040 |
| Table mip/zoom | 0.2→8, 0.4→9, 0.8→10, 1.6→11, 1000→12 ; far/near = -2/-1 |
| Niveau de radiation | 1 |
| Chargement bloquant du data asset | 1.97 ms la première fois, ~0.02 ms ensuite |

Décisions :

- **Échelle** : la taille de segment est en mètres, le pivot en cm. Avec ×100 la grille couvre
  X ∈ [-480000, 120000], Y ∈ [-380000, 220000] et englobe l'export web (X ∈ [-432009, 4124], Y ∈ [-373635, 191429]).
  Sans ce facteur elle ne fait que 60 m de côté. Constante `kSegmentSizeToWorldUnits = 100` dans
  `ingame_map_terrain_source.cpp` : c'est la seule constante hors données du jeu.
  Recoupement : 100000 cm / 2048 px = 48.8 cm/px natif, contre 48.4 cm/px pour l'export web (écart ~1 %).
- **Orientation** : non décidable sans texture. La grille est carrée (6×6, segments carrés), les deux
  hypothèses d'axes donnent la même étendue. À trancher visuellement avec l'aperçu des tuiles.
- **R2 `(2,5)`** : confirmé dans le data asset lui-même — `TerrainSegmentTextureRadiation2` de `(2,5)` référence
  `Terrain_R2_01_05`, sans `UVRegion`. C'est une erreur de données du jeu, pas un artefact de lecture.
  Reste à comparer avec le widget natif avant d'accepter le repli sur R1.
- **`TSoftObjectPtr::Get()`** échoue à chaque appel ; le chargement bloquant sert de résolution (0.02 ms).
  Le log correspondant n'est plus émis que s'il est lent ou en échec.
- **`RegisterOnTick` client** (inconnue 6) : levée, le service tourne toutes les 500 ms en 1.23.x.
- **Table mip/zoom** : les valeurs 8..12 ressemblent à des exposants de résolution (256..4096 px), pas à des
  index de mip d'une texture 2048. À garder en tête pour J2.

Placement : positions monde de 10 objets loggées ; l'utilisateur confirme visuellement que les marqueurs sont
au bon endroit. L'erreur métrique reste à mesurer contre le terrain.

### Code ajouté (J2.1 + sonde piste A)

- Aperçu : `LoadFromUTexture2D` sur les 36 segments d'un bloc, sans espacement. Ligne de log
  `[InGameMap] tiles R1: loaded=…/36 total_ms=… max_ms=… width=…px free_slots=…`. Jusqu'à 5 tentatives
  espacées de 3 s si des textures n'ont pas encore de ressource GPU.
- Case `R2` : choix manuel de la variante (rechargement des tuiles). La règle radiation → variante reste à observer.
- Case `Transpose tiles (debug)` : échange u/v dans la tuile, pour séparer l'orientation de la grille de celle des textures.
- Bouton `Hi-res test (my tile)` : piste A sur **une seule** tuile (celle du joueur) —
  `SetForceMipLevelsToBeResident(60 s)`, attente 3 s, recopie. Lignes `force mips:` et `force mips result:` (taille obtenue, durée de copie).
  Limité à une tuile pour borner la mémoire tant que le format de la copie n'est pas connu.

### Checklist du prochain essai

1. Ouvrir le panneau : le statut doit afficher `36/36 tiles`.
2. Le terrain colle-t-il aux marqueurs (bases sur la terre, pas dans l'eau) ? Sinon essayer `Swap grid axes`, puis `Transpose tiles`, et noter la combinaison correcte.
3. Ouvrir la carte native : montre-t-elle la même variante que R1 ou que R2 (case `R2`) ? Noter le niveau de radiation du statut.
4. Cliquer `Hi-res test (my tile)`, attendre ~4 s : la tuile du joueur devient-elle nette ? Un à-coup ?
5. Fournir le log.

## 2026-11-13 — variante de terrain : R2

Constat utilisateur : c'est **R2** qu'il faut afficher (niveau de radiation 1 dans le dernier log), ce qui
recoupe le résultat de `feature/new-ui` (« en jeu, la variante affichée était R2 »).

- R2 devient la variante par défaut ; la case `R2` reste pour comparer.
- La règle complète niveau → variante n'est connue que pour le niveau 1. Aucune bascule automatique n'est
  codée tant qu'un autre niveau n'a pas été observé (log `radiation level a -> b`).
- Conséquence : la case `(2,5)` affichera la tuile dupliquée `Terrain_R2_01_05` (erreur des données du jeu).
  À comparer avec la carte native avant de coder un repli sur R1 pour cette case.

## 2026-11-14 — mesures tuiles, piste haute résolution choisie, J3/J4

### Mesures (`logs/ModLoader.log` de 00:14, solo, CL-127004)

| Mesure | Valeur |
| --- | --- |
| Aperçu, 36 tuiles `LoadFromUTexture2D` d'un bloc | 21.7 ms au premier chargement, 38–42 ms ensuite ; 64×64 px par tuile |
| Slots texture | 36 utilisés sur 4096 |
| Piste A, tuile `(3,1)` `Terrain_R2_03_01` | `SetForceMipLevelsToBeResident(60 s)` puis copie 3 s plus tard : **2048×2048 en 2.6 ms** |
| Rechargement de l'aperçu pendant le forçage | la tuile forcée ressort en 2048 px (largeurs `64..2048`) |

Retour utilisateur : carte correcte, positions bonnes, certaines zones floues (aperçu 64 px). Aucun crash.

### Décisions

- **Orientation prouvée** : index.X de la grille suit Y monde, index.Y suit X monde, textures non transposées
  (`kSwapAxes = true`). Les cases de debug `Swap grid axes`, `Transpose tiles` et le cadre de l'export web sont retirés.
- **Piste haute résolution : A** (forçage des mips). Elle donne la pleine résolution sans aucune fonction native,
  donc sans pattern ni dépendance à la version map du loader. La piste B (lecture CPU + décodage BC1) et le cache
  disque sont abandonnés : inutiles au vu du coût mesuré.
- **Stratégie de cache** : aperçu permanent + haute résolution à la demande. Le rendu publie les tuiles visibles
  les plus proches du centre de la vue (dès qu'une tuile fait ≥ 200 px à l'écran), plafonnées par `TileBudgetMiB`.
  Le service force leurs mips 20 s, tente la copie à partir de 1.5 s (3 copies max par passe de 500 ms), abandonne
  après 15 s, libère 10 s après la sortie de la vue et tout de suite à la fermeture du panneau.
- **Budget** : `TileBudgetMiB=64` → 23 tuiles, en comptant 2.7 MiB par tuile (2048² BC1 avec mips). Le format réel
  de la copie n'est pas mesurable depuis le plugin : c'est une estimation, à recouper avec la VRAM observée.
  Dépassement transitoire possible (jusqu'à ~2× pendant 10 s) quand on déplace la vue.

### Code livré

- J2 : haute résolution automatique (ci-dessus). Statut `HD tiles n/36` en bas de la carte.
- J3 : boutons `Fit` et `Me` dans la barre de titre (`titleBarFn`), `Follow me`, zoom ancré sous le curseur,
  coordonnées monde sous le curseur. Le panneau reste un `RegisterPanel` (capture d'entrée pendant qu'il est ouvert,
  comme la carte native) ; pas de mode overlay sans capture.
- J4 : liens cargo avec item/quantité et extrémités au survol, noms des téléporteurs dès ~4 m/px, noms des joueurs,
  calque `Resources` (POI du snapshot : bases abandonnées, plantes, Ignitium, Star Tears ; épuisés atténués),
  encart rupture (vague, étape, temps qui avance entre deux snapshots), pastilles de légende sur les filtres.
- Info-bulles construites uniquement pour l'objet survolé (plus de formatage par marqueur à chaque image).

Builds : client debug et serveur release verts, aucun warning dans le code MapExtension.
Incident de build : un `cl.exe` lancé à 00:07 est resté bloqué 15 min en verrouillant `vc143.pdb` (erreur C1041) ; arrêté à la main.

### Non fait / non vérifié

- Rien de ce lot n'a tourné en jeu.
- Session sur serveur dédié jamais testée avec la carte.
- Règle radiation → variante connue pour le niveau 1 seulement ; pas de bascule automatique.
- Case R2 `(2,5)` : toujours la tuile dupliquée des données du jeu, pas de repli.
- J5 (changement de monde, rechargement à chaud, 30 min d'endurance, fuite de slots) non commencé.
- Les données statiques de `mapview` (gisements, catalogue de ressources) ne sont pas dans le plugin : hors périmètre.

### Checklist du prochain essai

1. Zoomer sur une zone : devient-elle nette en 2–4 s ? Des à-coups pendant le chargement ?
2. `HD tiles` monte en zoomant et retombe à 0 environ 10 s après un dézoom complet ?
3. Survol d'un lien : item et quantité corrects par rapport au viewer web ?
4. Boutons `Fit` / `Me` visibles dans la barre de titre ; `Follow me` suit le joueur.
5. Calque `Resources` : positions plausibles ; fluidité avec beaucoup de points.
6. Fermer/rouvrir plusieurs fois, retour au menu puis rechargement : pas de crash ; fournir le log.

## 2026-11-14 (00:33) — retour d'essai : terrain à recliquer, rupture « None », demande du catalogue

Retour utilisateur : zoom et netteté « parfaits » (piste A validée en usage réel). Trois problèmes.

### 1. Il fallait cliquer plusieurs fois sur `Terrain` / `Radiation terrain`

Log : les 36 tuiles sont chargées à chaque fois (`loaded=36/36`), y compris au premier chargement (17.4 ms),
pris dans la même passe que la première résolution du terrain, juste après l'arrivée en monde. Seuls les
rechargements suivants donnaient un affichage correct. Cause exacte non prouvée ; deux suspects :
première copie prise avant que le jeu ait fini de remplir les textures, ou réutilisation par nom d'une texture
en cours de libération lors des bascules rapides R1/R2.

Correctifs (couvrent les deux) :

- l'aperçu est recopié 1 s, 5 s et 15 s après le début de la séquence (rejouée à chaque nouveau monde) ;
- chaque copie porte un nom unique, plus aucun partage par nom ;
- recocher `Terrain` relance la séquence (recours manuel).

**À confirmer en jeu** : si l'affichage initial est encore faux, décrire ce qu'on voit (noir, mauvaise tuile, flou).

### 2. Variante de terrain

Confirmé par l'utilisateur : la carte à afficher est R2. La case `Radiation terrain` est supprimée,
seule la variante R2 est chargée (`kUseR2`). Aucune bascule selon le niveau de radiation.

### 3. Rupture affichée « None »

`wave` / `stage` / `step` valent `None` en dehors d'une vague ; le viewer web n'affiche pas ces champs mais
calcule la phase à partir du temps écoulé dans le cycle (`useRuptureTimeline.ts`). L'encart fait maintenant
pareil : cycle de 3240 s, phases 30 / 60 / 600 / 2430 / 120 s (burning, cooling, stabilizing, stable, incoming),
affichage de la phase, du temps restant dans la phase et du temps avant la prochaine rupture. Sans temps écoulé,
repli sur l'étape brute du jeu.

### 4. Catalogue statique de ressources

Décision : **pas de nouveau format**. Le plugin lit les fichiers du viewer web tels quels (`manifest.js`,
`resources-*.js`, `pois.js`, JSON enveloppé `SRMAPDATA(...)`) avec le `nlohmann/json` déjà embarqué.
Un seul catalogue à régénérer (`tools/build_map_data.py`) pour le web et le plugin.

- Emplacement : `Plugins/MapExtension/map-data/`, à côté de la DLL client (5 fichiers, ~575 Kio).
- `./build.sh client ...` prépare ce dossier dans `build/<config>/Plugins/` ; le workflow de release l'ajoute à l'archive client.
- Chargement en tâche de fond à l'activation ; dossier absent → message dans le volet et dans le log, la carte fonctionne sans.
- Contenu : 57 147 ressources en 21 types (minerais avec pureté, plantes, créatures) + 241 points d'intérêt en 6 groupes.
  Totaux vérifiés hors jeu par un décodage Python indépendant des deltas.
- Les « placements » (17 915 éléments de décor, zones, acteurs techniques) ne sont pas lus.
- UI : case `Resources` (calque entier), case `Filters` (volet de gauche avec un filtre par type, `All`/`None` par
  catégorie). Par défaut : points d'intérêt et minerais affichés, plantes et créatures masquées.
  Minerai pur = contour blanc. Survol = type, pureté ou nom, coordonnées.
- Plafond de 12 000 points dessinés par image, par sous-échantillonnage régulier (les plantes font 54 513 points).
- L'ancien calque des ressources observées en direct est dans le volet : `Observed in game`.

Limites : pas de correspondance catalogue ↔ observations en direct (le viewer web marque les plantes récoltées,
pas le plugin) ; libellés du catalogue en anglais uniquement.

Builds : client debug et serveur release verts, aucun warning dans le code MapExtension. Rien de ce lot n'a tourné en jeu.

### Checklist du prochain essai

1. Copier **le dossier `MapExtension`** en plus de la DLL (`build/Client Debug/Plugins/` → `ModLoader/Plugins/`).
2. À l'ouverture, le terrain est-il correct sans rien cliquer ? Sinon décrire l'image et attendre 15 s.
3. Encart rupture : phase et temps cohérents avec la page web ?
4. `Filters` : gisements au bon endroit par rapport au viewer web et au terrain ; fluidité avec toutes les plantes cochées.
5. Log : ligne `static catalog loaded: 57388 points in 27 groups` attendue.

## 2026-11-14 — icônes d'items et filtres

Retour utilisateur : le lot précédent fonctionne (terrain, rupture, catalogue). Demandes : interface de filtres, et
icônes des items à la place des points.

### Icônes

- Source : l'export brut contient `item.item_asset_path` par ressource (ex. `/Game/Chimera/Items/I_TitaniumOre.I_TitaniumOre_C`).
  `tools/build_map_data.py` le publie dans `manifest.js` (champ `item` par type). Catalogue régénéré : seul
  `manifest.js` change, les 7 autres fichiers sont identiques octet pour octet.
- 15 types sur 21 ont un item : les 7 minerais, les 3 créatures, 5 plantes (Hydrobulb, Oxallop, Polifruit, Purplant,
  Serpent Root). Sans item dans l'export : aggressive_plant, gold_fruit, nootka_lupine, sikkim_rhubarb, soulheart,
  thornfruit → ils gardent une pastille de couleur. Les points d'intérêt n'ont pas d'item.
- Chargement : `LoadItemIcon` (game thread) = `MakeSoftClassPath` → `LoadClassAsset_Blocking` → CDO `UAuItemDataBase`
  → brush `ItemIcon` → `LoadFromUTexture2D`. Uniquement des fonctions réfléchies, aucun pattern. Trois types par passe
  de 500 ms, un seul essai par type. Le helper SDK `item_icons.h` n'est pas utilisé : il scanne et charge tous les
  items du jeu au démarrage, ce qui est disproportionné pour 15 icônes dont on connaît déjà le chemin.
- Affichage : icône 18 px ; minerais et créatures toujours, plantes à partir de ~2,5 m/px (pastilles avant, trop dense).
  Gisement pur = cadre blanc. Les icônes servent aussi de légende dans le volet de filtres.

### Filtres

- Le volet était déjà là mais fermé par défaut derrière une case peu visible : il est maintenant ouvert par défaut,
  avec un bouton `Filters` dans la barre de titre.
- Ajout des filtres de pureté des gisements (`Pure` / `Normal` / `Impure`), comme les pastilles du viewer web.
- Non repris du viewer web : recherche texte, filtre par représentation (actor / pcg / deposit), placements.

Build client debug vert, aucun warning dans le code MapExtension. Code serveur inchangé depuis le dernier build serveur.
`pnpm run check` du viewer non relancé (seul un champ de données a été ajouté au manifest).
Rien de ce lot n'a tourné en jeu.

### Checklist du prochain essai

1. Recopier la DLL **et** le dossier `MapExtension` (le `manifest.js` a changé).
2. Log attendu quelques secondes après l'ouverture : `item icons loaded: 15` ; sinon lignes `no icon for ...`.
3. Les icônes correspondent-elles aux bons items ? Un à-coup à la première ouverture ?
4. Filtres de pureté : décocher `Impure` et `Normal` ne laisse que les gisements purs.

## 2026-11-14 — Items des cargos, Ignitium et Star Tears

Lot précédent validé en jeu par l'utilisateur (« ça fonctionne ») et committé : `85e5f5e`.

### Ce que le projet sait déjà d'Ignitium / Star Tears

Référence : `IGNITIUM_STAR_TEARS_ANALYSIS.md` et `game-context.md` (section « Ignitium et Star Tears »). Le snapshot
contient déjà tout : `PoiMarker` de type `Ignitium` / `StarTears`, avec `Depleted`, `Harvestability` et une `Source`
qui commence par `rupture_phase.` quand le Star Tears est **projeté** depuis un site Ignitium non récolté (estimation,
pas un acteur observé). La carte in-game ne recalcule donc rien : elle affiche ces marqueurs.

- Deux calques séparés, cochés par défaut, dans le volet de filtres (`Rupture resources`), avec le nombre observé.
  Les autres observations (bases abandonnées, plantes) restent derrière `Other observed in game`, décoché par défaut.
- Icône de l'item ; épuisé = presque effacé, présent mais non récoltable = atténué, estimation = anneau orange.
- Infobulle : état (`available` / `unavailable` / `depleted` / `unknown`) et mention de l'estimation.
- Limite inchangée : seuls les sites réellement observés par le plugin apparaissent (acteurs chargés autour des joueurs).

### Items des cargos

- Le snapshot ne porte que le nom affiché de l'item (`CargoConnection::ItemDisplayName`), y compris par le protocole
  serveur. Pour ne toucher ni aux types partagés ni au protocole, l'icône est retrouvée côté client par ce nom :
  `LoadItemIconsByName` parcourt une fois `GObjects`, garde les CDO `UAuItemDataBase` et compare nom de classe,
  `ItemName` et `UniqueItemName`. Le même parcours sert Ignitium (`I_FireWaveOre_C`) et Star Tears (`I_StarTears_C`).
- Un parcours seulement quand un nom inconnu apparaît ; les noms introuvables sont retentés toutes les 30 s.
- Affichage : icône de l'item posée sur chaque lien ; vue rapprochée, libellé `Item xN` sur le lien et icônes des
  items à côté de chaque cargo (4 max) ; infobulle d'un cargo = une ligne par item avec quantité et destination.
- Risque connu, non testé : en serveur dédié le nom vient du serveur ; si sa langue diffère de celle du client,
  le nom ne correspond à aucun item local et le lien reste sans icône (le texte reste correct).

Build client debug vert, aucun warning dans le code MapExtension. Changement client uniquement : build serveur non
relancé. Rien de ce lot n'a tourné en jeu.

### Checklist du prochain essai

1. Recopier la DLL (le dossier `MapExtension` n'a pas changé).
2. Log attendu : `item icons by name: N/M found, scan_ms=...` — relever `scan_ms` (coût du parcours de `GObjects`).
3. Chaque lien cargo porte-t-il la bonne icône ? Infobulle d'un dispatcher : bons items, quantités, destinations ?
4. Près d'un site Ignitium : icône visible, état correct ? Après la rupture : Star Tears estimé (anneau) puis observé ?

## 2026-11-14 — Retours sur le lot cargo / rupture

Retour utilisateur : les icônes s'affichent, Ignitium / Star Tears fonctionnent. Demandes traitées :

- **Icônes des cargos uniquement dans l'infobulle.** Plus rien sur les liens ni à côté des cargos. Survol d'un lien ou
  d'un cargo : `BeginTooltip` avec une ligne par item (icône, nom, quantité, destination ou provenance, 12 lignes max).
- **Sélection d'un cargo au clic.** Ses liens passent en trait épais opaque, les autres à ~15 % d'opacité et ne
  captent plus le survol ; anneau blanc sur le cargo. Re-clic ou clic dans le vide : retour à la normale. Un clic
  est un relâchement sans déplacement de la carte.
- **Fermeture au clavier.** `Echap` (panneau au premier plan ou curseur sur la carte, `IsKeyPressed`) et la touche
  bindée relue par `IsKeyHeld` dans le rendu, au cas où le keybind du loader ne la reçoit pas panneau ouvert.
  Anti-rebond de 400 ms entre les deux chemins ; la fermeture elle-même se fait sur le game thread.
  Non vérifié : `Echap` peut aussi ouvrir le menu pause du jeu.
- **Bandeau rupture** en haut au centre de la carte, repris de la `rupture-strip` du viewer web : phase courante,
  temps restant, piste des cinq phases aux couleurs du viewer avec curseur (largeur minimale de 2 % pour
  `Rupture incoming`). Remplace l'encart en haut à droite. Le panneau de détails dépliable n'est pas repris.
- **Ignitium / Star Tears selon la phase.** Constat à la lecture du code : la règle existe déjà dans la capture
  (`ApplyRuptureResourcePhase`, appliquée à chaque snapshot, donc aussi à la carte in-game) : burning + cooling =
  Ignitium seul ; stabilizing = les deux ; stable + incoming = Star Tears, les sites Ignitium non récoltés étant
  projetés en Star Tears. Ajout côté carte : le bandeau indique la ressource de la phase, avec son icône.
  À confirmer avec l'utilisateur : ce qu'il a vu d'incorrect, si la règle ci-dessus ne correspond pas.

Build client debug vert, aucun warning dans le code MapExtension. Changement client uniquement. Rien testé en jeu.

## 2026-11-14 — États des points d'intérêt et des bases, correctif Star Tears

Retour utilisateur : en `Arcadia stabilizing`, compteurs 4 / 4 et icône Star Tears affichée.

- **Cause.** En phase de transition la capture ne filtre rien (`ApplyRuptureResourcePhase` sort tôt) : le jeu fait
  déjà exister l'acteur Star Tears sur le site d'un Ignitium intact, et la carte dessinait le Star Tears par-dessus.
- **Correctif (carte in-game seulement).** Un Star Tears situé à moins de 300 cm d'un Ignitium non épuisé est masqué
  et ne compte plus. Hypothèse à confirmer en jeu : pendant `stabilizing` l'item à récolter est bien l'Ignitium.
  Le viewer web et la capture ne sont pas modifiés.
- **Option `Other observed in game` supprimée** : les bases viennent du catalogue, avec leur état (ci-dessous).

### États lus dans le jeu (SDK CL-127004, aucun pattern)

- `ACrMapMenuDataReplicationHelper::PointOfInterestReplicationData` : GUID du marqueur + `EPointOfInterestState`
  (Hidden, VisibleUnknown, VisibleDiscovered, Disabled). Types de POI du jeu : Antena, AbandonedBase, Cave, Obelisk,
  OrbitalLander, ForgottenEngine — les mêmes que le catalogue (241 POI, tous avec `guid`).
- `ACrAbandonBaseInfoReplicator::AbandonBaseDataContainer` : par base (`AbandonBasePOIId`), statut de chaque objet à
  trouver, coffre de plans, objet d'histoire et kyste d'infection.
- Jointure par GUID au format `%08X-%08X-%08X-%08X` (A-B-C-D), relue toutes les 2 s. **Non prouvée** : le log
  `poi states: game=N matched_catalog=M abandoned_bases=K` dira si le format et les acteurs sont bons en solo.
- Affichage : POI découvert = plein, sinon atténué ; base = anneau vert (tout fait), orange (en cours), gris (rien) ;
  infobulle avec l'état et les quatre compteurs.

### Non fait, faute de source

- **Obélisques** : `ECrAlienObeliskState` et `ChargeValue` vivent dans un fragment Mass (solo) ou une bulle répliquée
  (client multi) ; pas d'accès réfléchi simple. Seul l'état de découverte du POI est affiché.
- **Geo scanner** : aucune classe de ce nom dans le SDK. Candidat : les 39 POI `antenna`. À confirmer par l'utilisateur.
- **« Quêtes »** : interprétées comme les objectifs de collecte des bases ; pas de lien avec un système de quêtes.

Build client debug vert, aucun warning MapExtension. Client uniquement. Rien testé en jeu.

## 2026-11-14 — Antennes (geo scanners), obélisques, icônes des plantes manquantes

Précisions utilisateur : geo scanner = antenne (état activée ou non) ; obélisques = état + pourcentage de charge ;
ces états doivent venir du jeu, pas du catalogue.

### Antennes — lues en jeu

- `ACrAntennasOwner::AntennaContainer.AntennasData` (répliqué) : `AntennaLocation` (FVector2f) + `ECrAntennaState`
  (Infected, ReadyToCharge, Charging, ReadyToActivate, Activated, Locked) pour **toutes** les antennes.
- Unité de `AntennaLocation` non documentée : si toutes les positions tiennent dans ±20 000, elles sont prises pour
  des mètres et multipliées par 100. À vérifier avec le log (`first x= y=`).
- Affichage : triangle coloré par état, infobulle avec l'état. Quand ces données existent, les points `Antenna` du
  catalogue ne sont plus dessinés ; la case `Antenna` des filtres pilote les deux.

### Obélisques — accès partiel

- `ECrAlienObeliskState` (Disabled, Cooldown, Enabled, EnabledAndVisiblyCharged, AttackInProgress) et `ChargeValue`
  existent, mais dans `FCrAlienObeliskFragment` (Mass). Deux accès réfléchis seulement :
  1. `ACrMassAlienObeliskClientBubbleInfo` (client multijoueur) : position + état + charge des obélisques répliqués ;
  2. acteurs `ACrAlienObelisk` chargés près du joueur : position + `PrevChargeValue` (charge seule, pas d'état).
- **En solo, l'état n'est donc pas lisible** sans accéder au gestionnaire d'entités Mass en natif (piste : dump IDA de
  `UCrAlienObeliskUpdateChargeValueProcessor`). Non entrepris.
- Affichage : hexagone coloré par état (gris clair si inconnu), `NN%` en vue rapprochée, infobulle avec la valeur
  brute. Hypothèse : `ChargeValue` entre 0 et 1.
- Relecture toutes les 4 s (parcours de la liste d'acteurs), panneau ouvert seulement. Log à relever :
  `live structures: antennas=N (...) obelisks replicated=R actors=A (first state= charge=) scan_ms=`.

### Prickler et autres plantes sans icône

Le Prickler était bien dans le catalogue (831 points) mais sans item, donc en pastilles de 3 px. Ajout dans
`tools/build_map_data.py` d'un item de repli pour cinq plantes (Prickler, Prism Herb, Glowcap, Grubbler, Sulheart),
classes présentes dans le SDK ; chemins `/Game/Chimera/Items/I_X.I_X_C` supposés d'après le motif des autres items.
`manifest.js` régénéré : 20 types sur 21 ont un item (reste `aggressive_plant`). `pnpm run check` non relancé.

Build client debug vert, aucun warning MapExtension. Client + outil + manifest. Rien testé en jeu.

## 2026-11-14 — Monolithes : état lu dans les marqueurs de la carte native

Retour utilisateur : antennes correctes ; aucune donnée pour les monolithes (logs non fournis, session solo supposée).

- Nouvelle source, réfléchie et valable en solo : `ACrMapMenuDataReplicationHelper::BaseAttackMarkerDataContainer`
  (`FBaseAttackMarkerDataFastArrayItem`) : `Location`, `AttackLocationIfAny`, `ECrAlienObeliskState` par obélisque.
  C'est ce que la carte native utilise. Lue en premier ; bulle Mass et acteurs chargés ne servent plus qu'à ajouter
  la charge à l'obélisque connu à moins de 10 m.
- Inconnu : le jeu y met-il tous les obélisques ou seulement ceux qui menacent une base ? Le log
  `obelisks map_markers=N` le dira (75 obélisques dans le catalogue).
- Charge : toujours limitée à la bulle répliquée (multi) ou à l'acteur chargé près du joueur (`PrevChargeValue`).
  La piste native (fragment Mass `FCrAlienObeliskFragment`) n'est pas entreprise.
- Affichage ajouté : trait rouge de l'obélisque vers `AttackLocationIfAny` quand elle est renseignée.
- Même règle d'unité que les antennes (mètres si tout tient dans ±20 000).

Build client debug vert, aucun warning MapExtension. Client uniquement. Rien testé en jeu.

## 2026-11-14 — Mesures du log utilisateur (17:20, solo)

- `poi states: game=241 matched_catalog=241 abandoned_bases=29` : jointure par GUID **prouvée** (format A-B-C-D).
- `live structures: antennas=37 (first x=-265031 y=-120521 state=5)` : positions d'antennes en cm, utilisateur
  confirme l'affichage. 37 antennes côté jeu contre 39 dans le catalogue.
- `obelisks map_markers=0 replicated=0 actors=0` : **aucune source réfléchie ne donne les obélisques en solo** sur
  cette sauvegarde. Les marqueurs de la carte native semblent n'exister que pour un obélisque qui menace une base ;
  aucun acteur `ACrAlienObelisk` chargé (rendu par ISM, ou joueur trop loin — non tranché).
- `item icons by name: 19/19 found, scan_ms=17.6` ; `item icons loaded: 20` (les 5 chemins de repli des plantes sont bons).
- `scan_ms=62.4` pour le premier relevé des structures : probablement la première résolution des classes
  (`StaticClass`). Ajout d'un avertissement (5 max) si un passage suivant dépasse 10 ms, pour trancher.

Reste pour les obélisques : lecture native du fragment Mass `FCrAlienObeliskFragment`. Le pseudocode de
`UCrAlienObeliskUpdateChargeValueProcessor::Execute` montre le chemin du jeu (`ForEachEntityChunk`,
`InternalGetFragmentDataPtr`) ; cela demande des signatures vérifiées sur l'exécutable installé et un hook, donc une
décision de l'utilisateur (fragile aux mises à jour, risque de crash). Non entrepris.

## 2026-11-14 — Lecture native des obélisques (décision utilisateur : « Oui, lance »)

Vérifications avant d'écrire :

- Aucune liste d'obélisques réfléchie : `ACrAlienObeliskSpawner` n'a aucun champ propre, `UCrBaseAttackStateSubsystem`
  est opaque (0x50 octets de padding).
- **Le dump `StarRupture-Game-IDADump/` (juillet 2026) ne correspond pas à l'exécutable installé** (octobre, CL-127004) :
  les octets aux adresses du dump diffèrent. Ses adresses sont donc inutilisables ; seuls son assembleur et son
  pseudocode servent de guide. Tout a été revérifié sur l'exe installé (lecture seule).
- `FMassArchetypeData::GetFragmentDataForEntity` : signature de 37 octets (prologue + début du hachage), **1 seule
  occurrence** dans l'exe installé, à `0x14445A880`. Retourne null si l'archétype n'a pas le fragment.
- `FMassEntityManager::IsEntityValid` désassemblé sur l'exe installé, identique au dump : stockage d'entités à
  `+0x20`, type du variant à `+0x60` (1 ou 2 = stockage construit, l'aiguilleur retourne l'adresse telle quelle),
  vtable `+0x10` = archétype de l'index, `+0x48` = index valide.

Implémentation (client uniquement) :

- `plugin.cpp` exporte `OnPluginLoadHooks` (client) → `ResolveNativeAccess`. Fonctionnalité déclarée
  `ingame-map-monoliths` avec deux clés : la fonction ci-dessus (table du loader d'abord, notre signature ensuite,
  contrôle `FUNCTION_START`) et le champ natif `UMassEntitySubsystem::EntityManager` (ligne CL-127004, 0x38, comme
  `plugin_map.h`). Un échec coupe seulement les monolithes ; sans déclaration acceptée, rien n'est demandé.
- `ScanObelisksGuarded` : parcourt les index d'entités, demande `FCrAlienObeliskFragment` puis `FTransformFragment`.
  Lecture seule, sous `__try/__except` ; une exception ou un stockage inattendu coupe la lecture pour la session.
- Seuil d'attaque : `UCrAlienObeliskTrait::Parameters.ChargeValueToActivateAttack` lu dans les traits chargés
  (réfléchi). Pourcentage = charge / seuil ; si les traits divergent (niveaux d'obélisque), valeur brute seule.
- Aucun hook, aucune écriture mémoire. La lecture native complète les sources réfléchies (fusion à 10 m) et fait
  autorité sur l'état et la charge.
- Log : `obelisks native=N (entities=M) ... max=seuil scan_ms=`, et `obelisk charge threshold: traits= min= max=`.

Inconnus : coût du parcours complet (M entités toutes les 4 s, à lire dans `scan_ms`) ; plage réelle de la charge ;
cohérence du seuil entre niveaux. Limite assumée : valable pour CL-127004 seulement, à revérifier à chaque mise à
jour du jeu.

Builds : client debug et serveur release verts, aucun warning MapExtension. **Rien testé en jeu.**

## 2026-11-14 — Icônes de marqueur de la carte native

Demande utilisateur : réutiliser les textures `/Game/Chimera/UI/Map/Markers/T_UI_*_mapIcon`.

- `LoadTextureAsset` (chemin d'asset → `LoadAsset_Blocking` → `LoadFromUTexture2D`), 3 chargements max par passe,
  rangés dans `g_namedIcons` sous `marker:<nom>` (donc libérés et retentés comme les icônes d'items).
- Utilisées : `abandonedBase`, `cave`, `forgottenEngine`, `orbitalLander` (POI du catalogue, atténuées si non
  découvert, anneau de progression conservé pour les bases), `antenna` et `obelisk` (structures lues en jeu, teintées
  par la couleur d'état), `player`.
- Repli sur les formes géométriques tant qu'une icône n'est pas chargée.
- Non utilisées, faute de donnée correspondante sur la carte : `baseCore`, `coop`, `deadBody`, `deathSpot`, `drone`,
  `enemy`, `habitat`, `marker`, `unknown`, `zoom`, `T_PlayerFootstepMapMarker`, `T_WarningMapMarker`.
- Inconnu : les textures sont-elles blanches (la teinte par état suppose que oui) ? À vérifier en jeu.

Build client debug vert, aucun warning MapExtension. Rien testé en jeu.

## 2026-11-14 — Base cores et icône des téléporteurs

- Base cores : aucune position réfléchie dans `ACrBaseCoreReplicationHelper` (niveau, refroidissement, mais pas de
  coordonnées) et `ECrBuildingType` n'a pas de valeur « base core ». Source retenue :
  `ACrMapMenuDataReplicationHelper::BuildingsMarkerDataContainer` (`Location`, `BuildingName`, `BuildingUniqueName`),
  filtrée sur un nom unique contenant « core » (insensible à la casse). **Filtre non vérifié** : le log
  `building markers: N, base cores=M, unique names: ...` liste les noms uniques réellement présents pour le corriger.
- Affichage : calque « Base cores » (coché par défaut), icône `T_UI_baseCore_mapIcon`, nom au zoom, infobulle.
  Niveau du core non affiché (pas de jointure entité ↔ marqueur sans lecture native).
- Téléporteurs : icône `/Game/Chimera/UI/Buildings/Teleporter/Textures/T_UI_teleporterIcon_128x128`, teintée par la
  couleur du calque. La table des icônes de marqueur porte maintenant dossier + asset.

Build client debug vert, aucun warning MapExtension. Rien testé en jeu.

## 2026-11-14 — Niveau des base cores

- Retour utilisateur : les base cores s'affichent (le filtre « core » sur le nom unique fonctionne ; log non fourni).
- Niveau : `ACrBaseCoreReplicationHelper::BaseCoreDataContainer.BaseCoreData` (`UpgradeLevel`, `InfectionSphere`,
  `CurrentCoolingCapacity`, `CurrentCoolingCapacityUsed`), joint au marqueur par handle d'entite :
  `FCrBaseCoreData::BaseCore.Entity.CachedHandle` == `FBuildingMarkerDataFastArrayItem::BuildingHandle`. Tout réfléchi.
  Repli : un seul core et une seule entrée → jointure directe.
- Inconnus : `CachedHandle` est-il renseigné en solo (log `core data=N joined=M`) ? `UpgradeLevel` commence-t-il à 0
  ou à 1 (affiché brut) ?
- Affichage : « nom (lvl N) » au zoom, infobulle niveau + refroidissement utilisé/total, marqueur rouge si infecté.

Build client debug vert, aucun warning MapExtension. Rien testé en jeu.

## 2026-11-14 — Mesures du log utilisateur (20:01, solo)

- **Lecture native des obélisques prouvée** : `obelisks native=75 (entities=65535) ... scan_ms=2.0`. 75 = nombre du
  catalogue. Feature résolue sans échec au chargement. `entities=65535` : borne de `IsValidIndex` (probablement la
  taille d'une page du stockage), non investigué puisque les 75 sont trouvés.
- Seuil d'attaque : `obelisk charge threshold: traits=1 min=0.500 max=0.500` → pourcentage = charge / 0,5.
- Base cores : `building markers: 109, base cores=60, core data=60 joined=60`, noms uniques
  `BaseCore Habitat Teleporter HabitatBig`. Filtre « core » et jointure par `CachedHandle` **prouvés**.
  `first level=0` : `UpgradeLevel` semble commencer à 0 (à confirmer avec l'utilisateur).
- Icônes : `baseCore` et `teleporter` chargées ; les 7 autres en échec. Cause probable : `LoadFromUTexture2D` rend
  null tant que la texture n'est pas sur le GPU (documenté dans l'interface) ; ces deux-là sont déjà affichées par le
  HUD, les autres viennent d'être chargées. Correctif : `SetForceMipLevelsToBeResident` + nouvel essai à chaque passe
  (0,5 s, 20 max) au lieu de 30 s, et cause de l'échec dans le log (`asset not found`, `not a Texture2D`,
  `not resident`). **Non vérifié.**
- `live structures scan is slow: 665.3 ms` au premier relevé : `FindObject` sans filtre de type construisait le nom
  complet de chaque objet. Corrigé avec `EClassCastFlags::ScriptStruct`. À remesurer.

Build client debug vert, aucun warning MapExtension.

## 2026-11-14 — Log utilisateur 20:52 et niveau des bases

- Icônes de marqueur : **9/9 chargées**, les 7 en échec passent après 1 attente (`waits=1`) → cause confirmée
  (texture pas encore sur le GPU).
- Utilisateur : le jeu compte les niveaux de base à partir de 1 → `Level = UpgradeLevel + 1`.
- Premier relevé encore lent : `scan_ms=245.6` (665 avant), puis 2,0 ms. Reste suspecté : `FindObject` par nom
  complet sur tous les ScriptStruct et premières résolutions `StaticClass`. Passage à `FindObjectFast` + vérification
  du nom complet sur le seul résultat. À remesurer ; à-coup unique par session.
- Un relevé pris avant la fin du chargement donne `building markers: 0 ... core data=60 joined=0`, corrigé au relevé
  suivant (4 s).

Build client debug vert, aucun warning MapExtension.

## 2026-11-14 — Section cargo du volet des filtres

- Log 21:04 : premier relevé des structures à 170 ms (665 → 245 → 170), suivants à 2 ms. À-coup unique par session,
  reste attribué aux premières résolutions `StaticClass` ; non poursuivi.
- Demande utilisateur : « Cargo » et « Links » quittent la barre du haut pour une section « Cargo » en tête du volet.
- Ajouts : case « Unconnected cargo » (cochée par défaut ; décochée, un cargo sans lien est masqué), champ de
  recherche et liste des items en transit (icône, nombre de liens, case à cocher). Un ou plusieurs items cochés :
  seuls leurs liens et les cargos à leurs extrémités restent ; bouton « All » pour vider le filtre.
- État du filtre côté render thread uniquement (`g_cargoItemFilter`, `g_cargoItemSearch`), aucune donnée partagée.

Build client debug vert, aucun warning MapExtension. Rien testé en jeu.

## 2026-11-14 — Plantage au démarrage (21:12 et 21:13), cause non trouvée

Faits :

- Journal Windows : 2 plantages (21:12:55, 21:13:18), module `StarRupture-ModLoader-Core.dll` 1.23.3, exception
  `0xc0000409`, même décalage `0x3a9898` = `_invoke_watson` (PDB du loader) : arrêt volontaire du CRT du loader.
  Aucun autre plantage du jeu sur 4 jours.
- `ModLoader.log` : dernière ligne `[WorldEndPlay] SEH exception in original UWorld::EndPlay (code=0xC0000005)`,
  43 ms après « Plugins ready ». Dans un lancement réussi, ce moment est le passage au menu principal
  (`Browse: Map_MainMenu`), qui termine le monde de démarrage.
- Le plugin s'est chargé normalement (signature résolue, `PluginInit` OK). Carte jamais ouverte.
- Pas de processus du jeu resté en arrière-plan ; configs du loader et du plugin normales.

Ce qui a changé depuis le dernier lancement réussi (21:04) : uniquement la section cargo du volet (code de rendu,
exécuté seulement carte ouverte). Aucun code du plugin modifié sur le chemin de démarrage ou de fin de monde.

Non établi : si le plantage vient du plugin. Test discriminant demandé à l'utilisateur : relancer, puis lancer sans
la DLL.

## 2026-11-14 — Version 0.8 : suppression du viewer web

- Utilisateur : le build de 21:11 fonctionne (« ça fonctionne parfait »). Le plantage au démarrage de 21:12/21:13 ne
  s'est pas reproduit ; **cause jamais identifiée**, rien n'a été modifié pour le corriger.
- Décision utilisateur : `mapview/` supprimé (viewer, tuiles, tests locaux, `.nvmrc`). Le catalogue passe de
  `mapview/public/map-data/` à `map-data/` (racine) ; `build.sh`, `tools/build_map_data.py` et le workflow de release
  suivent. Le workflow ne construit plus le viewer et ne publie plus l'archive `-viewer.zip`.
- Choix faits sans demande explicite : `[InGameMap] Enabled` passe à `1` par défaut (seule interface restante) ;
  l'API HTTP et le contrat `viewer_contract_version` restent tels quels pour les anciens viewers, avec consigne de ne
  plus incrémenter le contrat.
- Docs mises à jour : `README.md`, `README.fr.md`, `DEVELOPERS.md`, `AGENTS.md`, `descriptionbbcode` (changelog v0.8),
  `THIRD_PARTY_NOTICES.md`, `game-context.md`, `changelog.fr.local` (local, non versionné).

Builds : client debug, client release et serveur release verts. Le build client release n'a pas été testé en jeu.

## 2026-11-14 — Réorganisation : dossier `src/` et découpage de la carte en jeu

Demande utilisateur : optimiser, refactorer, mettre le code dans `src/`.

- Tout le C++ du plugin passe sous `src/` (`src/client`, `src/server`, `src/shared`). `src/` est ajouté aux chemins
  d'inclusion, les `#include` ne changent pas. `third_party/` reste à la racine. Projet, test Python et docs adaptés.
- `ingame_map_ui.cpp` (2071 lignes) découpé le long de la frontière de threads :
  `ingame_map_service.cpp` (game thread), `ingame_map_render.cpp` (render thread), `ingame_map_ui.cpp` (activation),
  `ingame_map_internal.h` (constantes, `Published`, variables partagées en `inline`).
- `ingame_map_terrain_source.*` ne garde que le terrain et les textures ; l'état du jeu et la lecture native passent
  dans `ingame_map_live_state.*`.
- Déplacement strict : comparaison ligne à ligne avant/après, aucune ligne de code perdue. Seuls ajouts :
  `StartService` / `StopService` (extraits de `Enable` / `Disable`) et les déclarations de l'en-tête interne.
- **Aucune optimisation appliquée.** Coûts mesurés en jeu : relevé des structures 2 ms toutes les 4 s, icônes 17 ms
  une fois. Seul point lent connu : premier relevé à 170 ms (une fois par session), cause non mesurée finement.
  Pistes non mesurées, donc non touchées : ensembles reconstruits à chaque image dans `DrawOverlays` et
  `RenderCargoFilters`. `map_state_capture.cpp` (4414 lignes) non touché : code ancien, sans test en jeu disponible ici.

Builds : client debug, client release, serveur release verts ; `resource_observation_test.py` passe.
**Rien testé en jeu après la réorganisation.**

## 2026-11-15 — Analyse de performance (log 00:03) et correctifs

Utilisateur : la réorganisation fonctionne en jeu (commit `90b8e18`).

Ce que le log mesure (une session, carte ouverte à 00:01:38) :

| Poste | Mesure | Fréquence |
| --- | --- | --- |
| Premier relevé des structures | 168 ms | une fois par session, à la première ouverture |
| Relevés suivants | 1,7 ms | toutes les 4 s |
| Copie de l'aperçu (36 tuiles) | 29 à 35 ms | 3 fois par arrivée en monde, carte ouverte |
| Icônes d'items par nom | 18 ms | une fois |

Ce que le log ne mesure pas : le coût d'une passe de service courante et le coût du rendu par image.

Correctifs (non vérifiés en jeu) :

- `WarmUpLiveState` : résout pendant le chargement du monde les classes et les deux types de fragment dont le
  relevé a besoin. Hypothèse non prouvée : les 168 ms viennent de ces premières résolutions (chaque `StaticClass`
  parcourt GObjects). Le log `live state warm-up: ... ms=` et le `scan_ms` du premier relevé trancheront.
- Aperçu : 12 tuiles par passe au lieu de 36 d'un coup (≈10 ms par passe sur 3 passes). L'aperçu se complète en
  1,5 s au lieu d'apparaître d'un bloc.
- Mesure ajoutée : avec `[Diagnostics] LogRefreshTimings=1`, une ligne toutes les 30 s d'ouverture
  (`timings over 60 passes: service avg/max | render frames/avg/max`). Le rendu est chronométré côté CPU seulement.

Build client debug et release verts, aucun warning MapExtension.

## 2026-11-15 — Log 00:17 avec `LogRefreshTimings=1`

**Biais majeur : la DLL en jeu est le build Debug** (8 807 424 octets, identique à `build/Client Debug/`, contre
600 064 pour le Release). Toutes les mesures prises depuis le début l'ont donc été sans optimisation et avec la
bibliothèque standard de débogage. Elles surestiment le coût réel, d'un facteur inconnu.

Mesures (Debug) :

- `live state warm-up: classes=10/10 fragments=1 ms=195.0` au chargement ; premier relevé ensuite à 26 ms (168
  avant). Hypothèse confirmée : le coût venait des premières résolutions.
- Carte ouverte, par fenêtre de 30 s : service moyenne 4,0 à 4,3 ms (max 9 à 11 ms) toutes les 0,5 s ; rendu
  moyenne 1,4 à 1,6 ms par image (max 3 à 7 ms) sur ~2300 images.
- Aperçu étalé : total 47 à 70 ms sur 3 passes (30 ms d'un bloc avant) — pas un gain en cumul, à rejuger en Release.
- **Capture principale du snapshot (`map_state_capture.cpp`), toutes les 2 s, carte ouverte ou fermée** : 101
  relevés, total moyen 30 ms, médiane 27 ms, max 229 ms. Détail : `replicator` 20,5 ms en moyenne (max 62),
  `poi_capture` 13 ms (32 fois, max 161), `poi_snapshot_reuse` 5 ms. C'est le poste dominant du plugin.
- 13 rafraîchissements viennent de `HttpRequest(/cargo)` : un client HTTP interroge encore l'API (ancien viewer ?).

Lecture du code de `CaptureFromReplicator` : aucune opération lourde évidente (clés construites par
`ostringstream`, tables de hachage, noms d'items) ; ce profil est typiquement très pénalisé en Debug. Aucune
optimisation tentée à l'aveugle. Prochaine étape : mêmes mesures avec la DLL Release.

## 2026-11-15 — Mesures en Release (log 00:32)

DLL en jeu : Release (600 064 octets). Premier test en jeu de ce build ; l'utilisateur ne signale aucun défaut.

| Poste | Release | Debug (log précédent) |
| --- | --- | --- |
| Capture du snapshot, toutes les 2 s | médiane 3 ms, max 18 ms (86 relevés) | médiane 27 ms |
| dont `replicator` | moyenne 2 ms, max 5 | 20,5 ms |
| Rendu de la carte, par image | 0,29 à 0,56 ms, max 3,8 ms | 1,4 à 1,6 ms |
| Service, carte ouverte, hors copies | 0,8 à 1,1 ms en moyenne | 4 ms |
| Échauffement au chargement | 15,9 ms | 195 ms |
| Premier relevé des structures | 4,7 ms | 26 ms |
| Icônes d'items par nom (une fois) | 21 ms | 18 ms |
| Copie de l'aperçu, 36 tuiles | 67 à 91 ms cumulés | 47 à 70 ms |

Conclusion : la capture cargo n'est pas un problème en Release ; l'alerte du log précédent venait du build Debug.
Seul à-coup restant : la copie de l'aperçu (copie GPU, insensible à l'optimisation), ~2,2 ms par tuile, soit
25 à 30 ms par passe avec 12 tuiles — visible dans `service max=44 / 36 / 26 ms`.

Correctif : 4 tuiles par passe (~9 ms) et séquence de recopie à 1 / 7 / 20 s (au lieu de 1 / 5 / 15) pour qu'une
copie se termine avant la suivante. L'aperçu complet arrive en 4,5 s. Non vérifié en jeu.

Non expliqué : la séquence d'aperçu a été rejouée une seconde fois à 00:31:40 (case « Terrain » recochée ou nouveau
monde, non tranché). 136 lignes de mesure viennent encore de `HttpRequest(/cargo)` : un client HTTP interroge l'API.

Builds client release et debug verts.

## 2026-11-15 — Suppression de l'API HTTP locale

Décision utilisateur, après constat qu'un client interrogeait encore `/cargo` (136 captures déclenchées en 2 min).

- Supprimés : `src/map_state_http.*`, `src/map_state_json.*`, `tools/tests/http_server_smoke.cpp`, le réglage
  `[Http] Port`, `HandleProcessDetach` (ne servait qu'au serveur HTTP) et le contrat `viewer_contract_version`.
- Conservé : `RequestCargoSnapshotRefresh`, utilisé en interne (délégué de rupture, apparition d'une ressource) ; son
  motif de log devient `RequestedRefresh`. `nlohmann/json` reste, pour le chargeur du catalogue.
- Conservé sans lecteur : les champs `MapLocation` du snapshot et la projection de `map_state_types.h` (trop
  imbriqués dans la capture et le protocole serveur pour être retirés sans risque).
- Conséquence : une ancienne copie du viewer ne reçoit plus rien ; le plugin n'ouvre plus aucun port. Le protocole
  de synchronisation serveur (v6) est inchangé.
- Docs alignées : README (EN/FR), DEVELOPERS, AGENTS, descriptionbbcode, notices, game-context, changelog local.

Builds : client release, client debug, serveur release verts ; `resource_observation_test.py` passe ; la DLL
Release ne contient plus les chaînes du serveur HTTP. **Rien testé en jeu.**

## 2026-11-15 — Retour utilisateur : régression après les correctifs de performance

Utilisateur : la carte met plus de temps à charger, les icônes aussi, certaines ne se chargent jamais.

Log 01:14 (Release, build de 00:41) :

- Aperçu complet 5,5 s après l'ouverture (1,5 s au test précédent) : conséquence directe de l'étalement à 4 tuiles
  par passe. **Étalement retiré**, retour aux 36 tuiles d'un coup à 1 / 5 / 15 s (70 à 90 ms par copie). Décision :
  l'à-coup est préféré à un aperçu lent.
- Icônes : le log n'indique **aucun échec** (9 icônes de marqueur chargées en 2 s, `item icons loaded: 20`, aucun
  `no icon for`). La cause des icônes manquantes n'est donc pas établie.
- Constat annexe : `building markers: 0, base cores=0` à l'ouverture (01:11:30), et 109 / 60 seulement à 01:12:26.
  Les marqueurs de bâtiment du jeu arrivent tard ; les base cores sont absents de la carte pendant ce temps. Déjà
  visible dans les logs précédents (14 s à 00:01), indépendant des correctifs. Piste si c'est ce que l'utilisateur
  a vu : lire les base cores nativement, comme les monolithes.
- Conservés : échauffement au chargement (15 ms, invisible) et relevé `timings over 60 passes`.
- Ajout de diagnostic : taille de chaque icône copiée dans le log (`marker icon ... WxH`, `item icon ...: WxH`),
  pour détecter une icône prise avant la fin du streaming.

Build client release vert. Non testé en jeu.

## 2026-11-15 — Validation en jeu du build Release `ea1f2af` (log 01:24)

Utilisateur : tout fonctionne bien.

- Icônes : 20 icônes d'items en 256x256 (une en 512x512), 9 icônes de marqueur en 128x128. Aucune copie en petit
  mip : l'hypothèse « icône copiée trop tôt » est écartée pour ce test.
- Aperçu : complet 0,5 s après l'ouverture, 39 à 45 ms par copie.
- Capture du snapshot : 125 relevés en 161 s, médiane 3 ms, max 18 ms, 363 ms cumulés (0,2 % du temps).
- Carte ouverte : service 5 ms en moyenne sur la première fenêtre (max 47 ms, chargements initiaux), rendu 0,44 ms
  par image.
- Base cores : toujours `building markers: 0` à l'ouverture, 109 / 60 seize secondes plus tard. Retard du jeu,
  non corrigé.

**Correction d'une affirmation antérieure.** 69 captures sur 125 portent le motif `RequestedRefresh` alors que
l'API HTTP n'existe plus : elles viennent des demandes internes (apparition d'une ressource, délégué de rupture).
Avant la suppression, ce même chemin était journalisé `HttpRequest(/cargo)` quel que soit le demandeur. Les 136
lignes attribuées à « un client HTTP qui interroge encore l'API » étaient donc, au moins en grande partie, ces
demandes internes. Rien ne prouve qu'un ancien viewer tournait.

## 2026-11-15 — Lecture native des base cores

Décision utilisateur : lire les base cores comme les monolithes, pour supprimer le retard des marqueurs du jeu
(16 à 56 s mesurés entre l'ouverture de la carte et `building markers: 109`).

- `ScanObelisksGuarded` devient `ScanEntitiesGuarded` : un seul parcours, deux fragments cherchés
  (`FCrAlienObeliskFragment`, `FCrMassBuildingBaseCoreFragment`), plus `FTransformFragment` pour la position. Un
  archétype n'est rejeté que s'il n'a aucun des deux. Aucune nouvelle signature, aucun nouvel offset.
- Fusion : le marqueur du jeu, quand il existe, fournit le nom ; un core vu seulement nativement s'appelle
  « Base core ». Jointure du niveau par index d'entité (`CachedHandle.Index`), le numéro de série n'étant vérifié
  que s'il est connu (marqueur).
- Type du fragment introuvable : seuls les base cores natifs sont perdus, les monolithes restent lus.
- Log : `building markers: N, base cores=M (native=K), core data=.. joined=..`.

Inconnu : le fragment est-il porté uniquement par les base cores du joueur (60 attendus sur cette sauvegarde) ou
aussi par d'autres entités ? `native=K` le dira.

Builds client release et debug verts, aucun warning MapExtension. **Non testé en jeu.**

## 2026-11-15 — Base cores natifs validés, noms personnalisés

Log 12:15 : `building markers: 0, base cores=60 (native=60), core data=60` dès l'ouverture, puis 109 marqueurs
une minute plus tard. Le fragment `FCrMassBuildingBaseCoreFragment` n'est porté que par les 60 base cores du
joueur. Utilisateur : c'est bon, il manque les noms personnalisés.

Correctif : les noms viennent de `UCrBuildingCustomNameSubsystem::CustomNames` (clé : identifiant persistant
d'entité), la source déjà utilisée pour les cargos dans `map_state_capture.cpp`. Jointure sur
`FCrBaseCoreData::BaseCore.Entity.ID`, faite en même temps que celle du niveau. Un nom personnalisé remplace le
nom du marqueur ; sans nom personnalisé, le core garde le nom du marqueur ou « Base core ».

Inconnu : en solo, la table contient-elle tous les noms dès le chargement ? Log `named=N/M` (cores nommés / noms
connus du jeu, tous bâtiments).

Builds client release et debug verts. Non testé en jeu.
