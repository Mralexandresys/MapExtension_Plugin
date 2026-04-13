# Resume du probleme SDK et cargo

## Ce qui n'allait pas

Le commit `e09ff7a` a migre le build de `MapExtension_Plugin` vers `../StarRupture-Plugin-SDK`.

Au moment du test, le sous-module `StarRupture-Plugin-SDK/StarRupture SDK` ne pointait pas sur le meme commit que le `StarRupture SDK` utilise par `StarRupture-ModLoader` pour la build jeu testee.

Cette divergence changeait les types SDK exposes au plugin, notamment sur la replication cargo :

- ancien layout attendu par le runtime/modloader : `SenderReceiversContainer`, `FCrSenderReceiverData`, `PrevReceiver`
- layout vu par le plugin avec le SDK non aligne : `ReceiversContainer`, `FCrReceiverData`, sans `PrevReceiver`

## Details techniques des classes et fonctions impactees

Classes et structs SDK concernees :

- `SDK::ACrPackageTransportReplicator`
- `SDK::FCrSenderReceiversContainer`
- `SDK::FCrSenderReceiverData`
- `SDK::FCrReceiversContainer`
- `SDK::FCrReceiverData`
- `SDK::FCrPackageTransportConnectionData`

Champs qui ne correspondaient plus entre les deux snapshots SDK :

- `ACrPackageTransportReplicator::SenderReceiversContainer`
- `ACrPackageTransportReplicator::ReceiversContainer`
- `FCrPackageTransportConnectionData::PrevReceiver`
- `FCrPackageTransportConnectionData::SenderLocation`
- `FCrSenderReceiverData::bIsSender`
- `FCrSenderReceiverData::Entity`
- `FCrReceiverData::Receiver`

Fonctions plugin directement impactees :

- `CaptureFromReplicator(...)` dans `map_state_capture.cpp`
- `RefreshCargoSnapshotImpl(...)` dans `map_state_capture.cpp`
- `RequestCargoSnapshotRefresh(...)` dans `map_state_capture.cpp`
- `OnExperienceLoadComplete()` dans `map_state_capture.cpp`
- l'endpoint HTTP `/cargo` dans `map_state_http.cpp`
- l'endpoint HTTP `/rupture-cycle` dans `map_state_http.cpp`

Fonction ajoutee puis retiree car dangereuse :

- `TryBootstrapCargoSaveState(...)`

Cette fonction forcait des callbacks lifecycle runtime sur des objets qui n'etaient pas censes etre pilotes par le plugin :

- `UCrMassSaveSubsystem::OnSaveLoaded()`
- `UCrMassSaveSubsystem::OnPostSaveLoaded()`
- `UCrLogisticsRequestSubsystem::OnSaveLoaded()`
- `ACrPackageTransportReplicator::OnSaveLoaded()`

Effet observe : crash pendant le refresh lance depuis `OnExperienceLoadComplete()`.

## Ce que ca impliquait

- le plugin compilait correctement
- mais il lisait des structures runtime differentes de celles reellement utilisees en jeu
- `/cargo` pouvait rester vide ou incomplet
- la reconstruction sender/receiver etait incorrecte
- un bootstrap runtime ajoute pour compenser (`OnSaveLoaded()` force sur plusieurs objets cargo) a introduit un risque de crash pendant `ExperienceLoadComplete`

## Cause racine

Le probleme principal n'etait pas le build lui-meme, mais un desalignement entre :

- le SDK embarque dans `StarRupture-Plugin-SDK`
- le SDK reellement correspondant au modloader/runtime utilise pour cette build

En pratique : build OK, comportement runtime faux.

## Correction appliquee

- alignement du sous-module `StarRupture-Plugin-SDK/StarRupture SDK` sur le meme commit que le SDK du ModLoader
- retour a la logique cargo historique compatible avec ce layout SDK
- suppression du bootstrap cargo artificiel base sur `OnSaveLoaded()`
- conservation du refresh HTTP utile pour `/rupture-cycle`

Concretement, la logique restauree s'appuie de nouveau sur :

- `CaptureFromReplicator(...)` avec `SenderReceiversContainer`
- l'utilisation de `FCrSenderReceiverData::bIsSender`
- la prise en compte de `FCrPackageTransportConnectionData::PrevReceiver`
- un refresh cargo normal depuis `OnExperienceLoadComplete()` sans bootstrap force

## Etat final

- la logique cargo refonctionne
- le crash observe pendant le refresh cargo n'apparait plus
- `main` contient maintenant :
  - `e09ff7a` `migrate plugin build to StarRupture-Plugin-SDK`
  - `ee59680` `restore cargo runtime compatibility`

## Point a retenir

Si `StarRupture-Plugin-SDK` et `StarRupture-ModLoader` n'utilisent pas exactement le meme snapshot de `StarRupture SDK`, un plugin peut compiler tout en etant incompatible au runtime.
