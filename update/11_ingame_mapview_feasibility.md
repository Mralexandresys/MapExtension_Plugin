# Faisabilité de porter MapExtensionViewer dans le jeu

## Objet du document

Ce document évalue la faisabilité technique d'un remplacement total ou partiel de `mapview` par une interface rendue directement dans StarRupture au moyen de `StarRupture-Plugin-SDK`.

Il couvre :

- les capacités UI et graphiques actuellement offertes par le SDK ;
- les éléments de la carte native du jeu accessibles dans le SDK généré ;
- les différentes architectures possibles ;
- les contraintes de rendu, textures, threads, entrées et cycle de vie ;
- les risques de projection monde vers carte ;
- le retour d'expérience existant : une tentative d'ajout de points à la carte native a déjà produit des positions incorrectes ;
- un plan de validation progressif avant toute réécriture complète.

Le but n'est pas de conclure qu'une migration doit obligatoirement être réalisée, mais de définir ce qui est possible, ce qui est risqué et quelles preuves doivent être obtenues avant de remplacer le viewer web.

## Résumé exécutif

Un portage de MapExtensionViewer dans le jeu est techniquement possible avec le SDK actuel. Le SDK fournit :

- des panneaux et widgets ImGui ;
- une API ImGui étendue ;
- des draw-lists 2D ;
- le chargement de textures depuis un fichier, la mémoire, des pixels RGBA ou un `UTexture2D` vivant ;
- la souris, la molette, les raccourcis clavier et la capture des entrées ;
- du dessin debug dans le monde ;
- `ObjectWalker`, `ObjectProperties` et l'accès au SDK Unreal généré.

Le SDK généré montre aussi que StarRupture contient déjà un moteur de carte natif complet, avec terrain segmenté, mipmaps selon le zoom, fog of war, marqueurs, filtres, légende, zoom et centrage joueur.

La solution recommandée est une carte ImGui indépendante qui réutilise les textures et métadonnées natives du jeu. La modification directe du widget UMG natif est déconseillée : elle dépend fortement de champs privés, du cycle de vie CommonUI et de transformations internes non exposées. Le mauvais placement observé lors d'une tentative précédente est cohérent avec ce risque.

Avant de développer la carte complète, il faut réaliser un probe client-only qui prouve :

1. que les données et textures de terrain sont chargées et accessibles ;
2. que les brushes du terrain référencent des `UTexture2D` compatibles avec `LoadFromUTexture2D` ;
3. que la transformation monde vers carte native peut être reconstruite avec une erreur suffisamment faible ;
4. que les copies GPU peuvent être chargées progressivement sans freeze ;
5. que les assets restent valides lors d'un changement de monde.

## État actuel de MapExtensionViewer

Le viewer actuel est une application Vue 3/TypeScript empaquetée en fichier HTML local. Il affiche :

- les expéditeurs et récepteurs cargo ;
- les connexions cargo ;
- les téléporteurs ;
- les joueurs ;
- les bases abandonnées ;
- les ressources végétales et leur état depleted ;
- les annotations utilisateur ;
- les zones utilisateur ;
- la timeline du cycle de rupture ;
- les filtres, sélections, détails, tooltips et modes focus ;
- les contrôles de rafraîchissement et de configuration de l'endpoint.

Le fond actuel est un export statique de la carte du jeu, provenant de `StarRuptureMap`, découpé en 30 tuiles WebP :

- image source : `9019 x 11691` pixels ;
- taille d'une tuile : `2048 x 2048` ;
- grille : 5 colonnes x 6 lignes ;
- chemin : `mapview/public/map-tiles/base_newmap_q70_2048/`.

Le viewer possède déjà un petit moteur cartographique dans :

- `mapview/src/components/MapCanvas.vue` ;
- `mapview/src/composables/useMapPanZoom.ts` ;
- `mapview/src/composables/useMapViewEntities.ts` ;
- `mapview/src/composables/useUserAnnotations.ts` ;
- `mapview/src/composables/useRuptureTimeline.ts`.

Le portage in-game est donc une réécriture de moteur cartographique et d'interface, pas un simple remplacement du rendu HTML par quelques appels ImGui.

## Capacités du SDK pour une interface in-game

### Panneaux et widgets

`IPluginUIEvents` permet d'enregistrer :

