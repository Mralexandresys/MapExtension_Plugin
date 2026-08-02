# Rapport d'analyse du rendu de la carte native in-game

## Objet

Ce rapport consolide les résultats du POC d'assemblage décrit dans
`update/13_ingame_map_grid_poc.md` et analyse les problèmes observés pendant sa
validation dans StarRupture :

- orientation de la grille ;
- sélection de la mauvaise variante de terrain ;
- absence apparente de `Terrain_R2_02_05` ;
- pixellisation pendant le zoom ;
- conflit entre le zoom à la molette et le scroll du panneau ImGui.

Le document évalue si chaque problème est résoluble, présente les solutions
possibles et recommande un ordre d'investigation. Il ne constitue pas une
implémentation et ne modifie aucun contrat HTTP, snapshot ou protocole réseau.
Le périmètre reste strictement client-only.

## Résumé exécutif

La validation runtime confirme que la carte native peut être récupérée et
assemblée, mais révèle quatre écarts entre le POC et le comportement attendu :

1. la grille doit être transposée pour produire une carte cohérente ;
2. le POC assemble la variante R1 alors que la carte actuellement utilisée par
   le jeu est la variante R2 ;
3. la case R2 `(2,5)` référence la même ressource que `(1,5)` et
   `Terrain_R2_02_05` n'est pas visible dans les objets chargés ;
4. les copies GPU observées ne font que `64 x 64` pixels par tuile malgré un
   brush logique de `2048 x 2048`, ce qui explique la pixellisation au zoom ;
5. `GetMouseWheel()` expose le delta ImGui sans le consommer, de sorte que la
   carte zoome pendant que le panneau parent défile.

Aucun de ces constats ne remet en cause la faisabilité générale d'une carte
ImGui native. La sélection de R2 et la transposition sont simples à résoudre.
La qualité peut être améliorée avec une stratégie explicite de mipmaps. Le
conflit de molette est résoluble, mais sa correction propre nécessitera
probablement une fenêtre non scrollable ou une extension de l'API ModLoader.
La seule inconnue qui peut empêcher une restitution parfaitement fidèle est
la tuile R2 `(2,5)` si ses pixels sont réellement absents du build du jeu.

## Matrice de résolvabilité

| Problème | Résoluble | Confiance | Commentaire |
| --- | --- | --- | --- |
| Carte R1 au lieu de R2 | Oui | Élevée | Les deux brushes sont déjà exposés par `FCrTerrainSegmentData`. |
| Transposition de la grille | Oui | Élevée | Le comportement correct a été confirmé visuellement. |
| `Terrain_R2_02_05` manquante | Probablement | Moyenne | Vérifier d'abord `UVRegion` et le brush UMG vivant. |
| Pixellisation au zoom | Oui | Élevée | La copie `64 x 64` est la cause principale observée. |
| Scroll du panneau pendant le zoom | Oui | Élevée | La solution propre peut demander une évolution du SDK. |
| Projection monde-vers-carte | Pas encore évaluée | — | À calibrer après stabilisation du terrain et de son orientation. |

## État des données terrain

### Variantes R1 et R2

`FCrTerrainSegmentData` expose deux brushes :

```text
TerrainSegmentTexture
TerrainSegmentTextureRadiation2
```

Les observations runtime montrent que :

- `TerrainSegmentTexture` référence des ressources nommées `Terrain_R1_*` ;
- `TerrainSegmentTextureRadiation2` référence des ressources nommées
  `Terrain_R2_*` ;
- les deux variantes contiennent 36 références `UTexture2D` non nulles ;
- la carte visuellement utilisée par le jeu pendant le test correspond à R2.

Le suffixe R2 ne doit toutefois pas être interprété uniquement à partir du nom
de fichier. Le SDK désigne ce brush comme la variante `Radiation2`, ce qui peut
indiquer une sélection dépendante d'un état runtime. Avant de figer R2 comme
variante permanente, il faudra vérifier si le widget natif alterne R1/R2 selon
le monde, la radiation ou le cycle de rupture.

### Cause de l'affichage de R1 dans le POC

L'inventaire du probe inspecte les deux brushes, mais l'assemblage progressif
sélectionne actuellement le brush normal `TerrainSegmentTexture`. Le résultat
R1 est donc cohérent avec le chemin de données choisi par le POC et ne provient
pas d'une erreur de copie D3D12.

