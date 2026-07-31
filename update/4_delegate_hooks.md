# Point 4: hooks->Delegate (IPluginDelegateHook)

## Priorité: BASSE→MOYENNE (utile seulement pour capter un delegate précis)

> ⚠️ Corrigé pour refléter le vrai SDK (`plugin_interface.h`, `IPluginDelegateHook`, v47).

### API réelle : bas niveau, paramétrable, sans events nommés
`hooks->Delegate` **n'est pas** un catalogue d'événements de jeu prêts à l'emploi.
C'est un mécanisme générique qui splice une `UFunction` synthétique dans un
`TMulticastInlineDelegate<...>` UE5 existant, dont on doit fournir le **pointeur**.
Le callback ne reçoit **que** `userContext` (pas les arguments du broadcast).
Signature exacte :

```cpp
typedef uint64_t DelegateHookHandle;
typedef void (*PluginDelegateCallback)(void* userContext);

struct IPluginDelegateHook {
    DelegateHookHandle (*Hook)(void* delegatePtr, void* hostObject,
                               const char* hostClassName, const char* hostFuncName,
                               PluginDelegateCallback callback, void* userContext);
    bool (*Unhook)(DelegateHookHandle handle);
    bool (*IsHooked)(DelegateHookHandle handle);
};
```

- `delegatePtr` : adresse d'un membre delegate multicast **vivant** (ex.
  `&saveSubsystem->OnAfterSave`), à obtenir soi-même via le SDK / ObjectProperties.
- `hostObject` : l'objet qui « broadcast » (en général l'objet portant le delegate).
- `hostClassName` / `hostFuncName` : **optionnels**, passer `nullptr`/`nullptr`
  pour utiliser le template intégré du modloader (cas courant).
- retourne `0` en cas d'échec, sinon un handle pour `Unhook`/`IsHooked`.

### Corrections par rapport à la version initiale
- ❌ `IPluginDelegate` avec `RegisterOnGameInitialized / OnTick / OnRender /
  OnSaveStarted / OnAfterSave / OnLoadStarted / OnAfterLoad / OnWaveStarted /
  OnWaveEnded / OnWavePhaseChanged(int)` : **tout inventé**. Aucun de ces
  helpers n'existe. Le vrai nom de l'interface est `IPluginDelegateHook`.
- ❌ `IsReady()` : n'existe pas sur `IPluginDelegateHook`.
- ❌ Callbacks recevant `float delta` ou `int phase` : impossible, le callback
  est **paramétrique-libre** (`void(void* userContext)`).
- ❌ `hooks->Save->RegisterOnSaveLoaded` : `IPluginHooks` n'a pas de champ `Save`.
  Le save-loaded est sur `hooks->World->RegisterOnSaveLoaded(...)`.
- ❌ `PluginInit(IPluginSelf*, IPluginDelegate*)` et `PluginShutdown(IPluginSelf*)` :
  signatures fausses. Les vraies sont `bool PluginInit(IPluginSelf*)` et
  `void PluginShutdown()`.
- ❌ Lambdas avec capture passées comme callback : impossible via l'ABI C
  (pointeur de fonction nu requis) ; utiliser `userContext` pour l'état.

### Ce que le plugin utilise déjà (et qui suffit souvent)
Le cycle de vie passe par les vrais hooks haut niveau (voir `map_state_runtime.cpp`) :

```cpp
hooks->Engine->RegisterOnInit(OnEngineInit);
hooks->Engine->RegisterOnShutdown(OnEngineShutdown);
hooks->Engine->RegisterOnTick(OnEngineTick);           // refresh temps réel
hooks->Actors->RegisterOnActorBeginPlay(OnActorBeginPlay);
hooks->World->RegisterOnAnyWorldBeginPlay(OnAnyWorldBeginPlay);
hooks->World->RegisterOnBeforeWorldEndPlay(OnBeforeWorldEndPlay);
hooks->World->RegisterOnAfterWorldEndPlay(OnAfterWorldEndPlay);
hooks->Players->RegisterOnPlayerJoined(OnPlayerJoined);
```

À noter : `RegisterOnSaveLoaded` et `RegisterOnExperienceLoadComplete` existent bien côté SDK, mais ils sont volontairement **non enregistrés** dans l'état actuel du plugin. Les commentaires de `map_state_runtime.cpp` indiquent que ces chemins ont été désactivés sur la build line courante pour éviter des crashs / refreshs trop précoces ; le runtime s'appuie donc sur les chemins world/actor/tick plus sûrs.

Pour la fin de craft il y a `hooks->Crafting->RegisterOnCraftingFinished(...)` (v44), mais le plugin ne l'utilise pas actuellement.

### Quand `IPluginDelegateHook` devient pertinent
Uniquement pour réagir à un delegate **précis** non couvert par un hook dédié,
par ex. `UCrEnviroWaveSubsystem::On<...>` si un tel delegate multicast existe et
qu'on en a le pointeur. Exemple correct :

```cpp
static void OnWaveEvent(void* userContext) {
    // pas d'arguments du broadcast : re-lire l'état du subsystem ici
    RefreshRuptureCycleFromSubsystem();
}

DelegateHookHandle g_waveHook = 0;

void HookWaveDelegate(SDK::UCrEnviroWaveSubsystem* wave) {
    IPluginHooks* hooks = GetHooks();
    if (!hooks || !hooks->Delegate || !wave) return;
    void* delegatePtr = /* &wave->OnSomethingChanged (à résoudre via le SDK) */;
    g_waveHook = hooks->Delegate->Hook(delegatePtr, wave,
                                       nullptr, nullptr, // template intégré
                                       &OnWaveEvent, nullptr);
}

void PluginShutdown() {
    IPluginHooks* hooks = GetHooks();
    if (hooks && hooks->Delegate && g_waveHook) hooks->Delegate->Unhook(g_waveHook);
}
```

### Conclusion
`IPluginDelegateHook` est un outil générique bas niveau, pas une liste
d'événements. Il n'apporte rien tant qu'aucun delegate précis n'est ciblé, et le
callback sans arguments impose de relire l'état à la main. Le plugin est déjà
correctement câblé via les hooks haut niveau `Engine`/`Actors`/`World`/`Players`.
Priorité basse à moyenne, à n'utiliser que pour un delegate identifié.