- un panneau intégré au ModLoader avec `RegisterPanel` ;
- un widget ou overlay indépendant avec `RegisterWidget` ;
- des callbacks de fermeture ;
- des callbacks de changement de configuration ;
- une visibilité contrôlée par handle.

`PluginWindowHints` permet de définir la taille, la position, le pivot et des flags de fenêtre comme :

- absence de barre de titre ;
- absence de redimensionnement ;
- absence de déplacement ;
- absence de fond ;
- absence d'entrées souris ;
- absence de sauvegarde automatique de position.

### ImGui haut niveau

`IModLoaderImGui` expose notamment :

- textes et tooltips ;
- boutons, cases à cocher, radio buttons et progress bars ;
- inputs texte et numériques ;
- sliders et drag controls ;
- couleurs ;
- tables, arbres, tabs, menus et popups ;
- fenêtres enfants ;
- gestion du curseur et du layout ;
- requêtes de taille, focus, hover et état des items ;
- accès à la souris et au clipboard.

Ces fonctions suffisent pour reconstruire les panneaux de filtres, détails, annotations et configuration.

### Dessin 2D direct

Les draw-lists permettent de dessiner :

- lignes et polylignes ;
- rectangles, quads et triangles ;
- cercles, polygones et ellipses ;
- texte libre ;
- courbes de Bézier ;
- images ;
- chemins personnalisés ;
- clipping local.

Cela suffit pour dessiner une carte, des marqueurs, des connexions cargo, des zones et une timeline.

### Textures

`IPluginImGuiTextures` permet :

- `LoadFromFile` ;
- `LoadFromMemory` ;
- `LoadFromRGBA` ;
- `LoadFromUTexture2D` ;
- `Image` et `ImageButton` ;
- le partage et le comptage de références par nom ;
- jusqu'à la capacité annoncée par `GetCapacity`.

Les formats documentés pour les fichiers sont PNG, JPEG, BMP, GIF et TIFF. WebP et SVG ne sont pas annoncés.

### Entrées

Le SDK fournit :

- raccourcis clavier simples ou avec Ctrl/Shift/Alt ;
- souris, drag, double-clic et molette ;
- capture exclusive avec `AcquireInputCapture` ;
- passthrough coopératif avec `AcquireInputPassthrough`.

Une carte plein écran devrait probablement utiliser la capture exclusive. Une petite carte persistante pourrait utiliser le passthrough.

### Dessin dans le monde

`hooks->HUD->DebugDraw` peut dessiner des lignes, points, cercles, sphères, boîtes, flèches, textes et autres primitives dans le monde. Cette API est utile pour diagnostiquer les coordonnées et la projection, mais elle n'est pas un remplacement de la carte 2D.

## Éléments natifs de la carte trouvés dans le SDK

### Widget principal

Le SDK client contient :

- `UWBP_MapMenu_C` ;
- `UCrUW_MapMenu` ;
- `UCrUW_MapMenuMapArea` ;
- `UCrUW_MapMenuTerrain` ;
- `UCrUW_MapMenuTerrainSegment` ;
- `UCrUW_MapMenuMarker` ;
- `UCrUW_MapMenuZoomSlider` ;
- `UCrUW_MapMenuLegend` ;
- `UCrUW_MapMenuMarkerDetails` ;
- `UCrUW_MapMenuMarkersList` ;
- `UCrUW_MapMenuCrosshair`.

Le widget principal expose des actions de zoom, de centrage joueur, de placement de marqueur, de reset des filtres et de fermeture. Une partie importante de son état reste toutefois privée ou remplacée par des zones `Pad_*` dans le dump.

### Données de terrain

`UCrMapMenuDevSettings` contient notamment :

- `MaxZoom`, `DefaultZoom`, `MinZoom` ;
- les distances et facteurs de fusion des marqueurs ;
- `MapAreaPivotPoint` ;
- la taille de grille et de render target du fog of war ;
- une soft reference `TerrainData` vers `UCrMapMenuTerrainData` ;
- les catégories, couleurs, filtres et icônes de marqueurs.

`UCrMapMenuTerrainData` contient :

- `MapTerrainTopLeftPivotPoint` ;
- `MapTerrainSegmentSize` ;
- les distances de changement de visibilité ;
- les règles de changement de mipmap ;
- `TerrainMipMapByZoomValueData` ;
- `TerrainSegmentsData` ;
- `FogOfWarRenderTextureMaterial`.

