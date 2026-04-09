#pragma once

#include "plugin_interface.h"

IPluginLogger* GetLogger();
IPluginConfig* GetConfig();
IPluginScanner* GetScanner();
IPluginHooks* GetHooks();
const IPluginSelf* GetPluginSelf();

#define LOG_TRACE(format, ...) do { if (auto logger = GetLogger()) if (auto self = GetPluginSelf()) logger->Trace(self, format, ##__VA_ARGS__); } while (0)
#define LOG_DEBUG(format, ...) do { if (auto logger = GetLogger()) if (auto self = GetPluginSelf()) logger->Debug(self, format, ##__VA_ARGS__); } while (0)
#define LOG_INFO(format, ...) do { if (auto logger = GetLogger()) if (auto self = GetPluginSelf()) logger->Info(self, format, ##__VA_ARGS__); } while (0)
#define LOG_WARN(format, ...) do { if (auto logger = GetLogger()) if (auto self = GetPluginSelf()) logger->Warn(self, format, ##__VA_ARGS__); } while (0)
#define LOG_ERROR(format, ...) do { if (auto logger = GetLogger()) if (auto self = GetPluginSelf()) logger->Error(self, format, ##__VA_ARGS__); } while (0)
