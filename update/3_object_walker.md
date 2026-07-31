# Point 3: hooks->ObjectWalker

## Priorité: MOYENNE (peut simplifier la découverte des subsystems)

> ⚠️ Corrigé pour refléter le vrai SDK (`plugin_interface.h`, `IPluginObjectWalker`, v47).

### API réelle
`ObjectWalker` remplit des buffers **appartenant au plugin** (aucune allocation
ne traverse la frontière DLL). Il n'utilise **pas** de `std::function`. Signature exacte :

```cpp
enum PluginObjectLookupMode : int32_t {
    PluginObjectLookup_Both         = 0, // CDOs, archetypes + instances vivantes
    PluginObjectLookup_InstanceOnly = 1, // ignore ClassDefaultObject + ArchetypeObject
    PluginObjectLookup_CDOOnly      = 2,
};

struct PluginObjectInfo {
    void*    object;
    char     className[128];
    char     objectName[256];
    uint32_t nameNumber;
    uint32_t objectFlags;
    int32_t  objectIndex;
};

struct IPluginObjectWalker {
    bool  (*IsReady)(); // true seulement après EngineInit (GObjects peuplé)

    int   (*WalkAllObjectsInto)(PluginObjectLookupMode mode,
                                PluginObjectInfo* outArray, int capacity);
    int   (*FindObjectsByClassNameInto)(const char* className, PluginObjectLookupMode mode,
                                        PluginObjectInfo* outArray, int capacity);
    void* (*FindFirstObjectByName)(const char* objectName);
    int   (*FindObjectsByNameInto)(const char* objectName, PluginObjectLookupMode mode,
                                   PluginObjectInfo* outArray, int capacity);

    bool  (*InvokeUFunctionByName)(void* object, const char* className,
                                   const char* funcName, void* paramsBuffer);
    void* (*ResolveUFunction)(const char* className, const char* funcName);
    bool  (*InvokeResolvedUFunction)(void* object, void* resolvedFunction, void* paramsBuffer);
};
```

Corrections par rapport à la version initiale :

- ❌ `FindNextObjectByName / FindFirstObjectByClass / FindNextObjectByClass` :
  n'existent pas. On récupère toutes les instances d'un coup avec
  `FindObjectsByNameInto` / `FindObjectsByClassNameInto` (retour = **nombre total**
  de matches, qui peut dépasser `capacity` → re-appeler avec un buffer plus grand).
- ❌ `FindObjects(FilterFunc)`, `WalkActors(OnActorFoundFunc)` avec `std::function` :
  impossibles via l'ABI C, inventés. Pour filtrer, on itère soi-même le buffer.
- ❌ `FindSaveSubsystem()` / `FindRuptureSubsystem()` : n'existent pas.
- ⚠️ Le retour du walk contient des `PluginObjectInfo`, pas des `AActor*` typés.
  Le champ `object` est un `void*` (un `SDK::UObject*`), à caster selon `className`.
- ⚠️ `WalkAllObjectsInto` est **coûteux** (dizaines de milliers d'objets) : mettre
  en cache, ne jamais l'appeler par tick.

### Exemple correct pour ce plugin
Le plugin cherche déjà `UCrEnviroWaveSubsystem` via
`USubsystemBlueprintLibrary::GetWorldSubsystem`, ce qui reste la voie la plus
directe. `ObjectWalker` peut servir de **fallback** ou de diagnostic si le
subsystem n'est pas résolu par la voie normale :

```cpp
IPluginHooks* hooks = GetHooks();
if (hooks && hooks->ObjectWalker && hooks->ObjectWalker->IsReady()) {
    void* wave = hooks->ObjectWalker->FindFirstObjectByName("CrEnviroWaveSubsystem");
    if (wave) {
        auto* subsystem = reinterpret_cast<SDK::UCrEnviroWaveSubsystem*>(wave);
        // lire l'état du wave...
    }
}
```

Note : `FindFirstObjectByName` fait un match **exact du FName** (sans le préfixe
de classe `U`/`A` ; c'est le nom d'instance, pas le nom de classe). Pour trouver
par classe, utiliser `FindObjectsByClassNameInto("CrEnviroWaveSubsystem", ...)`.

### Recherche typée de propriétés (bonus, v47)
Pour lire une UPROPERTY sans casser sur un changement de layout, le SDK fournit
`hooks->ObjectProperties` (`IPluginObjectProperties`) :
`FindPropertyByName(className, propertyName)` puis
`GetIntProperty/GetFloatProperty/GetObjectProperty/...`. Plus robuste que de
caster une struct SDK et lire à un offset compilé en dur.

### Conclusion
`ObjectWalker` est réel et utile comme **fallback de découverte** par nom/classe,
mais l'accès actuel via `GetWorldSubsystem` + SDK généré reste préférable pour le
chemin nominal. Le gain concret est limité au cas où la résolution normale
échoue. Priorité moyenne, à traiter avec les vraies signatures ci-dessus.