Chaque `FCrTerrainSegmentData` contient :

- `TerrainSegmentGridIndex` ;
- `TerrainSegmentTexture` sous forme de `FSlateBrush` ;
- `TerrainSegmentTextureRadiation2` sous forme de `FSlateBrush`.

Le jeu possède donc déjà une carte tuilée avec variantes et niveaux de détails. Réutiliser ce système éviterait de distribuer les 30 tuiles WebP de l'export actuel.

### Brushes et ressources graphiques

`FSlateBrush` contient notamment :

- `ImageSize` ;
- `ResourceObject` ;
- une région UV ;
- des options de tiling et de dessin.

Le dump rend `ResourceObject` accessible dans la structure C++, même si la métadonnée Unreal l'indique comme privée. Il faut vérifier à l'exécution si le resource object de chaque segment est :

- un `UTexture2D`, directement copiable avec `LoadFromUTexture2D` ;
- un matériau, qui ne peut pas être copié directement par l'API texture ImGui ;
- un autre type de ressource Slate.

### Terrain, render target et fog of war

`UWBP_MapMenuTerrain_C` contient :

- `UTextureRenderTarget2D* RT` ;
- `UMaterialInstanceDynamic* BM`.

`UCrMapManuSubsystem` contient :

- `TArray<UTexture2D*> FOWTextures` ;
- `TArray<UMaterialInstanceDynamic*> FOWMaterials`.

Cela indique que la carte native compose probablement les segments de terrain et le fog of war dans un matériau ou render target. L'API ImGui copie directement les `UTexture2D`, mais n'annonce pas de prise en charge directe de `UTextureRenderTarget2D` ou `UMaterialInstanceDynamic`.

Une première version pourrait donc réutiliser les textures de terrain sans reproduire immédiatement le fog of war natif.

### Icônes et catégories natives

`UCrMapMenuCategoryData` contient :

- un nom localisé ;
- un `FSlateBrush Icon` ;
- une priorité ;
- un filtre de légende ;
- des options de rotation ;
- des options de visibilité par fog of war ;
- la possibilité d'afficher des détails.

Ces données permettraient de réutiliser les icônes et couleurs natives pour les joueurs, bâtiments, POI et autres catégories du jeu. Les concepts propres à MapExtension, comme dispatcher, receiver et connexion cargo, nécessiteraient probablement toujours des styles spécifiques.

### World Partition MiniMap

La classe Unreal générique `AWorldPartitionMiniMap` expose :

- `MiniMapWorldBounds` ;
- `UVOffset` ;
- `UTexture2D* MiniMapTexture` ;
- `WorldUnitsPerPixel`.

Cette piste doit être testée, mais la présence de la classe dans le SDK ne garantit pas qu'une instance soit cuite et chargée dans la build StarRupture. Le système `UCrMapMenuTerrainData` est une preuve plus directe du fonctionnement de la carte du jeu.

## Architectures possibles

### Option A - Modifier directement la carte UMG native

Principe : retrouver `UWBP_MapMenu_C` et `UCrUW_MapMenuMapArea`, puis ajouter des widgets `UCrUW_MapMenuMarker` ou des enfants dans `CanvasPanelMapArea`.

Avantages :

- apparence native ;
- zoom, pan et contrôles déjà implémentés ;
- intégration directe dans le menu du jeu ;
- réutilisation naturelle du fog of war et de la légende.

Risques :

- cycle de vie CommonUI complexe ;
- widget créé seulement lorsque la carte est ouverte ;
- nombreux champs privés ou non dumpés ;
- création et ownership des widgets UMG ;
- delegates et événements de hover ;
- risque que le jeu supprime les widgets ajoutés lors d'un refresh ;
- dépendance forte aux offsets et à la structure interne ;
- difficulté à récupérer exactement la transformation locale de la carte ;
- forte probabilité de casse après une mise à jour du jeu.

Cette option correspond au risque déjà rencontré : les points ajoutés à la carte native ne s'affichent pas correctement. Elle ne doit pas être reprise avant d'avoir prouvé la transformation native exacte.

Verdict : possible, mais non recommandée pour une fonctionnalité principale.

### Option B - Overlay ImGui au-dessus de la carte native

Principe : laisser le jeu afficher sa carte, puis dessiner les données MapExtension au-dessus avec la draw-list foreground.