### Transposition de la grille

La carte devient cohérente après activation de la transposition. Le mapping
empirique est donc de la forme :

```text
colonne du canvas <- TerrainSegmentGridIndex.Y
ligne du canvas   <- TerrainSegmentGridIndex.X
```

Cette permutation doit être considérée comme une partie de la transformation
native, pas comme une correction visuelle arbitraire. La future calibration
devra encore déterminer :

- si l'axe horizontal doit être inversé ;
- si l'axe vertical doit être inversé ;
- comment les axes de grille transposés correspondent aux axes monde X/Y ;
- si le pivot est le coin d'une case, son centre ou le coin de toute la grille.

La transposition devra être validée avec plusieurs landmarks avant de devenir
une constante définitive.

## Analyse de `Terrain_R2_02_05`

### Observation runtime

Les logs du premier probe montraient :

```text
grid=(1,5) radiation_name=Terrain_R2_01_05
grid=(2,5) radiation_name=Terrain_R2_01_05
```

La case `(2,5)` contient donc une référence non nulle, mais cette référence est
identique à celle de `(1,5)`. Le compteur `radiation_texture2d=36` mesure le
nombre de brushes qui pointent vers un `UTexture2D`, pas le nombre de textures
uniques.

### Hypothèse 1 — région UV différente

`FSlateBrush` contient plus d'informations que le seul `ResourceObject` :

- `UVRegion` ;
- `Tiling` ;
- `Mirroring` ;
- `DrawAs` ;
- `ImageType` ;
- `ImageSize` ;
- `TintColor`.

Deux brushes peuvent donc partager le même `UTexture2D` tout en affichant des
régions différentes. Le POC dessine actuellement chaque handle avec les UV
complets `(0,0)-(1,1)` et ne reproduit pas les autres propriétés du brush.

Avant de déclarer l'asset manquant, il faut comparer pour les cases `(1,5)` et
`(2,5)` :

```text
UVRegion.bIsValid
UVRegion.Min
UVRegion.Max
Mirroring
Tiling
DrawAs
ImageSize
ResourceObject
```

Si les régions UV diffèrent, la ressource dupliquée est probablement
intentionnelle et la correction consiste à respecter les métadonnées du
brush.

### Hypothèse 2 — remplacement runtime par UMG

Le data asset peut être transformé par le widget natif. Chaque
`UCrUW_MapMenuTerrainSegment` possède un `ImageTerrain`, dont le brush vivant
peut différer du brush stocké dans `UCrMapMenuTerrainData`.

Il faut comparer après ouverture de la carte native :

1. le brush R2 du data asset ;
2. le brush de `ImageTerrain` pour la case `(2,5)` ;
3. la ressource, les UV et le mirroring réellement appliqués.

Une différence indiquerait que le Blueprint ou une fonction native complète
les données au runtime.

### Hypothèse 3 — asset réellement absent ou mal référencé

Si les deux brushes ont exactement les mêmes métadonnées et si le widget
vivant ne les modifie pas, le data asset contient vraisemblablement une
référence dupliquée ou un fallback.

L'absence dans `ObjectWalker` ne suffit pas à prouver l'absence du fichier :
`ObjectWalker` ne voit que les objets chargés dans `GObjects`. La vérification
doit également couvrir :

- l'Asset Registry ;
- la liste des packages du `.pak` ;
- les soft references ;
- les objets chargés après ouverture de la carte native ;
- les variantes de nom ou de package autour de `Terrain_R2_02_05`.

### Solutions de fallback si l'asset n'existe pas

| Solution | Avantage | Inconvénient |
| --- | --- | --- |
| Suivre le brush natif dupliqué | Fidèle au data asset | Affiche potentiellement une mauvaise tuile. |
| Laisser la case vide | Rend l'anomalie explicite | Carte visuellement incomplète. |
| Utiliser `Terrain_R1_02_05` | Conserve la géographie | Mélange les styles R1 et R2. |
| Résoudre R2 par convention de nom | Peut retrouver un asset mal référencé | La convention n'est pas un contrat stable. |
| Fournir une texture externe | Résultat contrôlé | Maintenance, packaging et licence. |
| Reconstituer depuis les voisines | Masque le trou | Produit des pixels inventés. |

