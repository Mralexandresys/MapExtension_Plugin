# Plan : MapExtension sur le système multi-version (PLUGIN_MAP_MIGRATED)

Objectif : un seul DLL qui marche sur n'importe quel build du jeu, y compris un build sorti après la
compilation, sans rebuild. Le mod demande chaque donnée du jeu au loader par son nom (table des
versions, ou réflexion du jeu qui tourne) au lieu d'utiliser les offsets compilés.

Méthode de validation demandée : pendant la migration, garder l'ancien chemin (offsets compilés)
à côté du nouveau, journaliser les deux, et comparer les logs avant de retirer l'ancien.

Base : ModLoader `main` 4427455, Plugin SDK `main` 75ac2f5, SDK du jeu CL-130045 (8e1b5a7a).

## État mesuré (21/11)

- Vérificateur du SDK (`rule3-check`) : **199 lectures directes** de champs du jeu, 119 champs, 48 classes.

  | Fichier | Lectures |
  |---|---|
  | `client/ingame_map_live_state.cpp` | 78 |
  | `map_state_capture.cpp` | 64 |
  | `client/ingame_map_terrain_source.cpp` | 43 |
  | `server/map_sync_server.cpp` (hôte coop, compilé dans le client) | 8 |
  | `client/ingame_map_service.cpp` / `render.cpp` / `map_sync_client.cpp` | 4 / 1 / 1 |

- **1 495 expressions non typées** par le vérificateur : il peut rester des lectures qu'il ne voit pas.
- **~17 parcours de tableaux de structures du jeu** avec la taille compilée (`arr[i]` sur
  `TerrainSegmentsData`, `AllSenderReceiversData`, `ConnectionsData`, `TeleportersData`,
  `AntennasData`, marqueurs de bâtiments et d'attaque, obélisques répliqués, POI, bases abandonnées,
  base cores, etc.), dont 1 seul vu par le vérificateur.
- **4 `TMap` du jeu** parcourues : `CustomNames` (x3) et `Interactivity->Data`.
- **10 appels de fonctions du jeu** (paramètres compilés) : `GetCurrentStage`,
  `GetCurrentStageProgress`, `GetTimeSinceLastWaveStarted`, `GetCurrentType`,
  `GetBuildingCustomName`, `ServerGetBuildingCustomName`, `GetInteractionDisplayName`,
  `IsMineableChunk`, `BP_IsGatherableDepleted`, plus `GetDefaultObj` (helper Dumper-7, sans risque).
- **5 `SDK::UWorld::GetWorld()`**, 12 usages de `GObjects`, 3 `FindObjectFast`.
- **Déjà migré** : monolithes et base cores (requête Mass, uniquement des clés de la table, testé en
  jeu le 21/11). Seul manque : la taille de `FMassEntityQuery` sur CL-128786.

## Phase 0 : préparation

1. Commiter l'état actuel de `feat/0.10` (travail de l'autre session + ton 0.10), puis créer
   `feat/multi-version` dessus.
2. Une seule session modifie les sources pendant toute la migration.
3. Brancher le vérificateur dans le build en mode `report` (`tools/Rule3Check.targets` du SDK) :
   les 199 lectures deviennent des warnings, et on voit le compteur baisser à chaque build.

## Phase 1 : le socle

- `PluginSdkRuntime::Init(self->hooks)` comme toute première instruction de `PluginInit` : `GObjects`,
  les noms et `FindObjectFast` utilisent alors les globales du build qui tourne.
- Les 5 `SDK::UWorld::GetWorld()` deviennent `PluginSdkRuntime::GetWorld()`.
- `OnPluginLoadHooks` (existe déjà, client) appelle une seule fonction qui demande tous les champs.

## Phase 2 : un point d'accès unique aux champs du jeu

Nouveau fichier `src/game_fields.h/.cpp` :