Avantages :

- réutilisation complète du rendu natif ;
- aucun chargement de texture de terrain ;
- ajout limité aux marqueurs et connexions MapExtension.

Risques :

- nécessité de connaître la géométrie écran exacte du widget natif ;
- synchronisation avec zoom, pan, DPI et clipping ;
- nécessité d'observer l'ouverture et la fermeture de la carte ;
- risque de décalage entre le render thread ImGui et le tick UMG ;
- peu de champs publics exposent la translation réelle ;
- comportement différent selon résolution, ratio, UI scale et manette.

Verdict : séduisant en apparence, mais probablement aussi fragile que la modification directe du widget.

### Option C - Carte ImGui indépendante avec assets externes

Principe : porter le viewer actuel en C++/ImGui en conservant les tuiles exportées.

Avantages :

- indépendance vis-à-vis de l'UMG natif ;
- projection actuelle déjà connue ;
- comportement maîtrisé ;
- possibilité d'atteindre la parité fonctionnelle avec `mapview`.

Risques :

- les tuiles actuelles sont en WebP, format non annoncé par `LoadFromFile` ;
- conversion ou décodeur WebP nécessaire ;
- distribution des assets à côté de la DLL ;
- mise à jour des assets non couverte par un simple remplacement de DLL ;
- consommation VRAM élevée ;
- maintien de constantes de projection liées à un export statique ;
- problème de droits sur l'export de carte à surveiller.

Verdict : réalisable, mais ne profite pas des données natives découvertes.

### Option D - Carte ImGui indépendante avec assets natifs du jeu

Principe : utiliser `UCrMapMenuTerrainData`, les `FCrTerrainSegmentData`, les brushes, les icônes et les paramètres natifs, mais effectuer le rendu et les interactions dans un widget ImGui appartenant à MapExtension.

Avantages :

- pas de dépendance au widget UMG vivant ;
- pas de tuiles WebP externes ;
- assets automatiquement alignés sur la version du jeu ;
- réutilisation du pivot, de la taille des segments et des mipmaps ;
- possibilité de garder les snapshots et filtres MapExtension ;
- meilleure isolation et meilleur contrôle du cycle de vie ;
- réduction du risque juridique de redistribution de l'export statique.

Risques :

- besoin de charger et inspecter les soft assets ;
- besoin de vérifier le type concret des `ResourceObject` ;
- copie GPU et duplication de VRAM ;
- fog of war et matériaux dynamiques non directement copiables ;
- projection native à reconstruire et valider ;
- nécessité d'un cache de textures et d'un chargement progressif.

Verdict : option recommandée si le probe runtime confirme l'accès aux textures et à la projection.

### Option E - Approche hybride

Principe : conserver le viewer web complet et ajouter progressivement :

- un panneau de statut ;
- un widget de rupture ;
- une carte ImGui simplifiée ;
- éventuellement une carte complète plus tard.

Avantages :

- faible risque produit ;
- comparaison directe entre ancien et nouveau rendu ;
- rollback simple ;
- tests progressifs ;
- pas de perte immédiate des annotations et fonctions avancées.

Verdict : meilleure stratégie de migration.

## Analyse du mauvais placement des points sur la carte native

### Projection actuelle de MapExtension

`map_state_types.h` utilise actuellement une transformation affine très simple, calibrée sur l'export statique :

```text
mapX = (worldX - srcX1) * scaleX
mapY = (worldY - srcY1) * scaleY
```

Constantes principales :

- `kMapSrcX1 = -358583` ;
- `kMapSrcY1 = -263782` ;
- `kMapSrcX2 = -98583` ;
- `kMapSrcY2 = -9439` ;
- destination exportée de `1518.414983, 2272.832995` à `6895.078786, 7535.110312` ;
- image `9019 x 11691`.

Cette transformation est adaptée à l'image exportée et au système de viewBox du viewer. Elle ne doit pas être appliquée directement à un canvas UMG natif sans preuve que les deux espaces sont identiques.

### Espaces de coordonnées distincts

Une tentative sur la carte native peut traverser plusieurs espaces :

1. coordonnées monde Unreal ;
2. coordonnées logiques de terrain natif ;
3. index et coordonnées locales d'un segment ;
4. coordonnées du `CanvasPanelTerrain` ;
5. coordonnées du `CanvasPanelMapArea` ;
6. coordonnées après zoom et translation ;
7. coordonnées locales Slate ;
8. coordonnées absolues écran ;
9. coordonnées après DPI scale et UI scale.

