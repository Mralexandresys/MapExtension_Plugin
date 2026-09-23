# Suivi d’Ignitium et de Star Tears — analyse d’implémentation

Analyse du 21 septembre 2026, conservée comme référence des constats initiaux.

Implémenté depuis cette analyse : priorité de la récompense Star Tears, lecture de l'épuisement sans la politique permanente, disponibilité séparée, règles de phase chargées, consultation positive du sous-système, reset solo Heat/Moving, retrait des acteurs déjà observés de l'ancienne génération, lecture différée après BeginPlay sur les deux builds, rapprochement spatial non ambigu des registres répliqués, journaux de diagnostic, protocole 6 et viewer contrat 3. La projection reste recalculée et indiquée comme estimation. Le rapprochement utilise la position ou le centre des bounds dans une tolérance de 150 cm ; aucun index Mass typé ni masque opaque n'est interprété. Les lectures fraîches acteur/sous-système sont prioritaires sur ces positions sans type de ressource.

Restent à valider en jeu : chemins de récolte réellement empruntés, durée et relation exacte de transformation, réutilisation éventuelle des acteurs, ordre de renouvellement Mass. Les hooks natifs, la lecture native de la graine solo et l'attribution à un joueur ne sont pas implémentés. Les observations par captures peuvent manquer un acteur apparu puis retiré entre deux captures. Les sections « écart actuel » ci-dessous décrivent l'état avant ces corrections.

Le suivi recommandé combine les observations d’acteurs, les enregistrements d’épuisement du jeu et une génération de Rupture. La conversion d’un site Ignitium non récolté en Star Tears reste une règle de présentation prévue par le projet. Elle doit être recalculée à partir de ces observations, sans devenir elle-même une nouvelle observation.

## Sources et portée

- Fiche fournie par l’utilisateur : contexte fonctionnel de la succession Ignitium / Star Tears. Ses références `chatgpt-content-reference` ne permettent pas de retrouver les sources originales ; elles ne prouvent pas les détails d’implémentation du jeu.
- SDK du plugin local : `be365e3e8dd1c10460c0cd42883b5b736db8d618`.
- SDK généré local : `abd0ebd6ff10c99668127af250caa6d2b01d329b`. Les déclarations pertinentes ont été vérifiées dans les cibles Client et Server.
- `StarRupture-Game-IDADump/` : code natif désassemblé et pseudocode. La correspondance exacte avec l’exécutable actuellement installé n’a pas été établie. Les adresses présentes dans les noms de fichiers sont des références d’analyse, pas des adresses à intégrer au plugin.
- [Règles du projet](game-context.md), [capture actuelle](map_state_capture.cpp) et [documentation développeur](DEVELOPERS.md).

Les noms de champs et signatures du pseudocode IDA sont parfois mal reconstruits. Les exemples rencontrés incluent des noms de méthodes de vtable incohérents et les valeurs d’enum `X` / `Y`. Les conclusions ci-dessous utilisent aussi les déclarations du SDK et, pour Heat / Moving, les comparaisons numériques de l’assembleur.

## 1. Lire la présence et la disponibilité des acteurs

| Ressource | Identification | État à lire | Amélioration possible |
|---|---|---|---|
| Ignitium | `ACrOreActor::Resource == I_FireWaveOre_C` | `OreData.bIsDepleted`, `CurrentResourceCount`, `MaxResourceCount` | Pour les `ACrStandaloneMeteOreChunk`, lire aussi `IsMineableChunk()` : un acteur présent et non épuisé peut être non minable. |
| Star Tears | `ACrGatherableBaseActor::InteractionRewardResource == I_StarTears_C` | `bIsDepleted` | Compléter avec les conditions d’interaction liées au cycle. |
| Les deux | Instance vivante du `UWorld` actif | `UCrGatherableSpawnersSubsystem::BP_IsGatherableDepleted(actor)` | Consulter la mémoire d’épuisement du jeu pour un acteur chargé, en complément de son état. |

L’Ignitium `BP_FireWaveMeteOreChunk_C` hérite de `BP_StandaloneMeteOreChunkBase_C`, puis de `ACrStandaloneMeteOreChunk`, puis de `ACrOreActor`. Il ne faut donc pas limiter les hooks de minage à `ACrMeteOreActor`, qui est une autre branche d’héritage.

