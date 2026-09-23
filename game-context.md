# Contexte de jeu StarRupture pour MapExtension

Ce document décrit les concepts de jeu que `MapExtension_Plugin` observe, transporte
ou affiche. Il sert de référence métier pour éviter de confondre des données qui
se ressemblent techniquement mais n'ont pas le même sens pour le joueur.

Le périmètre est volontairement limité aux aspects attestés par le code du plugin,
le SDK utilisé par le projet et les exports locaux `analyse_map/map_v2_*` : monde
et carte, joueurs, logistique cargo, téléporteurs, ressources, gisements pour
extracteurs, points d'intérêt et cycle de rupture. Les autres systèmes de
StarRupture, comme le combat, la recherche, l'énergie, la construction générale ou
la progression, ne sont pas documentés ici tant qu'ils ne sont pas observés par ce
projet.

> **Règle générale**
>
> MapExtension combine un catalogue statique pré-généré et des observations live
> de la session active. Un point exporté, un acteur observé, un emplacement PCG,
> un socket d'extracteur et une ressource réellement disponible sont des concepts
> différents. Ils ne doivent jamais être présentés comme équivalents.

## Finalité de MapExtension

MapExtension est un outil de connaissance de situation. Il aide le joueur à
comprendre :

- où il se trouve et où se trouvent les autres joueurs ;
- où sont les points d'intérêt importants ;
- comment les bâtiments cargo sont reliés ;
- où sont les téléporteurs ;
- où se trouvent les ressources connues et les gisements exploitables par un
  bâtiment ;
- dans quel état se trouve le cycle de rupture.

Le plugin observe le jeu et publie des snapshots en lecture seule. Il ne :

- construit ni ne détruit de bâtiment ;
- ne pose pas d'extracteur sur un gisement ;
- ne crée pas et ne modifie pas de liaison cargo ;
- ne téléporte pas les joueurs ;
- ne récolte aucune ressource ;
- ne modifie aucune sauvegarde ;
- ne commande pas le cycle de rupture.

L'objet live principal s'appelle historiquement `CargoSnapshot`, mais il contient
bien plus que le cargo : joueurs, téléporteurs, POI live, monde, métadonnées de
capture et cycle de rupture.

## Monde et carte

### Monde suivi

Le monde principal suivi par le plugin est `ChimeraMain`. Le runtime associe ses
observations au monde actif et réinitialise son état mémorisé lors d'un changement
de monde ou de l'arrêt du moteur.

Deux sauvegardes différentes peuvent utiliser la même carte. Le nom du monde ne
suffit donc pas à identifier durablement une partie ou une sauvegarde.

### Coordonnées

StarRupture utilise les coordonnées monde Unreal :

- `x`, `y` et `z` sont exprimés en centimètres dans les données runtime ;
- `x` et `y` sont projetés sur l'image 2D de la carte ;
- `z` représente l'altitude et n'intervient pas dans la projection 2D ;
- l'image de référence utilisée par MapExtension mesure `9019 × 11691` pixels.

La projection de MapExtension est une transformation affine calibrée dans
`map_state_types.h`. Elle doit rester synchronisée avec `mapview`. Elle est la
projection utilisée par ce projet ; les exports disponibles ne prouvent pas qu'il
s'agit de la transformation interne exacte du menu de carte du jeu.

### World Partition et Data Layers

`World Partition` est le système Unreal qui charge et décharge les cellules du
monde. Une donnée live peut être absente simplement parce que sa cellule n'est pas
chargée près d'un joueur.

Les `Data Layers` sont des métadonnées Unreal de streaming et d'état runtime. Elles
ne représentent pas :

- des régions destinées au joueur ;
- des biomes à afficher automatiquement ;
- des phases du cycle de rupture.

## Catalogue statique et observations live

### Catalogue statique

Le catalogue distribué avec le viewer est généré à partir de :

- `map_v2_resources.jsonl` : ressources, points PCG et instances HISM ;
- `map_v2_placements.jsonl` : acteurs, bâtiments, volumes et sockets ;
- `map_v2_pois.geojson` : POI canoniques ;
- `map_v2_ore_veins.jsonl` : source privilégiée des gisements.

