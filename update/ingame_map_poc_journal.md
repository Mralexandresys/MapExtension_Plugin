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