Sources : [acteur Ignitium](<StarRupture-Plugin-SDK/StarRupture SDK/Client/SDK/BP_FireWaveMeteOreChunk_classes.hpp>), [classe parente](<StarRupture-Plugin-SDK/StarRupture SDK/Client/SDK/BP_StandaloneMeteOreChunkBase_classes.hpp>), [déclarations Client](<StarRupture-Plugin-SDK/StarRupture SDK/Client/SDK/Chimera_classes.hpp>), [structures Client](<StarRupture-Plugin-SDK/StarRupture SDK/Client/SDK/Chimera_structs.hpp>), [déclarations Server](<StarRupture-Plugin-SDK/StarRupture SDK/Server/SDK/Chimera_classes.hpp>).

Deux précautions sont nécessaires :

- `BP_IsGatherableDepleted()` renvoie également `false` lorsque certaines dépendances sont absentes : monde, sous-système Mass, entité active ou fragment de phase. Un `true` apporte une preuve d’épuisement ; un `false` isolé ne prouve pas la disponibilité. [Implémentation native](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/IsGatherableDepleted_1475F0E30.txt).
- `bIsPermanentlyGathered` est une propriété éditable de classe, distincte de `bIsDepleted`, qui est l’état répliqué. Dans les fragments de minerai, elle choisit explicitement entre enregistrement permanent et temporaire **au moment de l’épuisement**. Elle ne suffit donc pas à prouver qu’une récolte a déjà eu lieu. La capture actuelle utilise pourtant `bIsDepleted || bIsPermanentlyGathered` pour les gatherables, à deux endroits. Cette interprétation doit être corrigée en séparant état courant et politique de persistance, après vérification des classes concernées. [Choix permanent/temporaire](StarRupture-Game-IDADump/ACrStandaloneMeteOreChunk/OnResourceDepleted_1476BD280.txt), [initialisation du gatherable](StarRupture-Game-IDADump/ACrGatherableBaseActor/BeginPlay_1475E0300.txt).

La classification devrait également privilégier la récompense explicite `I_StarTears_C` avant les correspondances génériques par classe de plante. L’ordre actuel fait l’inverse ; un éventuel gatherable générique portant cette récompense serait classé comme plante. Les sources examinées ne prouvent pas quelle classe Blueprint porte actuellement cette récompense en jeu.

## 2. Retrouver une récolte après la disparition de l’acteur

Le chemin natif d’épuisement des fragments utilise :

```text
ACrStandaloneMeteOreChunk::MineResource(..., MiningActor)
  → ACrOreActor::DeactivateOre(...)
      → OreData.bIsDepleted = true
      → ACrStandaloneMeteOreChunk::OnResourceDepleted(...)
          → RegisterDepletedGatherable(actor)
            ou RegisterPermanentDepletedGatherable(actor)
```

Sources : [minage](StarRupture-Game-IDADump/ACrStandaloneMeteOreChunk/MineResource_1476B87A0.txt), [désactivation](StarRupture-Game-IDADump/ACrOreActor/DeactivateOre_14738B630.txt), [enregistrement d’épuisement](StarRupture-Game-IDADump/ACrStandaloneMeteOreChunk/OnResourceDepleted_1476BD280.txt).

En multijoueur, `ACrGatherableSpawnersRepActor` expose `RepDepletedGatherables` et `RepPermanentDepletedGatherables`. Les entrées contiennent une position et des bounds ; les entrées temporaires contiennent aussi `ClearEnviroWaveStageCombination`. Elles ne contiennent ni classe de ressource, ni identifiant de joueur, ni heure de récolte.

**Écart actuel :** `MarkPlantDepletedNearLocation()` n’accepte que `PoiKind::PlantResource`. Une récolte d’Ignitium ou de Star Tears manquée par le scan d’acteurs ne sera pas rattrapée par ce chemin. Le scan complet est espacé d’au moins cinq secondes.

Implémentation proposée :

