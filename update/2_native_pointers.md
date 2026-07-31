# Point 2: hooks->NativePointers

## Priorité: BASSE (le plugin n'installe aucun hook bas niveau)

> ⚠️ Corrigé pour refléter le vrai SDK (`plugin_interface.h`, `IPluginHooks::NativePointers`, v21).

### Ce que NativePointers est réellement
`hooks->NativePointers` (`IPluginNativePointers`) ne donne **pas** des pointeurs
de subsystems prêts à l'emploi ni des callbacks. Il expose des **adresses de
trampolines** (`uintptr_t`) vers des fonctions moteur déjà résolues par le
modloader, destinées à ceux qui veulent poser un hook manuel bas niveau. Signature exacte :

```cpp
struct IPluginNativePointers {
    uintptr_t (*EngineLoopInit)();
    uintptr_t (*GameEngineInit)();
    uintptr_t (*EngineLoopExit)();
    uintptr_t (*EnginePreExit)();
    uintptr_t (*EngineTick)();
    uintptr_t (*WorldBeginPlay)();
    uintptr_t (*WorldEndPlay)();
    uintptr_t (*SaveLoaded)();
    uintptr_t (*ExperienceLoadComplete)();
    uintptr_t (*ActorBeginPlay)();
    uintptr_t (*PlayerJoined)();
    uintptr_t (*PlayerLeft)();
    uintptr_t (*SpawnerActivate)();
    uintptr_t (*SpawnerDeactivate)();
    uintptr_t (*SpawnerDoSpawning)();
    uintptr_t (*HUDPostRender)();      // client only (0 sur server/generic)
    uintptr_t (*ClientMessageExec)();  // client only (0 sur server/generic)
    uintptr_t (*CraftingFinished)();   // v44 : ACrCrafter::NativeOnItemCraftingComplete
};
```

Corrections par rapport à la version initiale de ce document :

- ❌ Il n'y a **pas** de `IsReady()` sur `IPluginNativePointers`.
- ❌ Il n'y a **pas** de `Render()`, `PlayerInput()`, `Network()`, `OnGameInit()`,
  `OnGameShutdown()`, `SaveSubsystem()`, `RuptureSubsystem()`, `WaveSubsystem()`,
  `Refresh()`, ni de callback `OnReady(...)`. Tout cela était inventé.
- ❌ Il n'existe **pas** de `hooks->Scanner` (le scanner est `self->scanner`, pas
  dans `IPluginHooks`) ni de méthode `scanner->ScanPattern(...)`. Le vrai scanner
  est `IPluginScanner` avec `FindPatternInMainModule`, `FindUniquePattern`, etc.
- ❌ Il n'existe **pas** de `RegisterOnRender` / `RegisterOnTick(cb, addr)`. La
  vraie signature est `hooks->Engine->RegisterOnTick(PluginEngineTickCallback)`
  (aucun paramètre d'adresse).
- ❌ « disponibles depuis l'interface version 47 » est faux : `NativePointers`
  existe depuis **v21**.

### Prémisse « le code scanne des patterns » : FAUSSE
`map_state_capture.cpp` ne fait **aucun** pattern scan. Il trouve ses objets via
le SDK de gameplay généré :

```cpp
SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrTeleporter::StaticClass(), &actors);
SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, subsystemClass); // UCrEnviroWaveSubsystem
```

et enregistre ses callbacks de cycle de vie via `hooks->Engine` /
`hooks->World` (cf. `map_state_runtime.cpp`). Il n'y a donc pas de « scan lent à
maintenir » à remplacer.

### Y a-t-il un intérêt pour ce plugin ?
Quasi nul en l'état. `NativePointers` sert à qui veut poser un détour manuel avec
`hooks->Hooks->Install(targetAddress, detour, &original)`. Le plugin utilise les
callbacks haut niveau déjà fournis (`Engine->RegisterOnTick`,
`World->RegisterOnSaveLoaded`), ce qui est plus sûr et suffisant.

Un seul cas où `NativePointers` deviendrait pertinent : capter la fin de craft
sans passer par `hooks->Crafting` — mais `hooks->Crafting->RegisterOnCraftingFinished`
existe déjà (v44) et est préférable.

### Conclusion
Rien à migrer. `NativePointers` est un outil de hooking bas niveau non nécessaire
ici. Si un jour on veut un détour custom, utiliser l'adresse voulue avec
`hooks->Hooks->Install(...)`. Priorité basse.
