# Point 1: Gestion de l'accès à IPluginSelf

## Priorité: BASSE (nettoyage optionnel)

> ⚠️ Corrigé pour refléter le vrai SDK (`StarRupture-Plugin-SDK/include/plugin_interface.h`).
> Le SDK expose **un seul header** (`plugin_interface.h`) : il n'existe pas de
> `IPluginSelf.h`, `IPluginLogger.h`, `IPluginConfig.h`, `IPluginScanner.h`,
> `IPluginHooks.h` séparés. Toute structure décrite ici vient de ce header unique.

### État actuel (correct, pas un bug)
`plugin.cpp` conserve `IPluginSelf` et ses sous-interfaces dans des globals :

```cpp
static IPluginLogger* g_logger = nullptr;
static IPluginConfig* g_config = nullptr;
static IPluginScanner* g_scanner = nullptr;
static IPluginHooks* g_hooks = nullptr;
static const IPluginSelf* g_pluginSelf = nullptr;
```

Le SDK garantit que le pointeur `IPluginSelf*` passé à `PluginInit` est **stable
pour toute la durée de vie du plugin** (de `PluginInit` jusqu'au retour de
`PluginShutdown`) — cf. « API Reference » dans `PluginDevelopment.md`. Le stocker
dans un static est donc le pattern **recommandé** par le SDK, pas un défaut.

Il n'y a pas de rechargement à chaud du plugin dans le modloader, donc
l'argument « invalidé si le plugin est rechargé » ne s'applique pas.

### Amélioration réelle possible: un wrapper d'accès mince (facultatif)

Si on veut un accès plus explicite, on peut ajouter un petit helper **sans
changer la sémantique**. Signatures exactes du SDK :

```cpp
struct IPluginSelf {
    const char*     name;
    const char*     version;
    IPluginLogger*  logger;
    IPluginConfig*  config;
    IPluginScanner* scanner;
    IPluginHooks*   hooks;
};
```

```cpp
// plugin_helpers.h (ajout optionnel)
class PluginSelf {
    const IPluginSelf* self_;
public:
    explicit PluginSelf(const IPluginSelf* s) : self_(s) {}
    const IPluginSelf* raw()  const { return self_; }
    IPluginLogger*  logger()  const { return self_ ? self_->logger  : nullptr; }
    IPluginConfig*  config()  const { return self_ ? self_->config  : nullptr; }
    IPluginScanner* scanner() const { return self_ ? self_->scanner : nullptr; }
    IPluginHooks*   hooks()   const { return self_ ? self_->hooks   : nullptr; }
};
```

Remarques importantes (par rapport à la version initiale de ce document) :

- ❌ Pas de destructeur qui « nettoie » : rien n'est possédé, mettre le pointeur
  à `nullptr` dans un destructeur ne sert à rien et donne une fausse impression
  de gestion de ressource.
- ❌ Ce wrapper **n'apporte pas** de thread-safety. Les pointeurs sont déjà
  constants ; le vrai sujet de concurrence (le callback `OnEngineTick` s'exécute
  sur le game thread pendant qu'une requête HTTP lit le snapshot) se règle avec
  un mutex sur les **données**, pas sur `IPluginSelf`.
- ✅ Les fonctions de config prennent **toujours** `const IPluginSelf* self` en
  premier argument. Exemple correct :
  `config->ReadBool(self, "General", "Enabled", true);`
  (Pas `config->RegisterSchema(...)` — cette fonction n'existe pas, voir Point 9.)

### API de log réelle (rappel)
Les macros `LOG_*` de `plugin_helpers.h` passent déjà `self` correctement :

```cpp
struct IPluginLogger {
    void (*Log)(PluginLogLevel level, const IPluginSelf* self, const char* message);
    void (*Trace)(const IPluginSelf* self, const char* format, ...);
    void (*Debug)(const IPluginSelf* self, const char* format, ...);
    void (*Info) (const IPluginSelf* self, const char* format, ...);
    void (*Warn) (const IPluginSelf* self, const char* format, ...);
    void (*Error)(const IPluginSelf* self, const char* format, ...);
};
```

### Conclusion
Le code actuel est conforme au contrat du SDK. Un wrapper `PluginSelf` est un
confort purement cosmétique ; il ne corrige aucun bug et n'apporte aucune
garantie de threading. Priorité basse.
