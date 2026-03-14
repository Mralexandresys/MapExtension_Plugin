#include "map_state_capture.h"

#include "plugin_config.h"
#include "plugin_helpers.h"

#include "AuItems_classes.hpp"
#include "BP_PackageReceiver_classes.hpp"
#include "BP_PackageSender_classes.hpp"
#include "BP_Teleporter_classes.hpp"
#include "Chimera_classes.hpp"
#include "ChimeraUI_classes.hpp"
#include "Chimera_structs.hpp"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"
#include "UMG_classes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

using CargoConnection = MapStateRuntime::Detail::CargoConnection;
using CargoKind = MapStateRuntime::Detail::CargoKind;
using CargoMarker = MapStateRuntime::Detail::CargoMarker;
using CargoSnapshot = MapStateRuntime::Detail::CargoSnapshot;
using PlayerMarker = MapStateRuntime::Detail::PlayerMarker;
using ReceiverLinkInfo = MapStateRuntime::Detail::ReceiverLinkInfo;
using TeleporterMarker = MapStateRuntime::Detail::TeleporterMarker;

namespace
{
	constexpr int kMaxLoggedMarkersPerSnapshot = 8;
	constexpr int kMaxLoggedActorsPerKind = 4;
	constexpr int kMaxLoggedEnviroWaveActorClasses = 8;
	bool g_runtimePlanLogged = false;
	bool g_engineTickSeen = false;
	int g_anyWorldBeginPlayCount = 0;
	int g_saveLoadedCount = 0;
	int g_experienceLoadedCount = 0;
	SDK::UWorld* g_lastChimeraWorld = nullptr;
	std::string g_lastWorldName;
	bool g_chimeraWorldReady = false;
	bool g_enviroWaveDiagnosticsReady = false;
	float g_engineTickAccumulatorSeconds = 0.0f;

	std::mutex g_snapshotMutex;
	CargoSnapshot g_snapshot{};
	std::unordered_set<uint32_t> g_requestedCustomNameEntityIds;
	std::string g_lastEnviroWaveStateSignature;
	bool g_enviroWaveStateSignatureValid = false;
	std::string g_lastEnviroWaveTimerSignature;
	bool g_enviroWaveTimerSignatureValid = false;
	std::string g_lastRuptureCycleChatSignature;
	bool g_lastRuptureCycleChatSignatureValid = false;
	std::string g_lastRuptureCycleObservedSignature;
	bool g_lastRuptureCycleObservedSignatureValid = false;
	int64_t g_lastRuptureCycleObservedAtUnixMs = 0;
	std::string g_lastEnviroWaveActorInventorySignature;
	bool g_enviroWaveActorInventorySignatureValid = false;

	bool ShouldLogLifecycle()
	{
		return MapExtensionPluginConfig::Config::VerboseLifecycleLogs();
	}

	bool ShouldLogCargoSnapshots()
	{
		return MapExtensionPluginConfig::Config::LogCargoSnapshots();
	}

	bool ShouldLogRuptureCycleChat()
	{
		return MapExtensionPluginConfig::Config::LogRuptureCycleChat();
	}

	bool ShouldLogEnviroWaveDiagnostics()
	{
		return MapExtensionPluginConfig::Config::LogEnviroWaveDiagnostics();
	}

	bool ShouldLogEnviroWaveActorInventory()
	{
		return MapExtensionPluginConfig::Config::LogEnviroWaveActorInventory();
	}

	bool ShouldLogEnviroWavePostCaptureLogs()
	{
		return MapExtensionPluginConfig::Config::LogEnviroWavePostCaptureLogs();
	}

	bool ShouldLogActorScanFallback()
	{
		return MapExtensionPluginConfig::Config::LogActorScanFallback();
	}

	float GetRefreshIntervalSeconds()
	{
		int refreshMs = MapExtensionPluginConfig::Config::RefreshIntervalMs();
		if (refreshMs < 100)
		{
			refreshMs = 100;
		}
		return static_cast<float>(refreshMs) / 1000.0f;
	}

	bool IsChimeraWorldName(const std::string& worldName)
	{
		return worldName.find("ChimeraMain") != std::string::npos;
	}

	int GetEnviroWaveHeartbeatSnapshotInterval()
	{
		int refreshMs = MapExtensionPluginConfig::Config::RefreshIntervalMs();
		if (refreshMs < 100)
		{
			refreshMs = 100;
		}

		const int heartbeatMs = 5000;
		return std::max(1, (heartbeatMs + refreshMs - 1) / refreshMs);
	}

	const char* BoolToYesNo(bool value)
	{
		return value ? "yes" : "no";
	}

	const char* EnviroWaveToString(SDK::EEnviroWave wave)
	{
		switch (wave)
		{
		case SDK::EEnviroWave::None:
			return "None";
		case SDK::EEnviroWave::Heat:
			return "Heat";
		case SDK::EEnviroWave::Cold:
			return "Cold";
		default:
			return "Unknown";
		}
	}

	const char* EnviroWaveStageToString(SDK::EEnviroWaveStage stage)
	{
		switch (stage)
		{
		case SDK::EEnviroWaveStage::None:
			return "None";
		case SDK::EEnviroWaveStage::PreWave:
			return "PreWave";
		case SDK::EEnviroWaveStage::Moving:
			return "Moving";
		case SDK::EEnviroWaveStage::Fadeout:
			return "Fadeout";
		case SDK::EEnviroWaveStage::Growback:
			return "Growback";
		default:
			return "Unknown";
		}
	}

