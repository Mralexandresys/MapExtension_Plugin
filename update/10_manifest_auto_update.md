# Point 10: Manifest sidecar & auto-update

## Priorité: BASSE (fonctionnalité fournie par le modloader, pas par le plugin)

> ⚠️ Corrigé pour refléter le vrai système décrit dans
> `StarRupture-Plugin-SDK/PluginDevelopment.md` (« Plugin Manifest & Auto-Update »).
> **Le modloader implémente déjà l'auto-update. Le plugin n'écrit AUCUN code de
> mise à jour** ; il fournit seulement deux fichiers, généralement produits par
> le workflow CI.

### Comment ça marche réellement
`RunAutoUpdate()` s'exécute dans le modloader **avant** le chargement des DLL, en
deux passes. Le plugin n'a rien à coder : il suffit de publier un **sidecar** et
un **manifest distant** au bon format.

**Sidecar** `MyPlugin.json`, posé à côté de `MyPlugin.dll`, **un seul champ** :

```json
{
  "manifest_url": "https://example.com/MyPlugin/releases/latest/download/MyPlugin-client-manifest.json"
}
```

**Manifest distant** (hébergé à l'URL ci-dessus, relu à chaque démarrage) :

```json
{
  "plugin_name":           "MapExtension_Plugin",
  "version":               "1.2.0",
  "interface_version_min": 46,
  "interface_version_max": 47,
  "download_url":          "https://example.com/releases/download/v1.2.0/MapExtension_Plugin-Client.dll"
}
```

| Champ | Rôle |
|-------|------|
| `plugin_name` | nom affiché dans les logs |
| `version` | comparé à `update_state.ini` pour décider du téléchargement |
| `interface_version_min` / `max` | plage d'interface supportée par ce build |
| `download_url` | URL directe du DLL à installer si mise à jour nécessaire |

Runtime : le modloader lit `Plugins\*.json`, fetch le `manifest_url`, compare la
`version` distante à `[PluginVersions]` dans `update_state.ini`, et si elles
diffèrent **et** que la plage d'interface est compatible, télécharge le DLL dans
un `.tmp` puis le renomme atomiquement par-dessus l'ancien. Tout est piloté par
`[AutoUpdate] Enabled` dans `modloader.ini`. Erreurs réseau/parse = non fatales.

### Corrections par rapport à la version initiale
- ❌ Format de sidecar inventé : `manifest_version`, `author`, `homepage`,
  `icon`, `enabled`, `update_info { check_interval, notify_immediate }`,
  `files`, `dependencies`, `permissions`, `log_level`. **Aucun** de ces champs
  n'existe. Le sidecar réel contient **uniquement** `manifest_url`.
- ❌ Code à écrire dans `plugin.cpp` (`CheckForUpdates`, `FetchAndCheckManifest`,
  `FetchUrl`, `ParseManifestJSON`, `NotifyUserOfUpdate`, `UpdateManagerState`,
  `OnEngineTick` qui vérifie les updates…) : **inutile et faux**. C'est le
  modloader qui gère tout, avant même de charger le DLL. Un plugin ne peut pas
  se mettre à jour lui-même en cours d'exécution (le fichier est verrouillé).
- ❌ Fichiers `.so` / builds Linux : ce projet est **Windows** (`.dll` + `.pdb`).
- ❌ `PluginShutdown(IPluginSelf*)` / `SaveUpdateState()` : signature fausse et
  fonction inexistante.
- ⚠️ `interface_version_min/max` doivent correspondre à la plage réelle du SDK
  utilisé (actuellement `PLUGIN_INTERFACE_VERSION_MIN 46`, `MAX 47`), pas
  `47..65535`.

### Ce qu'il faut faire concrètement (aucune ligne de C++)
Le SDK fournit un workflow CI réutilisable qui génère et publie le manifest, le
sidecar et le ZIP automatiquement :

1. Ajouter dans le repo du plugin `.github/workflows/release.yml` appelant
   `AlienXAXS/StarRupture-Plugin-SDK/.github/workflows/plugin-release.yml@main`,
   ou reproduire explicitement les mêmes étapes dans le workflow release propre
   au projet.
2. Le workflow build le DLL avec `/p:ModLoaderBuildTag=<semver>`, génère
   `MapExtension_Plugin-client-manifest.json` (nom, version, plage d'interface
   lue dans `plugin_interface.h`, URL de download), et un ZIP `Plugins\`
   contenant `MapExtension_Plugin.dll` + `MapExtension_Plugin.json` (sidecar) +
   `.pdb` si disponible.
3. Publier la release ; dire aux utilisateurs d'installer **depuis le ZIP**. Le
   sidecar câble alors toutes les mises à jour futures silencieusement.

En self-hosting hors GitHub : générer manifest + sidecar aux formats ci-dessus et
héberger le manifest à une URL stable (permalink `latest`, pas versionnée).

### État actuel de `MapExtension_Plugin`
Le workflow SDK réutilisable existe bien dans
`StarRupture-Plugin-SDK/.github/workflows/plugin-release.yml` et sait générer les
assets nécessaires à l'auto-update.

En revanche, le workflow actuel du plugin (`.github/workflows/release.yml`) est
un workflow custom : il build les DLL client/serveur, build `mapview`, puis crée
les archives `MapExtension_Plugin-<tag>-client.zip` et
`MapExtension_Plugin-<tag>-server.zip`. Dans l'état observé, ces archives ne
contiennent pas encore :

- `Plugins/MapExtension_Plugin.json` (sidecar avec `manifest_url`) ;
- `MapExtension_Plugin-client-manifest.json` publié comme asset de release ;
- un asset DLL direct dont l'URL est référencée par le manifest.

Donc l'auto-update est **supporté par le modloader et le SDK**, mais il n'est pas
encore activé par les packages actuels de `MapExtension_Plugin`. Pour l'activer,
il faut adapter le workflow custom afin de générer/publier le sidecar et le
manifest, ou remplacer ce workflow par l'appel au workflow réutilisable du SDK si
son format de packaging suffit au projet.

### Conclusion
L'auto-update existe déjà, côté modloader. Le plugin ne doit écrire aucun code
C++ d'update. Le travail restant est uniquement release/CI : publier un sidecar
1-champ et un manifest distant 5-champs, puis inclure le sidecar dans le ZIP
d'installation. Priorité basse.
