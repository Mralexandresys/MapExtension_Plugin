# POC d'assemblage de la carte native in-game

## Objectif

Ce second POC client-only prolonge `update/12_ingame_map_probe.md`. Il doit
valider que les segments de `UCrMapMenuTerrainData` peuvent être récupérés,
copiés progressivement puis assemblés en une carte complète dans un canvas
ImGui, sans dépendre du widget UMG natif et sans conserver de pointeur
`UObject` entre deux ticks.

Le viewer navigateur reste l'interface principale. Ce jalon n'ajoute encore
aucun joueur, téléporteur, cargo, POI ou connexion et ne prétend pas valider la
projection monde-vers-carte.

## Périmètre implémenté

Le panneau ModLoader `Map probe` contient maintenant une section
`Native map grid POC` qui :

1. recharge `UCrMapMenuTerrainData` depuis la soft-reference configurée si le
   GC l'a retiré de `GObjects` ;
2. collecte un descripteur sans pointeur pour chaque segment normal : index du
   segment, coordonnées de grille, taille du brush et nom de ressource ;
3. trie les segments par grille et refuse de déclarer la carte complète si le
   nombre de cases planifiées ne correspond pas au rectangle observé ;
4. résout de nouveau le data asset et le `UTexture2D` de la tuile courante sur
   le game thread ;
5. copie au maximum une tuile par étape avec `LoadFromUTexture2D`, espacée de
   `50 ms` ;
6. retente jusqu'à trois fois une tuile dont la ressource D3D12 est encore en
   streaming, avec `500 ms` entre les essais ;
7. publie uniquement les handles GPU, coordonnées et métriques vers le render
   thread ;
8. dessine les handles avec `IModLoaderImGui::DL_AddImage` dans leur case de
   grille ;
9. libère toutes les copies lors d'un remplacement, d'une annulation, d'un
   changement de monde ou du shutdown ;
10. arrête et libère la grille si la projection RGBA estimée dépasse
    `128 MiB`.

Les handles de l'ancienne génération attendent deux ticks avant leur
libération. Une nouvelle génération attend également ces deux ticks avant sa
première copie afin d'éviter de cumuler inutilement les slots pendant un
rechargement.

## Contrôles du canvas

- `Load and assemble native map` : lance ou relance la copie de la grille ;
- `Cancel map load` : annule une copie en cours et libère ses handles ;
- `Clear map` : libère une carte terminée ;
- `Fit map` : recentre la grille et ajuste son zoom au canvas ;
- glisser avec le bouton gauche : déplace la carte ;
- molette : zoome autour du curseur ;
- `Flip grid X`, `Flip grid Y`, `Transpose grid` : permettent de déterminer
  visuellement l'orientation correspondant à la carte native.

Les coordonnées de chaque case sont affichées lorsque le zoom est suffisant.
Ces bascules ne modifient pas les données terrain et seront remplacées par une
orientation déterministe une fois la bonne transformation confirmée.

## Sécurité mémoire et threads

- `LoadAsset_Blocking`, l'accès aux tableaux SDK et `LoadFromUTexture2D`
  restent exclusivement sur le game thread.
- Le render thread ne reçoit aucun `UObject*` ; il lit des valeurs copiées et
  dessine des `PluginTextureHandle` sous le mutex du probe.
- Un handle retiré du vecteur partagé est libéré de façon différée afin de ne
  pas être détruit pendant une frame ImGui en vol.
- Une requête est refusée dès que `ChimeraMain` n'est plus actif, y compris
  dans la fenêtre entre le callback de fin de monde et le retour au menu.
- Le plafond `128 MiB` utilise une estimation RGBA du mip résident. Il protège
  le POC contre la copie accidentelle des 36 textures à haute résolution, mais
  ne constitue pas une mesure exacte de la VRAM D3D12 réelle.

## Activation et DLL

Conserver l'option expérimentale :

```ini
[Experimental]
InGameMapProbe=1
```

La DLL client Debug produite se trouve dans :