	const char* PreWaveSubstageToString(SDK::EEnviroWavePreWaveSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWavePreWaveSubstage::None:
			return "None";
		case SDK::EEnviroWavePreWaveSubstage::BeforeExplosion:
			return "BeforeExplosion";
		case SDK::EEnviroWavePreWaveSubstage::AfterExplosion:
			return "AfterExplosion";
		default:
			return "Unknown";
		}
	}

	const char* FadeoutSubstageToString(SDK::EEnviroWaveFadeoutSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWaveFadeoutSubstage::None:
			return "None";
		case SDK::EEnviroWaveFadeoutSubstage::FireWave:
			return "FireWave";
		case SDK::EEnviroWaveFadeoutSubstage::Burning:
			return "Burning";
		case SDK::EEnviroWaveFadeoutSubstage::Fading:
			return "Fading";
		default:
			return "Unknown";
		}
	}

	const char* GrowbackSubstageToString(SDK::EEnviroWaveGrowbackSubstage substage)
	{
		switch (substage)
		{
		case SDK::EEnviroWaveGrowbackSubstage::None:
			return "None";
		case SDK::EEnviroWaveGrowbackSubstage::MoonPhase:
			return "MoonPhase";
		case SDK::EEnviroWaveGrowbackSubstage::RegrowthStart:
			return "RegrowthStart";
		case SDK::EEnviroWaveGrowbackSubstage::Regrowth:
			return "Regrowth";
		default:
			return "Unknown";
		}
	}

	const char* GetRuptureCycleChatPrefix()
	{
		return MapExtensionPluginConfig::Config::RuptureCyclePrefix();
	}

	struct EnviroWaveActorClassInfo final
	{
		std::string ClassName;
		int Count = 0;
		std::string SampleActorName;
		SDK::AActor* SampleActor = nullptr;
	};

	struct EnviroWaveActorInventory final
	{
		bool GObjectsAvailable = false;
		int MatchingActorCount = 0;
		std::vector<EnviroWaveActorClassInfo> Classes;
	};

	struct RuptureCycleChatState final
	{
		bool GObjectsAvailable = false;
		bool ChatHudFound = false;
		bool PrefixFound = false;
		bool Parsed = false;
		bool HasSequence = false;
		uint64_t Sequence = 0;
		std::string RawLine;
		std::string Payload;
		std::string Wave;
		std::string Stage;
		std::string Step;
		double Progress = 0.0;
		bool ProgressValid = false;
		bool InProgress = false;
		bool InProgressValid = false;
		bool Paused = false;
		bool PausedValid = false;
		int NextPhase = 0;
		bool NextPhaseValid = false;
		double NextTime = 0.0;
		bool NextTimeValid = false;
		double SinceLastStart = 0.0;
		bool SinceLastStartValid = false;
	};

	bool TryIsObjectInWorld(SDK::UObject* obj, SDK::UWorld* world)
	{
		if (!obj || !world)
		{
			return false;
		}

		__try
		{
			SDK::UObject* outer = obj->Outer;
			return outer && (outer->Outer == static_cast<SDK::UObject*>(world));
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool TryIsActorObject(SDK::UObject* obj, SDK::UClass* actorClass)
	{
		if (!obj || !actorClass)
		{
			return false;
		}

		__try
		{
			return obj->IsA(actorClass);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	template <typename TObjectClass>
	SDK::UClass* TryGetStaticClass()
	{
		__try
		{
			return TObjectClass::StaticClass();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return nullptr;
		}
	}

	bool TryObjectHasOuterInChain(SDK::UObject* obj, SDK::UObject* targetOuter, int maxDepth = 12)
	{
		if (!obj || !targetOuter || maxDepth <= 0)
		{
			return false;
		}

		__try
		{
			SDK::UObject* current = obj;
			for (int depth = 0; current && depth < maxDepth; ++depth)
			{
				if (current == targetOuter)
				{
					return true;
				}
				current = current->Outer;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}

		return false;
	}

	bool TryParseYesNo(const std::string& value, bool& outValue)
	{
		if (value == "yes")
		{
			outValue = true;
			return true;
		}
		if (value == "no")
		{
			outValue = false;
			return true;
		}
		return false;
	}

	bool TryParseInt(const std::string& value, int& outValue)
	{
		if (value.empty())
		{
			return false;
		}

		char* end = nullptr;
		const long parsed = std::strtol(value.c_str(), &end, 10);
		if (!end || *end != '\0')
		{
			return false;
		}

		outValue = static_cast<int>(parsed);
		return true;
	}

	bool TryParseUInt64(const std::string& value, uint64_t& outValue)
	{
		if (value.empty())
		{
			return false;
		}

		char* end = nullptr;
		const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
		if (!end || *end != '\0')
		{
			return false;
		}

		outValue = static_cast<uint64_t>(parsed);
		return true;
	}

	bool TryParseDouble(const std::string& value, double& outValue)
	{
		if (value.empty())
		{
			return false;
		}

		char* end = nullptr;
		const double parsed = std::strtod(value.c_str(), &end);
		if (!end || *end != '\0')
		{
			return false;
		}

		outValue = parsed;
		return true;
	}

	std::string TrimWhitespace(std::string value)
	{
		const auto isSpace = [](unsigned char c)
		{
			return c == ' ' || c == '\t' || c == '\r' || c == '\n';
		};

		while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
		{
			value.erase(value.begin());
		}

		while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
		{
			value.pop_back();
		}

		return value;
	}

	std::string FindLastLineContainingPrefix(const std::string& text, const std::string& prefix)
	{
		if (text.empty() || prefix.empty())
		{
			return std::string();
		}

		const size_t prefixPos = text.rfind(prefix);
		if (prefixPos == std::string::npos)
		{
			return std::string();
		}

		size_t lineStart = text.rfind('\n', prefixPos);
		lineStart = (lineStart == std::string::npos) ? 0 : (lineStart + 1);
		size_t lineEnd = text.find('\n', prefixPos);
		if (lineEnd == std::string::npos)
		{
			lineEnd = text.size();
		}

		return TrimWhitespace(text.substr(lineStart, lineEnd - lineStart));
	}

	std::string StripRichTextMarkup(const std::string& value)
	{
		if (value.empty())
		{
			return std::string();
		}

		std::string stripped;
		stripped.reserve(value.size());

		bool insideTag = false;
		for (char ch : value)
		{
			if (ch == '<')
			{
				insideTag = true;
				continue;
			}
			if (insideTag)
			{
				if (ch == '>')
				{
					insideTag = false;
				}
				continue;
			}
			stripped.push_back(ch);
		}

		return stripped;
	}

	bool TryGetChatHistoryWidget(SDK::UCrUW_ChatHud* chatHud, SDK::URichTextBlock*& outChatHistory)
	{
		outChatHistory = nullptr;
		if (!chatHud)
		{
			return false;
		}

		__try
		{
			outChatHistory = chatHud->ChatHistory;
			return outChatHistory != nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			outChatHistory = nullptr;
			return false;
		}
	}

	bool TryGetRichTextBlockText(SDK::URichTextBlock* richTextBlock, SDK::FText& outText)
	{
		if (!richTextBlock)
		{
			return false;
		}

		__try
		{
			outText = richTextBlock->GetText();
			return outText.TextData != nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool TryGetChatHistoryText(SDK::UCrUW_ChatHud* chatHud, std::string& outText)
	{
		outText.clear();
		if (!chatHud)
		{
			return false;
		}

		SDK::URichTextBlock* chatHistory = nullptr;
		SDK::FText chatHistoryText{};

		if (!TryGetChatHistoryWidget(chatHud, chatHistory))
		{
			return false;
		}

		if (!TryGetRichTextBlockText(chatHistory, chatHistoryText))
		{
			return false;
		}

		outText = chatHistoryText.ToString();
		return !outText.empty();
	}

	bool TryParseRuptureCyclePayload(const std::string& payload, RuptureCycleChatState& outState)
	{
		std::istringstream iss(payload);
		std::string token;
		bool foundField = false;
		while (iss >> token)
		{
			const size_t equalsPos = token.find('=');
			if (equalsPos == std::string::npos || equalsPos == 0 || equalsPos == (token.size() - 1))
			{
				continue;
			}

			foundField = true;
			const std::string key = token.substr(0, equalsPos);
			const std::string value = token.substr(equalsPos + 1);

			if (key == "seq")
			{
				outState.HasSequence = TryParseUInt64(value, outState.Sequence);
			}
			else if (key == "wave" || key == "type")
			{
				outState.Wave = value;
			}
			else if (key == "stage")
			{
				outState.Stage = value;
			}
			else if (key == "step")
			{
				outState.Step = value;
			}
			else if (key == "pre" && value != "None")
			{
				outState.Step = value;
			}
			else if (key == "fade" && value != "None")
			{
				outState.Step = value;
			}
			else if (key == "grow" && value != "None")
			{
				outState.Step = value;
			}
			else if (key == "progress")
			{
				outState.ProgressValid = TryParseDouble(value, outState.Progress);
			}
			else if (key == "in_progress")
			{
				outState.InProgressValid = TryParseYesNo(value, outState.InProgress);
			}
			else if (key == "paused")
			{
				outState.PausedValid = TryParseYesNo(value, outState.Paused);
			}
			else if (key == "next_phase")
			{
				outState.NextPhaseValid = TryParseInt(value, outState.NextPhase);
			}
			else if (key == "next_time")
			{
				outState.NextTimeValid = TryParseDouble(value, outState.NextTime);
			}
			else if (key == "elapsed" || key == "since_last_start")
			{
				outState.SinceLastStartValid = TryParseDouble(value, outState.SinceLastStart);
			}
		}

		outState.Parsed = foundField;
		return outState.Parsed;
	}

	RuptureCycleChatState CaptureRuptureCycleChatState(SDK::UWorld* world)
	{
		RuptureCycleChatState state{};
		SDK::TUObjectArray* objectArray = SDK::UObject::GObjects.GetTypedPtr();
		if (!objectArray || objectArray->NumElements <= 0)
		{
			return state;
		}

		state.GObjectsAvailable = true;

		SDK::UClass* chatHudClass = TryGetStaticClass<SDK::UCrUW_ChatHud>();
		if (!chatHudClass)
		{
			return state;
		}

		const std::string prefix = GetRuptureCycleChatPrefix();
		RuptureCycleChatState fallbackState{};
		fallbackState.GObjectsAvailable = true;

		for (int32_t index = 0; index < objectArray->NumElements; ++index)
		{
			SDK::UObject* object = objectArray->GetByIndex(index);
			if (!object || !object->Class)
			{
				continue;
			}

			if (!TryIsActorObject(object, chatHudClass))
			{
				continue;
			}

			state.ChatHudFound = true;
			SDK::UCrUW_ChatHud* chatHud = static_cast<SDK::UCrUW_ChatHud*>(object);
			std::string chatText;
			if (!TryGetChatHistoryText(chatHud, chatText))
			{
				continue;
			}

			RuptureCycleChatState candidate{};
			candidate.GObjectsAvailable = true;
			candidate.ChatHudFound = true;
			candidate.RawLine = FindLastLineContainingPrefix(chatText, prefix);
			if (candidate.RawLine.empty())
			{
				continue;
			}

			candidate.PrefixFound = true;
			const size_t prefixPos = candidate.RawLine.rfind(prefix);
			candidate.Payload = TrimWhitespace(StripRichTextMarkup(candidate.RawLine.substr(prefixPos + prefix.size())));
			TryParseRuptureCyclePayload(candidate.Payload, candidate);

			const bool belongsToWorld = !world || TryObjectHasOuterInChain(object, static_cast<SDK::UObject*>(world));
			if (belongsToWorld)
			{
				return candidate;
			}

			if (!fallbackState.PrefixFound)
			{
				fallbackState = candidate;
			}
		}

		if (fallbackState.PrefixFound)
		{
			return fallbackState;
		}

		state.GObjectsAvailable = true;
		return state;
	}

	MapStateRuntime::Detail::RuptureCycleSnapshot ToRuptureCycleSnapshot(const RuptureCycleChatState& state)
	{
		MapStateRuntime::Detail::RuptureCycleSnapshot snapshot{};
		snapshot.Available = state.PrefixFound;
		snapshot.ChatHudFound = state.ChatHudFound;
		snapshot.PrefixFound = state.PrefixFound;
		snapshot.Parsed = state.Parsed;
		snapshot.HasSequence = state.HasSequence;
		snapshot.Sequence = state.Sequence;
		snapshot.Wave = state.Wave.empty() ? "None" : state.Wave;
		snapshot.Stage = state.Stage.empty() ? "Unknown" : state.Stage;
		snapshot.Step = state.Step.empty() ? "None" : state.Step;
		snapshot.ElapsedSeconds = state.SinceLastStart;
		snapshot.HasElapsed = state.SinceLastStartValid;
		snapshot.RawLine = state.RawLine;
		snapshot.RawPayload = state.Payload;
		return snapshot;
	}

	bool IsInterestingEnviroWaveActorName(const std::string& className, const std::string& actorName)
	{
		return className.find("EnviroWave") != std::string::npos
			|| className.find("WaveTimer") != std::string::npos
			|| className == "CrGatherableSpawnersRepActor"
			|| actorName.find("EnviroWave") != std::string::npos
			|| actorName.find("WaveTimer") != std::string::npos
			|| actorName.find("HeatWave") != std::string::npos
			|| actorName.find("ColdWave") != std::string::npos;
	}

	EnviroWaveActorInventory CaptureEnviroWaveActorInventory(SDK::UWorld* world)
	{
		EnviroWaveActorInventory inventory{};
		if (!world)
		{
			return inventory;
		}

		SDK::TUObjectArray* objectArray = SDK::UObject::GObjects.GetTypedPtr();
		if (!objectArray || objectArray->NumElements <= 0)
		{
			return inventory;
		}

		inventory.GObjectsAvailable = true;

		SDK::UClass* actorClass = TryGetStaticClass<SDK::AActor>();
		if (!actorClass)
		{
			return inventory;
		}

		std::unordered_map<std::string, size_t> classIndexes;
		for (int32_t index = 0; index < objectArray->NumElements; ++index)
		{
			SDK::UObject* object = objectArray->GetByIndex(index);
			if (!object || !object->Class)
			{
				continue;
			}

			if (!TryIsObjectInWorld(object, world) || !TryIsActorObject(object, actorClass))
			{
				continue;
			}

			SDK::AActor* actor = static_cast<SDK::AActor*>(object);
			const std::string className = actor->Class ? actor->Class->GetName() : std::string();
			const std::string actorName = actor->GetName();
			if (!IsInterestingEnviroWaveActorName(className, actorName))
			{
				continue;
			}

			++inventory.MatchingActorCount;
			auto it = classIndexes.find(className);
			if (it == classIndexes.end())
			{
				EnviroWaveActorClassInfo info{};
				info.ClassName = className;
				info.Count = 1;
				info.SampleActorName = actorName;
				info.SampleActor = actor;
				classIndexes.emplace(className, inventory.Classes.size());
				inventory.Classes.push_back(std::move(info));
			}
			else
			{
				++inventory.Classes[it->second].Count;
			}
		}

		std::sort(
			inventory.Classes.begin(),
			inventory.Classes.end(),
			[](const EnviroWaveActorClassInfo& lhs, const EnviroWaveActorClassInfo& rhs)
			{
				if (lhs.Count != rhs.Count)
				{
					return lhs.Count > rhs.Count;
				}
				return lhs.ClassName < rhs.ClassName;
			});

		return inventory;
	}

	template <typename TSubsystem>
	TSubsystem* TryGetWorldSubsystem(SDK::UWorld* world, SDK::UClass* subsystemClass)
	{
		if (!world || !subsystemClass)
		{
			return nullptr;
		}

		return static_cast<TSubsystem*>(
			SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, subsystemClass));
	}

	struct EnviroWaveDiagnostics final
	{
		std::string WorldName;
		SDK::ACrWaveTimerActor* GameStateTimerActor = nullptr;
		SDK::ACrWaveTimerActor* TimerSubsystemActor = nullptr;
		SDK::ACrEnviroWaveVisualsReplicationActor* WaveSubsystemReplicationActor = nullptr;
		SDK::ACrEnviroWaveVisualsReplicationActor* VisualsReplicationActor = nullptr;
		SDK::ACrGatherableSpawnersRepActor* GatherableRepActor = nullptr;
		bool HasGameState = false;
		bool IsDedicatedServer = false;
		bool HasWaveSubsystem = false;
		bool HasTimerSubsystem = false;
		bool TimerActorsMatch = false;
		bool VisualsReplicationActorsMatch = false;
		SDK::EEnviroWave Type = SDK::EEnviroWave::None;
		SDK::EEnviroWaveStage Stage = SDK::EEnviroWaveStage::None;
		SDK::EEnviroWavePreWaveSubstage PreWaveSubstage = SDK::EEnviroWavePreWaveSubstage::None;
		SDK::EEnviroWaveFadeoutSubstage FadeoutSubstage = SDK::EEnviroWaveFadeoutSubstage::None;
		SDK::EEnviroWaveGrowbackSubstage GrowbackSubstage = SDK::EEnviroWaveGrowbackSubstage::None;
		SDK::EEnviroWave GatherableRepWaveType = SDK::EEnviroWave::None;
		SDK::EEnviroWaveStage GatherableRepWaveStage = SDK::EEnviroWaveStage::None;
		int VisualsReplicationActorCount = 0;
		int GatherableRepActorCount = 0;
		bool VisualsWaterEvaporated = false;
		float VisualsWaterEvaporatedLastTimeChange = 0.0f;
		float NextTime = 0.0f;
		int32_t NextPhase = 0;
		bool TimerPaused = false;
		EnviroWaveActorInventory ActorInventory{};
	};

	template <typename TActorClass>
	int CountActorsOfClass(SDK::UWorld* world, TActorClass** firstActor = nullptr)
	{
		if (firstActor)
		{
			*firstActor = nullptr;
		}
		if (!world)
		{
			return 0;
		}

		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, TActorClass::StaticClass(), &actors);

		int count = 0;
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::AActor* actor = actors[index];
			if (!actor)
			{
				continue;
			}

			++count;
			if (firstActor && !*firstActor)
			{
				*firstActor = static_cast<TActorClass*>(actor);
			}
		}

		return count;
	}

	std::string BuildEnviroWaveStateSignature(const EnviroWaveDiagnostics& diagnostics)
	{
		std::ostringstream oss;
		oss
			<< diagnostics.HasGameState << '|'
			<< diagnostics.IsDedicatedServer << '|'
			<< diagnostics.HasWaveSubsystem << '|'
			<< diagnostics.HasTimerSubsystem << '|'
			<< (diagnostics.GameStateTimerActor != nullptr) << '|'
			<< (diagnostics.TimerSubsystemActor != nullptr) << '|'
			<< (diagnostics.WaveSubsystemReplicationActor != nullptr) << '|'
			<< diagnostics.VisualsReplicationActorCount << '|'
			<< (diagnostics.VisualsReplicationActor != nullptr) << '|'
			<< diagnostics.VisualsReplicationActorsMatch << '|'
			<< diagnostics.VisualsWaterEvaporated << '|'
			<< diagnostics.GatherableRepActorCount << '|'
			<< (diagnostics.GatherableRepActor != nullptr) << '|'
			<< diagnostics.TimerActorsMatch << '|'
			<< static_cast<int>(diagnostics.Type) << '|'
			<< static_cast<int>(diagnostics.Stage) << '|'
			<< static_cast<int>(diagnostics.PreWaveSubstage) << '|'
			<< static_cast<int>(diagnostics.FadeoutSubstage) << '|'
			<< static_cast<int>(diagnostics.GrowbackSubstage) << '|'
			<< static_cast<int>(diagnostics.GatherableRepWaveType) << '|'
			<< static_cast<int>(diagnostics.GatherableRepWaveStage) << '|'
			<< diagnostics.NextPhase << '|'
			<< diagnostics.TimerPaused;
		return oss.str();
	}

	std::string BuildEnviroWaveTimerSignature(const EnviroWaveDiagnostics& diagnostics)
	{
		std::ostringstream oss;
		oss.setf(std::ios::fixed);
		oss.precision(3);
		const bool hasTimerActor = diagnostics.GameStateTimerActor || diagnostics.TimerSubsystemActor;
		oss << hasTimerActor;
		if (hasTimerActor)
		{
			oss << '|' << diagnostics.NextTime
				<< '|' << diagnostics.NextPhase
				<< '|' << diagnostics.TimerPaused;
		}
		return oss.str();
	}

	std::string BuildEnviroWaveActorInventorySignature(const EnviroWaveDiagnostics& diagnostics)
	{
		std::ostringstream oss;
		oss
			<< diagnostics.ActorInventory.GObjectsAvailable << '|'
			<< diagnostics.ActorInventory.MatchingActorCount;
		for (const EnviroWaveActorClassInfo& info : diagnostics.ActorInventory.Classes)
		{
			oss << '|' << info.ClassName << ':' << info.Count;
		}
		return oss.str();
	}

	std::string BuildRuptureCycleChatSignature(const MapStateRuntime::Detail::RuptureCycleSnapshot& snapshot)
	{
		std::ostringstream oss;
		oss
			<< snapshot.Available << '|'
			<< snapshot.HasSequence << '|'
			<< snapshot.Sequence << '|'
			<< snapshot.Wave << '|'
			<< snapshot.Stage << '|'
			<< snapshot.Step << '|'
			<< snapshot.HasElapsed;
		if (snapshot.HasElapsed)
		{
			oss.setf(std::ios::fixed);
			oss.precision(3);
			oss << '|' << snapshot.ElapsedSeconds;
		}
		return oss.str();
	}

	int64_t GetCurrentUnixTimeMilliseconds()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	void ApplyRuptureCycleObservationTimestamp(MapStateRuntime::Detail::RuptureCycleSnapshot& snapshot)
	{
		snapshot.HasObservedAtUnixMs = false;
		snapshot.ObservedAtUnixMs = 0;

		if (!snapshot.Available || !snapshot.Parsed)
		{
			return;
		}

		const std::string signature = BuildRuptureCycleChatSignature(snapshot);
		if (!g_lastRuptureCycleObservedSignatureValid || signature != g_lastRuptureCycleObservedSignature)
		{
			g_lastRuptureCycleObservedSignature = signature;
			g_lastRuptureCycleObservedSignatureValid = true;
			g_lastRuptureCycleObservedAtUnixMs = GetCurrentUnixTimeMilliseconds();
		}

		snapshot.HasObservedAtUnixMs = g_lastRuptureCycleObservedAtUnixMs > 0;
		snapshot.ObservedAtUnixMs = g_lastRuptureCycleObservedAtUnixMs;
	}

	void ResetEnviroWaveDiagnosticsState()
	{
		g_lastEnviroWaveStateSignature.clear();
		g_enviroWaveStateSignatureValid = false;
		g_lastEnviroWaveTimerSignature.clear();
		g_enviroWaveTimerSignatureValid = false;
		g_lastRuptureCycleChatSignature.clear();
		g_lastRuptureCycleChatSignatureValid = false;
		g_lastRuptureCycleObservedSignature.clear();
		g_lastRuptureCycleObservedSignatureValid = false;
		g_lastRuptureCycleObservedAtUnixMs = 0;
		g_lastEnviroWaveActorInventorySignature.clear();
		g_enviroWaveActorInventorySignatureValid = false;
	}

	void LogRuptureCycleChatIfNeeded(
		const MapStateRuntime::Detail::RuptureCycleSnapshot& snapshot,
		uint64_t generation,
		const char* reason)
	{
		if (!ShouldLogRuptureCycleChat() || !snapshot.Available)
		{
			return;
		}

		const std::string signature = BuildRuptureCycleChatSignature(snapshot);
		if (g_lastRuptureCycleChatSignatureValid && signature == g_lastRuptureCycleChatSignature)
		{
			return;
		}

		g_lastRuptureCycleChatSignature = signature;
		g_lastRuptureCycleChatSignatureValid = true;

		const std::string seqText = snapshot.HasSequence ? std::to_string(snapshot.Sequence) : "--";
		const std::string elapsedText = snapshot.HasElapsed ? std::to_string(snapshot.ElapsedSeconds) : "--";

		LOG_INFO(
			"RuptureCycle chat #%llu from '%s': seq=%s wave=%s stage=%s step=%s elapsed=%s raw='%s'",
			static_cast<unsigned long long>(generation),
			reason ? reason : "unknown",
			seqText.c_str(),
			snapshot.Wave.c_str(),
			snapshot.Stage.c_str(),
			snapshot.Step.c_str(),
			elapsedText.c_str(),
			snapshot.RawPayload.c_str());
	}

	void MarkChimeraWorldReady(const char* reason)
	{
		if (g_chimeraWorldReady)
		{
			return;
		}

		g_chimeraWorldReady = true;
		if (ShouldLogLifecycle())
		{
			LOG_INFO(
				"ChimeraMain world marked ready by %s",
				reason ? reason : "unknown");
		}
	}

	void MarkEnviroWaveDiagnosticsReady(const char* reason)
	{
		if (g_enviroWaveDiagnosticsReady)
		{
			return;
		}

		g_enviroWaveDiagnosticsReady = true;
		if (ShouldLogLifecycle())
		{
			LOG_INFO(
				"EnviroWave diagnostics marked ready by %s",
				reason ? reason : "unknown");
		}
	}

	std::string CargoKindToString(CargoKind kind)
	{
		return kind == CargoKind::Sender ? "sender" : "receiver";
	}

	std::string NormalizeCargoBuildingName(CargoKind kind, const std::string& value)
	{
		if (value.empty())
		{
			return {};
		}

		if (kind == CargoKind::Sender)
		{
			if (value == "Package Sender" || value == "Cargo Sender")
			{
				return "Cargo Dispatcher";
			}

			return value;
		}

		if (value == "Package Receiver")
		{
			return "Cargo Receiver";
		}

		return value;
	}

	std::string GetConfiguredCargoBuildingName(CargoKind kind)
	{
		if (SDK::UCrBuilidngsDeveloperSettings* settings = SDK::UCrBuilidngsDeveloperSettings::GetDefaultObj())
		{
			const std::string configuredName = kind == CargoKind::Sender
				? settings->SenderDefaultName.ToString()
				: settings->ReceiverDefaultName.ToString();
			if (!configuredName.empty())
			{
				return NormalizeCargoBuildingName(kind, configuredName);
			}
		}

		return kind == CargoKind::Sender ? "Cargo Dispatcher" : "Cargo Receiver";
	}

	std::string GetItemDisplayName(const SDK::UAuItemDataBase* item)
	{
		if (!item)
		{
			return {};
		}

		const std::string itemName = item->ItemName.ToString();
		if (!itemName.empty())
		{
			return itemName;
		}

		if (!item->UniqueItemName.IsNone())
		{
			const std::string uniqueName = item->UniqueItemName.ToString();
			if (!uniqueName.empty())
			{
				return uniqueName;
			}
		}

		return item->GetName();
	}

	void AppendResourceName(CargoMarker& marker, const std::string& resourceName)
	{
		if (resourceName.empty())
		{
			return;
		}

		if (marker.ResourceSummary.empty())
		{
			marker.ResourceSummary = resourceName;
			return;
		}

		if (marker.ResourceSummary.find(resourceName) != std::string::npos)
		{
			return;
		}

		marker.ResourceSummary += ", ";
		marker.ResourceSummary += resourceName;
	}

	std::string ComposeMarkerDisplayName(const CargoMarker& marker)
	{
		if (marker.Kind == CargoKind::Sender && !marker.ResourceSummary.empty())
		{
			return marker.DisplayName + " - " + marker.ResourceSummary;
		}

		return marker.DisplayName;
	}

	std::string FormatVector(const SDK::FVector& value)
	{
		std::ostringstream oss;
		oss.setf(std::ios::fixed);
		oss.precision(1);
		oss << "(X=" << value.X << " Y=" << value.Y << " Z=" << value.Z << ")";
		return oss.str();
	}

	std::string FormatVector2f(const SDK::FVector2f& value)
	{
		std::ostringstream oss;
		oss.setf(std::ios::fixed);
		oss.precision(1);
		oss << "(X=" << value.X << " Y=" << value.Y << ")";
		return oss.str();
	}

	std::string BuildReplicationHelperKey(const SDK::FCrMassEntityReplicationHelper& helper)
	{
		std::ostringstream oss;
		oss << helper.NetID.Value << ':' << helper.Entity.ID;
		return oss.str();
	}

	std::string BuildLocationKey(CargoKind kind, const SDK::FVector& location)
	{
		std::ostringstream oss;
		oss << static_cast<int>(kind) << ':'
			<< static_cast<int>(std::lround(location.X)) << ':'
			<< static_cast<int>(std::lround(location.Y)) << ':'
			<< static_cast<int>(std::lround(location.Z));
		return oss.str();
	}

	std::string BuildLocationKey(const char* prefix, const SDK::FVector& location)
	{
		std::ostringstream oss;
		oss << (prefix ? prefix : "marker") << ':'
			<< static_cast<int>(std::lround(location.X)) << ':'
			<< static_cast<int>(std::lround(location.Y)) << ':'
			<< static_cast<int>(std::lround(location.Z));
		return oss.str();
	}

	SDK::FVector MakeVector(float x, float y, float z)
	{
		SDK::FVector value{};
		value.X = x;
		value.Y = y;
		value.Z = z;
		return value;
	}

	SDK::ACrGameStateBase* TryGetGameState(SDK::UWorld* world)
	{
		if (!world)
		{
			return nullptr;
		}

		SDK::AGameStateBase* gameStateBase = SDK::UGameplayStatics::GetGameState(world);
		return reinterpret_cast<SDK::ACrGameStateBase*>(gameStateBase);
	}

	EnviroWaveDiagnostics CaptureEnviroWaveDiagnostics(SDK::UWorld* world, SDK::ACrGameStateBase* gameState)
	{
		EnviroWaveDiagnostics diagnostics{};
		diagnostics.WorldName = world ? world->GetName() : g_lastWorldName;
		if (!IsChimeraWorldName(diagnostics.WorldName))
		{
			return diagnostics;
		}

		diagnostics.HasGameState = gameState != nullptr;

		if (gameState)
		{
			diagnostics.IsDedicatedServer = gameState->bIsDedicatedServer;
			diagnostics.GameStateTimerActor = gameState->WaveTimerActor;
		}

		SDK::UCrEnviroWaveSubsystem* waveSubsystem =
			TryGetWorldSubsystem<SDK::UCrEnviroWaveSubsystem>(
				world,
				SDK::UCrEnviroWaveSubsystem::StaticClass());
		if (waveSubsystem)
		{
			diagnostics.HasWaveSubsystem = true;
			diagnostics.WaveSubsystemReplicationActor = waveSubsystem->ReplicationActor;
			diagnostics.Type = waveSubsystem->GetCurrentType();
			diagnostics.Stage = waveSubsystem->GetCurrentStage();
			diagnostics.PreWaveSubstage = waveSubsystem->CurrentPreWaveSubstage;
			diagnostics.FadeoutSubstage = waveSubsystem->CurrentFadeoutSubstage;
			diagnostics.GrowbackSubstage = waveSubsystem->CurrentGrowbackSubstage;
		}

		SDK::UCrEnviroWaveTimerSubsystem* timerSubsystem =
			TryGetWorldSubsystem<SDK::UCrEnviroWaveTimerSubsystem>(
				world,
				SDK::UCrEnviroWaveTimerSubsystem::StaticClass());
		if (timerSubsystem)
		{
			diagnostics.HasTimerSubsystem = true;
			diagnostics.TimerSubsystemActor = timerSubsystem->TimerActor;
		}

		diagnostics.TimerActorsMatch =
			diagnostics.GameStateTimerActor != nullptr
			&& diagnostics.TimerSubsystemActor != nullptr
			&& diagnostics.GameStateTimerActor == diagnostics.TimerSubsystemActor;

		diagnostics.VisualsReplicationActorCount =
			CountActorsOfClass<SDK::ACrEnviroWaveVisualsReplicationActor>(
				world,
				&diagnostics.VisualsReplicationActor);
		if (diagnostics.VisualsReplicationActor)
		{
			diagnostics.VisualsWaterEvaporated = diagnostics.VisualsReplicationActor->bIsWaterEvaporated;
			diagnostics.VisualsWaterEvaporatedLastTimeChange =
				diagnostics.VisualsReplicationActor->WaterEvaporatedLastTimeChange;
		}
		diagnostics.VisualsReplicationActorsMatch =
			diagnostics.WaveSubsystemReplicationActor != nullptr
			&& diagnostics.VisualsReplicationActor != nullptr
			&& diagnostics.WaveSubsystemReplicationActor == diagnostics.VisualsReplicationActor;

		diagnostics.GatherableRepActorCount =
			CountActorsOfClass<SDK::ACrGatherableSpawnersRepActor>(
				world,
				&diagnostics.GatherableRepActor);
		if (diagnostics.GatherableRepActor)
		{
			diagnostics.GatherableRepWaveType = diagnostics.GatherableRepActor->RepEnviroWaveTypeChange;
			diagnostics.GatherableRepWaveStage = diagnostics.GatherableRepActor->RepEnviroWaveStageChange;
		}

		SDK::ACrWaveTimerActor* timerActor = diagnostics.GameStateTimerActor
			? diagnostics.GameStateTimerActor
			: diagnostics.TimerSubsystemActor;
		if (timerActor)
		{
			diagnostics.NextTime = timerActor->NextTime;
			diagnostics.NextPhase = timerActor->NextPhase;
			diagnostics.TimerPaused = timerActor->bPause;
		}

		if (ShouldLogEnviroWaveActorInventory())
		{
			diagnostics.ActorInventory = CaptureEnviroWaveActorInventory(world);
		}
		return diagnostics;
	}

	void LogEnviroWaveDiagnosticsIfNeeded(
		const EnviroWaveDiagnostics& diagnostics,
		uint64_t generation,
		const char* reason,
		bool isRealtimeRefresh)
	{
		if (!ShouldLogEnviroWaveDiagnostics())
		{
			return;
		}

		const std::string currentSignature = BuildEnviroWaveStateSignature(diagnostics);
		const bool stateChanged =
			!g_enviroWaveStateSignatureValid
			|| currentSignature != g_lastEnviroWaveStateSignature;
		const bool heartbeat =
			isRealtimeRefresh
			&& (generation % static_cast<uint64_t>(GetEnviroWaveHeartbeatSnapshotInterval()) == 0);
		if (!stateChanged && !heartbeat && isRealtimeRefresh)
		{
			return;
		}

		g_lastEnviroWaveStateSignature = currentSignature;
		g_enviroWaveStateSignatureValid = true;

		LOG_INFO(
			"EnviroWave snapshot #%llu from '%s': world='%s' dedicated=%s game_state=%s wave_subsystem=%s timer_subsystem=%s game_timer_actor=%p subsystem_timer_actor=%p timer_actor_match=%s",
			static_cast<unsigned long long>(generation),
			reason ? reason : "unknown",
			diagnostics.WorldName.c_str(),
			BoolToYesNo(diagnostics.IsDedicatedServer),
			BoolToYesNo(diagnostics.HasGameState),
			BoolToYesNo(diagnostics.HasWaveSubsystem),
			BoolToYesNo(diagnostics.HasTimerSubsystem),
			static_cast<void*>(diagnostics.GameStateTimerActor),
			static_cast<void*>(diagnostics.TimerSubsystemActor),
			BoolToYesNo(diagnostics.TimerActorsMatch));

		if (diagnostics.HasWaveSubsystem)
		{
			LOG_INFO(
				"  EnviroWave state: type=%s(%d) stage=%s(%d) pre=%s(%d) fadeout=%s(%d) growback=%s(%d)",
				EnviroWaveToString(diagnostics.Type),
				static_cast<int>(diagnostics.Type),
				EnviroWaveStageToString(diagnostics.Stage),
				static_cast<int>(diagnostics.Stage),
				PreWaveSubstageToString(diagnostics.PreWaveSubstage),
				static_cast<int>(diagnostics.PreWaveSubstage),
				FadeoutSubstageToString(diagnostics.FadeoutSubstage),
				static_cast<int>(diagnostics.FadeoutSubstage),
				GrowbackSubstageToString(diagnostics.GrowbackSubstage),
				static_cast<int>(diagnostics.GrowbackSubstage));

			LOG_INFO(
				"  EnviroWave settings: skipped (GetCurrentStageSettings crashes during load on this build)");
		}
		else
		{
			LOG_INFO("  EnviroWave state: subsystem unavailable for this world");
		}

		LOG_INFO(
			"  EnviroWave actors: subsystem_rep_actor=%p visuals_rep_count=%d visuals_rep_actor=%p visuals_rep_actor_match=%s rep_water_evaporated=%s rep_water_last_change=%.3f gatherable_rep_count=%d gatherable_rep_actor=%p gatherable_type=%s(%d) gatherable_stage=%s(%d)",
			static_cast<void*>(diagnostics.WaveSubsystemReplicationActor),
			diagnostics.VisualsReplicationActorCount,
			static_cast<void*>(diagnostics.VisualsReplicationActor),
			BoolToYesNo(diagnostics.VisualsReplicationActorsMatch),
			BoolToYesNo(diagnostics.VisualsWaterEvaporated),
			diagnostics.VisualsWaterEvaporatedLastTimeChange,
			diagnostics.GatherableRepActorCount,
			static_cast<void*>(diagnostics.GatherableRepActor),
			EnviroWaveToString(diagnostics.GatherableRepWaveType),
			static_cast<int>(diagnostics.GatherableRepWaveType),
			EnviroWaveStageToString(diagnostics.GatherableRepWaveStage),
			static_cast<int>(diagnostics.GatherableRepWaveStage));

		if (diagnostics.GameStateTimerActor || diagnostics.TimerSubsystemActor)
		{
			const char* timerSource = diagnostics.TimerActorsMatch
				? "game_state+timer_subsystem"
				: (diagnostics.GameStateTimerActor ? "game_state" : "timer_subsystem");
			LOG_INFO(
				"  EnviroWave timer: source=%s next_time=%.3f next_phase=%d timer_pause=%s",
				timerSource,
				diagnostics.NextTime,
				diagnostics.NextPhase,
				BoolToYesNo(diagnostics.TimerPaused));
		}
		else
		{
			LOG_INFO("  EnviroWave timer: actor unavailable");
		}
	}

	void LogEnviroWaveTimerChangesIfNeeded(
		const EnviroWaveDiagnostics& diagnostics,
		uint64_t generation,
		const char* reason)
	{
		const std::string currentSignature = BuildEnviroWaveTimerSignature(diagnostics);
		if (g_enviroWaveTimerSignatureValid && currentSignature == g_lastEnviroWaveTimerSignature)
		{
			return;
		}

		g_lastEnviroWaveTimerSignature = currentSignature;
		g_enviroWaveTimerSignatureValid = true;

		if (!(diagnostics.GameStateTimerActor || diagnostics.TimerSubsystemActor))
		{
			LOG_INFO(
				"  EnviroWave timer-change #%llu from '%s': actor unavailable",
				static_cast<unsigned long long>(generation),
				reason ? reason : "unknown");
			return;
		}

		const char* timerSource = diagnostics.TimerActorsMatch
			? "game_state+timer_subsystem"
			: (diagnostics.GameStateTimerActor ? "game_state" : "timer_subsystem");
		LOG_INFO(
			"  EnviroWave timer-change #%llu from '%s': source=%s next_time=%.3f next_phase=%d timer_pause=%s",
			static_cast<unsigned long long>(generation),
			reason ? reason : "unknown",
			timerSource,
			diagnostics.NextTime,
			diagnostics.NextPhase,
			BoolToYesNo(diagnostics.TimerPaused));
	}

	void LogEnviroWaveActorInventoryIfNeeded(
		const EnviroWaveDiagnostics& diagnostics,
		uint64_t generation,
		const char* reason)
	{
		const std::string currentSignature = BuildEnviroWaveActorInventorySignature(diagnostics);
		if (g_enviroWaveActorInventorySignatureValid
			&& currentSignature == g_lastEnviroWaveActorInventorySignature)
		{
			return;
		}

		g_lastEnviroWaveActorInventorySignature = currentSignature;
		g_enviroWaveActorInventorySignatureValid = true;

		if (!diagnostics.ActorInventory.GObjectsAvailable)
		{
			LOG_INFO(
				"  EnviroWave actor inventory #%llu from '%s': GObjects unavailable",
				static_cast<unsigned long long>(generation),
				reason ? reason : "unknown");
			return;
		}

		LOG_INFO(
			"  EnviroWave actor inventory #%llu from '%s': matches=%d unique_classes=%zu",
			static_cast<unsigned long long>(generation),
			reason ? reason : "unknown",
			diagnostics.ActorInventory.MatchingActorCount,
			diagnostics.ActorInventory.Classes.size());

		if (diagnostics.ActorInventory.Classes.empty())
		{
			LOG_INFO("    (no enviro-wave actors matched the current ChimeraMain world scan)");
			return;
		}

		const size_t maxLog = std::min(
			diagnostics.ActorInventory.Classes.size(),
			static_cast<size_t>(kMaxLoggedEnviroWaveActorClasses));
		for (size_t index = 0; index < maxLog; ++index)
		{
			const EnviroWaveActorClassInfo& info = diagnostics.ActorInventory.Classes[index];
			LOG_INFO(
				"    EnviroWave actor class='%s' count=%d sample_actor='%s' sample_ptr=%p",
				info.ClassName.c_str(),
				info.Count,
				info.SampleActorName.c_str(),
				static_cast<void*>(info.SampleActor));
		}

		if (diagnostics.ActorInventory.Classes.size() > maxLog)
		{
			LOG_INFO(
				"    ... %zu additional enviro-wave actor classes omitted",
				diagnostics.ActorInventory.Classes.size() - maxLog);
		}
	}

	void LogRuntimePlanIfNeededImpl()
	{
		if (g_runtimePlanLogged || !MapExtensionPluginConfig::Config::LogRuntimePlanOnce())
		{
			return;
		}

		g_runtimePlanLogged = true;
		LOG_INFO("Runtime plan: capture cargo dispatcher/receiver network data from runtime sources, then publish snapshots over local HTTP.");
		LOG_INFO("Runtime plan: use package transport replication first, actor scans as fallback, and keep the web UI separate from Unreal widgets.");
		LOG_INFO(
			"Runtime plan: refresh the cached snapshot every %d ms while ChimeraMain is running.",
			MapExtensionPluginConfig::Config::RefreshIntervalMs());
		LOG_INFO("Runtime plan: also refresh immediately when relevant actors begin play in ChimeraMain.");
	}

	std::string HexEncode(const std::string& value)
	{
		static const char* kHex = "0123456789abcdef";
		std::string out;
		out.reserve(value.size() * 2);
		for (unsigned char ch : value)
		{
			out.push_back(kHex[(ch >> 4) & 0xF]);
			out.push_back(kHex[ch & 0xF]);
		}
		return out;
	}

	std::string SanitizePrintableKey(const std::string& value)
	{
		std::string out;
		out.reserve(value.size());
		for (unsigned char ch : value)
		{
			if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
			{
				out.push_back(static_cast<char>(ch));
			}
			else if (ch == '_' || ch == '-' || ch == '.')
			{
				out.push_back(static_cast<char>(ch));
			}
			else
			{
				out.push_back('_');
			}
		}
		return out;
	}

	std::string MakePublicKey(CargoKind kind, const std::string& internalKey)
	{
		bool isMostlyPrintable = !internalKey.empty();
		for (unsigned char ch : internalKey)
		{
			if (ch < 32 || ch > 126)
			{
				isMostlyPrintable = false;
				break;
			}
		}

		const std::string prefix = kind == CargoKind::Sender ? "sender_" : "receiver_";
		if (isMostlyPrintable)
		{
			return prefix + SanitizePrintableKey(internalKey);
		}

		return prefix + HexEncode(internalKey);
	}

	std::string MakeTaggedPublicKey(const char* prefix, const std::string& internalKey)
	{
		const std::string safePrefix = prefix ? prefix : "marker";
		bool isMostlyPrintable = !internalKey.empty();
		for (unsigned char ch : internalKey)
		{
			if (ch < 32 || ch > 126)
			{
				isMostlyPrintable = false;
				break;
			}
		}

		if (isMostlyPrintable)
		{
			return safePrefix + "_" + SanitizePrintableKey(internalKey);
		}

		return safePrefix + "_" + HexEncode(internalKey);
	}

	void LogSnapshotSummary(const CargoSnapshot& snapshot)
	{
		if (!ShouldLogCargoSnapshots())
		{
			return;
		}

		LOG_INFO(
			"Cargo snapshot #%llu from '%s': markers=%zu, senders=%d, receivers=%d, connections=%zu, teleporters=%zu, players=%zu, replicator=%s, teleporter_replicator=%s, actor_fallback=%s",
			static_cast<unsigned long long>(snapshot.Generation),
			snapshot.Reason.c_str(),
			snapshot.Markers.size(),
			snapshot.SenderCount,
			snapshot.ReceiverCount,
			snapshot.Connections.size(),
			snapshot.Teleporters.size(),
			snapshot.Players.size(),
			snapshot.HasPackageTransportReplicator ? "yes" : "no",
			snapshot.HasTeleportReplicator ? "yes" : "no",
			snapshot.UsedActorFallback ? "yes" : "no");

		const size_t maxLog = std::min(snapshot.Markers.size(), static_cast<size_t>(kMaxLoggedMarkersPerSnapshot));
		for (size_t index = 0; index < maxLog; ++index)
		{
			const CargoMarker& marker = snapshot.Markers[index];
			LOG_INFO(
				"  Cargo %s #%zu key=%s label='%s' resource='%s' source=%s world=%s map=%s",
				CargoKindToString(marker.Kind).c_str(),
				index + 1,
				marker.PublicKey.c_str(),
				ComposeMarkerDisplayName(marker).c_str(),
				marker.ResourceSummary.c_str(),
				marker.Source.c_str(),
				FormatVector(marker.WorldLocation).c_str(),
				FormatVector2f(marker.MapLocation).c_str());
		}
	}

	bool IsRealtimeRefreshReason(const char* reason)
	{
		return reason != nullptr && std::strcmp(reason, "EngineTick") == 0;
	}

	bool IsRelevantRealtimeActorImpl(SDK::AActor* actor)
	{
		return actor
			&& (
				actor->IsA(SDK::ABP_PackageReceiver_C::StaticClass())
				|| actor->IsA(SDK::ABP_PackageSender_C::StaticClass())
				|| actor->IsA(SDK::ACrItemReceiverBuilding::StaticClass())
				|| actor->IsA(SDK::ACrItemSenderBuilding::StaticClass())
				|| actor->IsA(SDK::ACrTeleporter::StaticClass())
				|| actor->IsA(SDK::ABP_Teleporter_C::StaticClass())
				|| actor->IsA(SDK::ACrMegamachineTeleporterDevice::StaticClass())
				|| actor->IsA(SDK::ACrCharacterPlayerBase::StaticClass()));
	}

	bool IsEnviroWaveDiagnosticsActorImpl(SDK::AActor* actor)
	{
		return actor
			&& (
				actor->IsA(SDK::ACrWaveTimerActor::StaticClass())
				|| actor->IsA(SDK::ACrGatherableSpawnersRepActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWaveVisualsReplicationActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWaveVisualsActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWavePlayerFXActor::StaticClass())
				|| actor->IsA(SDK::ACrEnviroWaveRegion::StaticClass()));
	}

	CargoMarker* AddOrUpdateMarker(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& markerIndexes,
		CargoKind kind,
		const SDK::FVector& worldLocation,
		std::string internalKey,
		const char* source,
		std::string displayName,
		std::string resourceName)
	{
		const auto existing = markerIndexes.find(internalKey);
		if (existing != markerIndexes.end())
		{
			CargoMarker& marker = snapshot.Markers[existing->second];
			if (marker.DisplayName.empty() && !displayName.empty())
			{
				marker.DisplayName = NormalizeCargoBuildingName(kind, displayName);
			}
			AppendResourceName(marker, resourceName);
			return &marker;
		}

		CargoMarker marker{};
		marker.Kind = kind;
		marker.WorldLocation = worldLocation;
		marker.MapLocation = MapStateRuntime::Detail::WorldToMap(worldLocation);
		marker.DisplayName = !displayName.empty()
			? NormalizeCargoBuildingName(kind, displayName)
			: GetConfiguredCargoBuildingName(kind);
		AppendResourceName(marker, resourceName);
		marker.Source = source ? source : "unknown";
		marker.InternalKey = std::move(internalKey);
		marker.PublicKey = MakePublicKey(kind, marker.InternalKey);
		snapshot.Markers.push_back(std::move(marker));
		markerIndexes.emplace(snapshot.Markers.back().InternalKey, snapshot.Markers.size() - 1);
		return &snapshot.Markers.back();
	}

	TeleporterMarker* AddOrUpdateTeleporter(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& teleporterIndexes,
		const SDK::FVector& worldLocation,
		std::string internalKey,
		const char* source,
		std::string displayName)
	{
		const auto existing = teleporterIndexes.find(internalKey);
		if (existing != teleporterIndexes.end())
		{
			TeleporterMarker& marker = snapshot.Teleporters[existing->second];
			if (marker.DisplayName.empty() && !displayName.empty())
			{
				marker.DisplayName = std::move(displayName);
			}
			if (marker.Source.empty() && source)
			{
				marker.Source = source;
			}
			return &marker;
		}

		TeleporterMarker marker{};
		marker.WorldLocation = worldLocation;
		marker.MapLocation = MapStateRuntime::Detail::WorldToMap(worldLocation);
		marker.DisplayName = !displayName.empty() ? std::move(displayName) : "Teleporter";
		marker.Source = source ? source : "unknown";
		marker.InternalKey = std::move(internalKey);
		marker.PublicKey = MakeTaggedPublicKey("teleporter", marker.InternalKey);
		snapshot.Teleporters.push_back(std::move(marker));
		teleporterIndexes.emplace(snapshot.Teleporters.back().InternalKey, snapshot.Teleporters.size() - 1);
		return &snapshot.Teleporters.back();
	}

	PlayerMarker* AddOrUpdatePlayer(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& playerIndexes,
		const SDK::FVector& worldLocation,
		std::string internalKey,
		const char* source,
		std::string displayName)
	{
		const auto existing = playerIndexes.find(internalKey);
		if (existing != playerIndexes.end())
		{
			PlayerMarker& marker = snapshot.Players[existing->second];
			if (marker.DisplayName.empty() && !displayName.empty())
			{
				marker.DisplayName = std::move(displayName);
			}
			if (marker.Source.empty() && source)
			{
				marker.Source = source;
			}
			return &marker;
		}

		PlayerMarker marker{};
		marker.WorldLocation = worldLocation;
		marker.MapLocation = MapStateRuntime::Detail::WorldToMap(worldLocation);
		marker.DisplayName = !displayName.empty() ? std::move(displayName) : "Player";
		marker.Source = source ? source : "unknown";
		marker.InternalKey = std::move(internalKey);
		marker.PublicKey = MakeTaggedPublicKey("player", marker.InternalKey);
		snapshot.Players.push_back(std::move(marker));
		playerIndexes.emplace(snapshot.Players.back().InternalKey, snapshot.Players.size() - 1);
		return &snapshot.Players.back();
	}

	std::string GetInteractionDisplayName(SDK::AActor* actor)
	{
		if (!actor)
		{
			return {};
		}

		const SDK::TArray<SDK::UActorComponent*> components =
			actor->K2_GetComponentsByClass(SDK::UCrInteractionComponent::StaticClass());
		for (int index = 0; index < components.Num(); ++index)
		{
			SDK::UCrInteractionComponent* interactionComponent =
				static_cast<SDK::UCrInteractionComponent*>(components[index]);
			if (!interactionComponent)
			{
				continue;
			}

			const std::string displayName = interactionComponent->GetInteractionDisplayName().ToString();
			if (!displayName.empty())
			{
				return displayName;
			}
		}

		return {};
	}

	SDK::UCrBuildingCustomNameSubsystem* TryGetBuildingCustomNameSubsystem(SDK::UWorld* world)
	{
		if (!world)
		{
			return nullptr;
		}

		return static_cast<SDK::UCrBuildingCustomNameSubsystem*>(
			SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(
				world,
				SDK::UCrBuildingCustomNameSubsystem::StaticClass()));
	}

	std::string GetBuildingCustomName(SDK::UWorld* world, SDK::AActor* buildingActor)
	{
		if (!world || !buildingActor)
		{
			return {};
		}

		SDK::UCrBuildingCustomNameSubsystem* customNameSubsystem =
			TryGetBuildingCustomNameSubsystem(world);
		if (!customNameSubsystem)
		{
			return {};
		}

		return customNameSubsystem->GetBuildingCustomName(buildingActor).ToString();
	}

	uint32_t GetPersistentEntityIdValue(const SDK::FCrMassPersistentEntityID& entityId)
	{
		return entityId.ID;
	}

	uint32_t GetReplicationHelperEntityIdValue(const SDK::FCrMassEntityReplicationHelper& helper)
	{
		return GetPersistentEntityIdValue(helper.Entity);
	}

	SDK::ACrPlayerControllerBase* TryGetLocalCrPlayerController(SDK::UWorld* world)
	{
		if (!world)
		{
			return nullptr;
		}

		return static_cast<SDK::ACrPlayerControllerBase*>(
			SDK::UGameplayStatics::GetPlayerController(world, 0));
	}

	std::string FindCachedBuildingCustomName(
		SDK::UCrBuildingCustomNameSubsystem* customNameSubsystem,
		const SDK::FCrMassEntityReplicationHelper& helper)
	{
		if (!customNameSubsystem)
		{
			return {};
		}

		const uint32_t targetId = GetReplicationHelperEntityIdValue(helper);
		if (targetId == 0)
		{
			return {};
		}

		for (const auto& entry : customNameSubsystem->CustomNames)
		{
			if (GetPersistentEntityIdValue(entry.Key()) == targetId)
			{
				return entry.Value().ToString();
			}
		}

		return {};
	}

	void RequestBuildingCustomNameIfNeeded(
		SDK::UWorld* world,
		const SDK::FCrMassEntityReplicationHelper& helper)
	{
		const uint32_t entityId = GetReplicationHelperEntityIdValue(helper);
		if (!world || entityId == 0)
		{
			return;
		}

		if (!g_requestedCustomNameEntityIds.insert(entityId).second)
		{
			return;
		}

		SDK::ACrPlayerControllerBase* playerController = TryGetLocalCrPlayerController(world);
		if (!playerController)
		{
			g_requestedCustomNameEntityIds.erase(entityId);
			return;
		}

		playerController->ServerGetBuildingCustomName(helper);
	}

	std::string GetBuildingCustomName(
		SDK::UWorld* world,
		const SDK::FCrMassEntityReplicationHelper& helper)
	{
		if (!world)
		{
			return {};
		}

		SDK::UCrBuildingCustomNameSubsystem* customNameSubsystem =
			TryGetBuildingCustomNameSubsystem(world);
		const std::string cachedName = FindCachedBuildingCustomName(customNameSubsystem, helper);
		if (!cachedName.empty())
		{
			return cachedName;
		}

		RequestBuildingCustomNameIfNeeded(world, helper);
		return {};
	}

	std::string GetCargoActorDisplayName(
		SDK::UWorld* world,
		SDK::AActor* actor,
		CargoKind kind)
	{
		const std::string customName = GetBuildingCustomName(world, actor);
		if (!customName.empty())
		{
			return customName;
		}

		const std::string interactionName = GetInteractionDisplayName(actor);
		if (!interactionName.empty())
		{
			return interactionName;
		}

		return GetConfiguredCargoBuildingName(kind);
	}

	CargoMarker* FindMarkerByLocation(
		CargoSnapshot& snapshot,
		CargoKind kind,
		const SDK::FVector& worldLocation)
	{
		const std::string locationKey = BuildLocationKey(kind, worldLocation);
		for (CargoMarker& marker : snapshot.Markers)
		{
			if (marker.Kind != kind)
			{
				continue;
			}

			if (BuildLocationKey(kind, marker.WorldLocation) == locationKey)
			{
				return &marker;
			}
		}

		return nullptr;
	}

	std::string GetTeleporterDisplayName(SDK::ACrTeleporter* teleporter)
	{
		if (!teleporter)
		{
			return {};
		}

		const std::string interactionName = GetInteractionDisplayName(teleporter);
		if (!interactionName.empty())
		{
			return interactionName;
		}

		return teleporter->GetName();
	}

	std::string GetTeleporterDisplayName(SDK::ACrMegamachineTeleporterDevice* teleporter)
	{
		if (!teleporter)
		{
			return {};
		}

		const std::string interactionText = teleporter->InteractionText.ToString();
		if (!interactionText.empty())
		{
			return interactionText;
		}

		const std::string interactionName = GetInteractionDisplayName(teleporter);
		if (!interactionName.empty())
		{
			return interactionName;
		}

		return teleporter->GetName();
	}

	std::string GetPlayerDisplayName(SDK::APawn* playerPawn)
	{
		if (!playerPawn)
		{
			return {};
		}

		if (SDK::AController* controller = playerPawn->GetController())
		{
			if (SDK::APlayerState* playerState = controller->PlayerState)
			{
				const std::string playerName = playerState->GetPlayerName().ToString();
				if (!playerName.empty())
				{
					return playerName;
				}
			}
		}

		return playerPawn->GetName();
	}

	std::string GetPlayerInternalKey(SDK::APawn* playerPawn)
	{
		if (!playerPawn)
		{
			return {};
		}

		if (SDK::AController* controller = playerPawn->GetController())
		{
			if (SDK::APlayerState* playerState = controller->PlayerState)
			{
				const std::string playerStateName = playerState->GetName();
				if (!playerStateName.empty())
				{
					return "player_state:" + playerStateName;
				}
			}

			const std::string controllerName = controller->GetName();
			if (!controllerName.empty())
			{
				return "controller:" + controllerName;
			}
		}

		const std::string pawnName = playerPawn->GetName();
		if (!pawnName.empty())
		{
			return "pawn:" + pawnName;
		}

		return {};
	}

	void CapturePlayerPawn(
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& playerIndexes,
		SDK::APawn* playerPawn,
		const char* source)
	{
		if (!playerPawn)
		{
			return;
		}

		const SDK::FVector worldLocation = playerPawn->K2_GetActorLocation();
		std::string internalKey = GetPlayerInternalKey(playerPawn);
		if (internalKey.empty())
		{
			internalKey = BuildLocationKey("player", worldLocation);
		}

		AddOrUpdatePlayer(
			snapshot,
			playerIndexes,
			worldLocation,
			internalKey,
			source,
			GetPlayerDisplayName(playerPawn));
	}

	void CaptureFromReplicator(
		SDK::UWorld* world,
		CargoSnapshot& snapshot,
		SDK::ACrGameStateBase* gameState,
		std::unordered_map<std::string, size_t>& markerIndexes)
	{
		if (!gameState)
		{
			return;
		}

		SDK::ACrPackageTransportReplicator* replicator = gameState->PackageTransportReplicator;
		snapshot.HasPackageTransportReplicator = replicator != nullptr;
		if (!replicator)
		{
			return;
		}

		snapshot.UsedReplicator = true;
		std::unordered_map<std::string, ReceiverLinkInfo> receiverLookup;
		std::unordered_set<std::string> connectionKeys;

		const SDK::FCrReceiversContainer& receivers = replicator->ReceiversContainer;
		for (int index = 0; index < receivers.ReceiversData.Num(); ++index)
		{
			const SDK::FCrReceiverData& receiver = receivers.ReceiversData[index];
			const std::string receiverKey = BuildReplicationHelperKey(receiver.Receiver);
			const SDK::FVector worldLocation = MakeVector(receiver.Location.X, receiver.Location.Y, receiver.Location.Z);
			CargoMarker* marker = AddOrUpdateMarker(
				snapshot,
				markerIndexes,
				CargoKind::Receiver,
				worldLocation,
				receiverKey,
				"package_transport_replicator.receiver",
				GetBuildingCustomName(world, receiver.Receiver),
				{});
			if (marker)
			{
				const auto markerIndex = markerIndexes.find(marker->InternalKey);
				if (markerIndex == markerIndexes.end())
				{
					continue;
				}
				receiverLookup.emplace(
					marker->InternalKey,
					ReceiverLinkInfo{
						markerIndex->second,
						marker->WorldLocation,
						marker->MapLocation,
						marker->PublicKey
					});
			}
		}

		const SDK::FCrPackageTransportConnectionsContainer& connections = replicator->ConnectionsContainer;
		snapshot.ConnectionCount = connections.ConnectionsData.Num();
		for (int index = 0; index < connections.ConnectionsData.Num(); ++index)
		{
			const SDK::FCrPackageTransportConnectionData& connection = connections.ConnectionsData[index];
			const std::string senderKey = BuildReplicationHelperKey(connection.Sender);
			const std::string receiverKey = BuildReplicationHelperKey(connection.Receiver);
			const std::string resourceName = GetItemDisplayName(connection.Item);
			const SDK::FVector senderLocation = MakeVector(connection.SenderLocation.X, connection.SenderLocation.Y, connection.SenderLocation.Z);
			CargoMarker* senderMarker = AddOrUpdateMarker(
				snapshot,
				markerIndexes,
				CargoKind::Sender,
				senderLocation,
				senderKey,
				"package_transport_replicator.connection",
				GetBuildingCustomName(world, connection.Sender),
				resourceName);

			auto receiverIt = receiverLookup.find(receiverKey);
			if (!senderMarker || receiverIt == receiverLookup.end())
			{
				continue;
			}

			CargoMarker& receiverMarker = snapshot.Markers[receiverIt->second.MarkerIndex];
			AppendResourceName(receiverMarker, resourceName);

			std::string dedupeKey = senderMarker->InternalKey;
			dedupeKey.push_back('\x1F');
			dedupeKey += receiverKey;
			dedupeKey.push_back('\x1F');
			dedupeKey += resourceName;
			if (!connectionKeys.insert(dedupeKey).second)
			{
				continue;
			}

			CargoConnection route{};
			route.SenderKey = senderMarker->PublicKey;
			route.ReceiverKey = receiverIt->second.PublicKey;
			route.SenderLabel = ComposeMarkerDisplayName(*senderMarker);
			route.ReceiverLabel = receiverMarker.DisplayName;
			route.ItemDisplayName = resourceName;
			route.RequestedAmount = connection.RequestedAmount;
			route.SenderWorldLocation = senderMarker->WorldLocation;
			route.ReceiverWorldLocation = receiverMarker.WorldLocation;
			route.SenderMapLocation = senderMarker->MapLocation;
			route.ReceiverMapLocation = receiverMarker.MapLocation;
			snapshot.Connections.push_back(std::move(route));
		}
	}

	template <typename TActorClass>
	int CaptureActorsOfClass(
		SDK::UWorld* world,
		CargoSnapshot& snapshot,
		std::unordered_map<std::string, size_t>& markerIndexes,
		CargoKind kind,
		const char* source,
		bool allowCreateIfMissing = true)
	{
		if (!world)
		{
			return 0;
		}

		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, TActorClass::StaticClass(), &actors);

		const int count = actors.Num();
		const int maxLog = ShouldLogActorScanFallback() ? std::min(count, kMaxLoggedActorsPerKind) : 0;
		std::string actorClassName = "(unknown)";
		if (SDK::UClass* actorClass = TActorClass::StaticClass())
		{
			actorClassName = actorClass->GetName();
		}

		for (int index = 0; index < count; ++index)
		{
			SDK::AActor* actor = actors[index];
			if (!actor)
			{
				continue;
			}

			const SDK::FVector worldLocation = actor->K2_GetActorLocation();
			const std::string displayName = GetCargoActorDisplayName(world, actor, kind);
			CargoMarker* existingMarker = FindMarkerByLocation(snapshot, kind, worldLocation);
			if (existingMarker)
			{
				if (!displayName.empty())
				{
					existingMarker->DisplayName = displayName;
				}
			}
			else if (allowCreateIfMissing)
			{
				AddOrUpdateMarker(
					snapshot,
					markerIndexes,
					kind,
					worldLocation,
					BuildLocationKey(kind, worldLocation),
					source,
					displayName,
					{});
			}

			if (index < maxLog)
			{
				LOG_INFO(
					"  Actor fallback %s #%d class=%s actor=%s custom_name='%s' world=%s",
					CargoKindToString(kind).c_str(),
					index + 1,
					actorClassName.c_str(),
					actor->GetName().c_str(),
					displayName.c_str(),
					FormatVector(worldLocation).c_str());
			}
		}

		return count;
	}

	void CaptureActorFallback(SDK::UWorld* world, CargoSnapshot& snapshot, std::unordered_map<std::string, size_t>& markerIndexes)
	{
		const size_t before = snapshot.Markers.size();
		snapshot.ActorReceiverCount = CaptureActorsOfClass<SDK::ABP_PackageReceiver_C>(
			world,
			snapshot,
			markerIndexes,
			CargoKind::Receiver,
			"actor_scan.receiver");
		if (snapshot.ActorReceiverCount == 0)
		{
			snapshot.ActorReceiverCount = CaptureActorsOfClass<SDK::ACrItemReceiverBuilding>(
				world,
				snapshot,
				markerIndexes,
				CargoKind::Receiver,
				"actor_scan.receiver_base",
				false);
		}
		snapshot.ActorSenderCount = CaptureActorsOfClass<SDK::ABP_PackageSender_C>(
			world,
			snapshot,
			markerIndexes,
			CargoKind::Sender,
			"actor_scan.sender");
		if (snapshot.ActorSenderCount == 0)
		{
			snapshot.ActorSenderCount = CaptureActorsOfClass<SDK::ACrItemSenderBuilding>(
				world,
				snapshot,
				markerIndexes,
				CargoKind::Sender,
				"actor_scan.sender_base",
				false);
		}

		if (snapshot.Markers.size() > before)
		{
			snapshot.UsedActorFallback = true;
		}
	}

	void CaptureTeleporters(
		SDK::UWorld* world,
		CargoSnapshot& snapshot,
		SDK::ACrGameStateBase* gameState)
	{
		std::unordered_map<std::string, size_t> teleporterIndexes;
		std::unordered_map<std::string, size_t> teleporterIndexesByLocation;

		if (gameState && gameState->TeleportReplicator)
		{
			snapshot.HasTeleportReplicator = true;
			snapshot.UsedTeleporterReplicator = true;

			const SDK::FCrTeleportersContainer& teleporters = gameState->TeleportReplicator->TeleportersContainer;
			for (int index = 0; index < teleporters.TeleportersData.Num(); ++index)
			{
				const SDK::FCrTeleporterData& teleporter = teleporters.TeleportersData[index];
				const SDK::FVector worldLocation =
					MakeVector(teleporter.Location.X, teleporter.Location.Y, teleporter.Location.Z);
				const std::string internalKey = BuildReplicationHelperKey(teleporter.Teleporter);

					AddOrUpdateTeleporter(
						snapshot,
						teleporterIndexes,
						worldLocation,
						internalKey,
						"teleport_replicator",
						GetBuildingCustomName(world, teleporter.Teleporter));
				teleporterIndexesByLocation.emplace(
					BuildLocationKey("teleporter", worldLocation),
					snapshot.Teleporters.size() - 1);
			}
		}

		if (!world)
		{
			snapshot.TeleporterCount = static_cast<int>(snapshot.Teleporters.size());
			return;
		}

		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrTeleporter::StaticClass(), &actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ACrTeleporter* teleporter = static_cast<SDK::ACrTeleporter*>(actors[index]);
			if (!teleporter)
			{
				continue;
			}

			const SDK::FVector worldLocation = teleporter->K2_GetActorLocation();
			std::string displayName = GetBuildingCustomName(world, teleporter);
			if (displayName.empty())
			{
				displayName = GetTeleporterDisplayName(teleporter);
			}
			const std::string locationKey = BuildLocationKey("teleporter", worldLocation);
			const auto existingByLocation = teleporterIndexesByLocation.find(locationKey);
			if (existingByLocation != teleporterIndexesByLocation.end())
			{
				TeleporterMarker& marker = snapshot.Teleporters[existingByLocation->second];
				if (!displayName.empty())
				{
					marker.DisplayName = displayName;
				}
				marker.Source = "teleport_replicator+actor";
				continue;
			}

			AddOrUpdateTeleporter(
				snapshot,
				teleporterIndexes,
				worldLocation,
				locationKey,
				"actor_scan.teleporter",
				displayName);
		}

		actors.Clear();
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ABP_Teleporter_C::StaticClass(), &actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ABP_Teleporter_C* teleporter = static_cast<SDK::ABP_Teleporter_C*>(actors[index]);
			if (!teleporter)
			{
				continue;
			}

			const SDK::FVector worldLocation = teleporter->K2_GetActorLocation();
			std::string displayName = GetBuildingCustomName(world, teleporter);
			if (displayName.empty())
			{
				displayName = GetTeleporterDisplayName(static_cast<SDK::ACrTeleporter*>(teleporter));
			}
			const std::string locationKey = BuildLocationKey("teleporter", worldLocation);
			const auto existingByLocation = teleporterIndexesByLocation.find(locationKey);
			if (existingByLocation != teleporterIndexesByLocation.end())
			{
				TeleporterMarker& marker = snapshot.Teleporters[existingByLocation->second];
				if (!displayName.empty())
				{
					marker.DisplayName = displayName;
				}
				if (marker.Source == "teleport_replicator")
				{
					marker.Source = "teleport_replicator+bp_actor";
				}
				else if (marker.Source.empty())
				{
					marker.Source = "bp_actor_scan.teleporter";
				}
				continue;
			}

			AddOrUpdateTeleporter(
				snapshot,
				teleporterIndexes,
				worldLocation,
				locationKey,
				"bp_actor_scan.teleporter",
				displayName);
			teleporterIndexesByLocation.emplace(locationKey, snapshot.Teleporters.size() - 1);
		}

		actors.Clear();
		SDK::UGameplayStatics::GetAllActorsOfClass(
			world,
			SDK::ACrMegamachineTeleporterDevice::StaticClass(),
			&actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ACrMegamachineTeleporterDevice* teleporter =
				static_cast<SDK::ACrMegamachineTeleporterDevice*>(actors[index]);
			if (!teleporter)
			{
				continue;
			}

			const SDK::FVector worldLocation = teleporter->K2_GetActorLocation();
			std::string displayName = GetBuildingCustomName(world, teleporter);
			if (displayName.empty())
			{
				displayName = GetTeleporterDisplayName(teleporter);
			}
			const std::string locationKey = BuildLocationKey("teleporter", worldLocation);
			const auto existingByLocation = teleporterIndexesByLocation.find(locationKey);
			if (existingByLocation != teleporterIndexesByLocation.end())
			{
				TeleporterMarker& marker = snapshot.Teleporters[existingByLocation->second];
				if (!displayName.empty())
				{
					marker.DisplayName = displayName;
				}
				if (marker.Source == "teleport_replicator")
				{
					marker.Source = "teleport_replicator+device_actor";
				}
				else if (marker.Source.empty())
				{
					marker.Source = "actor_scan.teleporter_device";
				}
				continue;
			}

			AddOrUpdateTeleporter(
				snapshot,
				teleporterIndexes,
				worldLocation,
				locationKey,
				"actor_scan.teleporter_device",
				displayName);
			teleporterIndexesByLocation.emplace(locationKey, snapshot.Teleporters.size() - 1);
		}

		snapshot.TeleporterCount = static_cast<int>(snapshot.Teleporters.size());
	}

	void CapturePlayers(SDK::UWorld* world, CargoSnapshot& snapshot)
	{
		if (!world)
		{
			snapshot.PlayerCount = 0;
			return;
		}

		std::unordered_map<std::string, size_t> playerIndexes;
		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrCharacterPlayerBase::StaticClass(), &actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			SDK::ACrCharacterPlayerBase* player = static_cast<SDK::ACrCharacterPlayerBase*>(actors[index]);
			if (!player)
			{
				continue;
			}

			CapturePlayerPawn(
				snapshot,
				playerIndexes,
				static_cast<SDK::APawn*>(player),
				"actor_scan.player");
		}

		const int localPlayerCount = std::max(
			1,
			SDK::UGameplayStatics::GetNumLocalPlayerControllers(world));
		for (int playerIndex = 0; playerIndex < localPlayerCount; ++playerIndex)
		{
			SDK::APawn* playerPawn = SDK::UGameplayStatics::GetPlayerPawn(world, playerIndex);
			if (playerPawn)
			{
				CapturePlayerPawn(
					snapshot,
					playerIndexes,
					playerPawn,
					"local_player_pawn");
				continue;
			}

			SDK::APlayerController* controller =
				SDK::UGameplayStatics::GetPlayerController(world, playerIndex);
			if (!controller)
			{
				continue;
			}

			CapturePlayerPawn(
				snapshot,
				playerIndexes,
				controller->K2_GetPawn(),
				"local_player_controller");
		}

		snapshot.PlayerCount = static_cast<int>(snapshot.Players.size());
	}

	CargoSnapshot CopySnapshotImpl()
	{
		std::lock_guard<std::mutex> lock(g_snapshotMutex);
		return g_snapshot;
	}

	bool RefreshCargoSnapshotImpl(SDK::UWorld* world, const char* reason)
	{
		const bool isRealtimeRefresh = IsRealtimeRefreshReason(reason);
		CargoSnapshot nextSnapshot{};
		CargoSnapshot previousSnapshot{};
		{
			std::lock_guard<std::mutex> lock(g_snapshotMutex);
			nextSnapshot.Generation = g_snapshot.Generation + 1;
			previousSnapshot = g_snapshot;
		}
		nextSnapshot.Reason = reason ? reason : "unknown";
		nextSnapshot.WorldName = g_lastWorldName;

		std::unordered_map<std::string, size_t> markerIndexes;
		SDK::ACrGameStateBase* gameState = TryGetGameState(world);
		EnviroWaveDiagnostics enviroWaveDiagnostics{};
		if (ShouldLogEnviroWaveDiagnostics() && g_enviroWaveDiagnosticsReady)
		{
			if (ShouldLogLifecycle())
			{
				LOG_INFO("EnviroWave diagnostics: probing SDK state during '%s'", nextSnapshot.Reason.c_str());
			}
			enviroWaveDiagnostics = CaptureEnviroWaveDiagnostics(world, gameState);
			if (ShouldLogLifecycle())
			{
				LOG_INFO("EnviroWave diagnostics: SDK probe completed during '%s'", nextSnapshot.Reason.c_str());
			}
		}
		CaptureFromReplicator(world, nextSnapshot, gameState, markerIndexes);
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Cargo refresh '%s': replicator stage completed", nextSnapshot.Reason.c_str());
		}
		if (!isRealtimeRefresh || !nextSnapshot.HasPackageTransportReplicator)
		{
			CaptureActorFallback(world, nextSnapshot, markerIndexes);
			if (ShouldLogLifecycle())
			{
				LOG_INFO("Cargo refresh '%s': actor fallback stage completed", nextSnapshot.Reason.c_str());
			}
		}
		CaptureTeleporters(world, nextSnapshot, gameState);
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Cargo refresh '%s': teleporter stage completed", nextSnapshot.Reason.c_str());
		}
		CapturePlayers(world, nextSnapshot);
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Cargo refresh '%s': player stage completed", nextSnapshot.Reason.c_str());
		}

		nextSnapshot.RuptureCycle = ToRuptureCycleSnapshot(CaptureRuptureCycleChatState(world));
		ApplyRuptureCycleObservationTimestamp(nextSnapshot.RuptureCycle);

		nextSnapshot.SenderCount = 0;
		nextSnapshot.ReceiverCount = 0;
		for (const CargoMarker& marker : nextSnapshot.Markers)
		{
			if (marker.Kind == CargoKind::Sender)
			{
				++nextSnapshot.SenderCount;
			}
			else
			{
				++nextSnapshot.ReceiverCount;
			}
		}

		if (nextSnapshot.WorldName.empty() && world)
		{
			nextSnapshot.WorldName = world->GetName();
		}

		if (nextSnapshot.Markers.empty() && ShouldLogCargoSnapshots())
		{
			LOG_WARN(
				"Cargo snapshot #%llu from '%s' found no cargo markers (replicator=%s, actor_receivers=%d, actor_senders=%d)",
				static_cast<unsigned long long>(nextSnapshot.Generation),
				nextSnapshot.Reason.c_str(),
				nextSnapshot.HasPackageTransportReplicator ? "yes" : "no",
				nextSnapshot.ActorReceiverCount,
				nextSnapshot.ActorSenderCount);
		}

		{
			std::lock_guard<std::mutex> lock(g_snapshotMutex);
			g_snapshot = nextSnapshot;
		}

		if (ShouldLogEnviroWaveDiagnostics() && g_enviroWaveDiagnosticsReady)
		{
			if (ShouldLogEnviroWavePostCaptureLogs())
			{
				LogEnviroWaveDiagnosticsIfNeeded(
					enviroWaveDiagnostics,
					nextSnapshot.Generation,
					nextSnapshot.Reason.c_str(),
					isRealtimeRefresh);
				LogEnviroWaveTimerChangesIfNeeded(
					enviroWaveDiagnostics,
					nextSnapshot.Generation,
					nextSnapshot.Reason.c_str());
			}
			else if (ShouldLogLifecycle())
			{
				LOG_INFO("EnviroWave diagnostics: post-capture logs skipped during '%s' (LogEnviroWavePostCaptureLogs=0)", nextSnapshot.Reason.c_str());
			}
			if (ShouldLogEnviroWaveActorInventory())
			{
				LogEnviroWaveActorInventoryIfNeeded(
					enviroWaveDiagnostics,
					nextSnapshot.Generation,
					nextSnapshot.Reason.c_str());
			}
		}
		LogRuptureCycleChatIfNeeded(
			nextSnapshot.RuptureCycle,
			nextSnapshot.Generation,
			nextSnapshot.Reason.c_str());

		if (!isRealtimeRefresh)
		{
			LogSnapshotSummary(nextSnapshot);
		}
		else if (
			nextSnapshot.Markers.size() != previousSnapshot.Markers.size()
			|| nextSnapshot.Connections.size() != previousSnapshot.Connections.size()
			|| nextSnapshot.Teleporters.size() != previousSnapshot.Teleporters.size()
			|| nextSnapshot.Players.size() != previousSnapshot.Players.size())
		{
			LOG_INFO(
				"Realtime cargo snapshot #%llu changed: markers=%zu, connections=%zu, teleporters=%zu, players=%zu",
				static_cast<unsigned long long>(nextSnapshot.Generation),
				nextSnapshot.Markers.size(),
				nextSnapshot.Connections.size(),
				nextSnapshot.Teleporters.size(),
				nextSnapshot.Players.size());
		}
		return !nextSnapshot.Markers.empty()
			|| !nextSnapshot.Teleporters.empty()
			|| !nextSnapshot.Players.empty();
	}

	void TryRefreshCurrentWorldImpl(const char* reason)
	{
		if (!g_lastChimeraWorld)
		{
			if (ShouldLogLifecycle())
			{
				LOG_INFO(
					"Skipping snapshot refresh '%s': ChimeraMain world is not loaded yet",
					reason ? reason : "unknown");
			}
			return;
		}
		if (!g_chimeraWorldReady)
		{
			if (ShouldLogLifecycle())
			{
				LOG_INFO(
					"Skipping snapshot refresh '%s': ChimeraMain world is not ready yet",
					reason ? reason : "unknown");
			}
			return;
		}

		SDK::UWorld* world = g_lastChimeraWorld;
		RefreshCargoSnapshotImpl(world, reason);
	}
}

