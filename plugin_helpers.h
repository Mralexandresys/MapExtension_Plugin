#pragma once

#include "plugin_interface.h"

IPluginLogger* GetLogger();
IPluginConfig* GetConfig();
IPluginScanner* GetScanner();
IPluginHooks* GetHooks();

#define LOG_TRACE(format, ...) if (auto logger = GetLogger()) logger->Trace("MapExtension_Plugin", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) if (auto logger = GetLogger()) logger->Debug("MapExtension_Plugin", format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) if (auto logger = GetLogger()) logger->Info("MapExtension_Plugin", format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) if (auto logger = GetLogger()) logger->Warn("MapExtension_Plugin", format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) if (auto logger = GetLogger()) logger->Error("MapExtension_Plugin", format, ##__VA_ARGS__)