`map_v2_catalog.json` décrit les métadonnées et règles de rupture pour référence ;
le générateur ne le lit plus et ne publie plus son bloc rupture.

Il décrit le monde exporté, pas l'état courant d'une sauvegarde. Il ne permet pas,
à lui seul, de savoir si une ressource :

- est actuellement présente ;
- a déjà été récoltée ;
- vient de réapparaître ;
- possède encore une quantité exploitable.

### Observations live

Le plugin observe les acteurs et sous-systèmes accessibles dans la session active.
Les données live dépendent :

- des cellules chargées ;
- des acteurs répliqués ;
- des sous-systèmes disponibles ;
- de l'intervalle de rafraîchissement ;
- du transport réseau en session dédiée.

Certaines dernières positions connues sont conservées en mémoire dans le monde
actif, notamment pour les ressources observées puis déchargées ou épuisées. Cette
mémoire n'est pas un historique permanent par sauvegarde.

## Joueurs

Un marqueur joueur contient :

- sa position monde et sa position projetée sur la carte ;
- son nom affiché ;
- une source de capture ;
- une clé stable pour le snapshot ;
- l'indicateur `self`, qui désigne le joueur propre au client destinataire.

En solo/local, le premier contrôleur local est normalement considéré comme
`self`. Sur serveur dédié, le serveur marque séparément le joueur demandeur dans
le snapshot envoyé à chaque client.

`self` ne signifie pas chef de groupe, propriétaire du serveur ou joueur
sélectionné. L'ordre du tableau des joueurs n'a pas de signification de gameplay.

Une position joueur est une photographie périodique, pas un flux continu entre
deux rafraîchissements.

## Cargo et logistique

### Bâtiments cargo

MapExtension distingue deux rôles :

- `Cargo Dispatcher` : expéditeur de ressources ;
- `Cargo Receiver` : récepteur de ressources.

Un marqueur cargo contient son rôle, son nom, sa position, sa source et un résumé
des ressources observées dans ses connexions. Ce résumé n'est pas l'inventaire du
bâtiment.

### Connexions cargo

Une connexion cargo relie un `Cargo Dispatcher` à un `Cargo Receiver`. Elle
contient :

- les deux extrémités et leurs positions ;
- le type d'objet demandé ;
- la quantité demandée (`RequestedAmount`) ;
- la date de dernière observation.

La connexion représente un **flux demandé**. Elle ne prouve pas :

- qu'un nombre équivalent d'objets physiques est actuellement en transit ;
- que la livraison arrivera sans interruption ;
- que le récepteur possède déjà cette quantité en stock.

Le réplicateur de transport est la source privilégiée. Une détection de secours
par acteurs peut retrouver des bâtiments chargés, mais pas nécessairement leurs
connexions. Une connexion récemment observée peut être conservée brièvement pour
lisser une absence transitoire du réplicateur.

Le plugin n'administre aucune route : les lignes du viewer représentent seulement
les relations observées.

## Téléporteurs

Un téléporteur est affiché comme un point nommé avec une position monde et une
position carte. Les données peuvent venir du réplicateur de téléportation ou d'un
fallback sur les acteurs chargés.

Le modèle actuel ne contient aucune liaison entre téléporteurs. La présence d'un
marqueur ne prouve pas que le téléporteur est alimenté, activé, accessible ou
utilisable à l'instant affiché.

## Ressources

### Représentations dans les exports

Les ressources brutes utilisent plusieurs représentations complémentaires :

| `kind` | Sens | Ce que cela ne prouve pas |
|---|---|---|
| `resource_hism_instance` | Instance exacte d'un mesh de ressource déjà placé | Qu'elle est encore disponible dans la sauvegarde courante |
| `resource_point` | Point PCG précalculé où une ressource peut apparaître | Qu'une instance existe actuellement à cet endroit |
| `resource_actor` | Acteur, ancre ou marqueur fixe | Que chaque ressource enfant future apparaîtra exactement sur l'ancre |

