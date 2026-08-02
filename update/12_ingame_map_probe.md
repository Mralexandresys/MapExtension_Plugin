# POC de probe pour la migration de la carte in-game

## Objectif

Ce POC transforme la recommandation de
`update/11_ingame_mapview_feasibility.md` en un test runtime client-only. Il
doit répondre à la première question bloquante avant toute migration de
`mapview` : les données terrain natives et au moins une texture de segment
sont-elles accessibles, copiables et stables hors du widget UMG de la carte ?

Le viewer navigateur reste l'interface principale. Le POC n'altère pas la
carte native, ne change aucun snapshot, endpoint HTTP ou paquet réseau et ne
compile aucun code UI dans le build serveur.

## Périmètre implémenté

Le module `client/ingame_map_probe.cpp` :

1. enregistre un panneau ModLoader `Map probe` quand le probe est activé ;
2. inventorie les instances et CDO de :
   - `CrMapMenuDevSettings` ;
   - `CrMapMenuTerrainData` ;
   - `CrMapManuSubsystem` ;
   - `WBP_MapMenuTerrain_C` ;
   - `WBP_MapMenu_C` ;
   - `WorldPartitionMiniMap` ;
3. lit le CDO de `UCrMapMenuDevSettings` et distingue une soft-reference
   `TerrainData` déjà résolue d'un asset absent de `GObjects` ;
4. si l'asset a été collecté dans `ChimeraMain`, le recharge sur le game thread
   avec `UKismetSystemLibrary::LoadAsset_Blocking` et mesure ce chargement ;
5. inspecte le `UCrMapMenuTerrainData` obtenu : pivot, taille monde d'un
   segment, règles de mipmap, grille et brushes normaux/radiation ;
6. journalise le nom, la classe, la taille du brush et le type concret de
   chaque `ResourceObject` ;
7. choisit le premier brush normal dont la ressource est un `UTexture2D`, ou
   une ressource `radiation2` en fallback ;
8. copie exactement cette texture avec `LoadFromUTexture2D` sur le game
   thread et effectue un second essai après 500 ms si son resource D3D12 est
   encore en streaming ;
9. affiche le handle obtenu dans le panneau ImGui sur le render thread ;
10. mesure le temps d'inventaire/inspection, le chargement bloquant de l'asset,
    le temps de copie GPU, la durée du callback de rendu et une estimation RGBA
    du niveau de base ;
11. retire le handle du rendu lors de la fin de monde et libère les copies GPU
    de façon différée ou au shutdown.

Le chargement bloquant ne s'exécute que dans `ChimeraMain`. Aucun pointeur
`UObject` n'est conservé après le probe : seul le handle de la copie GPU gérée
par le ModLoader traverse les frames.

`AWorldPartitionMiniMap` est seulement inspecté comme source alternative de
bornes et de texture. Sa texture n'est pas utilisée par ce POC.

## Activation

Le probe est désactivé par défaut. Dans
`Plugins/config/MapExtension_Plugin.ini` :

```ini
[Experimental]
InGameMapProbe=1
```

Redémarrer ensuite le jeu. L'option n'est pas appliquée à chaud.

Le panneau est enregistré sous le libellé `Map probe` dans l'interface du
ModLoader. Les observations détaillées sont aussi écrites dans
`modloader.log` avec le préfixe `In-game map probe`.

## Protocole de test runtime

### 1. Référence avant ouverture de la carte

1. Entrer dans un monde `ChimeraMain`.
2. Ne pas ouvrir la carte native.
3. Ouvrir le panneau `Map probe`.
4. Cliquer sur `Run inventory + one-segment probe`.
5. Conserver les lignes d'inventaire, le statut, les champs `already resolved`
   et `blocking load`, ainsi que les timings.

Cette passe détermine si le data asset est déjà vivant ou s'il doit être
résolu depuis la soft-reference configurée. Un inventaire à zéro suivi de
`blocking load: attempted=yes, succeeded=yes` est valide : il prouve que
l'asset peut être chargé indépendamment du widget UMG vivant.

### 2. Passe après ouverture de la carte

1. Ouvrir puis fermer normalement la carte native du jeu.
2. Revenir au panneau `Map probe`.
3. Relancer le probe.
4. Comparer notamment :
   - les instances `WBP_MapMenuTerrain_C` et `WBP_MapMenu_C` ;
   - la résolution préalable de `TerrainData` et l'utilisation éventuelle du
     chargement bloquant ;
   - le nombre de `ResourceObject` non nuls ;
   - le nombre de ressources reconnues comme `UTexture2D` ;
   - le succès et la durée de `LoadFromUTexture2D`.

Un succès uniquement après ouverture de la carte indique une dépendance de
chargement à étudier. Un succès avant ouverture, y compris via la
soft-reference configurée, est un signal plus fort en faveur d'une carte
ImGui indépendante. Si le premier `LoadFromUTexture2D` rencontre une ressource
en streaming, attendre le second essai automatique avant de conclure.

### 3. Changement de monde

1. Avec une preview visible, quitter le monde vers le menu.
2. Vérifier l'absence de crash et la disparition de la preview.
3. Recharger un monde.
4. Relancer le probe et vérifier qu'une nouvelle copie est affichée.
5. Répéter au moins une fois avec DLSS/Streamline actif si cette configuration
   est utilisée habituellement.