La stratégie recommandée est de reproduire d'abord le brush natif complet,
UV inclus. Un fallback ne doit être choisi qu'après avoir prouvé que la donnée
correcte n'existe pas.

## Analyse de la pixellisation

### Preuve principale

Les mesures runtime montrent simultanément :

```text
brush=(2048.0,2048.0)
texture=64x64
```

`ImageSize=2048 x 2048` est une taille logique Slate. Elle ne garantit pas que
la ressource GPU possède actuellement son mip maximal. Le handle produit par
`LoadFromUTexture2D` mesure réellement `64 x 64` dans les observations.

### Effet du canvas

Le canvas ne détruit pas une image haute résolution. `DL_AddImage` dessine un
quad et laisse le GPU échantillonner la texture. En revanche, il agrandit une
copie déjà très petite :

- à l'échelle ajustée, une tuile `64 x 64` est déjà souvent affichée sur plus
  de 64 pixels ;
- lors d'un zoom important, chaque pixel source couvre plusieurs pixels écran ;
- au zoom maximal du POC, l'agrandissement peut approcher un facteur 16.

La méthode d'affichage rend donc la faiblesse de la source visible, mais la
cause première est le mip résident copié.

### Copie indépendante du streaming Unreal

`LoadFromUTexture2D` crée une ressource appartenant au ModLoader. Après la
copie :

- le source Unreal peut charger d'autres mips ;
- le source peut être collecté par le GC ;
- le handle copié reste à sa résolution initiale ;
- zoomer dans ImGui ne demande aucun nouveau mip à Unreal.

Une texture copiée en `64 x 64` ne devient donc jamais automatiquement une
texture `2048 x 2048`.

### Facteurs secondaires

- Le sampler D3D12 peut utiliser un filtrage point ou linéaire.
- Un filtrage linéaire réduit l'aspect en blocs, mais ajoute du flou et ne crée
  aucun détail.
- La compression de texture devient visible lors d'un fort agrandissement.
- Une `UVRegion` ignorée peut étirer une sous-image incorrecte.
- Les jointures entre tuiles peuvent souffrir de filtrage de bord ou d'un
  manque d'inset demi-texel.
- Le DPI de l'interface influence la taille affichée, mais n'explique pas une
  copie source de `64 x 64`.

## Options pour améliorer la qualité

### Option A — forcer les mips Unreal à résider

`UTexture2D` hérite de `UStreamableRenderAsset`, et le SDK expose :

```text
SetForceMipLevelsToBeResident
```

Une stratégie possible serait :

1. résoudre les textures R2 ;
2. demander la résidence des mips pendant une durée limitée ;
3. attendre plusieurs ticks la fin du streaming ;
4. vérifier la taille réellement disponible ;
5. copier la texture ;
6. relâcher la contrainte de résidence.

Avantages :

- assets natifs ;
- qualité maximale disponible ;
- indépendance vis-à-vis de l'ouverture manuelle de la carte.

Inconvénients :

- consommation du pool de streaming ;
- stutters possibles ;
- absence de garantie que l'appel permette de choisir un mip intermédiaire ;
- risque de charger les 36 textures en pleine résolution.

### Option B — copier un mip explicitement choisi

La cible recommandée est `512 x 512` par tuile. L'API actuelle
`LoadFromUTexture2D(texture, name)` ne permet pas de choisir un mip.

Une évolution du bridge ModLoader pourrait accepter :

- un index de mip ;
- une résolution cible ;
- un mode de filtrage de réduction.

Avantages : budget déterministe et bonne qualité. Inconvénient : évolution de
l'interface ModLoader nécessaire.

### Option C — downsampling GPU

Une texture haute résolution peut être chargée temporairement puis réduite
vers une ressource `512 x 512` via un blit GPU ou un render target.

Avantages : qualité et mémoire contrôlées. Inconvénients : complexité D3D12,
synchronisation avec Streamline et gestion de ressources intermédiaires.

### Option D — niveaux de détail dynamiques

Conserver une grille basse résolution et charger des tuiles détaillées
uniquement autour de la zone visible à fort zoom.

