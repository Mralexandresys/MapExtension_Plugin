# Plan — POC carte logistique in-game (MapExtension_Plugin, base `feature/0.7`)

> Document de passation destiné à un agent de développement. Il résume toute
> l'analyse faite le 2026-11-13 (SDK v1.23.2, branche `feature/new-ui`, mod
> MiniMap, SDK de jeu CL-127004). Il est autoportant : l'agent ne doit pas avoir
> besoin de refaire cette recherche, seulement de **vérifier** les points
> marqués « à vérifier ».

## 0. Mode d'emploi pour l'agent

- Répondre et documenter **en français** (règle projet).
- Lire d'abord `AGENTS.md` (repo plugin) et `../AGENTS.md` (repo racine) : leurs règles priment sur ce plan.
- Travailler sur une **nouvelle branche** créée depuis `feature/0.7` : `feature/ingame-map-poc`.
- Ne **pas** merger ni rebaser `feature/new-ui` (voir §4).
- Ne **pas** consulter ni réutiliser le code source du mod MiniMap (licence, voir §6). Tous les faits utiles sur le jeu sont déjà dans ce document.
- Après chaque modification C++ : lancer les builds (§11) et résumer les erreurs. Ne pas committer ni pousser sans demande explicite.
- Avancer **jalon par jalon** (§9). Chaque jalon a un critère de sortie vérifiable ; ne pas passer au suivant sans l'avoir atteint ou sans avoir documenté pourquoi.
- Consigner les mesures runtime et décisions dans `update/ingame_map_poc_journal.md` (à créer).

## 1. Objectif

Afficher **dans le jeu** une carte logistique MapExtension :

- fond de carte = terrain natif du jeu (`UCrMapMenuTerrainData`), en résolution utile au zoom ;
- projection monde → carte **identique à celle de la carte native**, lue depuis les données du jeu (pas de calibration manuelle) ;
- surcouches MapExtension : expéditeurs/récepteurs cargo, connexions cargo, téléporteurs, joueurs, état du cycle de rupture ;
- fenêtre ImGui ModLoader déplaçable/redimensionnable, pan à la souris, zoom molette sans conflit de scroll.

### Non-objectifs du POC