### 4. Solo et serveur dédié

Le rendu est toujours local au client. Exécuter la matrice précédente :

- dans une partie solo ou locale ;
- sur un client connecté à un serveur dédié.

Aucun module serveur n'est requis pour le probe et aucun nouveau transport de
données n'est ajouté.

## Éléments de preuve à conserver

Pour chaque passe, conserver :

- le build/tag du plugin et la version du jeu ;
- la résolution, le ratio d'écran, le mode DLSS/FSR et l'UI scale ;
- le bloc `In-game map probe inventory` ;
- le bloc `In-game map probe terrain` et les lignes de segments ;
- la ligne `configured TerrainData blocking load` lorsqu'elle apparaît ;
- la ligne `texture copy succeeded` ou l'erreur exacte ;
- une capture du segment affiché ;
- les temps d'inspection, de copie GPU et du callback de rendu ;
- le comportement lors d'une fermeture/réouverture du monde.

Le POC ne peut pas conclure à lui seul que la projection est correcte : il
collecte les paramètres natifs nécessaires au jalon suivant.

## Critère de passage au jalon projection

Passer à une calibration de projection si les observations runtime montrent
que :

- au moins un `FCrTerrainSegmentData` expose un `ResourceObject` de type
  `UTexture2D` ;
- la copie GPU réussit de manière répétable ;
- le segment rendu correspond visuellement au terrain attendu ;
- la durée de copie ne provoque pas de freeze inacceptable ;
- une sortie et une nouvelle entrée dans le monde restent stables ;
- les assets ne dépendent pas en permanence d'une instance UMG fragile.

Le POC suivant, décrit dans `update/13_ingame_map_grid_poc.md`, assemble une
grille basse résolution. Après sa validation visuelle, la calibration devra
comparer au moins cinq landmarks entre :

1. la projection actuelle de MapExtension ;
2. une transformation affine générale ;
3. la transformation dérivée de `MapTerrainTopLeftPivotPoint`,
   `MapTerrainSegmentSize` et `TerrainSegmentGridIndex`.

Il devra afficher l'erreur en pixels et en unités monde avant l'ajout des
joueurs, téléporteurs, connexions, annotations ou filtres.

## Conditions d'arrêt

Ne pas engager le port complet si :

- tous les brushes utilisent des matériaux ou render targets non copiables ;
- les textures n'existent que pendant une courte fenêtre UMG ;
- les copies GPU échouent ou provoquent des freezes visibles ;
- les changements de monde rendent le cache instable ;
- les résultats changent de façon non reproductible entre deux ouvertures de
  carte.

Dans ce cas, conserver le viewer web et étudier soit les assets externes, soit
un overlay beaucoup plus limité.

## Première observation runtime

Une exécution client avec ModLoader `v1.16.1`, jeu
`++Earth20+Release-CU1-CL-121391` et Streamline actif a fourni les résultats
suivants :

- `MapMenuTerrainData` expose 36 segments sur une grille `(0,0)-(5,5)` ;
- les 36 brushes normaux et les 36 brushes `radiation2` référencent tous des
  `UTexture2D` ;
- les brushes annoncent `2048 x 2048` ; la copie du mip alors résident a donné
  une texture `64 x 64` ;
- le pivot terrain observé est `(-480000, -380000, 0)` et la taille monde
  déclarée d'un segment est `(1000, 1000, 0)` ;
- un premier essai a échoué pendant le streaming D3D12, puis deux copies ont
  réussi en `26.316 ms` et `31.974 ms` ;
- après le GC, l'inventaire est passé de une instance de
  `CrMapMenuTerrainData` à zéro, ce qui expliquait l'erreur du bouton manuel ;
- une sortie puis une nouvelle entrée dans `ChimeraMain` n'a provoqué aucun
  crash et une nouvelle copie a réussi.

Une seconde exécution avec le fallback corrigé a ensuite confirmé :

- une copie directe au world begin play en `38.417 ms`, avant qu'une instance
  `WBP_MapMenu_C` soit présente ;
- deux rechargements réussis après GC alors que l'inventaire indiquait
  `CrMapMenuTerrainData instances=0`, en `11.290 ms` puis `2.606 ms` ;
- une copie GPU réussie après chacun de ces rechargements, en `29.850 ms` puis
  `31.081 ms` ;
- une nouvelle copie en `28.982 ms` lorsque l'asset était à nouveau déjà
  résolu ;
- plusieurs exécutions manuelles et un arrêt moteur propres, sans erreur du
  probe ni crash.

Ces résultats lèvent le blocage principal sur le type des ressources et
valident le fallback de chargement de la soft-reference. Ils ne valident pas
encore l'assemblage visuel à pleine résolution ni la projection. Le retry GPU
après 500 ms n'a pas été déclenché pendant la seconde exécution, toutes les
copies ayant réussi au premier essai.

## État de validation

Le module et sa garde client-only sont validés par compilation client. Le
chargement après GC, les copies GPU répétées et l'arrêt moteur sont maintenant
validés en jeu avec Streamline actif. L'assemblage progressif de la grille est
implémenté dans `update/13_ingame_map_grid_poc.md` et attend sa validation
runtime avant la calibration de projection. Le comportement du retry différé
restera à confirmer lors d'une future occurrence de streaming.
