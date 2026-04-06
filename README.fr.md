# MapExtension_Plugin

README anglais : `README.md`

`MapExtension_Plugin` expose les donnees de carte de StarRupture via un endpoint HTTP local et inclut `mapview`, une interface web locale utilisee pour afficher la carte, les entites, leurs connexions et la timeline du cycle de rupture.

Le plugin fonctionne aussi bien en partie solo qu'en multijoueur, mais il n'a besoin d'etre installe que cote client. Pour recuperer le cycle de rupture sur serveur dedie, il faut l'associer a `RuptureCycleToChat_Plugin` cote serveur.

## Fonctionnalites

- Voir quels `Cargo Dispatchers` sont relies a quels `Cargo Receivers`, et inversement
- Afficher leurs positions directement sur la carte
- Voir les objets actuellement transportes dans le reseau
- Afficher la position des `teleporteurs`
- Afficher la position des `joueurs`
- Exposer `GET /health`, `GET /cargo` et `GET /rupture-cycle` sur le serveur HTTP local
- Reconstruire l'etat du cycle de rupture a partir des messages de chat serveur quand `RuptureCycleToChat_Plugin` est present
- Utiliser un fallback local via `UCrEnviroWaveSubsystem` en solo/local quand aucun message de chat serveur n'est disponible

## Mapview

Le `mapview` inclus est une interface web locale autonome concue pour lire les donnees du plugin et les afficher dans un navigateur en ouvrant le fichier genere `dist/MapExtensionViewer.html`.

Il consomme a la fois les donnees de carte/cargo et l'endpoint de cycle de rupture pour afficher la timeline de l'interface.

## Choix d'interface

Le projet a d'abord vise une integration plus directe dans l'interface du jeu.

En pratique, l'UI de StarRupture s'est revelee trop limitee pour obtenir un resultat fiable et maintenable dans de bonnes conditions. Le projet a donc bascule vers une interface web locale.

Ce n'est pas forcement la solution ideale sur le plan de l'integration, mais c'est aujourd'hui l'approche la plus efficace pour iterer rapidement, afficher les donnees correctement et garder un outil reellement utilisable.

## Etat du projet

Il s'agit d'une premiere ebauche fonctionnelle.

Le projet va continuer a evoluer prochainement, mais les retours, corrections et contributions sont les bienvenus.

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
LogRuptureCycleChat=0
LogActorScanFallback=0

[Http]
Port=9000

[Runtime]
RefreshIntervalMs=500

[Chat]
RuptureCyclePrefix=[RUPTURE_CYCLE]
```

- `Enabled` : active ou desactive le plugin (`1` ou `0`)
- `VerboseLifecycleLogs` : active les logs de cycle de vie (`1` ou `0`)
- `LogRuntimePlanOnce` : loggue la strategie runtime une fois (`1` ou `0`)
- `LogCargoSnapshots` : loggue les snapshots cargo (`1` ou `0`)
- `LogRuptureCycleChat` : loggue l'etat du cycle de rupture parse depuis le chat serveur ou recupere localement en solo (`1` ou `0`)
- `LogActorScanFallback` : loggue le fallback actor scan (`1` ou `0`)
- `Port` : definit le port HTTP local utilise par le plugin
- `RefreshIntervalMs` : definit l'intervalle de refresh runtime en millisecondes
- `RuptureCyclePrefix` : prefixe de chat attendu depuis le plugin serveur du cycle de rupture

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