1. Copier les entrées d’épuisement dans un index spatial propre au monde et à la génération courants.
2. Les associer aux sites déjà identifiés, en conservant séparément Ignitium et Star Tears. Utiliser d’abord une correspondance précise, puis des bounds et une tolérance validée en jeu.
3. Conserver le masque de phase : le jeu utilise un fragment de phase de l’entité pour choisir son registre d’épuisement. Ce fragment est opaque dans le SDK généré ; le récupérer nécessiterait un accès natif validé ou une autre preuve d’association.
4. Si plusieurs ressources se superposent et que l’association est ambiguë, ne pas marquer les deux récoltées. Un événement portant l’acteur est préférable à une déduction par proximité.
5. Ne pas traiter le retrait d’une entrée répliquée comme une preuve de repousse : les listes peuvent être vidées lors d’un changement de phase/génération.

Le jeu possède aussi un chemin `RegisterConditionallyErasedDepletedGatherable()`. Les tableaux répliqués ne constituent donc pas à eux seuls une preuve de couverture de toutes les formes de récolte. La couverture de Star Tears doit être relevée pendant une récolte réelle.

Sources : [enregistrement et fragment Mass](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/RegisterDepletedGatherable_1475FC310.txt), [réception d’une entrée](StarRupture-Game-IDADump/FCrRepDepletedGatherableData/PostReplicatedAdd_1475FAFE0.txt), [enregistrement conditionnel](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/RegisterConditionallyErasedDepletedGatherable_1475FBD50.txt), [changement de génération et listes répliquées](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/OnGlobalSeedChanged_1475F6E00.txt).

## 3. Suivre correctement une nouvelle génération, y compris en solo

Le dump montre que `UCrGatherableSpawnersSubsystem::OnEnviroWaveStarted()` tire une nouvelle graine pour `Heat` + `Moving`. Son traitement est différé : `Tick()` attend la suppression des anciennes entités concernées, puis appelle `OnGlobalSeedChanged()`.

**Écart actuel :** `RefreshObservedRuptureResourceGeneration()` dépend uniquement de `ACrGatherableSpawnersRepActor`. Or `OnWorldBeginPlay()` ne crée ce réplicateur que pour `NM_DedicatedServer` et `NM_ListenServer`, pas pour `NM_Standalone`. Le reset de génération actuel ne couvre donc pas à lui seul le solo décrit par ce dump.

Méthodes possibles :

- En multijoueur, conserver la lecture de `RepGlobalGatherablePCGSeed`, ainsi que des indications de phase répliquées lorsque nécessaires.
- En solo et côté serveur, lire `UCrGatherableSpawnersSubsystem::GetGlobalSeed()` via une résolution native validée. Ce getter existe dans le dump mais n’a pas de wrapper dans le SDK généré. Son adresse doit être résolue pour l’exécutable cible ; ne pas lire directement l’offset du dump comme s’il était garanti.
- Sans accès natif au getter, utiliser les delegates de Rupture déjà suivis pour ouvrir une nouvelle génération locale à l’entrée en `Heat/Moving`. Cela couvre le cycle normal, mais pas nécessairement un changement de graine forcé sans transition de phase.
- Invalider l’ancienne génération, puis éviter de réintroduire ses acteurs pendant leur destruction différée. Garder leur identité faible jusqu’à leur retrait, ou suivre la fin du traitement de génération. Un simple « vider puis rescanner immédiatement » peut recopier des acteurs encore présents de l’ancien cycle.

Sources : [création du réplicateur](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/OnWorldBeginPlay_1475FA400.txt), [nouvelle graine](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/OnEnviroWaveStarted_1475F4360.txt), [getter](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/GetGlobalSeed_1475EA520.txt), [traitement différé](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/Tick_1476048C0.txt), [net modes](StarRupture-Game-IDADump/_Types/ENetMode.txt).

## 4. Observer la transition vers Star Tears

Les points d’observation utilisables sont :

- `OnWaveStartedDynamic`, `OnWaveFadeoutSubstageChanged` et `OnWaveGrowbackSubstageChanged`, déjà disponibles sur `UCrEnviroWaveSubsystem` et suivis par le plugin ;
- `GetCurrentStage()`, `GetCurrentStageProgress()` et les sous-phases courantes ;
- les apparitions d’acteurs via `RegisterOnActorBeginPlay`, suivies d’une lecture différée pour laisser leur initialisation se terminer ;
- `IsMineableChunk()` pour savoir si l’Ignitium observé est encore minable ;
- `UGatherableCropSettings::GatherableInteractivityData`, puis `UCrGatherableActorInteractivityData::Data`, pour retrouver les phases, sous-phases et bornes de progression autorisant l’interaction avec la classe de gatherable observée.

