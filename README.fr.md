# MapExtension_Plugin

README anglais : `README.md`

`MapExtension_Plugin` ajoute une carte logistique dans StarRupture. La touche `F8` ouvre en jeu un panneau ModLoader qui dessine le terrain de la carte du jeu avec le reseau cargo, les teleporteurs, les joueurs, les base cores, les monolithes, les antennes, les ressources et le cycle de rupture.

Depuis la version 0.8, la carte est dans le jeu. L'ancien viewer navigateur (`MapExtensionViewer.html`, `map-tiles/`) et son API HTTP locale sont supprimes ; voir [Mise a jour depuis la 0.7](#mise-a-jour-depuis-la-07).

Le plugin fonctionne en solo et en coop ; seul le build client est a installer. StarRupture a arrete le support des serveurs dedies, aucun paquet serveur n'est donc plus publie (le dernier, 0.8, n'est plus maintenu).

En coop (un joueur heberge la partie), chaque joueur installe le build client. L'hote peut partager sa carte avec les joueurs qui le rejoignent (experimental, pas encore teste dans une vraie partie coop ; les retours sont les bienvenus) : cocher **Share map with co-op players (experimental)** dans la barre de la carte en jeu (enregistre dans `[Coop] ShareMapWithClients`). Les joueurs qui rejoignent voient alors le reseau cargo, les teleporteurs, les joueurs, les points d'interet et le cycle de rupture de l'hote ; la barre affiche `Map: from host`, ou `Map: local` quand l'hote ne partage pas ou utilise une autre version de MapExtension.

## Fonctionnalites

- Ouvrir la carte avec `F8` (configurable), la fermer avec la meme touche ou `Echap` ; glisser pour deplacer, molette pour zoomer, option "Follow me"
- Voir quels `Cargo Dispatchers` sont relies a quels `Cargo Receivers` ; survoler un cargo ou un lien affiche les items transportes avec leur icone et les quantites demandees (quantites demandees, pas un debit mesure)
- Cliquer un cargo pour isoler ses liens ; les autres s'estompent
- Filtrer le reseau cargo par item transporte, avec un champ de recherche, et masquer au besoin les points cargo sans lien
- Afficher les `teleporteurs` et les `joueurs`, avec son propre joueur mis en evidence
- Afficher ses base cores avec leur nom, leur niveau, l'usage du refroidissement et l'etat d'infection
- Afficher chaque monolithe avec son etat (desactive, en recharge, actif, charge, attaque en cours), sa charge en pourcentage du seuil d'attaque et la cible d'une attaque en cours
- Afficher les antennes (geo scanners) avec leur etat
- Afficher les bases abandonnees avec leur etat de decouverte et l'avancement de la collecte (objets, coffres de plans, objets d'histoire, kystes d'infection), ainsi que les grottes et autres points d'interet
- Afficher l'Ignitium et les Star Tears selon la phase de rupture, avec en haut de la carte un bandeau de rupture indiquant la phase, le temps restant et la ressource de la phase
- Parcourir un catalogue du monde pre-genere : gisements avec purete, plantes, creatures et points d'interet, un filtre par type, dessines avec les icones du jeu

## D'ou viennent les donnees