Un point correct dans l'espace exporté peut être incorrect dans l'espace UMG même si le fond visuel semble identique.

### Causes probables du décalage observé

#### 1. Origine différente

La carte native expose au moins :

- `MapAreaPivotPoint` ;
- `MapTerrainTopLeftPivotPoint`.

L'export actuel utilise des offsets `dst_x1` et `dst_y1`. Ces origines ne sont pas nécessairement équivalentes.

#### 2. Axe Y inversé ou axes permutés

Unreal utilise X/Y dans le monde, tandis que Slate utilise X vers la droite et Y vers le bas. Le terrain natif peut également appliquer une inversion ou une rotation interne. La projection actuelle ne contient ni rotation ni terme croisé.

#### 3. Rotation ou shear non pris en compte

Une transformation indépendante X vers X et Y vers Y est insuffisante si la carte exportée ou native contient une légère rotation. Une transformation affine générale est alors nécessaire :

```text
u = aX + bY + c
v = dX + eY + f
```

Elle doit être calibrée avec au moins trois points non colinéaires, idéalement davantage pour mesurer l'erreur résiduelle.

#### 4. Crop ou bordure de l'export

L'image `9019 x 11691` contient des zones hors contenu. `mapview` compense avec `imageX = -dst_x1` et `imageY = -dst_y1`. Le widget natif peut ne pas avoir le même crop ou les mêmes marges.

#### 5. Mauvais espace CanvasPanel

Définir la position d'un enfant de `CanvasPanelMapArea` avec une coordonnée calculée pour `CanvasPanelTerrain`, ou inversement, produit un offset et une échelle incorrects.

#### 6. Anchors, alignment et pivot UMG

Les `UCanvasPanelSlot` utilisent anchors, offsets et alignment. Un marker centré avec un alignment `0.5, 0.5` ne se positionne pas comme un élément aligné en haut à gauche.

#### 7. Zoom et translation natifs non appliqués

Le zoom peut être appliqué comme render transform sur un parent et non comme modification des positions des enfants. Ajouter le point au mauvais niveau de la hiérarchie peut appliquer le zoom zéro, une fois ou deux fois.

#### 8. DPI et UI scale

Les coordonnées absolues écran diffèrent des coordonnées locales Slate. Un résultat correct en 1920x1080 peut être faux dans une autre résolution si le DPI scale n'est pas retiré ou appliqué correctement.

#### 9. Position actor versus position de marker native

Le jeu peut utiliser une position métier différente de `K2_GetActorLocation` : pivot de bâtiment, entity fragment, centre de bounds, position de socket ou position quantifiée. Il faut comparer exactement la même source monde.

#### 10. World origin ou contexte de monde

Il faut vérifier que le point et le widget appartiennent au même `UWorld`, notamment lors des transitions, previews, menus ou sessions dédiées.

#### 11. Fusion ou clamp des marqueurs

Le système natif fusionne les marqueurs proches et peut les contraindre aux bords. Un point peut être calculé correctement puis déplacé volontairement par la logique native.

#### 12. Mauvais moment du cycle de vie

Un point ajouté avant `Construct`, avant l'initialisation du terrain ou pendant un rebuild du widget peut être repositionné ou supprimé plus tard.

## Méthode correcte pour résoudre la projection

### Ne pas partir des constantes de l'export

La projection native doit être dérivée des données natives ou mesurée sur le widget natif. Les constantes de `map_state_types.h` restent valides pour le viewer exporté, pas automatiquement pour UMG.

### Calibration avec landmarks

Sélectionner plusieurs landmarks dont les coordonnées monde et la position native peuvent être observées :

- joueur local ;
- deux ou trois bâtiments très éloignés ;
- téléporteurs ;
- POI fixes.

Collecter pour chacun :

```text
worldX, worldY
nativeLocalX, nativeLocalY
zoom
translation
geometry size
DPI scale
```

Résoudre ensuite la transformation affine générale. Avec plus de trois points, utiliser une résolution aux moindres carrés et calculer :

- erreur moyenne ;
- erreur maximale ;
- distribution spatiale de l'erreur.

Si l'erreur augmente selon les zones, la carte peut utiliser une projection non affine, un crop par segment ou des données monde différentes.