Le `CanInteract()` natif consulte effectivement ces règles. Il tient aussi compte du joueur et de sa distance : l’appeler aveuglément pour tous les points de la carte confondrait disponibilité de la ressource et capacité d’interaction immédiate d’un joueur. Pour la carte, lire les règles de phase séparément et garder « récoltable » distinct de « à portée ».

Sources : [conditions natives d’interaction](StarRupture-Game-IDADump/ACrGatherableBaseActor/CanInteract_1475E20C0.txt), [transmission de la sous-phase au Blueprint](StarRupture-Game-IDADump/ACrOreActor/OnGrowbackSubstageChanged_14739ED30.txt).

**Limite démontrée :** le SDK contient les fonctions Blueprint `BP_OnGrowbackSubstageChanged` et `ExecuteUbergraph_BP_FireWaveMeteOreChunk`, mais leurs fichiers `functions.cpp` sont des wrappers `ProcessEvent`, pas le corps des graphes. Le dump examiné ne démontre pas un événement natif universel « cet Ignitium vient de devenir cette Star Tear », ni une relation parent/enfant entre les deux acteurs. `ResourceLifeSpanSeconds` est exposé, mais cela ne prouve ni sa valeur runtime ni une durée universelle de transformation.

La projection documentée du projet reste donc utile. Pour la préciser, journaliser un site non récolté à travers une Rupture, avec phase réelle, sous-phase, génération, états et apparitions. Une corrélation spatiale permet de rapprocher les observations ; elle ne doit pas être présentée comme une identité d’acteur garantie. Les 690 secondes de la timeline actuelle ne sont pas démontrées comme seuil moteur par ces sources.

## 5. Passer à un suivi événementiel des récoltes

| Point d’observation natif | Ce qu’il apporte | Limite |
|---|---|---|
| `ACrStandaloneMeteOreChunk::MineResource` | Acteur, `MiningActor`, résultat `FCrMinedResource` | Vérifier une récolte réussie, pas seulement un coup de minage. |
| `ACrGatherableBaseActor::NativeOnInteract` | Gatherable et contrôleur joueur | Peut sortir sans récolte, notamment faute de place ; vérifier le résultat ou le changement d’état. |
| Implémentation effective de `ICrGatherableInterface::DepleteResource` | Ressource, quantité et joueur transmis | Le SDK expose le contrat ; le corps concret et l’ABI de l’implémentation restent à résoudre. |
| `RegisterDepletedGatherable` / `RegisterPermanentDepletedGatherable` / `RegisterConditionallyErasedDepletedGatherable` | Acteur avant sa disparition et régime d’épuisement | Ne donne pas à lui seul le joueur ni la cause exacte de l’épuisement. |
| `UCrGatherableSpawnersSubsystem::InstancedInteractionReward` | Chemin alternatif d’interaction avec un mesh instancié | Vérifier si Star Tears utilise ce chemin ; ne pas supposer que tous les gatherables passent par un acteur classique. |

Sources : [interaction du gatherable](StarRupture-Game-IDADump/ACrGatherableBaseActor/NativeOnInteract_1475F23B0.txt), [contrat de déplétion](<StarRupture-Plugin-SDK/StarRupture SDK/Client/SDK/Chimera_functions.cpp>), [récompense instanciée](StarRupture-Game-IDADump/UCrGatherableSpawnersSubsystem/InstancedInteractionReward_1475F04E0.txt).

Le SDK fournit `IPluginHookScanner` et `IPluginHookUtils::Install/Remove` pour ces interceptions natives. Ce ne sont pas des callbacks métier déjà prêts à enregistrer. Les signatures et ajustements de pointeur des interfaces C++ doivent être vérifiés pour chaque cible. Un hook du seul wrapper `exec...` ou de `ProcessEvent` ne couvre pas nécessairement les appels natifs directs.

Résoudre les signatures au chargement, installer dans `PluginInit`, retirer avant déchargement. Copier les données nécessaires avant que l’acteur puisse disparaître, appeler le comportement original exactement une fois et pousser un événement dans une file du plugin. Le tick applique les événements et demande une capture ; le hook n’effectue pas un scan complet réentrant. Utiliser les hooks d’état sur l’autorité solo/serveur et le transport de snapshots existant pour le viewer distant.