Avantages : bonne qualité locale et mémoire contenue. Inconvénients : cache,
éviction, transitions et logique de streaming supplémentaires.

### Option E — limiter le zoom

Limiter l'agrandissement au ratio proche de un texel pour un pixel écran évite
la pixellisation extrême.

Avantage : solution simple et sûre. Inconvénient : aucun détail supplémentaire
n'est disponible.

### Budget mémoire indicatif

Estimation RGBA sans overhead GPU :

| Résolution d'une tuile | Mémoire pour 36 tuiles |
| --- | ---: |
| `64 x 64` | `0,56 MiB` |
| `256 x 256` | `9 MiB` |
| `512 x 512` | `36 MiB` |
| `1024 x 1024` | `144 MiB` |
| `2048 x 2048` | `576 MiB` |

La cible `512 x 512` offre le meilleur compromis initial : carte effective de
`3072 x 3072`, zoom raisonnable et environ `36 MiB` RGBA.

## Analyse du conflit de molette

### Cause

Le SDK v52 expose :

```text
GetMouseWheel()
GetMouseWheelH()
```

Ces fonctions retournent le delta de la frame directement depuis `ImGuiIO`.
Elles ne consomment pas l'événement et n'en attribuent pas la propriété au
canvas.

Le déroulement probable est :

1. ImGui reçoit `WM_MOUSEWHEEL` ;
2. la fenêtre ou le panneau parent applique son scroll ;
3. le callback du plugin lit le même delta ;
4. la carte applique son zoom.

Le même événement produit donc les deux effets.

### Pourquoi l'`InvisibleButton` ne suffit pas

L'`InvisibleButton` permet de détecter le survol, les clics et le drag. La
molette n'est cependant pas un bouton de souris et l'item ne devient pas
automatiquement propriétaire de `MouseWheelY`.

`WantCaptureMouse` empêche principalement le jeu de traiter la souris. Il
n'empêche pas ImGui de faire défiler ses propres fenêtres.

### Limites de l'interface actuelle

L'interface disponible n'expose pas directement :

- `ConsumeMouseWheel` ;
- la propriété de `ImGuiKey_MouseWheelY` ;
- `SetItemKeyOwner` ;
- un `BeginChild` acceptant `NoScrollWithMouse`.

Le plugin peut lire la molette, mais pas proprement empêcher le panneau parent
de l'utiliser.

## Options pour corriger la molette

### Option A — consommation ou ownership dans le ModLoader

Ajouter une primitive qui retourne le delta et réserve la molette au canvas
survolé.

Avantages : comportement ImGui correct, pas de jitter, gestion fiable des
fenêtres imbriquées. Inconvénient : nouvelle version d'interface nécessaire.

### Option B — fenêtre dédiée non scrollable

Déplacer la carte dans un widget/fenêtre séparé avec un comportement équivalent
à `NoScrollWithMouse`.

Avantages : séparation nette entre carte et diagnostics, ergonomie adaptée à
une future UI. Inconvénients : le flag n'est pas explicitement exposé pour les
panneaux actuels et `NoScrollbar` seul peut ne pas bloquer la propagation de la
molette.

### Option C — child avec flags

Étendre `BeginChild` pour accepter des flags, notamment `NoScrollWithMouse`.
La signature actuellement exposée ne reçoit que l'identifiant, la taille et le
booléen de bordure.

### Option D — restaurer le scroll parent

Mémoriser `GetScrollY()` et appeler `SetScrollY()` lorsque la molette est
utilisée au-dessus du canvas.

Avantage : réalisable avec l'API actuelle. Inconvénients : workaround fragile,
risque de saut visuel, timing dépendant d'ImGui et mauvaise gestion possible des
scrolls imbriqués.

### Option E — contrôles de zoom explicites

Utiliser temporairement un slider et des boutons `+`/`-`.

Avantages : fiable immédiatement et aucun conflit. Inconvénient : interaction
moins naturelle.

La solution recommandée pour une interface durable est une fenêtre de carte
non scrollable ou une extension SDK permettant de consommer la molette.

## Vérifications techniques prioritaires

### Brushes R2

Pour chaque segment, et en priorité `(1,5)` et `(2,5)`, relever :