### Calibration par données de terrain

Utiliser en priorité :

- `MapTerrainTopLeftPivotPoint` ;
- `MapTerrainSegmentSize` ;
- `TerrainSegmentGridIndex` ;
- la taille réelle de chaque brush ;
- `MapAreaPivotPoint`.

Une hypothèse à valider est :

```text
segmentX = floor((worldX - terrainTopLeftX) / segmentWorldSizeX)
segmentY = floor((worldY - terrainTopLeftY) / segmentWorldSizeY)
localU = remainder / segmentWorldSize
localV = remainder / segmentWorldSize
```

L'orientation des axes et le signe doivent être déterminés empiriquement.

### Comparaison avec les marqueurs natifs

Le moyen le plus fiable est d'observer un marker natif dont la position monde est connue, puis de lire sa position locale finale dans le canvas. Cela permet d'inférer la fonction réellement utilisée par le jeu sans supposer que l'export et le widget partagent la même calibration.

## Textures et mémoire GPU

### Coût potentiel

Une image RGBA complète de `9019 x 11691` représente environ 402 Mo non compressés. Les fichiers compressés occupent moins sur disque, mais la texture GPU est décompressée.

Les segments natifs et leurs mipmaps peuvent réduire le coût si seuls les segments visibles sont copiés. Toutefois, `LoadFromUTexture2D` crée une ressource appartenant au ModLoader : la texture du jeu est donc dupliquée en VRAM.

### Chargement bloquant

Les fonctions `Load*` effectuent une copie GPU bloquante et doivent être appelées depuis le game thread, jamais depuis le callback ImGui du render thread.

Il faut une file de chargement :

```text
Renderer demande un segment
    -> file thread-safe
    -> game tick charge quelques segments
    -> publication du handle ImGui
    -> renderer utilise le handle à la frame suivante
```

Il faut limiter le budget par frame et retry lorsqu'une texture streamée n'est pas encore dans un état lisible.

### Cache

Le cache devrait être indexé par :

- monde ;
- variante de terrain ;
- index de segment ;
- niveau de mip ;
- nom stable de texture.

Il doit :

- conserver les segments visibles ;
- précharger une marge autour du viewport ;
- libérer les segments éloignés ;
- invalider tous les handles au changement de monde ou shutdown ;
- ne jamais appeler `FreeTexture` pendant que le render thread peut encore utiliser le handle.

## Threads et ownership

### Game thread

Doit gérer :

- accès aux `UObject` ;
- ObjectWalker et résolution des assets ;
- lecture des données terrain ;
- chargement et libération des textures ;
- création éventuelle de ressources UMG ;
- capture des snapshots de jeu.

### Render thread

Doit gérer :

- appels ImGui ;
- draw-lists ;
- hit-testing basé sur des données copiées ;
- état visuel local ;
- aucun accès direct à des acteurs ou widgets Unreal vivants.

### Publication des données

Le renderer doit recevoir un modèle immuable :

```text
MapUiSnapshot
  - projection
  - tiles prêtes
  - marqueurs visibles
  - connexions visibles
  - détails sélectionnés
  - rupture timeline
```

Le remplacement du snapshot doit être atomique ou protégé par un verrou très court. Il ne faut pas conserver le mutex de capture pendant toute une frame ImGui.

## Interactions et hit-testing

Les primitives de draw-list ne créent pas automatiquement des widgets interactifs. Il faut implémenter :

- hit-test cercle pour les marqueurs ;
- hit-test rectangle pour les zones ;
- distance souris-segment pour les connexions ;
- priorité en cas de chevauchement ;
- distinction clic/drag ;
- double-clic éventuel ;
- tooltips ;
- focus clavier ;
- clipping du hit-test au viewport.

Avec beaucoup d'entités, un index spatial ou au minimum un culling viewport est recommandé.

## Pan, zoom et transformations ImGui

Le portage doit gérer :

- fit initial selon le ratio de fenêtre ;
- zoom borné ;
- ancrage du zoom autour du curseur ou du centre ;
- translation ;
- contraintes pour ne pas perdre la carte ;
- centrage sur joueur ou sélection ;
- conversion écran vers carte ;
- taille de marqueur stable à l'écran ;
- sélection non déclenchée après un drag ;
- redimensionnement de fenêtre et DPI.

