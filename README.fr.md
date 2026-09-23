# MapExtension_Plugin

Les onglets Filtres, Affichage et Avance restent accessibles quel que soit le prereglage. Les actions groupees sur les ressources ne changent que les ressources ; Avance contient les controles globaux des couches. Reinitialiser restaure les filtres live et statiques du prereglage courant, purete comprise. Isoler une ressource est un raccourci ensuite ajustable. Les filtres replies sortent du parcours clavier.

Le viewer utilise une barre de session compacte, un panneau de filtres bleu nuit et des accents cyan pour les selections. La frequence de rafraichissement se regle dans les parametres ; la timeline de rupture reste visible au-dessus de la carte.

README anglais : `README.md`

`MapExtension_Plugin` expose les donnees de carte de StarRupture via un endpoint HTTP local et inclut `mapview`, une interface web locale utilisee pour afficher la carte, les entites, leurs connexions et la timeline du cycle de rupture.

Le plugin fonctionne aussi bien en partie solo qu'en multijoueur. En solo/local, seul le build client est necessaire. Sur serveur dedie, il faut installer le build client sur la machine du joueur et le build serveur sur le serveur dedie afin que le serveur puisse envoyer les donnees de carte et de cycle de rupture aux clients connectes.

## Fonctionnalites

- Voir quels `Cargo Dispatchers` sont relies a quels `Cargo Receivers`, et inversement
- Afficher leurs positions directement sur la carte
- Voir les objets et quantites demandes dans les connexions cargo (sans mesure du debit reel)
- Afficher la position des `teleporteurs`
- Afficher la position des `joueurs`, avec son propre joueur mis en avant dans une couleur distincte
- Afficher les bases abandonnees, les plantes recoltables prises en charge, Ignitium et Star Tears comme points d'interet (POI), dont Hydrobulb, Polifruit, Oxallop, Purplant, Serpent Root, Prickler, Prism Herb, Sulheart, Gold Fruit, Thornfruit, Sikkim Rhubarb et Nootka Lupine
- Parcourir un catalogue du monde pre-genere contenant les POI principaux, plantes, minerais, ressources animales, batiments, zones et elements techniques optionnels, avec des filtres persistants et une recherche
- Cumuler les ressources observees en direct dans les zones chargees au lieu de retirer leurs marqueurs quand le joueur quitte le rayon de chargement courant
- Conserver la position des ressources epuisees ou recoltees observees dans le monde actif avec un etat visuel distinct
- Utiliser une vue compacte du cycle de rupture avec phase, temps restant, legende et details de timeline
- Filtrer la carte pour ne garder que les marqueurs et zones personnels
- Centrer la carte sur son propre joueur avec le controle `Joueur` ou le raccourci `P`
- Exposer `GET /health`, `GET /cargo` et `GET /rupture-cycle` sur le serveur HTTP local
- Recevoir les snapshots de carte et de cycle de rupture envoyes par la build serveur en session serveur dediee
- Utiliser un fallback local via `UCrEnviroWaveSubsystem` en solo/local quand aucun snapshot serveur n'est disponible

## Mapview

Le `mapview` inclus est une interface web locale concue pour lire les donnees du plugin et les afficher dans un navigateur en ouvrant le fichier genere `dist/MapExtensionViewer.html`.

Une fois packagee, il faut garder les dossiers generes `map-tiles/` et `map-data/` a cote de `MapExtensionViewer.html` ; l'interface charge le fond de carte depuis les tuiles et le catalogue statique depuis les fichiers de donnees compacts.

Il consomme a la fois les donnees de carte/cargo et l'endpoint de cycle de rupture pour afficher la timeline de l'interface.

L'interface combine les observations live du plugin avec un catalogue statique pre-genere du monde. Les POI principaux sont des pins selectionnables, tandis que les couches plus volumineuses de ressources, batiments et zones utilisent un rendu canvas. Des filtres avec recherche controlent les couches, categories et types de ressources, sources de representation, batiments, zones et elements techniques ; les choix sont conserves dans `localStorage`.

Le catalogue fournit les positions des plantes ; les observations live mettent a jour ces memes points avec leur derniere disponibilite observee. Sans observation, l'etat reste inconnu. Une plante observee est indiquee disponible ou epuisee, et sa position reste sur la carte apres recolte. La carte, l'infobulle et le panneau de details ouvert suivent chaque nouvelle observation, y compris un retour a l'etat disponible. Il s'agit du dernier etat connu, pas d'une verification continue des zones dechargees. La categorie Plantes et les filtres par ressource s'appliquent aux positions du catalogue comme aux positions observees. Le prereglage Reseau garde les plantes masquees tant qu'elles ne sont pas demandees.