- Tous les `SR_FIELD`, `SR_SIZE`, `SR_PARAM`, `SR_PARAMS_SIZE`, rangés par fonctionnalité.
- Une fonctionnalité déclarée (`DeclareFeature`) par groupe : cargo, téléporteurs, joueurs, terrain,
  cycle de rupture, antennes, bâtiments/base cores, bases abandonnées, ressources récoltables,
  monolithes (déjà fait). Un champ introuvable coupe son groupe, jamais tout le plugin.
- Une fonction `ResolveGameFields(scanner, self)` appelée depuis `OnPluginLoadHooks`.

## Phase 3 : double lecture et journalisation (audit)

Un interrupteur `[Debug] FieldAudit` dans la config (désactivé par défaut) et une macro unique pour
lire un champ :

```cpp
// Valeur par le nouveau système ; en audit, comparée avec la lecture compilée obj->Member.
MX_FIELD(obj, FCrAntennaData, State)
```

Ce que l'audit écrit dans `ModLoader.log` :

1. **Au chargement, une ligne par champ :** clé, offset compilé (`offsetof`), offset résolu, taille
   compilée, taille résolue, source (table ou réflexion). Préfixe `[FieldAudit] field`.
2. **Une ligne par taille de tableau :** `sizeof` compilé contre taille résolue (`SR_SIZE`).
3. **Une ligne par fonction du jeu :** offsets des paramètres compilés contre `SR_PARAM`.
4. **Pendant la partie :** à chaque lecture, comparaison octet par octet des deux valeurs. La
   première différence par clé est journalisée avec les deux valeurs, puis seulement compter.
5. **Toutes les 60 s :** résumé `clé lectures=N différences=M`.

Garde-fou : la lecture compilée n'est faite que si `offset compilé + taille` tient dans la taille
réelle de l'objet sur le build qui tourne. Sinon, elle est notée `hors limites`. Elle se fait aussi
sous `__try` dans un petit helper POD, pour qu'un build différent ne puisse pas faire planter l'audit.

Outil : `tools/compare_field_audit.py` lit le log et sort un tableau clé / offsets / différences.

## Phase 4 : migration fichier par fichier

L'ordre va du plus isolé au plus central. Un build et une comparaison d'audit sont faits à chaque étape.

1. `client/ingame_map_terrain_source.cpp` (43 lectures, 3 tableaux) : tuiles du terrain.
2. `client/ingame_map_live_state.cpp` (78) : antennes, marqueurs, obélisques répliqués, POI,
   bases abandonnées, base cores, noms personnalisés (`TMap`).
3. `map_state_capture.cpp` (64) : cargo, connexions, téléporteurs, joueurs, cycle de rupture
   (`UCrEnviroWaveSubsystem`, `Interactivity->Data`), ressources, noms d'objets.
4. `server/map_sync_server.cpp` (8), `ingame_map_service.cpp` (4), `render.cpp` (1),
   `map_sync_client.cpp` (1) : trier d'abord, certains signalements portent sur nos propres types.

Règles pour chaque site, d'après le guide du SDK :

- `obj->Membre` devient `MX_FIELD(obj, Classe, Membre)`, ou `.In(obj)` avec un test de null.
- `arr[i]` sur une structure du jeu devient `SR_SIZE` + `PluginMap::ElementAt`.
- Une `TMap` ou un `TSet` d'une structure du jeu devient `PluginMap::ScriptMapView` / `ScriptSetView`.
- Pas de copie de structure du jeu : lire en place, ou copier dans nos propres structures.
- Les structures imbriquées (`Container.Array`) se lisent en chaîne avec `.In()`.

## Phase 5 : fonctions du jeu

- Les 10 appels passent par `SR_PARAMS_SIZE` + `SR_PARAM` + `PluginMap::ParamsBlock` +
  `PluginMap::Call`. Les fonctions du moteur (`UGameplayStatics`,
  `USubsystemBlueprintLibrary`) restent compilées : le SDK ne les considère pas comme du jeu.

## Phase 6 : enums

- Le mod convertit des enums du jeu en entiers et les interprète : état des monolithes, état des
  antennes, étapes du cycle, type de ressource, etc.