La logique actuelle de `useMapPanZoom.ts` constitue une référence fonctionnelle, mais doit être réécrite en C++.

## Filtres, sélection et détails

La logique de `useMapViewEntities.ts` doit être portée sans la mélanger au rendu. Une architecture en trois couches est recommandée :

```text
MapUiState
  - filtres
  - zoom/pan
  - sélection
  - options

MapUiModelBuilder
  - calcule entités et connexions visibles
  - calcule focus et orphelins
  - construit les détails

MapUiRenderer
  - dessine uniquement le modèle
```

Cela facilite les tests et évite de recalculer inutilement les filtres à chaque primitive dessinée.

## Annotations utilisateur

Le viewer utilise `localStorage` pour les marqueurs et zones personnels. Une interface C++ doit définir un stockage local, par exemple :

```text
Plugins/config/MapExtension_Plugin.annotations.json
```

Il faut prévoir :

- écriture atomique ;
- validation et récupération après fichier corrompu ;
- version du schéma ;
- migration ;
- import/export JSON ;
- compatibilité avec les exports du viewer web ;
- choix de fichier, non fourni directement par le SDK UI ;
- protection contre l'écriture depuis le render thread.

Les annotations devraient être portées après validation du rendu et de la projection, pas dans le premier prototype.

## Timeline de rupture

La timeline est plus simple à porter que la carte. Les calculs actuels peuvent être traduits en C++ :

- durées des phases ;
- extrapolation à partir de `elapsed_seconds` et `observed_at_unix_ms` ;
- phase courante ;
- temps restant ;
- largeur visuelle minimale de la phase imminente ;
- marqueur de progression.

Elle constitue un bon premier widget ImGui pour valider le cycle de vie UI avant la carte.

## Intégration client/serveur

La nouvelle UI doit rester client-only :

- aucun ImGui ni texture côté serveur ;
- données solo capturées localement ;
- données dédiées lues depuis le cache client alimenté par le serveur ;
- protocole de synchronisation inchangé tant que la forme des snapshots ne change pas.

Le nouveau code devrait vivre dans `client/`, par exemple :

```text
client/map_ui_runtime.cpp
client/map_ui_renderer.cpp
client/map_ui_assets.cpp
client/map_ui_projection.cpp
client/map_ui_state.cpp
```

Ces fichiers doivent être exclus des configurations serveur dans le projet Visual Studio.

L'initialisation et le shutdown doivent rester symétriques dans `MapStateRuntime::RegisterCallbacks` et `UnregisterCallbacks`.

## Cycle de vie

Il faut gérer explicitement :

- PluginInit ;
- EngineInit ;
- début du monde Chimera ;
- premier chargement des assets ;
- ouverture éventuelle de la carte native ;
- world end play ;
- retour menu ;
- reconnexion ;
- hot unload ;
- process detach.

Tous les handles ImGui, textures copiées, callbacks et tokens d'input doivent être libérés. Un token de capture oublié peut bloquer les contrôles jusqu'au redémarrage du ModLoader.

## Packaging, mises à jour et licences

### Avec assets natifs

Si les textures du jeu sont réutilisées à l'exécution :

- aucune copie de la carte n'est incluse dans l'archive ;
- la carte suit naturellement les mises à jour du jeu ;
- la distribution est plus légère ;
- la dépendance à `base_map.webp` et ses dérivés disparaît potentiellement.

### Avec assets exportés

Il faut distribuer les tuiles à côté de la DLL ou les embarquer. L'auto-updater ne remplaçant que la DLL, une stratégie de mise à jour d'assets reste nécessaire. Les notices existantes indiquent aussi que l'image de carte reste copyright Creepy Jar S.A.

## Stabilité et maintenance

Une erreur dans le viewer web affecte principalement la page du navigateur. Une erreur dans la version C++ peut :

- faire crasher le jeu ;
- bloquer le game thread ;
- bloquer le render thread ;
- provoquer un freeze GPU ;
- conserver un pointeur UObject invalide ;
- utiliser une texture après libération ;
- bloquer les contrôles du joueur.

Le coût de test et le niveau de prudence requis sont donc supérieurs à ceux du viewer web.

## Plan de probe recommandé

### Étape 1 - Inventaire runtime

Après EngineInit et world begin play, utiliser `ObjectWalker` pour chercher :

