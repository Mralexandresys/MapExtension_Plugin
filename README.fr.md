# MapExtension_Plugin

README anglais : `README.md`

`MapExtension_Plugin` expose les donnees de carte de StarRupture via un endpoint HTTP local et inclut `mapview`, une interface web locale utilisee pour afficher la carte, les entites, leurs connexions et la timeline du cycle de rupture.

Le plugin fonctionne aussi bien en partie solo qu'en multijoueur. En solo/local, seul le build client est necessaire. Sur serveur dedie, il faut installer le build client sur la machine du joueur et le build serveur sur le serveur dedie afin que le serveur puisse envoyer les donnees de carte et de cycle de rupture aux clients connectes.

## Fonctionnalites

- Voir quels `Cargo Dispatchers` sont relies a quels `Cargo Receivers`, et inversement
- Afficher leurs positions directement sur la carte
- Voir les objets actuellement transportes dans le reseau
- Afficher la position des `teleporteurs`
- Afficher la position des `joueurs`
- Exposer `GET /health`, `GET /cargo` et `GET /rupture-cycle` sur le serveur HTTP local
- Recevoir les snapshots de carte et de cycle de rupture envoyes par la build serveur en session serveur dediee
- Utiliser un fallback local via `UCrEnviroWaveSubsystem` en solo/local quand aucun snapshot serveur n'est disponible

## Mapview

Le `mapview` inclus est une interface web locale concue pour lire les donnees du plugin et les afficher dans un navigateur en ouvrant le fichier genere `dist/MapExtensionViewer.html`.

Une fois packagee, il faut garder le dossier genere `map-tiles/` a cote de `MapExtensionViewer.html` ; l'interface charge le fond de carte depuis ces tuiles.

Il consomme a la fois les donnees de carte/cargo et l'endpoint de cycle de rupture pour afficher la timeline de l'interface.

## Installation et mises a jour

L'archive de release client contient :

- `Plugins/MapExtension_Plugin.dll`
- `Plugins/MapExtension_Plugin.json`
- `MapExtensionViewer.html`
- `map-tiles/`

Copier le contenu de `Plugins/` dans `StarRupture/Binaries/Win64/Plugins/`, puis garder `MapExtensionViewer.html` a cote de son dossier `map-tiles/`, n'importe ou sur la machine.

`MapExtension_Plugin.json` est le sidecar de mise a jour du modloader. Son seul champ est `manifest_url`, qui pointe vers le manifest de release `latest/download`. Quand le sidecar est present, le modloader verifie au demarrage s'il existe une version plus recente du plugin et remplace `MapExtension_Plugin.dll` avant de charger le moindre plugin. Installer uniquement la DLL seule desactive les mises a jour automatiques.

Deux limites a connaitre :

- La mise a jour automatique ne remplace que la DLL. `MapExtensionViewer.html` et `map-tiles/` ne sont jamais touches, puisqu'ils vivent hors du dossier du jeu. L'interface le detecte d'elle-meme : quand le plugin annonce un contrat de payload plus recent que celui avec lequel l'interface locale a ete construite, une fenetre propose le telechargement direct de l'asset `MapExtension_Plugin-<tag>-viewer.zip` correspondant, avec les liens vers la release GitHub et la page du mod. Remplacer `MapExtensionViewer.html` et `map-tiles/` ensemble, puis recharger la page.
- Le build serveur n'est pas couvert par le sidecar. Il faut le mettre a jour a la main et le garder sur la meme version que le client, les deux cotes partageant `shared/map_sync_protocol.h`.

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

- `./build_client.sh release`

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
