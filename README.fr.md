# MapExtension_Plugin

README anglais : `README.md`

`MapExtension_Plugin` expose les donnees de carte de StarRupture via un endpoint HTTP local et s'accompagne de `mapview`, une interface web locale pour les visualiser.

Le plugin fonctionne aussi bien en partie solo qu'en partie sur serveur.

Il n'a besoin d'etre installe que cote client. Aucune installation serveur n'est necessaire : les informations exploitees par le mod sont deja presentes sur le client.

## Ce que fait le plugin

- capture les `Package Sender` et `Package Receiver` depuis les donnees runtime,
- projette les cargos, teleporteurs et joueurs en coordonnees de carte,
- publie le snapshot courant via `GET /health` et `GET /cargo`.

## Ce que fait mapview

`mapview/` est l'interface standalone associee au plugin.

- elle lit les donnees exposees par le plugin,
- elle affiche la carte, les entites et leurs connexions,
- elle produit un build autonome en un seul fichier HTML.

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
LogActorScanFallback=0

[Http]
Port=9000

[Runtime]
RefreshIntervalMs=500
```

- `Enabled` : active ou desactive le plugin (`1` ou `0`)
- `VerboseLifecycleLogs` : active les logs de cycle de vie (`1` ou `0`)
- `LogRuntimePlanOnce` : loggue la strategie runtime une fois (`1` ou `0`)
- `LogCargoSnapshots` : loggue les snapshots cargo (`1` ou `0`)
- `LogActorScanFallback` : loggue le fallback actor scan (`1` ou `0`)
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