- **Cargo, teleporteurs, joueurs, cycle de rupture** : lus dans le jeu en cours (ou envoyes par l'hote en coop quand il partage sa carte), rafraichis tous les `RefreshIntervalMs`.
- **Antennes, bases abandonnees, etats de decouverte, nom et niveau des base cores** : lus dans les donnees que le jeu replique pour sa propre carte.
- **Monolithes et position des base cores** : l'etat et la charge des monolithes n'existent que dans les donnees d'entites internes du jeu, et le jeu publie ses marqueurs de base plusieurs dizaines de secondes apres le chargement d'un monde. Le build client lit les deux par une requete d'entites Mass en lecture seule, dont les fonctions et les tailles viennent toutes de la table des versions du mod loader. Si le loader ne couvre pas un build du jeu, il coupe uniquement cette fonctionnalite ("In-game map: monolith state and charge") : les monolithes perdent leur etat, les base cores apparaissent avec le retard du jeu, et le reste du plugin continue de fonctionner.
- **Apres une mise a jour du jeu** : le client demande au mod loader chaque valeur du jeu par son nom (positions des membres, tailles des structures, fonctions du jeu, valeurs d'enum) au lieu d'utiliser les positions du build du jeu pour lequel il a ete compile ; la meme DLL continue donc de fonctionner apres une mise a jour du jeu, sans nouvelle version de MapExtension. Le mod loader met a jour sa table des versions a chaque demarrage. Une valeur qu'il ne trouve pas sur un nouveau build coupe uniquement la fonctionnalite de la carte qui en a besoin.
- **Gisements, plantes, creatures, points d'interet** : un catalogue statique livre avec l'archive client dans `Plugins/MapExtension/map-data/`. Les positions sont pre-generees ; ce ne sont pas des observations en direct.
- **Ignitium et Star Tears** : observes dans les zones chargees pres des joueurs, puis projetes selon la phase de rupture.

Le plugin observe uniquement. Il n'automatise pas le jeu, ne modifie pas les sauvegardes et n'envoie aucune commande en jeu.

## Ignitium et Star Tears

Les positions viennent d'acteurs runtime valides dans les zones chargees. Tant que la timeline publiee est en Arcadia stable (a partir de 690 secondes dans le cycle de 3240 secondes), un site d'Ignitium valide et non recolte est expose comme Star Tears au lieu d'Ignitium ; une vraie observation de Star Tears au meme endroit remplace cette projection. Pendant la transition de stabilisation, la carte en jeu n'affiche que l'Ignitium quand les deux sont signales au meme endroit.

Les marqueurs distinguent les etats disponible, non recoltable pour le moment, epuise et estime d'apres le cycle. Le dechargement d'une zone ne prouve pas une recolte.

La timeline utilise un cycle calibre de 3240 secondes (30/60/600/2430/120 secondes), anime entre les observations. Elle peut deriver par rapport au jeu.

## Compatibilite de la synchronisation serveur dedie

Aucun paquet serveur n'est plus publie. Le client parle toujours le protocole de synchronisation v6, donc la derniere DLL serveur (0.8) continue de fonctionner avec lui, sans maintenance. Les versions de protocole doivent correspondre exactement : un client v6 ignore les paquets d'une autre version de protocole.

## Installation et mises a jour

Necessite [StarRupture-ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader).

L'archive de release client contient :

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `Plugins/MapExtension/map-data/` (catalogue statique du monde)

Installation :

1. Copier le contenu du dossier `Plugins/` de l'archive dans `StarRupture/Binaries/Win64/ModLoader/Plugins/`. Garder le dossier `MapExtension/` a cote de la DLL : sans lui la carte s'ouvre quand meme, mais les gisements, plantes, creatures et points d'interet manquent.
2. Lancer le jeu, charger une partie et appuyer sur `F8`.

`MapExtension_Plugin.json` est le sidecar de mise a jour du modloader. Son seul champ est `manifest_url`, qui pointe vers le manifest de release `latest/download`. Quand le sidecar est present, le modloader verifie au demarrage s'il existe une version plus recente du plugin et remplace `MapExtension_Plugin.dll` avant de charger les plugins. Installer uniquement la DLL seule desactive les mises a jour automatiques.

Une limite a connaitre :

- La mise a jour automatique ne remplace que la DLL. `Plugins/MapExtension/map-data/` n'est pas mis a jour ; quand une release modifie le catalogue, recopier ce dossier depuis l'archive client.

Les mises a jour automatiques peuvent etre desactivees globalement dans le modloader avec `[AutoUpdate] Enabled=0` dans `modloader.ini`.

### Mise a jour depuis la 0.9

- Remplacer la DLL client ; le dossier `Plugins/MapExtension/` ne change pas.
- La carte continue maintenant de fonctionner apres une mise a jour du jeu, sans attendre une nouvelle version de MapExtension (voir "Apres une mise a jour du jeu" plus haut). Garder le Mod Loader a jour.
- Nouvelle option de diagnostic `[Diagnostics] FieldAudit` (defaut `0`).
- Coop (experimental, pas encore teste dans une vraie partie coop) : nouvelle option `[Coop] ShareMapWithClients` (defaut `0`), activee par l'hote depuis la carte en jeu. L'hote et les joueurs qui rejoignent doivent avoir la meme version de MapExtension. Les retours sont les bienvenus.
- Serveur dedie : StarRupture a arrete le support des serveurs dedies, aucun paquet serveur n'est donc plus publie. Le protocole de synchronisation reste v6 ; la DLL serveur 0.8 continue de fonctionner avec ce client.

### Mise a jour depuis la 0.8

- Remplacer la DLL client ; le dossier `Plugins/MapExtension/` ne change pas.
- Une fenetre s'ouvre maintenant au milieu de l'ecran a chaque partie chargee. Elle indique que la carte s'ouvre en jeu avec la touche configuree, et demande aux joueurs venant d'une version anterieure a la 0.7 de retelecharger le mod sur Nexus Mods. La fermer avec OK ; passer `[InGameMap] ShowPopup=0` pour ne plus l'afficher.
- `[InGameMap] Enabled` est supprime : la carte en jeu est toujours active, et une ancienne valeur `Enabled=0` est ignoree.
- Serveur dedie : le protocole de synchronisation reste v6 et le build serveur ne change pas ; la DLL serveur 0.8 continue de fonctionner avec le client 0.9.

### Mise a jour depuis la 0.7

- Remplacer la DLL client et copier le nouveau dossier `Plugins/MapExtension/` a cote.
- `MapExtensionViewer.html`, `map-tiles/` et le dossier `map-data/` gardes a cote du viewer ne servent plus ; les supprimer.
- La carte en jeu est maintenant activee par defaut. Un `MapExtension_Plugin.ini` existant garde sa propre valeur `[InGameMap] Enabled` : la passer a `1` si elle vaut `0`.
- L'API HTTP locale (`/health`, `/cargo`, `/rupture-cycle`) est supprimee avec le viewer. Une ancienne copie du viewer ne recoit plus de donnees et le reglage `[Http] Port` est ignore ; le plugin n'ouvre aucun port reseau.
- Serveur dedie : le protocole de synchronisation reste v6 ; mettre a jour la DLL serveur en meme temps que le client.

## Etat du projet

Le projet continue d'evoluer ; les retours, corrections et contributions sont les bienvenus.

Le plugin se build contre `StarRupture-Plugin-SDK`.

Exemple :

- `./build.sh client release`

Pour les details de build et de workflow, voir `DEVELOPERS.md`.

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
FieldAudit=0

[Runtime]
RefreshIntervalMs=2000

[Coop]
ShareMapWithClients=0

[InGameMap]
ToggleKey=F8
TileBudgetMiB=64
ShowPopup=1
```

- `Enabled` : active ou desactive le plugin (`1` ou `0`)
- `VerboseLifecycleLogs` : active les logs de cycle de vie (`1` ou `0`)
- `LogRuntimePlanOnce` : loggue la strategie runtime une fois (`1` ou `0`)
- `LogCargoSnapshots` : loggue les snapshots cargo (`1` ou `0`)
- `LogRuptureCycleEvents` : loggue les changements d'etat du cycle de rupture et les evenements monde/serveur lies (`1` ou `0`)
- `LogResourceObservations` : loggue les changements d'etat observes, les resets de generation locale et les changements du nombre d'entrees d'epuisement ambigues (`1` ou `0`). Activer avec `LogRuptureCycleEvents` sur l'autorite solo/hote/serveur pour comparer une recolte et un cycle complet. Ce sont des heures d'observation, pas des heures exactes de recolte ni une attribution a un joueur ; un acteur bref peut disparaitre entre deux captures. Les hooks natifs de recolte demandent encore une validation sur les executables cibles.
- `LogActorScanFallback` : loggue le fallback actor scan (`1` ou `0`)
- `LogRefreshTimings` : loggue les timings par phase de refresh (`1` ou `0`)
- `FieldAudit` : pour le diagnostic apres une mise a jour du jeu. Relit aussi chaque valeur du jeu a l'ancienne (a la position qu'elle avait dans le build du jeu pour lequel le mod a ete compile) et loggue, au chargement puis toutes les 60 s, les valeurs qui ont bouge ou qui different (`1` ou `0`, defaut `0` ; lu au chargement)
- `RefreshIntervalMs` : definit l'intervalle de refresh runtime en millisecondes
- `Coop.ShareMapWithClients` : quand vous hebergez une partie coop, envoie votre carte aux joueurs qui vous rejoignent avec la meme version de MapExtension (defaut `0`). Experimental. Se coche aussi dans la case **Share map with co-op players (experimental)** de la carte en jeu, visible seulement par l'hote
- `InGameMap.ToggleKey` : touche qui ouvre ou ferme ce panneau (defaut `F8`)
- `InGameMap.TileBudgetMiB` : memoire video approximative autorisee pour les tuiles de terrain en pleine resolution (defaut `64` ; `0` garde uniquement l'apercu basse resolution)
- `InGameMap.ShowPopup` : a chaque partie chargee, affiche une fenetre centree, fermee par son bouton OK, qui indique que la carte s'ouvre en jeu avec la touche configuree et que les joueurs venant d'une version anterieure a la 0.7 doivent retelecharger le mod sur Nexus Mods (defaut `1` ; `0` la masque)

## Remerciements

- Merci a `AlienXAXS` pour `StarRupture-ModLoader`, le mod loader qui a permis a `MapExtension_Plugin` de voir le jour :
  `https://github.com/AlienXAXS/StarRupture-ModLoader`
- Merci a `bithoarder` pour `StarRuptureMap`, dont la carte servait de fond a l'ancien viewer web :
  `https://github.com/bithoarder/StarRuptureMap/`
- Ce projet s'inspire aussi de `StarRuptureSaveMap` :
  `https://github.com/thanamatos/StarRuptureSaveMap`
- Merci egalement aux developpeurs de StarRupture pour le jeu.

## Avertissement

Il s'agit d'un outil de modding. Utilisation a vos risques.

Les auteurs ne peuvent pas etre tenus responsables d'eventuels dommages, regressions ou incompatibilites provoques par son utilisation. Tout est fait pour limiter les impacts sur les sauvegardes et la compatibilite future, mais cela ne peut pas etre garanti.