Un même site peut avoir plusieurs représentations. Elles ne doivent pas être
additionnées mécaniquement comme autant de ressources distinctes.

L'export local recense notamment :

- des minerais ;
- des plantes ;
- des ressources animales ;
- des zones de génération et d'exclusion ;
- des sockets où un bâtiment extracteur peut être placé.

### Rochers minables à la main

Les minerais exportés comme `resource_hism_instance` sont des rochers ou meshes de
minerai interactifs, minables à la main. Ils ne sont pas de simples décorations.

MapExtension **ne les affiche pas individuellement**. Il existe plusieurs centaines
de milliers de lignes HISM ; les publier comme des gisements serait une erreur de
sens, car un rocher minable à la main n'est pas un emplacement réservé à un
bâtiment extracteur.

Les HISM minéraux servent uniquement d'index interne au générateur pour aider à
identifier le minerai et la pureté de certains sockets d'extracteur.

### Gisements pour extracteurs

Un gisement affiché par MapExtension correspond à un
`resource_deposit_socket` : un emplacement du monde où le joueur peut placer un
bâtiment extracteur.

Points essentiels :

- un marqueur `deposit` représente **un socket d'extracteur** ;
- il ne représente pas un rocher minable à la main ;
- il n'est pas le centroïde ni le regroupement de plusieurs rochers ;
- les sockets sont publiés individuellement, sans regroupement spatial ;
- le marqueur décrit un emplacement de construction/extraction, pas l'état live
  du bâtiment que le joueur pourrait y construire.

La source privilégiée est désormais `map_v2_ore_veins.jsonl`, qui fournit la
transformation exacte du socket, le minerai, la pureté, l'extracteur et la fiabilité
de la jointure. Sans ce fichier, le générateur utilise le rapprochement avec les
meshes minéraux et garde les puretés indéterminées à `unknown`.

Le catalogue distribué contient 660 gisements :

| Minerai | Gisements | Impurs | Normaux | Purs |
|---|---:|---:|---:|---:|
| Titane | 180 | 53 | 89 | 38 |
| Calcium | 102 | 22 | 56 | 24 |
| Tungstène / wolfram | 107 | 31 | 52 | 24 |
| Goethite | 102 | 0 | 102 | 0 |
| Soufre | 95 | 95 | 0 | 0 |
| Hélium-3 | 74 | 0 | 74 | 0 |
| **Total** | **660** | **201** | **373** | **86** |

