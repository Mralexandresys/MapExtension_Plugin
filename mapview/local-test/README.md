# Local test

Mock API locale pour `mapview`.

## Lancement

Depuis `MapExtension_Plugin/mapview` :

```bash
pnpm run mock-api
```

Le serveur ecoute par defaut sur `http://127.0.0.1:9000`. Utiliser l'endpoint suivant
dans l'UI :

```text
127.0.0.1:9000
```

Endpoints exposes :

- `/health`
- `/cargo`
- `/rupture-cycle`

## Payloads internes et scenarios POI

Sans override, le mock fournit :

- un `/health` compatible avec la version de contrat du viewer, sans afficher la
  pop-up de mise a jour ;
- un `/rupture-cycle` sans donnees live, utile pour verifier l'etat vide et les vues
  detaillee/compacte ;
- un `/cargo` avec deux joueurs (`MockSelf` avec `self: true`, rendu dans la couleur
  « moi », et `MockAlly`) et quatre POI visibles dans une zone rapprochee de la carte :
  - une base abandonnee, rendue avec l'icone de batiment fissure ;
  - une ressource `Gold Fruit` `available`, rendue avec un point plein ;
  - une ressource `Gold Fruit` `depleted`, rendue attenuee et en contour ;
  - une ressource `Plant Fiber` `available`, qui exerce une autre couleur de la
    palette.

Les deux exemples `Gold Fruit` doivent conserver la meme couleur malgre leur etat,
tandis que `Plant Fiber` permet de verifier la palette stable par ressource. Survoler
un POI ou lui donner le focus au clavier affiche son type, son nom, sa ressource et son
etat.

Pour verifier les filtres :

1. Le mode `Reseau` affiche la base abandonnee et les trois ressources.
2. Les boutons `Bases abandonnees` et `Ressources vegetales` les masquent ou les
   restaurent independamment.
3. Le mode `Ressources` conserve seulement les trois ressources vegetales.
4. Les modes `Teleporteurs` et `Joueurs` masquent tous les POI.
5. `Afficher uniquement mes marqueurs et zones` masque les POI et toutes les autres
   entites du plugin pour ne laisser que les annotations utilisateur.

## Overrides de payload

Creer le dossier `local-test/data/` s'il n'existe pas, puis y deposer un ou plusieurs
fichiers JSON non vides :

- `health.json`
- `cargo.json`
- `rupture-cycle.json` ou `rupture_cycle.json`

Chaque fichier remplace entierement le payload interne de l'endpoint correspondant ;
il n'est pas fusionne avec les valeurs par defaut. Un fichier absent ou vide laisse le
mock utiliser son payload interne. Les fichiers sont relus a chaque requete : apres une
modification, utiliser le refresh du viewer suffit. Relancer `pnpm run mock-api` permet
aussi de voir dans le terminal quels overrides sont detectes au demarrage.

Ce mecanisme permet notamment de tester d'autres positions ou ressources, un cycle de
rupture live et une version de contrat superieure dans `/health`.

## Tester un ancien payload `/cargo` sans POI

Pour verifier la retrocompatibilite avec un plugin anterieur aux POI (et au champ
`self` des joueurs), copier la fixture fournie :

```bash
cp examples/cargo-legacy-no-pois.json data/cargo.json
```

Cette fixture omet volontairement le tableau `pois`, les compteurs
`counts.pois`/`counts.abandoned_bases`/`counts.plant_resources` et le champ `self`
des joueurs, comme le ferait un plugin plus ancien.

Forcer ensuite un refresh du viewer. Le resultat attendu est une carte toujours
utilisable, aucun POI rendu et des compteurs POI a zero, sans exiger les champs
`pois`, `counts.pois`, `counts.abandoned_bases` ou `counts.plant_resources`.

Supprimer `local-test/data/cargo.json`, puis rafraichir le viewer, restaure le scenario
interne avec les quatre POI.