Le frontend filtre les placements fondes sur des classes dans la couche `building` : seuls les types d'acteur Unreal dont le nom contient `KeyCard`, `Coralion_Egg` ou `Spawner` (sans distinction de casse) sont rendus. Cette regle ne modifie pas les couches `zone` et `technical`.

Les bases abandonnees utilisent leur propre icone sur la carte. Les ressources vegetales utilisent une couleur de palette stable derivee du nom de la ressource, tandis qu'Ignitium et Star Tears ont des couleurs fixes specifiques. Les ressources epuisees restent visibles avec un marqueur attenue et en contour. Des filtres separes controlent les bases abandonnees, les ressources vegetales, Ignitium et Star Tears.

Les filtres Ignitium et Star Tears utilisent les positions validees depuis les acteurs runtime dans les zones chargees pres des joueurs. Les grands volumes PCG d'inclusion et d'exclusion ne sont pas affiches comme des emplacements de ressources. Quand la timeline publiee indique Arcadia stable (a partir de 690 secondes dans le cycle de 3240 secondes, avec `PreWave` en secours si le temps ecoule est indisponible), un site Ignitium valide et non recolte est expose comme Star Tears plutot que comme Ignitium. Cette projection est recalculee depuis les observations a chaque capture ; une observation reelle de Star Tears au meme site la remplace, meme epuisee, afin qu'une ancienne projection ne laisse pas de doublon disponible. Pendant la transition de stabilisation, les deux ressources peuvent etre affichees dans la courte fenetre de fin de cycle, avec Star Tears au-dessus du marqueur Ignitium sous-jacent.

Ignitium et Star Tears distinguent **disponible**, **non récoltable pour le moment**, **épuisé** et **état inconnu**. Ignitium utilise la requête de minage du jeu ; les Star Tears observées utilisent les règles d'interaction chargées pour leur classe et la phase du cycle. Des règles absentes laissent l'état inconnu. Un site Ignitium intact mais temporairement non récoltable peut encore produire le marqueur Star Tears projeté. Celui-ci porte la mention **Disponible (estimation du cycle)** ; une observation réelle de Star Tears reste prioritaire. Le déchargement d'une zone ne prouve pas une récolte. Une position d'épuisement répliquée ne met à jour une ressource connue que si l'association spatiale est unique, afin de ne pas marquer deux ressources superposées comme épuisées. Une lecture récente de l'acteur et du sous-système reste prioritaire sur ces positions sans type de ressource.

La barre de rupture affiche la phase et le temps restant. Un clic ou `Entree`/`Espace` ouvre les details ; `Echap`, le bouton Fermer ou un clic exterieur les referme. Sur mobile, la barre reste visible sans defilement horizontal, les details restent dans la fenetre et les commandes de carte disposent d'un espace reserve sous les filtres. Le controle `Joueur` ou le raccourci `P` centre la carte sur le joueur `self`, ou sur le premier joueur en l'absence de cet indicateur.

La timeline utilise une calibration de 3240 secondes (30/60/600/2550), animee entre les observations. Elle peut diverger du jeu ; les reglages bruts Heat/Cold ne sont pas une correspondance directe avec ces phases. Cette calibration reste volontairement conservee.

## Couverture des plantes

Le catalogue statique fourni avec l'interface contient les donnees pre-generees completes pour les plantes et les POI. Il est charge directement par le viewer : le plugin ne lance donc plus de scan complet World Partition, ne deplace plus le joueur, ne suspend plus le cycle de rupture et n'ecrit plus de cache POI par sauvegarde.

Le snapshot live peut toujours capturer les instances `ACrGatherableBaseActor` et `ACrOreActor` prises en charge dans les zones deja chargees afin de refleter le monde actif. Ces observations restent uniquement en memoire pour le monde courant ; l'etat des acteurs vivants et les donnees repliquees des emplacements epuises conservent les dernieres positions connues. Les observations Ignitium et Star Tears sont supprimees lorsque le seed PCG global replique change. En solo, ou ce replicateur est absent, l'entree en Heat/Moving ouvre une nouvelle generation locale. Les acteurs deja observes et encore en attente de suppression sont exclus de la nouvelle generation. Ce secours ne detecte pas un changement force du seed solo sans transition de phase.