- nom et adresse du `ResourceObject` ;
- `UVRegion.bIsValid`, Min et Max ;
- `Mirroring` ;
- `Tiling` ;
- `DrawAs` ;
- `ImageType` ;
- `ImageSize` ;
- nombre de ressources uniques.

### Widget natif

Après ouverture de la carte :

- identifier le `UCrUW_MapMenuTerrainSegment` correspondant à `(2,5)` ;
- lire le brush de `ImageTerrain` ;
- comparer ressource et UV avec le data asset ;
- déterminer l'état qui sélectionne R1 ou R2.

### Streaming des textures

Pour plusieurs tuiles R2, relever avant et après ouverture de la carte :

- taille authored/built ;
- taille du handle copié ;
- `FirstResourceMemMip` ;
- `LODBias` ;
- `LODGroup` ;
- `Filter` ;
- `MipLoadOptions` ;
- `NeverStream` ;
- `bHasStreamingUpdatePending` ;
- `bForceMiplevelsToBeResident` ;
- temps nécessaire pour obtenir un mip plus détaillé.

### Interaction ImGui

Pendant une molette au-dessus du canvas, relever :

- état de survol du canvas ;
- delta `GetMouseWheel()` ;
- `GetScrollY()` avant et après la frame ;
- fenêtre réellement scrollée ;
- présence d'un parent ou child scrollable ;
- comportement dans une fenêtre dédiée sans diagnostics.

## Plan recommandé

### Étape 1 — fidélité des brushes

1. Inspecter toutes les métadonnées R2.
2. Résoudre le cas `(2,5)`.
3. Comparer le data asset au brush UMG vivant.
4. Confirmer l'état de sélection R1/R2.

### Étape 2 — carte R2 déterministe

1. Utiliser la variante active confirmée.
2. Appliquer la transposition validée.
3. Respecter les UV et le mirroring.
4. Vérifier les jointures et landmarks.

### Étape 3 — qualité et budget

1. Mesurer la résolution authored et résidente.
2. Tester la résidence forcée sur une seule tuile.
3. Choisir une cible, idéalement `512 x 512`.
4. Mesurer temps, stutter et mémoire pour 36 tuiles.
5. Décider entre mip sélectionné, downsampling ou LOD dynamique.

### Étape 4 — interaction

1. Tester une fenêtre non scrollable dédiée.
2. Évaluer le workaround `GetScrollY`/`SetScrollY` uniquement comme solution
   temporaire.
3. Si nécessaire, proposer une extension SDK de consommation de molette.

### Étape 5 — projection

Commencer seulement après stabilisation du terrain :

- appliquer la transposition et les inversions confirmées ;
- placer au moins cinq landmarks connus ;
- comparer la transformation native à `WorldToMap` ;
- mesurer l'erreur en pixels et unités monde.

## Critères de passage

Le terrain est prêt pour la calibration lorsque :

- R2 est sélectionnée selon une règle runtime confirmée ;
- la case `(2,5)` est expliquée ou possède un fallback explicitement accepté ;
- toutes les régions UV et orientations de brush sont respectées ;
- les 36 cases forment une image continue ;
- la résolution cible et le budget mémoire sont définis ;
- le zoom ne dépasse pas de façon excessive la résolution disponible ;
- la molette ne déplace plus simultanément la carte et son conteneur ;
- les changements de monde et le shutdown restent sûrs.

## Conclusion

Les problèmes remontés sont majoritairement résolubles et ne constituent pas
un no-go pour la migration :

- le passage de R1 à R2 est direct une fois la règle de sélection confirmée ;
- la transposition est une propriété désormais observée de la grille native ;
- la pixellisation provient principalement du mip `64 x 64` copié et peut être
  corrigée par une politique explicite de résidence et de résolution ;
- le conflit de molette vient d'une absence de consommation dans l'API v52 et
  peut être résolu par une fenêtre non scrollable ou une extension SDK ;
- la seule incertitude durable concerne les pixels de `Terrain_R2_02_05` si
  aucune région UV, substitution runtime ou ressource packagée ne permet de
  les retrouver.

La prochaine investigation doit donc porter sur les brushes R2 complets et le
streaming des mips, avant toute calibration de projection ou ajout de marqueurs.