- Au chargement, lire leurs valeurs par nom avec `PluginMap::EnumValue`, construire une table de
  correspondance, et la journaliser dans l'audit. Une valeur d'enum qui change de numéro ne casse
  alors rien en silence.

## Phase 7 : activation

- `PLUGIN_GAME_BUILT_CL, PLUGIN_MAP_MIGRATED` dans `PluginInfo` (`plugin.cpp`).
- Vérificateur en mode `enforce` : une lecture directe ajoutée plus tard fait échouer le build.
- Les faux positifs (nos propres types) vont dans l'allow-list, avec une raison pour chacun.

## Phase 8 : tests

| Test | Montage | Attendu |
|---|---|---|
| T1 même build | SDK CL-130045, jeu CL-130045, audit actif | 0 différence, toutes les fonctionnalités actives |
| **T2 build croisé** | **SDK CL-127004 (commit 659656c0 du SDK du jeu), jeu CL-130045** | Plugin accepté, carte complète ; l'audit montre exactement les offsets qui ont bougé entre les deux builds, et seul le nouveau système est juste |
| T3 fonctionnel | En jeu : cargo, téléporteurs, joueurs, base cores, monolithes, antennes, ressources, cycle de rupture, terrain, partage coop | Tout s'affiche comme avant |
| T4 vraie mise à jour | Prochaine mise à jour du jeu, sans rebuild | Le mod tourne ; l'audit dit ce qui a bougé |

T2 simule une mise à jour du jeu dès aujourd'hui. Le rapport de compatibilité du loader
(`compat-report-130045.json`) signale déjà une différence entre ces deux builds :
`UCrEnviroWaveTimerSubsystem::TimerActor` est passé de `0x90` à `0x98`.

## Phase 9 : nettoyage et documentation

- Une fois T1 et T2 validés : laisser l'audit derrière la config (désactivé par défaut), ou le retirer.
- `README.md` / `README.fr.md` / `descriptionbbcode` : le mod suit les mises à jour du jeu sans
  nouvelle version. Une version minimale du ModLoader est-elle à annoncer ? À décider (voir plus bas).
- `AGENTS.md` / `DEVELOPERS.md` : la règle « jamais `obj->Membre` », le rôle de `game_fields`,
  l'audit, le vérificateur en mode `enforce`.
- `release.yml` : la release n'a plus besoin du SDK du dernier build du jeu ; le CL déclaré vient
  de `build_cl.txt`.
- Journal : ajouter les résultats de T1 à T4.

## Inconnues et risques

- **Lectures invisibles** : 1 495 expressions non typées. L'audit ne voit que le code exécuté
  pendant la partie de test. Il faut donc aussi relire chaque fichier à la main.
- **Champs non décrits par la réflexion** : en dehors de MassEntity, aucun trouvé pour l'instant. S'il
  en apparaît, il faut demander une ligne aux mainteneurs par une issue sur le Plugin SDK.
- **Bitfields** (booléens empaquetés) : à repérer, ils passent par `SR_BITFIELD`.
- **Fonctions du moteur** : leurs paramètres restent compilés. Ce n'est un risque que si le jeu
  change de version d'Unreal.
- **CL-128786** : la taille de `FMassEntityQuery` manque, donc les monolithes y sont coupés.
- **Version minimale du ModLoader** : le mode migré demande une interface v72 ou plus, et la
  marche sur un build inconnu demande la 1.26.0. `AGENTS.md` interdit d'annoncer un minimum
  sans demande explicite : c'est à toi de décider.
- **Release** : la CI utilise le SDK v1.26.0 (CL-127004). Après la migration, ce n'est plus
  bloquant, puisque le DLL marche sur tous les builds. Avant, il faut attendre une release du SDK.
- **Taille du chantier** : environ 200 sites, 17 tableaux, 4 `TMap`, 10 fonctions et les enums.
  Le travail est surtout mécanique, mais chaque étape demande un test en jeu.