La detection par classe de recompense publie Hydrobulb, Polifruit, Oxallop, Purplant, Serpent Root, Prickler, Prism Herb et Sulheart. Des classes d'acteur gatherable explicites ajoutent Gold Fruit, Thornfruit, Sikkim Rhubarb, Nootka Lupine et le gatherable generique `Plant_h` du jeu. Les acteurs dont `InteractionRewardResource` vaut `I_StarTears_C` publient Star Tears ; les `ACrOreActor` dont `Resource` vaut `I_FireWaveOre_C` publient Ignitium.

## Donnees POI de `/cargo`

`GET /cargo` inclut les totaux POI dans `counts` et un tableau `pois` :

```json
{
  "counts": {
    "pois": 1,
    "abandoned_bases": 0,
    "plant_resources": 1,
    "ignitium": 0,
    "star_tears": 0
  },
  "pois": [
    {
      "kind": "plant_resource",
      "label": "Hydrobulb",
      "resource": "Hydrobulb",
      "depleted": false,
      "state": "available",
      "source": "actor_observation.gatherable",
      "unique_key": "example-plant-key",
      "world": { "x": 0.0, "y": 0.0, "z": 0.0 },
      "map": { "x": 0.0, "y": 0.0 }
    }
  ]
}
```

- `kind` vaut `abandoned_base`, `plant_resource`, `ignitium` ou `star_tears`.
- `label` est le libelle affiche ; `resource` est le nom de ressource detecte et peut etre vide pour une base abandonnee.
- `depleted` vaut `true` pour les acteurs de ressource epuises observes dans le monde actif. Leur derniere position connue reste publiee afin que l'interface les affiche avec un marqueur attenue et en contour.
- `state` vaut `available`, `unavailable`, `unknown` ou `depleted` et distingue impossibilite temporaire de recolte et epuisement. Le contrat viewer 3 ajoute cette distinction : remplacer le viewer avec le plugin. Sans `state`, les anciens payloads utilisent `depleted`.
- `source` identifie le chemin de capture, `unique_key` identifie le POI pour l'interface, `world` contient les coordonnees Unreal `x`/`y`/`z` et `map` contient les coordonnees projetees `x`/`y`.
- `counts.pois` est le nombre total de POI ; `counts.abandoned_bases`, `counts.plant_resources`, `counts.ignitium` et `counts.star_tears` contiennent les totaux par kind.

## Compatibilite de la synchronisation serveur dedie

Les snapshots de serveur dedie utilisent le protocole de synchronisation v6, qui transporte les POI par pages de 64 entrees maximum par requete, associe chaque page a une revision stable du contenu, marque le marqueur joueur propre a chaque client, et valide les identifiants de snapshot, generations, revisions, nombres d'elements, nombres de chunks, totaux et disposition des pages avant de publier un snapshot distant. Les pages de revisions POI differentes ne sont jamais fusionnees. Les versions de protocole doivent correspondre exactement : un client ou serveur v6 ignore les paquets d'une autre version de protocole au lieu de tenter un downgrade.

**Mettre a jour les builds client et serveur dedie ensemble.** La mise a jour automatique du modloader ne remplace que la DLL client ; la DLL du serveur dedie doit etre remplacee manuellement pendant la meme mise a jour. Ne pas laisser les deux cotes sur des releases differentes.

## Installation et mises a jour

L'archive de release client contient :

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `MapExtensionViewer.html`
- `map-tiles/`
- `map-data/`

Copier le contenu de `Plugins/` dans `StarRupture/Binaries/Win64/Plugins/`, puis garder `MapExtensionViewer.html`, `map-tiles/` et `map-data/` ensemble, n'importe ou sur la machine.

`MapExtension_Plugin.json` est le sidecar de mise a jour du modloader. Son seul champ est `manifest_url`, qui pointe vers le manifest de release `latest/download`. Quand le sidecar est present, le modloader verifie au demarrage s'il existe une version plus recente du plugin et remplace `MapExtension_Plugin.dll` avant de charger le moindre plugin. Installer uniquement la DLL seule desactive les mises a jour automatiques.

Deux limites a connaitre :

- La mise a jour automatique ne remplace que la DLL. `MapExtensionViewer.html`, `map-tiles/` et `map-data/` ne sont jamais touches, puisqu'ils vivent hors du dossier du jeu. L'interface detecte les contrats de payload incompatibles : quand le plugin annonce un contrat plus recent que celui avec lequel l'interface locale a ete construite, une fenetre propose le telechargement direct de l'asset `MapExtension_Plugin-<tag>-viewer.zip` correspondant, avec les liens vers la release GitHub et la page du mod. Les ameliorations retrocompatibles du viewer peuvent ne pas declencher cette fenetre ; installer donc manuellement l'archive viewer correspondante pour profiter des nouvelles fonctions d'interface. Remplacer `MapExtensionViewer.html`, `map-tiles/` et `map-data/` ensemble, puis recharger la page.
- Le build serveur n'est pas couvert par le sidecar. Le protocole de synchronisation v6 exige des builds client et serveur correspondants : mettre a jour les deux DLL ensemble et remplacer manuellement celle du serveur dedie.