Les douze anciens sockets non identifiés sont attribués au tungstène. Le type
`unknown_ore` est exclu du catalogue, y compris ses 389 anciennes ancres d'acteur.
Les représentations acteur/PCG redondantes avec un gisement du même minerai sont
retirées au build (tolérance de 500 cm sur chaque axe horizontal, sans comparaison
d'altitude), soit 366 points pour les exports actuels.

### Pureté des gisements

`impure`, `normal` et `pure` décrivent un rendement, pas la quantité restante,
l'épuisement ou la présence d'une foreuse. `unknown` reste une valeur valide pour
une pureté indéterminée, même si aucun des 660 gisements actuels ne l'utilise.

La pureté est exacte pour les 271 sockets spécialisés de goethite, soufre et
hélium-3. Pour les autres, elle est déduite de la jointure exportée : 282 à fiabilité
élevée, 100 moyenne et 7 faible (ces deux dernières catégories concernent le
tungstène). Les positions de socket exactes ne rendent pas ces puretés exactes.
La fiabilité reste consultable en préréglage Technique ; les vues joueur affichent
la pureté sans mention « estimée » ni marqueur creux lié à cette fiabilité.

Le bloc Gisements regroupe minerais et puretés. L'extracteur reste indiqué à la
sélection ; il n'a plus de filtre propre, chaque minerai correspondant à une seule
machine. Ces comptes décrivent l'export, jamais le stock d'une partie.

### Familles de ressources connues

#### Minerais

- calcium ;
- titane ;
- goethite ;
- hélium-3 ;
- soufre ;
- wolfram / minerai de tungstène ;
- quartz ;
- minerai inconnu ;
- Ignitium, ressource liée au cycle de rupture.

#### Plantes et ressources végétales

Le catalogue ou le runtime reconnaissent notamment :

- Hydrobulb ;
- Polifruit ;
- Oxallop ;
- Purplant / Pourprier ;
- Serpent Root / Ophidine ;
- Nootka Lupine ;
- Thornfruit ;
- Gold Fruit ;
- Sikkim Rhubarb ;
- Prickler ;
- Prism Herb ;
- Sulheart ;
- Star Tears.

La couverture et la représentation diffèrent selon les espèces. La présence d'un
nom dans cette liste ne garantit pas des positions statiques exhaustives.

#### Ressources animales

L'export contient notamment :

- œufs de coraillon ;
- œufs de vulpir ;
- viande de Skylisk.

### Disponibilité et épuisement

Une observation live peut indiquer qu'une ressource est :

- disponible ;
- épuisée ;
- récoltée définitivement ;
- inconnue lorsque l'état n'est pas observable.

L'épuisement normal et la récolte permanente sont distincts. Le catalogue indique
que l'épuisement permanent utilise un registre séparé et n'est pas annulé par la
régénération normale.

Une dernière position connue peut rester affichée après le déchargement de la
cellule. `depleted` est donc un dernier état observé, pas nécessairement une
vérification effectuée à l'instant exact où le navigateur l'affiche.

## Zones de génération

Les exports distinguent plusieurs familles de volumes :

- `resource_spawn_zone` : volume dans lequel le jeu peut générer des ressources ;
- `resource_exclusion_zone` : volume où cette génération est interdite ;
- `world_spawn_region` : région de génération générale du monde, notamment pour
  les créatures et ennemis ; ce n'est pas une zone de ressources ;
- `resource_spawn_marker` : point technique d'ancrage ou de génération.

Un volume décrit une règle spatiale, pas la présence certaine d'une ressource.
Les grands volumes PCG ne doivent pas être transformés en faux marqueurs précis.

## Points d'intérêt

### POI canoniques statiques

Le GeoJSON du catalogue contient `241` POI canoniques :

| Type | Nombre |
|---|---:|
| Grottes | 96 |
| Obélisques | 75 |
| Antennes / géoscanners | 39 |
| Bases abandonnées | 29 |
| Forgotten Engine | 1 |
| Orbital Lander | 1 |

Chaque POI canonique possède un nom, une description, un type, une position et un
`poi_guid`. Il s'agit de repères pré-générés, pas d'un état live d'accessibilité ou
de progression.

### POI live

Le snapshot live utilise quatre catégories :

- `abandoned_base` ;
- `plant_resource` ;
- `ignitium` ;
- `star_tears`.

Un POI live peut inclure un état d'épuisement, un nom de ressource, sa dernière
position connue et la source de capture.

Le mot « POI » a donc deux portées dans le projet : les repères canoniques statiques
et les catégories d'acteurs observés en direct. Elles ne doivent pas être
confondues.

## Cycle de rupture

### État exposé

Le cycle de rupture est représenté comme une timeline comprenant :

- `wave` : type de vague, notamment `Heat` ou `Cold` ;
- `stage` : étape principale nommée par le moteur, distincte de la phase affichée ;
- `step` : sous-phase ou étape ;
- `elapsed_seconds` : temps écoulé lorsque disponible ;
- `observed_at_unix_ms` : date de l'observation.

Les stages reconnus par le runtime comprennent :

- `PreWave` ;
- `Moving` ;
- `Fadeout` ;
- `Growback`.

Le viewer anime localement le temps entre deux snapshots à partir du temps écoulé
et de la date d'observation. Ses phases utilisent une calibration conservée de
3240 secondes (30/60/600/2550), qui peut diverger du jeu. Les réglages bruts Heat/Cold
ne se substituent pas directement à ces phases. Il ne commande pas le cycle.

Le front mobile de rupture n'est pas une zone statique dessinée sur la carte.

### Autorité de l'état

- en solo/local, le plugin utilise préférentiellement
  `UCrEnviroWaveSubsystem` ;
- sur serveur dédié, le serveur capture l'état autoritatif et l'envoie au client ;
- le client expose ensuite ce snapshot distant à son viewer HTTP local.

« Autoritatif » signifie que le serveur est la source retenue pour la session. Cela
ne supprime pas la périodicité de capture ni le délai réseau.

### Régénération liée à la rupture

Les ressources épuisées non permanentes peuvent être associées à un masque de
phase. Les bits exportés sont :

| Phase | Bit |
|---|---:|
| `Heat.PreWave` | 1 |
| `Heat.Moving` | 2 |
| `Heat.Fadeout` | 4 |
| `Heat.Growback` | 8 |
| `Cold.PreWave` | 16 |
| `Cold.Moving` | 32 |
| `Cold.Fadeout` | 64 |
| `Cold.Growback` | 128 |

Le catalogue indique qu'une nouvelle graine PCG globale est tirée lorsque `Heat`
entre dans le stage `Moving`. Les observations mémorisées d'Ignitium et de Star
Tears sont alors invalidées pour éviter de mélanger deux générations.

Les minerais ne suivent pas tous le même modèle de régénération :

- les minerais Mass/HISM ont une régénération temporisée dont la durée exacte
  n'est pas disponible dans l'export ;
- les acteurs de minerai placés `ACrOreActor` et `ACrMeteOreActor` se régénèrent à
  la fin de l'`EnviroWave` ;
- l'épuisement permanent n'est pas effacé par la régénération normale.

## Ignitium et Star Tears

Ignitium et Star Tears sont des ressources liées au cycle de rupture.

Détection runtime :

- Ignitium est reconnu sur un `ACrOreActor` dont la ressource est
  `I_FireWaveOre_C` ;
- Star Tears est reconnu sur un gatherable dont la récompense est
  `I_StarTears_C`.

Les grands volumes candidats ne sont pas convertis en positions précises. Une
position live exacte doit venir d'un acteur réellement observé.

Pendant la phase stable, un site Ignitium déjà validé et non récolté peut être
projeté comme Star Tears. Une observation réelle de Star Tears au même site reste
prioritaire. Pendant la transition, Ignitium et Star Tears peuvent coexister.

Un marqueur Star Tears projeté depuis un site Ignitium n'est donc pas forcément
l'observation directe d'un acteur Star Tears. Un site Ignitium épuisé ne doit pas
être converti en Star Tears.

## Solo, multijoueur et serveur dédié

### Solo ou partie locale

Le build client lit directement l'état local du monde : acteurs, réplicateurs et
sous-systèmes disponibles.

### Serveur dédié

Le build serveur capture les données autoritatives. Le build client :

1. demande un snapshot au serveur ;
2. reçoit et met en cache les données ;
3. expose ce cache sur l'API HTTP locale consommée par le navigateur.

Les deux builds doivent utiliser la même version du protocole. Le protocole actuel
est la version `5` et ne tente pas de downgrade. Les données synchronisées incluent
cycle de rupture, joueurs, téléporteurs, cargo, connexions et POI.

Une incompatibilité ou l'absence d'un snapshot distant peut produire des données
absentes ; cela ne prouve pas que les objets n'existent pas sur le serveur.

## Fraîcheur et limites temporelles

Le snapshot est une publication périodique, pas un journal exhaustif. Sa `generation`
avance à chaque rafraîchissement, même sans changement des observations ; la révision
de contenu POI sert séparément à invalider les pages réseau. Valeurs
importantes du comportement actuel :

- intervalle runtime documenté par défaut : `2000 ms` ;
- scan complet des POI live limité à environ `5000 ms` ;
- conservation temporaire d'une connexion cargo disparue : jusqu'à `15000 ms` ;
- appariement d'une plante live à un point statique : rayon de `150 cm` ;
- priorité d'un acteur Star Tears réel sur une projection : rayon horizontal de
  `300 cm`.

Une donnée absente peut signifier :

- acteur non chargé ;
- acteur non pris en charge ;
- réplicateur ou sous-système indisponible ;
- observation pas encore rafraîchie ;
- snapshot distant manquant ;
- protocole incompatible ;
- information absente des exports.

Elle ne signifie pas toujours que l'objet n'existe pas dans le monde.

## Ce que chaque donnée prouve

| Donnée | Ce qu'elle prouve | Ce qu'elle ne prouve pas |
|---|---|---|
| Joueur live | Position observée du pawn | Position continue entre deux snapshots |
| Connexion cargo | Flux demandé entre deux bâtiments | Quantité physique actuellement en transit |
| Téléporteur | Téléporteur observé à une position | Alimentation, activation ou destination liée |
| POI canonique | Repère statique nommé et positionné | État live ou accessibilité actuelle |
| Instance HISM minérale | Rocher interactif exporté | Gisement où poser un extracteur |
| Point PCG | Emplacement possible de génération | Ressource actuellement présente |
| `deposit` | Socket où poser un bâtiment extracteur | Rocher minable à la main ou bâtiment déjà construit |
| Pureté `unknown` | Pureté non déterminée | Pureté normale |
| Zone de génération | Volume où une génération peut se produire | Ressource présente à un point précis |
| Ressource `depleted` | Dernier état épuisé connu | Heure exacte de la récolte |
| Ignitium live | Acteur Ignitium réellement observé | Persistance après changement de graine |
| Star Tears projeté | Site Ignitium validé compatible avec la phase | Observation directe obligatoire de Star Tears |
| Stage de rupture | État du sous-système ou du serveur | Position du front de rupture |
| Data Layer | Métadonnée Unreal de streaming | Phase de rupture ou zone de gameplay |

## Glossaire EN/FR

| Anglais / identifiant | Français recommandé |
|---|---|
| world | monde |
| world coordinates | coordonnées monde |
| map coordinates | coordonnées carte |
| static world catalog | catalogue statique du monde |
| live observation | observation live / en direct |
| World Partition | World Partition / partition du monde |
| Data Layer | Data Layer / couche de données |
| point of interest / POI | point d'intérêt / POI |
| Cargo Dispatcher | Cargo Dispatcher / répartiteur cargo |
| Cargo Receiver | Cargo Receiver / récepteur cargo |
| requested amount | quantité demandée |
| teleporter | téléporteur |
| player marker | marqueur joueur |
| hand-mineable rock | rocher minable à la main |
| HISM instance | instance HISM |
| resource point | point de ressource |
| resource actor | acteur ou ancre de ressource |
| resource deposit socket | socket de gisement |
| extractor deposit | gisement pour extracteur |
| deposit purity | pureté du gisement |
| impure | impure |
| normal | normale |
| pure | pure |
| unknown | inconnue |
| depleted | épuisée / récoltée |
| permanently gathered | récoltée définitivement |
| spawn zone | zone de génération |
| exclusion zone | zone d'exclusion |
| rupture cycle | cycle de rupture |
| wave | vague |
| stage | phase principale |
| step / substage | étape / sous-phase |
| PreWave | avant-vague |
| Moving | déplacement |
| Fadeout | dissipation |
| Growback | repousse / régénération |
| PCG seed | graine PCG |

## Sources de vérité du projet

Lorsqu'un comportement change, maintenir ce document avec les sources suivantes :

- `map_state_types.h` : modèle live partagé ;
- `map_state_capture.cpp` : règles de capture et d'interprétation runtime ;
- `shared/map_sync_protocol.h` : données transportées en session dédiée ;
- `tools/build_map_data.py` : conversion du catalogue statique ;
- `analyse_map/readme.md` et `analyse_map/map_v2_catalog.json` : contenu et limites
  des exports ;
- `mapview/README.md` : comportement visible du viewer ;
- `README.md` et `README.fr.md` : contrat utilisateur général.

Toute nouvelle affirmation de gameplay doit être liée à une donnée exportée, un
champ runtime, une classe du SDK ou une observation reproductible. En cas de doute,
préférer `inconnu` à une valeur inventée.
