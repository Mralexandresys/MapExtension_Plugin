# Point 9: Configuration type-safe

## Priorité: AUCUNE — déjà implémenté

> ⚠️ Corrigé pour refléter le vrai SDK ET l'état réel du code.
> **Ce point est obsolète : `plugin_config.h` fait déjà exactement ce qui est
> recommandé ici, avec l'API officielle du SDK.**

### Prémisse initiale : FAUSSE
La version initiale affirmait que la config utilise « des defines pour les
chemins, des structs manuelles, du parsing INI manuel ». C'est inexact.
`plugin_config.h` définit déjà un `ConfigSchema` et l'installe via
`InitializeFromSchema`, puis lit chaque valeur avec les helpers type-safe :

```cpp
// plugin_config.h (extrait réel)
static const ConfigSchema SCHEMA = {
    CONFIG_ENTRIES,
    static_cast<int>(sizeof(CONFIG_ENTRIES) / sizeof(ConfigEntry))
};

static void Initialize(const IPluginSelf* self) {
    s_self = self;
    s_config = self ? self->config : nullptr;
    if (s_config && s_self)
        s_config->InitializeFromSchema(s_self, &SCHEMA);   // génère l'INI + defaults
}

static bool IsEnabled() {
    return (s_config && s_self) ? s_config->ReadBool(s_self, "General", "Enabled", true) : true;
}
// ... ReadBool/ReadInt pour Diagnostics, Http, etc.
```

Le parsing du fichier INF, la génération avec les valeurs par défaut et la
validation sont gérés **par le modloader** au travers de `InitializeFromSchema`
et `ValidateConfig`. Le plugin n'écrit aucun parseur.

### API réelle du SDK (à utiliser, et déjà utilisée)
```cpp
enum class ConfigValueType { String, Integer, Float, Boolean, Keybind };

struct ConfigEntry {
    const char* section;
    const char* key;             // (pas "name")
    ConfigValueType type;
    const char* defaultValue;
    const char* description;
    float rangeMin;              // bornes numériques
    float rangeMax;
};

struct ConfigSchema { const ConfigEntry* entries; int entryCount; };

struct IPluginConfig {
    bool  (*ReadString)(const IPluginSelf*, const char* section, const char* key,
                        char* outValue, int maxLen, const char* defaultValue);
    bool  (*WriteString)(const IPluginSelf*, const char* section, const char* key, const char* value);
    int   (*ReadInt)(const IPluginSelf*, const char* section, const char* key, int defaultValue);
    bool  (*WriteInt)(const IPluginSelf*, const char* section, const char* key, int value);
    float (*ReadFloat)(const IPluginSelf*, const char* section, const char* key, float defaultValue);
    bool  (*WriteFloat)(const IPluginSelf*, const char* section, const char* key, float value);
    bool  (*ReadBool)(const IPluginSelf*, const char* section, const char* key, bool defaultValue);
    bool  (*WriteBool)(const IPluginSelf*, const char* section, const char* key, bool value);
    bool  (*InitializeFromSchema)(const IPluginSelf*, const ConfigSchema* schema);
    void  (*ValidateConfig)(const IPluginSelf*, const ConfigSchema* schema);
};
```

### Corrections par rapport à la version initiale
- ❌ `ConfigValueType { Boolean, Integer, Float, String, Array, FilePath }` :
  faux. Les valeurs réelles sont `String, Integer, Float, Boolean, Keybind`
  (pas d'`Array`, pas de `FilePath`).
- ❌ `ConfigEntry` avec un champ `name` : le vrai champ est `key`, et il y a
  aussi `rangeMin`/`rangeMax`.
- ❌ `IsReady()`, `RegisterSchema()`, `ValidateSelf()`, `ReadAny()/WriteAny()`,
  `ReadInt32()`, `ReadString()` renvoyant `std::string` : **inventés**. Le vrai
  `ReadString` écrit dans un buffer `char*` de taille `maxLen`. Le schéma
  s'installe avec `InitializeFromSchema`, se valide avec `ValidateConfig`.
- ❌ La section « helpers » qui rouvre le fichier INI avec `std::ifstream` et le
  parse à la main : c'est précisément l'anti-pattern que le point prétend
  supprimer, et c'est incompatible avec le modèle du SDK. À ne pas faire.
- ⚠️ Les sections/clés proposées (`Runtime/WorkerThreads`, `Http/MaxConnections`,
  `Cargo/*`, `Debug/*`) ne correspondent pas au schéma réel du plugin
  (`General`, `Diagnostics`, `Http`, `Runtime/RefreshIntervalMs`, ...).

### Conclusion
Rien à faire : la configuration utilise déjà l'API type-safe officielle
(`ConfigSchema` + `InitializeFromSchema` + `ReadBool/ReadInt`). Ce document peut
être archivé/supprimé. Priorité : aucune.