Les noms `Register...`, `DepleteResource`, `MineResource`, `TriggerGenerationWithCurrentSeed` décrivent des opérations du jeu à observer. Le plugin ne doit pas les appeler pour provoquer une récolte ou une génération.

Source API : [interface du ModLoader](StarRupture-Plugin-SDK/include/plugin_interface.h).

## Ordre d’implémentation recommandé

1. **Fiabiliser les observations** : priorité de classification Star Tears, séparation entre épuisement et politique permanente, lecture de `IsMineableChunk()`, consultation prudente de `BP_IsGatherableDepleted()`.
2. **Corriger la génération en solo** et empêcher la réintroduction des anciens acteurs pendant la transition.
3. **Étendre la récupération des états d’épuisement** aux ressources de Rupture avec une association non ambiguë ; instrumenter d’abord les récoltes pour vérifier les chemins utilisés.
4. **Ajouter les hooks de récolte validés** pour couvrir les acteurs disparus entre deux scans. L’attribution à un joueur exige un événement portant ce joueur ; elle ne peut pas venir des tableaux de positions épuisées.
5. **Préciser la transition** avec les conditions d’interaction du jeu et un relevé de cycle. Garder la projection documentée et la priorité de l’observation réelle.

Un enregistrement interne de site devrait garder monde, génération, position, observations séparées par ressource, état d’épuisement, possibilité de récolte, source et heure de dernière observation. L’heure d’observation n’est pas l’heure exacte de récolte. Une disparition par streaming n’est pas une récolte. Une ressource momentanément non récoltable n’est pas nécessairement épuisée et ne doit pas bloquer à elle seule la projection vers Star Tears.

Si ces distinctions sont exposées au viewer, elles nécessitent une évolution coordonnée des types, de la sérialisation, du protocole client/serveur et du frontend. Le booléen `depleted` seul ne représente pas tous ces états. Ces distinctions sont désormais portées par le protocole v6 et le contrat viewer 3.

## Validation avant intégration

- Ignitium intact, coups de minage sans épuisement, puis récolte complète ; acteur retiré avant le prochain scan.
- Site Ignitium laissé intact jusqu’à apparition réelle de Star Tears ; comparer timestamps, positions et sous-phases.
- Star Tears récoltée, inventaire plein, ressource hors portée, coexistence avec Ignitium proche.
- Sortie puis retour dans une zone chargée, sans transformer le streaming en événement de récolte.
- Nouvelle Rupture en solo, serveur hôte et serveur dédié ; anciens acteurs présents pendant le renouvellement ; client arrivant en cours de cycle.
- Effacement d’un registre temporaire, persistance permanente et changement de monde.
- Hooks retirés correctement ; correspondance des signatures et disposition des objets vérifiées sur les exécutables cibles.

Les changements C++ correspondants demanderont les builds Client et Server prescrits dans `AGENTS.md`, ainsi que des tests des transitions d’état. Les builds et les scénarios simulés ne remplacent pas le relevé en jeu des chemins de récolte et de transformation.

## Vérifications réalisées après implémentation

- Builds Client Release et Server Release : succès, zéro erreur et zéro avertissement.
- `python3 tools/tests/resource_observation_test.py` : transitions de génération, états transmis, bornes/sous-phases d'interaction, projection et priorité des observations, rapprochements ambigus, historique spatial face à un acteur fraîchement lu. Ces tests compilent les fonctions de production avec des types moteur simulés.
- Viewer : `pnpm run check` et `pnpm run build` réussis. Chromium sur le HTML de production ouvert en `file://` : quatre états, ancien booléen `depleted`, estimation explicitement libellée, remplacement par une observation réelle, infobulle immobile et sélection actualisées, disparition du point.
- Régressions navigateur : plante du catalogue inconnue → disponible → épuisée → disponible, absence d'observation, sélections, annotations, raccourcis et affichage mobile.
- Description BBCode : balises appariées équilibrées.

Aucune récolte ni Rupture réelle n'a été exécutée pour cette validation. Les essais en jeu listés ci-dessus restent nécessaires avant d'affirmer une couverture complète des récoltes.