namespace MapStateRuntime
{
namespace Detail
{
	CargoSnapshot CopySnapshot()
	{
		return CopySnapshotImpl();
	}

	void LogRuntimePlanIfNeeded()
	{
		LogRuntimePlanIfNeededImpl();
	}

	bool IsRelevantRealtimeActor(SDK::AActor* actor)
	{
		return IsRelevantRealtimeActorImpl(actor);
	}

	bool IsEnviroWaveDiagnosticsActor(SDK::AActor* actor)
	{
		return IsEnviroWaveDiagnosticsActorImpl(actor);
	}

	bool RefreshCargoSnapshot(SDK::UWorld* world, const char* reason)
	{
		return RefreshCargoSnapshotImpl(world, reason);
	}

	void TryRefreshCurrentWorld(const char* reason)
	{
		TryRefreshCurrentWorldImpl(reason);
	}
}
}

namespace MapStateRuntime
{
	void OnEngineInit()
	{
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Engine init received");
		}
		Detail::LogRuntimePlanIfNeeded();
		Detail::TryRefreshCurrentWorld("EngineInit");
	}

	void OnEngineShutdown()
	{
		if (ShouldLogLifecycle())
		{
			LOG_INFO("Engine shutdown received");
		}
		g_engineTickAccumulatorSeconds = 0.0f;
		g_engineTickSeen = false;
		g_lastChimeraWorld = nullptr;
		g_lastWorldName.clear();
		g_chimeraWorldReady = false;
		g_enviroWaveDiagnosticsReady = false;
		g_requestedCustomNameEntityIds.clear();
		ResetEnviroWaveDiagnosticsState();
	}

	void OnEngineTick(float deltaSeconds)
	{
		if (!g_lastChimeraWorld || !g_chimeraWorldReady)
		{
			return;
		}

		if (!g_engineTickSeen)
		{
			g_engineTickSeen = true;
			if (ShouldLogLifecycle())
			{
				LOG_INFO(
					"EngineTick callback is alive; realtime cargo refresh interval=%d ms",
					MapExtensionPluginConfig::Config::RefreshIntervalMs());
			}
		}

		const float refreshIntervalSeconds = GetRefreshIntervalSeconds();
		if (deltaSeconds <= 0.0f || deltaSeconds > 5.0f)
		{
			g_engineTickAccumulatorSeconds = refreshIntervalSeconds;
		}
		else
		{
			g_engineTickAccumulatorSeconds += deltaSeconds;
		}

		if (g_engineTickAccumulatorSeconds < refreshIntervalSeconds)
		{
			return;
		}

		g_engineTickAccumulatorSeconds = 0.0f;
		Detail::RefreshCargoSnapshot(g_lastChimeraWorld, "EngineTick");
	}

	void OnActorBeginPlay(void* actor)
	{
		if (!g_lastChimeraWorld || !g_chimeraWorldReady || !actor)
		{
			return;
		}

		SDK::AActor* beginPlayActor = static_cast<SDK::AActor*>(actor);
		const bool cargoActor = Detail::IsRelevantRealtimeActor(beginPlayActor);
		const bool enviroWaveDiagnosticsActor =
			ShouldLogEnviroWaveDiagnostics() && Detail::IsEnviroWaveDiagnosticsActor(beginPlayActor);
		if (!cargoActor && !enviroWaveDiagnosticsActor)
		{
			return;
		}

		if (ShouldLogLifecycle())
		{
			LOG_INFO("ActorBeginPlay refresh trigger: class=%s actor=%s source=%s",
				beginPlayActor->Class ? beginPlayActor->Class->GetName().c_str() : "(null)",
				beginPlayActor->GetName().c_str(),
				enviroWaveDiagnosticsActor ? "enviro_wave" : "cargo");
		}

		g_engineTickAccumulatorSeconds = 0.0f;
		Detail::RefreshCargoSnapshot(
			g_lastChimeraWorld,
			enviroWaveDiagnosticsActor ? "ActorBeginPlay(EnviroWave)" : "ActorBeginPlay");
	}

	void OnAnyWorldBeginPlay(SDK::UWorld* world, const char* worldName)
	{
		++g_anyWorldBeginPlayCount;
		const char* safeName = worldName ? worldName : "(null)";
		if (ShouldLogLifecycle())
		{
			LOG_INFO("AnyWorldBeginPlay #%d: world=%p name=%s", g_anyWorldBeginPlayCount, static_cast<void*>(world), safeName);
		}

		if (std::strstr(safeName, "ChimeraMain") != nullptr)
		{
			g_lastChimeraWorld = world;
			g_lastWorldName = safeName;
			g_chimeraWorldReady = false;
			g_enviroWaveDiagnosticsReady = false;
			g_engineTickAccumulatorSeconds = 0.0f;
			g_requestedCustomNameEntityIds.clear();
			ResetEnviroWaveDiagnosticsState();
			if (ShouldLogLifecycle())
			{
				LOG_INFO("ChimeraMain detected: deferring cargo snapshot until the world is ready");
			}
		}
	}

	void OnSaveLoaded()
	{
		++g_saveLoadedCount;
		if (ShouldLogLifecycle())
		{
			LOG_INFO("SaveLoaded #%d: refreshing cargo snapshot from save state", g_saveLoadedCount);
		}
		if (g_lastChimeraWorld)
		{
			MarkChimeraWorldReady("SaveLoaded");
		}
		Detail::TryRefreshCurrentWorld("SaveLoaded");
	}

	void OnExperienceLoadComplete()
	{
		++g_experienceLoadedCount;
		if (ShouldLogLifecycle())
		{
			LOG_INFO("ExperienceLoadComplete #%d: refreshing cargo snapshot from runtime replication", g_experienceLoadedCount);
		}
		if (g_lastChimeraWorld)
		{
			MarkChimeraWorldReady("ExperienceLoadComplete");
			MarkEnviroWaveDiagnosticsReady("ExperienceLoadComplete");
		}
		Detail::TryRefreshCurrentWorld("ExperienceLoadComplete");
	}
}