```text
build/Client Debug/Plugins/MapExtension_Plugin.dll
```

La copier dans `StarRupture/Binaries/Win64/ModLoader/Plugins/`, redémarrer le
jeu, puis ouvrir le panneau `Map probe`.

## Protocole de test runtime

### 1. Assemblage avant la carte native

1. Entrer dans un monde `ChimeraMain`.
2. Ne pas ouvrir la carte native du jeu.
3. Ouvrir `Map probe`.
4. Cliquer sur `Load and assemble native map`.
5. Attendre le statut final.
6. Vérifier que le canvas passe progressivement de cases vides aux 36 tuiles.
7. Conserver une capture avec les coordonnées de grille visibles.

Résultat attendu pour les données actuellement observées :

```text
Success: assembled 36/36 native terrain tiles
```

Les logs importants utilisent les préfixes suivants :

```text
In-game map grid started
In-game map grid tile copied
In-game map grid finished
```

### 2. Orientation

Tester les huit combinaisons utiles de retournement/transposition et noter
celle qui produit une géographie continue et reconnaissable. Vérifier en
particulier :

- continuité des routes et reliefs aux jointures ;
- absence de tuile tournée ou réfléchie individuellement ;
- position relative de plusieurs landmarks connus ;
- cohérence des bords nord/sud et est/ouest.

Ne pas inscrire encore cette orientation comme vérité de projection sans la
comparer aux coordonnées monde de landmarks.

### 3. Streaming et budget

1. Ouvrir puis fermer la carte native.
2. Relancer l'assemblage.
3. Comparer les dimensions de mip résident, les temps et la mémoire estimée.

Si les mips sont devenus trop grands, le probe doit s'arrêter avec un message
semblable à :

```text
Map load stopped: resident mip ... projects ... MiB, above the 128.0 MiB POC budget
```

Cet arrêt est volontaire. Il évite qu'un test charge jusqu'à environ
`576 MiB` pour 36 textures RGBA `2048 x 2048`, hors overhead GPU.

### 4. Cycle de vie

1. Pendant un chargement, cliquer sur `Cancel map load`.
2. Relancer et attendre la carte complète.
3. Cliquer sur `Clear map`, puis relancer.
4. Quitter vers le menu pendant un chargement.
5. Revenir dans `ChimeraMain` et reconstruire la carte.
6. Fermer le jeu avec la carte complète visible.

Aucune copie ne doit continuer après la fin de `ChimeraMain`, aucun ancien
handle ne doit réapparaître dans le monde suivant et aucun crash ne doit se
produire avec Streamline/DLSS actif.

## Éléments de preuve à conserver

- capture de la grille complète avec l'orientation retenue ;
- ligne `In-game map grid started` ;
- les dimensions minimales et maximales des tuiles copiées ;
- ligne `In-game map grid finished` ;
- temps total, temps maximal d'une copie et durée murale ;
- mémoire RGBA estimée ;
- résultat d'une annulation, d'un changement de monde et du shutdown ;
- version du jeu, du ModLoader, mode DLSS/FSR et résolution d'écran.

## Critère de passage à la projection

Passer au POC de calibration si :

- les 36 cases sont copiées et visibles ;
- les jointures forment une carte cohérente avec une orientation stable ;
- le chargement progressif ne provoque pas de freeze inacceptable ;
- la mémoire reste sous le budget avec un mip exploitable ;
- les cycles annulation, changement de monde et shutdown sont sûrs.

Le jalon suivant devra afficher au moins cinq landmarks dont les coordonnées
monde sont connues, comparer la transformation native dérivée du pivot et de
la taille des segments à la projection actuelle de `map_state_types.h`, puis
journaliser l'erreur en pixels et en unités monde.

## État de validation

Le POC compile dans la configuration `Client Debug|x64`. La validation visuelle
de l'assemblage, de l'orientation, du budget mémoire et du cycle de vie exige
maintenant une nouvelle exécution dans StarRupture.