- `CrMapMenuDevSettings` ;
- `CrMapMenuTerrainData` ;
- `CrMapManuSubsystem` ;
- `WBP_MapMenuTerrain_C` ;
- `WBP_MapMenu_C` ;
- `WorldPartitionMiniMap`.

Répéter l'inventaire avant et après l'ouverture de la carte native.

### Étape 2 - Inspection des données terrain

Journaliser :

- nom et classe de l'objet ;
- nombre de segments ;
- indices de grille ;
- top-left pivot ;
- taille monde d'un segment ;
- règles de mipmap ;
- taille des brushes ;
- nom et classe de chaque `ResourceObject` ;
- variantes radiation.

### Étape 3 - Test d'une texture

Sur le game thread :

1. sélectionner un segment ;
2. vérifier que `ResourceObject` est un `UTexture2D` ;
3. appeler `LoadFromUTexture2D` ;
4. afficher le handle dans un panneau ImGui minimal ;
5. tester avant et après ouverture de la carte native ;
6. tester fermeture et réouverture du monde.

### Étape 4 - Calibration projection

Collecter au moins cinq points de contrôle répartis sur la carte. Comparer :

- projection actuelle MapExtension ;
- projection affine générale ;
- projection dérivée du pivot et de la taille des segments ;
- position locale des marqueurs natifs.

Le probe doit afficher ou logger l'erreur en pixels et en unités monde.

### Étape 5 - Prototype carte basse complexité

Afficher :

- terrain seulement ;
- joueur local ;
- téléporteurs ;
- zoom et pan ;
- un panneau de diagnostic de projection.

Ne pas inclure encore les connexions, annotations, filtres avancés ou fog of war.

### Étape 6 - Validation performances

Mesurer :

- temps de chargement de chaque texture ;
- temps total de préchargement ;
- VRAM estimée ;
- durée du render callback ;
- nombre de primitives ;
- stabilité lors du streaming ;
- impact en solo et serveur dédié.

## Critères go/no-go

### Go pour une carte ImGui native

Continuer si :

- les textures de terrain sont des `UTexture2D` accessibles ;
- elles peuvent être chargées sans freeze majeur ;
- la projection est reproductible avec une faible erreur ;
- les assets sont disponibles sans dépendre en permanence du widget UMG natif ;
- le cache GPU reste dans un budget acceptable ;
- les changements de monde sont sûrs ;
- l'UI reste réactive avec un nombre réaliste de marqueurs et connexions.

### No-go ou maintien du viewer web

Conserver le viewer web comme interface principale si :

- les segments sont uniquement des matériaux ou render targets non copiables ;
- les assets ne sont disponibles que pendant un état UMG fragile ;
- la projection native ne peut pas être reconstruite de manière stable ;
- les freezes de chargement sont visibles ;
- la duplication VRAM est trop élevée ;
- les mises à jour du jeu cassent fréquemment la structure ;
- la parité annotations/filtres demande un coût disproportionné.

## Recommandation finale

La faisabilité est réelle et le SDK contient davantage d'éléments natifs réutilisables que ne le laissait penser une approche basée uniquement sur l'export WebP. Le meilleur candidat est une carte ImGui indépendante alimentée par `UCrMapMenuTerrainData` et les textures référencées par `FCrTerrainSegmentData`.

La modification directe de la carte UMG ne doit pas être la première option. Le mauvais placement déjà observé montre que la projection et la hiérarchie de transformations natives ne sont pas encore comprises. Ajouter davantage de widgets ou corriger des offsets à la main risquerait de produire une solution spécifique à une résolution ou à une version du jeu.

Le probe client-only recommandé a maintenant confirmé que les 36 segments normaux et les 36 variantes radiation sont des `UTexture2D` copiables. Il a aussi montré que `UCrMapMenuTerrainData` peut être collecté après son utilisation initiale : l'intégration doit donc résoudre sa soft-reference configurée à la demande plutôt que conserver un pointeur UObject brut.

Le premier POC et ses observations sont décrits dans `update/12_ingame_map_probe.md`. Le second, décrit dans `update/13_ingame_map_grid_poc.md`, récupère progressivement les segments normaux et les assemble dans un canvas ImGui avec pan, zoom, contrôles d'orientation et budget mémoire. La prochaine action recommandée est de valider cette grille en jeu, puis de calibrer la projection avec des landmarks avant tout engagement sur une réécriture complète de `mapview`.