Les mises a jour automatiques peuvent etre desactivees globalement dans le modloader avec `[AutoUpdate] Enabled=0` dans `modloader.ini`.

## Choix d'interface

Le projet a d'abord vise une integration plus directe dans l'interface du jeu.

En pratique, l'UI de StarRupture s'est revelee trop limitee pour obtenir un resultat fiable et maintenable dans de bonnes conditions. Le projet a donc bascule vers une interface web locale.

Ce n'est pas forcement la solution ideale sur le plan de l'integration, mais c'est aujourd'hui l'approche la plus efficace pour iterer rapidement, afficher les donnees correctement et garder un outil reellement utilisable.

## Etat du projet

Il s'agit d'une premiere ebauche fonctionnelle.

Le projet va continuer a evoluer prochainement, mais les retours, corrections et contributions sont les bienvenus.

Le plugin se build contre `StarRupture-Plugin-SDK`.

Exemple :

- `./build.sh client release`

Pour les details de build et de workflow, voir `DEVELOPERS.md`.

Pour tout ce qui concerne le build et le developpement, voir `DEVELOPERS.md`.

## Licence

Le code de `MapExtension_Plugin` est distribue sous licence MIT. Voir `LICENSE`.

Les composants et references tiers sont documentes dans `THIRD_PARTY_NOTICES.md`.

Attention : certains assets ou references tiers peuvent relever de droits distincts du code MIT.

## Configuration du plugin

Le plugin cree `Plugins/config/MapExtension_Plugin.ini` avec :

```ini
[General]
Enabled=1

[Diagnostics]
VerboseLifecycleLogs=0
LogRuntimePlanOnce=0
LogCargoSnapshots=0
LogRuptureCycleEvents=0
LogResourceObservations=0
LogActorScanFallback=0
LogRefreshTimings=0

[Http]
Port=9000

[Runtime]
RefreshIntervalMs=2000
```

- `Enabled` : active ou desactive le plugin (`1` ou `0`)
- `VerboseLifecycleLogs` : active les logs de cycle de vie (`1` ou `0`)
- `LogRuntimePlanOnce` : loggue la strategie runtime une fois (`1` ou `0`)
- `LogCargoSnapshots` : loggue les snapshots cargo (`1` ou `0`)
- `LogRuptureCycleEvents` : loggue les changements d'etat du cycle de rupture et les evenements monde/serveur lies (`1` ou `0`)
- `LogResourceObservations` : loggue les changements d'etat observes, les resets de generation locale et les changements du nombre d'entrees d'epuisement ambigues (`1` ou `0`). Activer avec `LogRuptureCycleEvents` sur l'autorite solo/hote/serveur pour comparer une recolte et un cycle complet. Ce sont des heures d'observation, pas des heures exactes de recolte ni une attribution a un joueur ; un acteur bref peut disparaitre entre deux captures. Les hooks natifs de recolte demandent encore une validation sur les executables cibles.
- `LogActorScanFallback` : loggue le fallback actor scan (`1` ou `0`)
- `LogRefreshTimings` : loggue les timings par phase de refresh (`1` ou `0`)
- `Port` : definit le port HTTP local utilise par le plugin
- `RefreshIntervalMs` : definit l'intervalle de refresh runtime en millisecondes

## Remerciements

- Merci a `AlienXAXS` pour `StarRupture-ModLoader`, le mod loader qui a permis a `MapExtension_Plugin` de voir le jour :
  `https://github.com/AlienXAXS/StarRupture-ModLoader`
- Merci a `bithoarder` pour `StarRuptureMap`, utilise pour la carte du jeu StarRupture :
  `https://github.com/bithoarder/StarRuptureMap/`
- Ce projet s'inspire aussi de `StarRuptureSaveMap` :
  `https://github.com/thanamatos/StarRuptureSaveMap`
- Merci egalement aux developpeurs de StarRupture pour le jeu.

## Avertissement

Il s'agit d'un outil de modding. Utilisation a vos risques.

Les auteurs ne peuvent pas etre tenus responsables d'eventuels dommages, regressions ou incompatibilites provoques par son utilisation. Tout est fait pour limiter les impacts sur les sauvegardes et la compatibilite future, mais cela ne peut pas etre garanti.
