#pragma once

#include <windows.h>
#include "plugin_interface.h"

extern "C" {
__declspec(dllexport) PluginInfo* GetPluginInfo();
__declspec(dllexport) bool PluginInit(IPluginSelf* self);
#if defined(MODLOADER_CLIENT_BUILD)
__declspec(dllexport) void OnPluginLoadHooks(IPluginSelf* self, IPluginHookScanner* scanner);
#endif
__declspec(dllexport) void PluginShutdown();
}