- Pas de minimap centrée joueur à cap tournant : le mod MiniMap existe déjà, ne pas le dupliquer. La valeur propre de MapExtension est la **logistique** (cargo, téléporteurs, rupture, sync serveur dédié).
- Pas de remplacement du viewer web `mapview` : il reste l'interface complète (annotations, zones, filtres avancés). Le POC est additif.
- Pas de modification du widget UMG natif de la carte (option A de l'étude : rejetée, trop fragile).
- Pas de changement du protocole client/serveur, des endpoints HTTP, ni du JSON.
- Pas de brouillard de guerre ni de filtres natifs dans le POC (piste ultérieure, §10).

## 2. Contexte projet

- `MapExtension_Plugin` : plugin ModLoader StarRupture, **un plugin, deux builds** (`MODLOADER_CLIENT_BUILD` / `MODLOADER_SERVER_BUILD`).
- Aujourd'hui : le client expose un serveur HTTP local (`/health`, `/cargo`, `/rupture-cycle`) consommé par `mapview` (Vue 3) ; en dédié, le serveur envoie des snapshots via `IPluginNetworkChannel`.
- Le modèle central est `CargoSnapshot` (`map_state_types.h`) : marqueurs cargo, connexions, téléporteurs, joueurs, rupture. Le POC **consomme** ce snapshot (local ou cache distant) ; il ne crée pas de nouvelle source de données.
- **Aucune UI ImGui n'existe sur `feature/0.7`** (aucun `RegisterPanel`/`RegisterWidget`) : tout le code UI du POC est nouveau.
- Projection actuelle du viewer web (`map_state_types.h:16-35`) : affine calibrée à la main sur l'export statique 9019×11691 px (`kMapSrcX1/Y1`, `kMapDstX1/Y1`, …). Elle reste la référence de contrôle pour valider la projection native (§9, J1).

## 3. État SDK / ModLoader (vérifié le 2026-11-13)

| Élément | Valeur |
| --- | --- |
| Plugin SDK checkouté | `v1.23.2` = `52b48fcc1632f0b713695919c7ba7978111dc428` |
| ModLoader correspondant | `v1.23.2` (dernier : `v1.23.3`, correctifs UI seulement) |
| Interface plugin | `PLUGIN_INTERFACE_VERSION 75`, MIN 70 → nécessite ModLoader ≥ 1.23.0 |
| SDK de jeu (`StarRupture SDK`) | `659656c` « v0.3.5 SDK (CL-127004) » |
| Builds actuels | client debug + serveur release OK, 0 erreur, aucun warning hors SDK |

Note : le SDK de jeu n'a pas de `Client/build_cl.txt`, donc `PLUGIN_GAME_BUILT_CL = 0` par défaut. Si le POC utilise `PluginMap::Feature`/version map, épingler `PluginGameBuiltCL=127004` (voir `StarRupture-Plugin-SDK/PluginDevelopment.md`, section « Your game build »).

## 4. Héritage de `feature/new-ui` (à lire, ne pas merger)

Branche partie de `1b3c183` (v0.5). Depuis, `feature/0.7` a divergé (99 fichiers modifiés, 36 en commun). Les commits `5799833` et `800fc80` (POI) sont une **ancienne version** de ce qui a été intégré via la PR #5 : obsolètes.

Contenu utile (lire avec `git show feature/new-ui:<chemin>`) :

| Source | Intérêt |
| --- | --- |
| `update/11_ingame_mapview_feasibility.md` | Étude des options A–E. Retenir : D (ImGui + assets natifs) et E (hybride). |
| `update/12_ingame_map_probe.md`, `client/ingame_map_probe.cpp` (commit `c8845ce`) | Résolution de `UCrMapMenuTerrainData` via soft-ref, rechargement après GC, inventaire des segments. Réutilisable **comme inspiration** (code maison). |
| `update/13_ingame_map_grid_poc.md` (commit `3aec538`) | Copie progressive des tuiles, publication game thread → render thread par handles uniquement, libération différée de 2 ticks, plafond mémoire 128 MiB. Bon patron de cycle de vie. |
| `update/14_ingame_map_rendering_findings.md` | Résultats runtime : voir ci-dessous. |

Résultats runtime établis sur cette branche :

1. Le terrain natif se récupère : 36 segments, deux brushes par segment (`TerrainSegmentTexture` = `Terrain_R1_*`, `TerrainSegmentTextureRadiation2` = `Terrain_R2_*`).
2. La grille assemblée n'est cohérente qu'avec **transposition** : colonne canvas ← `TerrainSegmentGridIndex.Y`, ligne canvas ← `TerrainSegmentGridIndex.X` (à reconfirmer avec la projection native, §9 J1).
3. En jeu, la variante affichée était **R2**.
4. La case R2 `(2,5)` référence la même ressource que `(1,5)` (`Terrain_R2_01_05`) — cause non élucidée (UVRegion ? fallback ?).
5. `LoadFromUTexture2D` ne copie que le **mip résident** : 64×64 par tuile pour un brush logique 2048×2048 → pixellisation.
6. `GetMouseWheel()` ne consomme pas la molette : la carte zoome et le panneau parent défile.
7. Le POC dépendait de `hooks->Engine->RegisterOnTick` côté client, déclaré peu fiable par `AGENTS.md`.

## 5. Faits du jeu (SDK CL-127004, `StarRupture-Plugin-SDK/StarRupture SDK/Client/SDK/`)

Types inchangés entre l'ancien SDK de jeu (`f3eb06b`) et CL-127004 : `UCrMapMenuTerrainData`, `FCrTerrainSegmentData`, `UCrUW_MapMenuTerrainSegment`.

### 5.1 Données terrain et projection native

`UCrMapMenuTerrainData : UDataAsset` (`ChimeraUI_classes.hpp`, autour de la ligne 1350) :

| Champ | Offset | Rôle |
| --- | --- | --- |
| `FVector3f MapTerrainTopLeftPivotPoint` | 0x0030 | coin haut-gauche de la grille en coordonnées monde |
| `FVector3f MapTerrainSegmentSize` | 0x003C | taille monde d'un segment |
| `int32 MapFarToEdgeMipMapChange`, `MapNearToEdgeMipMapChange` | — | politique de mip native près des bords |
| `TArray<FCrTerrainMipMapByZoomValueData> TerrainMipMapByZoomValueData` | 0x0060 | table zoom → mip (`float MaximalZoomValue`, `int32 MipMap`) |
| `TArray<FCrTerrainSegmentData> TerrainSegmentsData` | 0x0070 | segments |

`FCrTerrainSegmentData` (`ChimeraUI_structs.hpp:720-726`) : `FIntPoint TerrainSegmentGridIndex` (0x0000), `FSlateBrush TerrainSegmentTexture`, `FSlateBrush TerrainSegmentTextureRadiation2` (0x00C0, taille 0xB0).

**Conséquence clé** : la projection monde → carte se déduit directement :

```
gx = (worldX - TopLeftPivot.X) / SegmentSize.X     // ou Y selon l'orientation à confirmer
gy = (worldY - TopLeftPivot.Y) / SegmentSize.Y
segment = (floor(gx), floor(gy)) ; uv local = fraction(gx, gy)
```

L'orientation exacte (quel axe monde ↔ quel index de grille, signe des tailles, transposition observée en §4) est **à vérifier** en J1 en loggant les valeurs et en les confrontant à `kMap*` de `map_state_types.h`.

### 5.2 Accès au data asset

- `UCrMapMenuDevSettings : UDeveloperSettings` (`ChimeraUI_classes.hpp`, champ ligne 1134) : `TSoftObjectPtr<UCrMapMenuTerrainData> TerrainData` (0x03C8). À lire sur le CDO (`GetDefaultObj()`), puis résoudre/charger la soft-ref (patron déjà prouvé par le probe de `feature/new-ui`).
- Le même CDO expose `MapAreaPivotPoint`, `MinZoom/DefaultZoom/MaxZoom`, `FogOfWarSegmentsGridSize`, `FogOfWarRenderTargetSize`, `MapMenuFiltersData` (pistes ultérieures).

### 5.3 Choix R1 / R2

- `ACrMapMenuDataReplicationHelper` (`Chimera_classes.hpp:9347`) : `int32 CurrentRadiationLevelReplicated` (0x0468), plus l'événement `OnCurrentRadiationLevel(int32 Current, int32 New, bool Immediately)`.
- Hypothèse forte : le niveau de radiation courant sélectionne la variante (R2 = `TerrainSegmentTextureRadiation2`). **Table de correspondance niveau → brush à vérifier** en jeu (logguer le niveau quand la carte native montre R1 et quand elle montre R2).
- Le même acteur porte les conteneurs de marqueurs natifs (`BuildingsMarkerDataContainer`, `PlayersMarkerDataContainer`, `PointOfInterestReplicationData`…) : utiles plus tard pour comparer nos positions à celles du jeu.

### 5.4 Widget natif (référence, ne pas modifier)

`UCrUW_MapMenu` → `UCrUW_MapMenuMapArea` (`ChimeraUI_classes.hpp:9507`) → `UCrUW_MapMenuTerrain` (`TArray<UCrUW_MapMenuTerrainSegment*> TerrainSegments`). Utile uniquement pour le diagnostic R2 `(2,5)` (comparer le brush vivant `ImageTerrain` au data asset).

### 5.5 Textures et streaming

- Fonctions UFunction accessibles par le SDK généré (ProcessEvent, pas de pattern) : `SetForceMipLevelsToBeResident(float Seconds, int32 CinematicLODGroupMask)` (`Engine_classes.hpp:2663`) et la variante `UStreamableRenderAsset` à 5 paramètres (`Engine_classes.hpp:3082`).
- Fonctions **natives** (non exposées par le SDK généré) utiles pour lire les pixels pleine résolution côté CPU, visibles dans le dump IDA local `StarRupture-Game-IDADump/` (⚠️ dump du 2026-07-19, **build plus ancien** : adresses non valables pour CL-127004, mais noms/assembleur utiles pour écrire des patterns) :
  - `UTexture2D/` : `GetRunningPlatformData`, `GetMipData`, `GetNumMips`, `GetNumResidentMips`, `GetNumMipsAllowed`, `GetPixelFormat`, `StreamIn`, `GetCPUCopy`, `GetInitialMipData` ;
  - `UStreamableRenderAsset/` : `WaitForStreaming`, `WaitForPendingInitOrStreaming`, `IsFullyStreamedIn`, `HasPendingInitOrStreaming`, `SetForceMipLevelsToBeResident` ;
  - `FBulkData/` : `GetBulkDataSize`, `GetCopy`, `LockReadOnly`, `Unlock`, `CanLoadFromDisk`, `MakeSureBulkDataIsLoaded`.
- Les tuiles terrain sont stockées en **BC1 (DXT1)** (fait établi par MiniMap) : un décodeur BC1 maison est nécessaire pour le chemin CPU (format public, ~80 lignes).

## 6. Le mod MiniMap (LetMeInside / Kian369) — ce qu'on en retient

Dépôt : https://github.com/LetMeInside/StarRupture-Plugin-MiniMap (v1.2.0, 2026-10-29).

### ⚠️ Licence

« MiniMap Source Available License » : lecture/étude privée seulement. **Interdit** de copier, adapter, porter ou réimplémenter des portions substantielles dérivées de son expression, ou de s'en servir comme base d'un dérivé. Règle pour l'agent : **clean-room**. Ne pas ouvrir son code. N'utiliser que les faits sur le jeu ci-dessous (qui relèvent du jeu, pas de son code) et écrire une implémentation originale. Si une collaboration est souhaitée (helper partagé dans le SDK, autorisation), c'est à l'utilisateur de contacter l'auteur, qui co-maintient désormais le ModLoader.

### Faits sur le jeu confirmés par l'existence de MiniMap

1. La projection native se lit dans `UCrMapMenuTerrainData` (`MapTerrainTopLeftPivotPoint`, `MapTerrainSegmentSize`, `TerrainSegmentGridIndex`) → pas de calibration par landmarks.
2. La variante de terrain dépend de `ACrMapMenuDataReplicationHelper::CurrentRadiationLevelReplicated`.
3. La pleine résolution est atteignable en lisant le **mip 0 depuis les bulk data CPU** de la texture (BC1), découpé et décodé **par morceaux à la demande** puis chargé via `LoadFromRGBA` ; un cache disque évite de refaire le travail. Le forçage des mips résidents (`SetForceMipLevelsToBeResident` / `StreamIn` / `WaitForStreaming`) est un chemin alternatif.
4. La molette se gère via `IPluginInputEvents::RegisterMouseWheel` (v70) avec un modificateur, en ne consommant que quand `event->uiCapturing == false`.
5. Le ModLoader v1.23.0 annonce que « la MiniMap trouve ses fonctions de jeu sur chaque build connu » : les fonctions natives ci-dessus sont **probablement** présentes dans la **version map** du loader. **À vérifier** (§9 J2) : si oui, les demander par clé (`PluginMap::Function(scanner, self, "UTexture2D::GetMipData", fallbackPattern)`) plutôt que maintenir nos propres patterns. Liste et format des clés : https://github.com/AlienXAXS/StarRupture-ModLoader/blob/main/VersionMap.md et lignes `[Map]` du `ModLoader.log`.

## 7. API ModLoader à utiliser (`StarRupture-Plugin-SDK/include/`)

| Besoin | API | Référence |
| --- | --- | --- |
| Fenêtre carte | `IPluginUIEvents::RegisterWidget` / `RegisterPanel`, `PluginWindowHints`, flags `PluginWindowFlags_*` | `plugin_interface.h:2397-2427, 2492` |
| Taille fenêtre | `IModLoaderImGui::SetNextWindowSize`, `SetNextWindowSizeConstraints` (v74) | `plugin_interface.h:2264-2276` |
| Boutons barre de titre, repli | `PluginPanelDesc::titleBarFn`, `IsPanelCollapsed`/`SetPanelCollapsed` (v75) | changelog v75, `PluginDevelopment.md` |
| Fenêtre au-dessus des menus du jeu | `PluginWindowFlags_MenuMouseWhereUncovered` (v75), `PluginWindowFlags_PauseMenuMouseInputs` (v71) | `plugin_interface.h:2415, 2427` |
| Savoir si la carte native est ouverte | `IPluginGameMenu::GetUIState` / `RegisterOnUIStateChanged`, bit `PLUGIN_UI_MAP` (v74) | `plugin_interface.h:3394-3440, 3489` |
| Molette | `IPluginInputEvents::RegisterMouseWheel` (v70), `PluginMouseWheelEvent` | `plugin_interface.h:1767-1835` |
| Touche d'ouverture | `RegisterKeybindByName` + `ConfigValueType::Keybind` | `plugin_interface.h` |
| Textures | `IPluginImGuiTextures::LoadFromRGBA`, `LoadFromUTexture2D` (game thread uniquement, jamais dans le render callback), `FreeTexture` (sûr partout depuis v70), `GetCapacity`, `GetFreeSlotCount` | `plugin_interface.h:2300-2395` |
| Thread | `IPluginEngineEvents::IsGameThread` (v72) | `plugin_interface.h:1612` |
| Fonctions natives résistantes aux MAJ | `plugin_map.h` : `PluginMap::Function`, `Feature`, `DeclareFeatures` ; `PLUGIN_SCAN_FLAG_MAP_KEY` | `include/plugin_map.h`, `PluginDevelopment.md` §« Map keys », §« Features » |
| Champs résistants aux MAJ (optionnel POC) | `ResolveField`, `SR_FIELD`, `PLUGIN_FIELD_ANY_SIZE`, `PLUGIN_FIELD_LATE` | `PluginDevelopment.md` §« Game-class fields » |

Contraintes connues :

- `LoadFromUTexture2D` ne choisit pas le mip → inutilisable seul pour la haute résolution.
- `RegisterMouseWheel` : quand une fenêtre ModLoader a la capture (`uiCapturing`), la valeur de retour est ignorée et ImGui défile quand même → la fenêtre carte doit avoir un contenu qui **ne déborde pas** (canvas = `GetContentRegionAvail()`, `PluginWindowFlags_NoScrollbar`) pour qu'il n'y ait rien à défiler.
- Les menus/bits UI peuvent rester posés pendant l'animation de fermeture (voir commentaire `PLUGIN_UI_ANY_MENU`).
- `ItemIcons`/`item_icons.h` : textures ImGui seulement (pas exportables vers le navigateur), hors périmètre.

## 8. Architecture cible du POC (client only)

Nouveaux fichiers sous `client/` (exclus des configurations serveur dans `MapExtension_Plugin.vcxproj`, comme `client/*.cpp` aujourd'hui) :

| Module | Rôle | Thread |
| --- | --- | --- |
| `client/ingame_map_terrain_source.{h,cpp}` | Résout CDO → soft-ref → `UCrMapMenuTerrainData` ; produit un descripteur **POD sans UObject** : pivot, taille segment, liste (gridX, gridY, nom ressource R1/R2), table mip/zoom ; niveau de radiation courant. Re-résolu à chaque besoin, jamais de pointeur UObject gardé entre ticks. | game thread |
| `client/ingame_map_projection.h` | Transformation monde ↔ carte native (pure, header-only, testable). Inclut la transposition/inversions confirmées en J1. | partout |
| `client/ingame_map_tiles.{h,cpp}` | Cache de tuiles : niveau « aperçu » (1 texture par segment, basse résolution) + morceaux haute résolution à la demande autour de la vue ; budget slots/mémoire ; libération différée. | file de demandes (render) → service (game thread) |
| `client/ingame_map_texture_reader.{h,cpp}` | Lecture pixels pleine résolution (chemin CPU bulk data + décodage BC1, ou chemin forçage de mips). SEH confiné dans de petits helpers POD. | game thread |
| `client/ingame_map_ui.{h,cpp}` | Fenêtre, pan/zoom, dessin terrain + surcouches depuis le dernier `CargoSnapshot`, légende minimale, timeline rupture compacte. | render callback (lecture seule de données publiées) |

Publication des données : le game thread publie un état immuable (handles de texture, descripteurs, copie du snapshot) sous mutex court ou double-buffer ; le render callback ne touche **aucun** UObject.

Déclenchement des services game thread : ne pas reposer uniquement sur `EngineTick` (cf. `AGENTS.md`). Vérifier d'abord dans `ModLoader.log` que `RegisterOnTick` fonctionne sur le client avec le loader 1.23.x ; sinon/en plus, utiliser les callbacks monde (`RegisterOnAnyWorldBeginPlay`, `RegisterOnBeforeWorldEndPlay`) et l'ouverture de la fenêtre comme points de service.

Configuration (`plugin_config.h`) : section `InGameMap`, `Enabled` (Boolean, défaut `0`, expérimental), `ToggleKey` (Keybind), `TileBudgetMiB` (Integer, défaut 64). Mettre à jour README/README.fr/DEVELOPERS/descriptionbbcode en conséquence.

## 9. Jalons

### J0 — Préparation

- Branche `feature/ingame-map-poc` depuis `feature/0.7`.
- Vérifier SDK `v1.23.2` + sous-module `659656c` (`git -C StarRupture-Plugin-SDK describe --tags`, `git -C StarRupture-Plugin-SDK submodule status`).
- Squelette des fichiers §8, entrée config désactivée par défaut, enregistrement/désenregistrement symétrique dans `map_state_runtime.cpp` (client seulement).
- **Sortie** : builds client et serveur verts ; plugin inchangé fonctionnellement quand `InGameMap.Enabled=0`.

### J1 — Données terrain + projection native (sans texture)

- Résoudre `UCrMapMenuTerrainData` ; logguer une fois : pivot, taille segment, nombre et étendue de la grille, table mip/zoom, noms R1/R2 par case, niveau de radiation.
- Dessiner la grille vide (cadres + index) et les surcouches MapExtension (cargo, téléporteurs, joueurs) avec la projection native.
- Confronter à la projection web : pour plusieurs positions de joueurs/téléporteurs, comparer la case/UV native avec `WorldToMap` (`map_state_types.h`) ramené à la grille de l'export 9019×11691. Les deux doivent concorder à une similitude près (translation/échelle/transposition). Documenter la transformation trouvée.
- Établir la règle radiation → R1/R2 (observer la carte native).
- **Sortie** : erreur de placement mesurée (en mètres) sur ≥ 5 objets connus, orientation et règle R1/R2 écrites dans le journal ; aucune constante « magique » hors données du jeu, sauf l'orientation prouvée.

### J2 — Terrain basse résolution puis haute résolution

1. Aperçu : `LoadFromUTexture2D` par segment (même si 64×64) pour valider l'assemblage avec la projection J1. Avec le loader 1.23.2 la copie est ~0,7 ms : charger les 36 sans espacement artificiel, mesurer.
2. Haute résolution, deux pistes à évaluer dans cet ordre :
   - **A. Forçage des mips** via `SetForceMipLevelsToBeResident` (UFunction, sans pattern), attente de `IsFullyStreamedIn`/`GetNumResidentMips` sur plusieurs ticks, puis copie. Mesurer stutter, mémoire, résolution obtenue.
   - **B. Lecture CPU** : obtenir le mip 0 via les fonctions natives §5.5 (d'abord par clé de version map, sinon pattern maison issu du dump IDA, déclaré dans une `PluginMap::Feature` « terrain-hires » pour se couper proprement si non résolu), décoder BC1 par morceaux (ex. 256×256), `LoadFromRGBA`.
3. Stratégie de cache : aperçu permanent + morceaux haute résolution autour de la vue, éviction LRU, respect de `GetCapacity()` / `TileBudgetMiB` (repères : 36 tuiles × 512² RGBA ≈ 36 MiB ; × 2048² ≈ 576 MiB, exclu).
4. Cache disque optionnel (sous le dossier du plugin) seulement si B est retenu et que le décodage est coûteux ; clé = CL du jeu + nom de ressource + mip.
5. Élucider R2 `(2,5)` : comparer `UVRegion`, `Mirroring`, `ResourceObject` de `(1,5)` et `(2,5)`, puis le brush vivant du widget natif. Fallback accepté seulement après preuve : afficher la tuile R1 correspondante.
- **Sortie** : carte continue nette jusqu'au zoom max retenu, sans freeze perceptible (mesures dans le journal), mémoire sous budget, piste A ou B choisie et justifiée.

### J3 — Fenêtre et interactions

- Widget dédié (`RegisterWidget`), contenu non débordant, `PluginWindowFlags_NoScrollbar` ; `SetNextWindowSizeConstraints`.
- Pan au glisser ; zoom molette : ImGui `GetMouseWheel()` quand la fenêtre a la capture (plus de conflit puisque rien ne défile), `RegisterMouseWheel` + modificateur quand elle est en overlay sans capture.
- Touche d'ouverture configurable ; bouton barre de titre « centrer sur moi » (`titleBarFn`).
- Option : ouverture automatique d'un panneau compact quand `PLUGIN_UI_MAP` passe à 1, avec `PluginWindowFlags_MenuMouseWhereUncovered` pour cohabiter avec la carte native.
- **Sortie** : aucune interaction parasite (scroll, clics passant au jeu) en jeu normal, menu pause et carte native ouverte.

### J4 — Surcouches logistiques

- Réutiliser le `CargoSnapshot` existant (local en solo, cache distant en dédié via `client/map_state_remote_cache.cpp`) ; ne pas créer de second transport.
- Dessin : marqueurs cargo (expéditeur/récepteur), lignes de connexion avec item/quantité au survol, téléporteurs, joueurs (soi distingué), widget compact de rupture (`wave/stage/step`, `elapsed_seconds` + `observed_at_unix_ms` pour l'avance fluide).
- Filtres minimaux (cases à cocher par catégorie).
- **Sortie** : parité visuelle de ces 5 couches avec `mapview` sur une même partie (captures dans le journal), en solo **et** en dédié.

### J5 — Robustesse et cycle de vie

- Changement de monde, retour menu, rechargement du plugin (bouton RELOAD / `RemoteReload`), shutdown : toutes les textures libérées, aucun UObject retenu.
- Déclarer `PluginReload` adapté dans `PluginInfo::mapFlags` si le rechargement à chaud en monde pose problème.
- Valider que le crash-guard du loader ne se déclenche jamais pendant 30 min de jeu avec la carte ouverte/fermée en boucle.
- **Sortie** : protocole runtime §11 passé sans fuite de slots (`GetFreeSlotCount` revient à sa valeur initiale).

## 10. Inconnues prioritaires (ordre de levée)

1. Orientation et signes de la projection native (J1).
2. Règle radiation → R1/R2 (J1).
3. Disponibilité des fonctions natives texture dans la version map du loader (J2) — consulter `VersionMap.md` et les lignes `[Map]` du log.
4. Résolution obtenue par forçage de mips vs lecture CPU, et coût (J2).
5. Cause de R2 `(2,5)` (J2).
6. Fiabilité de `RegisterOnTick` côté client avec le loader 1.23.x (J0/J2).

Pistes **hors POC**, à noter seulement : brouillard de guerre natif (`UCrMapManuSubsystem::FOWTextures`, `FogOfWarSegmentsGridSize`), respect des filtres natifs de la carte, comparaison avec les marqueurs natifs (`ACrMapMenuDataReplicationHelper`), migration `PLUGIN_MAP_MIGRATED` de tout le plugin.

## 11. Validation

Builds (depuis `MapExtension_Plugin/`) :

```bash
./build.sh client debug --summary
```

```bash
./build.sh server release --summary
```

Puis `./summarize_build.sh client` / `./summarize_build.sh server`. Les warnings sous `StarRupture SDK/` (C4309/C4369) sont du bruit connu ; seuls les warnings/erreurs dans le code MapExtension comptent. Le build serveur doit rester inchangé (code POC exclu).

Protocole runtime (à exécuter par l'utilisateur, l'agent prépare la checklist et lit les logs fournis) :

1. Solo : ouvrir la carte POC avant puis après ouverture de la carte native ; vérifier netteté, alignement des marqueurs, absence de freeze.
2. Changer de radiation (ou de monde) : la variante suit la carte native.
3. Dédié : serveur avec build serveur MapExtension, client avec build client ; surcouches alimentées par le cache distant.
4. Retour menu / relance de partie / RELOAD : pas de fuite (`GetFreeSlotCount`), pas de crash.
5. Fournir `ModLoader.log` (lignes `[Map]`, logs MapExtension) au journal.

## 12. Pièges du projet (rappel)

- Ne pas mélanger `try/catch` et `__try/__except` dans une même fonction ; SEH uniquement dans de petits helpers POD (`map_state_capture.cpp` en donne l'exemple).
- `LoadFromUTexture2D` : game thread seulement, jamais depuis le render callback (crash avec DLSS).
- Aucun pointeur UObject conservé entre ticks ; re-résoudre par nom/soft-ref.
- HTTP/JSON restent client-only ; `shared/map_sync_protocol.h` reste POD et inchangé.
- Ne pas modifier `StarRupture-Plugin-SDK/StarRupture SDK/**` ; ne jamais stager le SDK.
- Garder `README.md` et `README.fr.md` alignés ; documenter toute nouvelle option de config.

## 13. Livrables du POC

- Code sous `client/ingame_map_*` + enregistrement runtime + config, builds verts.
- `update/ingame_map_poc_journal.md` : mesures, captures, décisions (orientation, R1/R2, piste haute résolution, budget).
- Section courte dans `DEVELOPERS.md` décrivant l'architecture et l'activation expérimentale.
- Recommandation finale : continuer (et vers quoi), ou arrêter, avec les preuves.

## 14. Références

- `AGENTS.md`, `../AGENTS.md`, `game-context.md`.
- `StarRupture-Plugin-SDK/PluginDevelopment.md` (changelog interface v70–v75, Map keys, Features, Game-class fields, hooks UI/Input/ImGuiTextures).
- `StarRupture-Plugin-SDK/include/plugin_interface.h`, `plugin_map.h`.
- Notes de version ModLoader v1.21.3 → v1.23.3 : `gh release view <tag> -R AlienXAXS/StarRupture-ModLoader`.
- Branche `feature/new-ui` : `update/11` à `update/14`, `client/ingame_map_probe.cpp`.
- Dump IDA local : `StarRupture-Game-IDADump/` (build antérieur, noms et assembleur seulement).
- MiniMap (référence produit, **ne pas lire le code**) : https://github.com/LetMeInside/StarRupture-Plugin-MiniMap.
