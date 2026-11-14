#include "ingame_map_live_state.h"

#include "../map_state_sdk_helpers.h"
#include "../plugin_helpers.h"

#include "ChimeraUI_classes.hpp"
#include "MassCommon_structs.hpp"
#include "MassEntity_classes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

#if !defined(MODLOADER_CLIENT_BUILD)
#error "La carte in-game est client-only"
#endif

// Etat du jeu lu pour la carte : donnees repliquees par le jeu pour sa propre
// carte, et lecture native des obelisques (entites Mass). Game thread uniquement.

namespace MapExtensionClient::InGameMap
{
namespace
{
	SDK::ACrMapMenuDataReplicationHelper* FindMapHelper(SDK::UWorld* world)
	{
		SDK::AGameStateBase* gameState = world ? SDK::UGameplayStatics::GetGameState(world) : nullptr;
		if (!MapStateSdk::TryIsObjectOfClass(gameState, SDK::ACrGameStateBase::StaticClass()))
		{
			return nullptr;
		}
		SDK::ACrMapMenuDataReplicationHelper* helper =
			static_cast<SDK::ACrGameStateBase*>(gameState)->MapMenuDataReplicationHelper;
		return MapStateSdk::TryIsObjectOfClass(helper, SDK::ACrMapMenuDataReplicationHelper::StaticClass()) ? helper : nullptr;
	}

	std::string GuidKey(const SDK::FGuid& guid)
	{
		char text[40];
		std::snprintf(text, sizeof(text), "%08X-%08X-%08X-%08X",
			static_cast<unsigned>(guid.A), static_cast<unsigned>(guid.B), static_cast<unsigned>(guid.C), static_cast<unsigned>(guid.D));
		return text;
	}

	constexpr int kMaxPoiEntries = 4096;

	void CountStatuses(const SDK::TArray<SDK::FCrAbandonBaseDataCollectedStatus>& statuses, int& done, int& total)
	{
		total = std::clamp(statuses.Num(), 0, kMaxPoiEntries);
		done = 0;
		for (int index = 0; index < total; ++index)
		{
			done += statuses[index].Status ? 1 : 0;
		}
	}

	// ---- Lecture native des obelisques (entites Mass) ----
	// Aucune source reflechie ne liste les obelisques en solo : leur etat et leur
	// charge vivent dans FCrAlienObeliskFragment. On parcourt le stockage d'entites
	// du FMassEntityManager et on demande le fragment a la fonction du jeu.
	// Disposition verifiee sur l'executable CL-127004 (IsEntityValid et
	// InternalGetFragmentDataPtr) ; le champ EntityManager n'est resolu que pour ce
	// CL, donc tout autre build coupe la fonctionnalite au chargement.
	constexpr size_t kEntityStorageOffset = 0x20;      // TVariant<Empty, SingleThreaded, Concurrent>
	constexpr size_t kEntityStorageTypeOffset = 0x60;  // 1 ou 2 : un stockage est construit
	constexpr int kStorageSlotGetArchetype = 2;        // IEntityStorageInterface, vtable +0x10
	constexpr int kStorageSlotIsValidIndex = 9;        // vtable +0x48
	constexpr int kMaxEntityIndex = 4 * 1024 * 1024;
	constexpr int kMaxNativeObelisks = 256;
	constexpr char kKeyGetFragment[] = "FMassArchetypeData::GetFragmentDataForEntity";
	constexpr char kKeyEntityManager[] = "UMassEntitySubsystem::EntityManager";
	// Debut de la fonction, unique dans l'executable CL-127004 (une seule occurrence).
	constexpr char kPatternGetFragment[] =
		"40 53 55 57 48 83 EC 30 48 8B D9 4C 8B CA 49 C1 E9 04 4C 8B D2 41 8B C1 C1 E8 10 41 33 C1 44 69 C8 6B CA EB 85";

	using GetFragmentDataFn = void* (*)(void* archetype, const void* fragmentType, int32_t entityIndex);
	GetFragmentDataFn g_getFragmentData = nullptr;
	int32_t g_entityManagerOffset = -1;
	bool g_nativeFeatureOn = false;  // ecrit par le loader pendant OnPluginLoadHooks
	bool g_nativeFaulted = false;    // game thread : une lecture a leve une exception, on n'y revient pas

	struct RawObelisk
	{
		double X, Y, AttackX, AttackY;
		int State;
		float Charge;
	};

	// POD uniquement : __try interdit les objets a destructeur. Retourne le nombre
	// d'obelisques, -1 si le stockage n'a pas la forme attendue, -2 sur exception.
	// ponytail: parcours complet des entites a chaque appel ; memoriser les handles
	// d'obelisque si scan_ms monte.
	int ScanObelisksGuarded(const uint8_t* manager, const void* obeliskType, const void* transformType,
		RawObelisk* out, int capacity, int* scanned)
	{
		__try
		{
			const uint8_t storageType = manager[kEntityStorageTypeOffset];
			if (storageType != 1 && storageType != 2)
			{
				return -1;
			}
			void* storage = const_cast<uint8_t*>(manager) + kEntityStorageOffset;
			void** vtable = *static_cast<void***>(storage);
			const auto getArchetype = reinterpret_cast<void* (*)(void*, int32_t)>(vtable[kStorageSlotGetArchetype]);
			const auto isValidIndex = reinterpret_cast<bool (*)(void*, int32_t)>(vtable[kStorageSlotIsValidIndex]);
			// Les archetypes sans le fragment sont rejetes sans rappeler le jeu.
			void* rejected[64] = {};
			int found = 0;
			int index = 1;  // l'index 0 est l'entite invalide
			for (; index < kMaxEntityIndex && isValidIndex(storage, index); ++index)
			{
				void* archetype = getArchetype(storage, index);
				if (!archetype)
				{
					continue;  // entite libre ou seulement reservee
				}
				void*& slot = rejected[(reinterpret_cast<uintptr_t>(archetype) >> 4) & 63];
				if (slot == archetype)
				{
					continue;
				}
				const auto* fragment = static_cast<const SDK::FCrAlienObeliskFragment*>(g_getFragmentData(archetype, obeliskType, index));
				if (!fragment)
				{
					slot = archetype;
					continue;
				}
				const auto* transform = static_cast<const SDK::FTransformFragment*>(g_getFragmentData(archetype, transformType, index));
				if (!transform || found >= capacity)
				{
					continue;
				}
				out[found].X = transform->Transform.Translation.X;
				out[found].Y = transform->Transform.Translation.Y;
				out[found].AttackX = fragment->AttackLocation.X;
				out[found].AttackY = fragment->AttackLocation.Y;
				out[found].State = static_cast<int>(fragment->State);
				out[found].Charge = fragment->ChargeValue;
				++found;
			}
			*scanned = index - 1;
			return found;
		}
		__except (1)  // EXCEPTION_EXECUTE_HANDLER
		{
			return -2;
		}
	}

	// Charge qui declenche l'attaque, lue une fois dans les traits charges par le jeu.
	// <= 0 tant qu'aucun trait n'est charge ou si les niveaux d'obelisque divergent.
	float ResolveObeliskChargeMax()
	{
		static float cached = 0.0f;
		static int attempts = 0;
		if (cached != 0.0f || attempts >= 5 || !SDK::UObject::GObjects)
		{
			return cached;
		}
		++attempts;
		SDK::UClass* traitClass = SDK::UCrAlienObeliskTrait::StaticClass();
		float low = 0.0f, high = 0.0f;
		int traits = 0;
		const int count = SDK::UObject::GObjects->Num();
		for (int index = 0; index < count; ++index)
		{
			SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(index);
			if (!object || !object->Class || object->IsDefaultObject() || !object->IsA(traitClass))
			{
				continue;
			}
			const float value = static_cast<SDK::UCrAlienObeliskTrait*>(object)->Parameters.ChargeValueToActivateAttack;
			if (value > 0.0f)
			{
				low = traits ? (std::min)(low, value) : value;
				high = (std::max)(high, value);
				++traits;
			}
		}
		if (traits > 0)
		{
			cached = low == high ? low : -1.0f;
			LOG_INFO("[InGameMap] obelisk charge threshold: traits=%d min=%.3f max=%.3f", traits, low, high);
		}
		return cached;
	}

	// Retourne false si la lecture native est indisponible.
	bool CaptureNativeObelisks(SDK::UWorld* world, std::vector<RawObelisk>& out, int& scanned)
	{
		if (!g_nativeFeatureOn || g_nativeFaulted || !g_getFragmentData || g_entityManagerOffset < 0)
		{
			return false;
		}
		// Structs natifs enracines : jamais collectes, donc memorisables pour la session.
		static SDK::UObject* obeliskType = nullptr;
		static SDK::UObject* transformType = nullptr;
		if (!obeliskType || !transformType)
		{
			// Recherche par nom court (rapide), nom complet verifie une fois sur le resultat.
			obeliskType = SDK::UObject::FindObjectFast<SDK::UObject>("CrAlienObeliskFragment", SDK::EClassCastFlags::ScriptStruct);
			transformType = SDK::UObject::FindObjectFast<SDK::UObject>("TransformFragment", SDK::EClassCastFlags::ScriptStruct);
			if (!obeliskType || !transformType
				|| obeliskType->GetFullName() != "ScriptStruct Chimera.CrAlienObeliskFragment"
				|| transformType->GetFullName() != "ScriptStruct MassCommon.TransformFragment")
			{
				obeliskType = transformType = nullptr;
				return false;
			}
		}
		SDK::UObject* subsystem = SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, SDK::UMassEntitySubsystem::StaticClass());
		if (!MapStateSdk::TryIsObjectOfClass(subsystem, SDK::UMassEntitySubsystem::StaticClass()))
		{
			return false;
		}
		// TSharedPtr<FMassEntityManager> : le premier pointeur est l'objet.
		const uint8_t* manager = *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<const uint8_t*>(subsystem) + g_entityManagerOffset);
		if (!manager)
		{
			return false;
		}
		out.resize(kMaxNativeObelisks);
		const int found = ScanObelisksGuarded(manager, obeliskType, transformType, out.data(), kMaxNativeObelisks, &scanned);
		out.resize(found > 0 ? found : 0);
		if (found < 0)
		{
			g_nativeFaulted = true;
			LOG_ERROR("[InGameMap] native obelisk read disabled for this session (%s)",
				found == -2 ? "access fault" : "unexpected entity storage");
			return false;
		}
		return true;
	}
}

	void ResolveNativeAccess(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		if (!self || !scanner || !scanner->DeclareFeature || !scanner->Resolve || !scanner->ResolveField)
		{
			return;
		}
		static const char* const keys[] = { kKeyGetFragment, kKeyEntityManager };
		PluginFeatureDesc feature = PLUGIN_FEATURE_DESC_INIT;
		feature.id = "ingame-map-monoliths";
		feature.title = "In-game map: monolith state and charge";
		feature.keys = keys;
		feature.keyCount = 2;
		feature.enabled = &g_nativeFeatureOn;
		// Sans declaration, un echec de resolution refuserait tout le plugin : on ne demande rien.
		if (!scanner->DeclareFeature(self, &feature))
		{
			g_nativeFeatureOn = false;
			return;
		}

		PluginScanRequest scan = PLUGIN_SCAN_REQUEST_INIT;
		scan.hookName = kKeyGetFragment;
		scan.pattern = kPatternGetFragment;
		scan.kind = PLUGIN_SCAN_FUNCTION_START;
		scan.flags = PLUGIN_SCAN_FLAG_MAP_KEY;  // table du loader d'abord, notre signature ensuite
		g_getFragmentData = reinterpret_cast<GetFragmentDataFn>(scanner->Resolve(self, &scan));

		// Membre natif (TSharedPtr replie dans du padding par le SDK) : meme ligne que plugin_map.h.
		static const PluginFieldCandidate rows[] = { { 127004, 0x38, 0x10, 0, nullptr } };
		PluginFieldRequest field = PLUGIN_FIELD_REQUEST_INIT;
		field.key = kKeyEntityManager;
		field.className = "UMassEntitySubsystem";
		field.member = "EntityManager";
		field.flags = PLUGIN_FIELD_NATIVE;
		field.size = 0x10;
		field.builtCL = PLUGIN_GAME_BUILT_CL;
		field.builtOffset = 0x38;
		field.candidates = rows;
		field.candidateCount = 1;
		PluginFieldResult result{};
		if (scanner->ResolveField(self, &field, &result))
		{
			g_entityManagerOffset = result.offset;
		}
	}

	LiveStructures CaptureLiveStructures()
	{
		LiveStructures live;
		SDK::UWorld* world = SDK::UWorld::GetWorld();
		if (!world)
		{
			return live;
		}
		SDK::AActor* owner = SDK::UGameplayStatics::GetActorOfClass(world, SDK::ACrAntennasOwner::StaticClass());
		if (MapStateSdk::TryIsObjectOfClass(owner, SDK::ACrAntennasOwner::StaticClass()))
		{
			const auto& antennas = static_cast<SDK::ACrAntennasOwner*>(owner)->AntennaContainer.AntennasData;
			const int count = std::clamp(antennas.Num(), 0, kMaxPoiEntries);
			float extent = 0.0f;
			for (int index = 0; index < count; ++index)
			{
				const SDK::FCrAntennaData& data = antennas[index];
				live.Antennas.push_back({ data.AntennaLocation.X, data.AntennaLocation.Y, static_cast<int>(data.State) });
				extent = (std::max)({ extent, std::abs(data.AntennaLocation.X), std::abs(data.AntennaLocation.Y) });
			}
			// Unite non documentee : le monde fait ~500 000 cm de demi-largeur. Si toutes
			// les positions tiennent dans 20 000, elles sont en metres.
			if (extent > 0.0f && extent < 20000.0f)
			{
				for (LiveAntenna& antenna : live.Antennas)
				{
					antenna.X *= 100.0f;
					antenna.Y *= 100.0f;
				}
			}
		}

		if (SDK::ACrMapMenuDataReplicationHelper* helper = FindMapHelper(world))
		{
			const auto& buildings = helper->BuildingsMarkerDataContainer.BuildingsMarkerData;
			live.BuildingMarkers = std::clamp(buildings.Num(), 0, kMaxPoiEntries);
			float buildingExtent = 0.0f;
			for (int index = 0; index < live.BuildingMarkers; ++index)
			{
				const SDK::FBuildingMarkerDataFastArrayItem& building = buildings[index];
				buildingExtent = (std::max)({ buildingExtent, std::abs(building.Location.X), std::abs(building.Location.Y) });
				const std::string unique = building.BuildingUniqueName.IsNone() ? std::string() : building.BuildingUniqueName.ToString();
				if (live.BuildingNames.size() < 400 && live.BuildingNames.find(unique) == std::string::npos)
				{
					live.BuildingNames += unique + " ";
				}
				std::string lower = unique;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (lower.find("core") == std::string::npos)
				{
					continue;
				}
				std::string name = building.BuildingName.ToString();
				LiveBaseCore core{ building.Location.X, building.Location.Y, name.empty() ? unique : name };
				core.EntityIndex = building.BuildingHandle.Index;
				core.EntitySerial = building.BuildingHandle.SerialNumber;
				live.BaseCores.push_back(std::move(core));
			}
			// Niveau et refroidissement : meme entite, retrouvee par le handle que le jeu met en cache.
			SDK::AActor* coreHelper = SDK::UGameplayStatics::GetActorOfClass(world, SDK::ACrBaseCoreReplicationHelper::StaticClass());
			if (MapStateSdk::TryIsObjectOfClass(coreHelper, SDK::ACrBaseCoreReplicationHelper::StaticClass()))
			{
				const auto& data = static_cast<SDK::ACrBaseCoreReplicationHelper*>(coreHelper)->BaseCoreDataContainer.BaseCoreData;
				live.BaseCoreData = std::clamp(data.Num(), 0, kMaxPoiEntries);
				for (int index = 0; index < live.BaseCoreData; ++index)
				{
					const SDK::FCrBaseCoreData& entry = data[index];
					const SDK::FMassEntityHandle handle = entry.BaseCore.Entity.CachedHandle;
					for (LiveBaseCore& core : live.BaseCores)
					{
						// Un seul core et une seule entree : jointure evidente meme sans handle en cache.
						const bool only = live.BaseCoreData == 1 && live.BaseCores.size() == 1;
						if (only || (handle.Index != 0 && core.EntityIndex == handle.Index && core.EntitySerial == handle.SerialNumber))
						{
							core.Level = entry.UpgradeLevel + 1;  // le jeu stocke a partir de 0 et affiche a partir de 1
							core.Cooling = entry.CurrentCoolingCapacity;
							core.CoolingUsed = entry.CurrentCoolingCapacityUsed;
							core.Infected = entry.InfectionSphere;
							++live.BaseCoresJoined;
							break;
						}
					}
				}
			}
			if (buildingExtent > 0.0f && buildingExtent < 20000.0f)  // metres, meme regle que les antennes
			{
				for (LiveBaseCore& core : live.BaseCores)
				{
					core.X *= 100.0f; core.Y *= 100.0f;
				}
			}
			const auto& markers = helper->BaseAttackMarkerDataContainer.BaseAttackMarkerData;
			const int count = std::clamp(markers.Num(), 0, kMaxPoiEntries);
			float extent = 0.0f;
			for (int index = 0; index < count; ++index)
			{
				const SDK::FBaseAttackMarkerDataFastArrayItem& marker = markers[index];
				LiveObelisk obelisk{ marker.Location.X, marker.Location.Y, static_cast<int>(marker.State), -1.0f };
				obelisk.HasAttack = marker.AttackLocationIfAny.X != 0.0f || marker.AttackLocationIfAny.Y != 0.0f;
				obelisk.AttackX = marker.AttackLocationIfAny.X;
				obelisk.AttackY = marker.AttackLocationIfAny.Y;
				live.Obelisks.push_back(obelisk);
				extent = (std::max)({ extent, std::abs(marker.Location.X), std::abs(marker.Location.Y) });
			}
			live.ObeliskMarkers = count;
			if (extent > 0.0f && extent < 20000.0f)  // metres, meme regle que les antennes
			{
				for (LiveObelisk& obelisk : live.Obelisks)
				{
					obelisk.X *= 100.0f; obelisk.Y *= 100.0f; obelisk.AttackX *= 100.0f; obelisk.AttackY *= 100.0f;
				}
			}
		}
		// Rattache une charge (et un etat s'il manque) a l'obelisque deja connu a moins de 10 m.
		const auto merge = [&](float x, float y, int state, float charge)
		{
			for (LiveObelisk& other : live.Obelisks)
			{
				if (std::abs(other.X - x) < 1000.0f && std::abs(other.Y - y) < 1000.0f)
				{
					other.Charge = charge;
					other.State = other.State < 0 ? state : other.State;
					return;
				}
			}
			live.Obelisks.push_back({ x, y, state, charge });
		};

		SDK::TArray<SDK::AActor*> bubbles;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrMassAlienObeliskClientBubbleInfo::StaticClass(), &bubbles);
		for (int bubbleIndex = 0; bubbleIndex < bubbles.Num(); ++bubbleIndex)
		{
			if (!MapStateSdk::TryIsObjectOfClass(bubbles[bubbleIndex], SDK::ACrMassAlienObeliskClientBubbleInfo::StaticClass()))
			{
				continue;
			}
			const auto& items = static_cast<SDK::ACrMassAlienObeliskClientBubbleInfo*>(bubbles[bubbleIndex])->AlienObeliskSerializer.AlienObelisk;
			const int count = std::clamp(items.Num(), 0, kMaxPoiEntries);
			for (int index = 0; index < count; ++index)
			{
				const SDK::FCrReplicatedAlienObelisk& agent = items[index].Agent;
				merge(static_cast<float>(agent.PositionYaw.Position.X), static_cast<float>(agent.PositionYaw.Position.Y),
					static_cast<int>(agent.AlienObeliskData.State), agent.AlienObeliskData.ChargeValue);
				++live.ObeliskReplicated;
			}
		}

		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrAlienObelisk::StaticClass(), &actors);
		for (int index = 0; index < actors.Num(); ++index)
		{
			if (!MapStateSdk::TryIsObjectOfClass(actors[index], SDK::ACrAlienObelisk::StaticClass()))
			{
				continue;
			}
			auto* obelisk = static_cast<SDK::ACrAlienObelisk*>(actors[index]);
			const SDK::FVector location = obelisk->K2_GetActorLocation();
			const float x = static_cast<float>(location.X), y = static_cast<float>(location.Y);
			++live.ObeliskActors;
			// La bulle, lue avant, connait la charge exacte : l'acteur ne la remplace pas.
			const bool charged = std::any_of(live.Obelisks.begin(), live.Obelisks.end(), [&](const LiveObelisk& other)
			{
				return other.Charge >= 0.0f && std::abs(other.X - x) < 1000.0f && std::abs(other.Y - y) < 1000.0f;
			});
			if (!charged)
			{
				merge(x, y, -1, obelisk->PrevChargeValue);
			}
		}

		// Lecture native : fait autorite sur l'etat et la charge quand elle repond.
		std::vector<RawObelisk> native;
		if (CaptureNativeObelisks(world, native, live.EntitiesScanned))
		{
			live.ObeliskNative = static_cast<int>(native.size());
			for (const RawObelisk& raw : native)
			{
				const float x = static_cast<float>(raw.X), y = static_cast<float>(raw.Y);
				auto known = std::find_if(live.Obelisks.begin(), live.Obelisks.end(), [&](const LiveObelisk& other)
				{
					return std::abs(other.X - x) < 1000.0f && std::abs(other.Y - y) < 1000.0f;
				});
				if (known == live.Obelisks.end())
				{
					live.Obelisks.push_back({ x, y });
					known = live.Obelisks.end() - 1;
				}
				known->State = raw.State;
				known->Charge = raw.Charge;
				if (raw.State == 4 && (raw.AttackX != 0.0 || raw.AttackY != 0.0))  // attaque en cours
				{
					known->HasAttack = true;
					known->AttackX = static_cast<float>(raw.AttackX);
					known->AttackY = static_cast<float>(raw.AttackY);
				}
			}
		}
		if (!live.Obelisks.empty())
		{
			const float chargeMax = ResolveObeliskChargeMax();
			for (LiveObelisk& obelisk : live.Obelisks)
			{
				obelisk.ChargeMax = chargeMax;
			}
		}
		return live;
	}

	PoiStateMap CapturePoiStates()
	{
		PoiStateMap states;
		SDK::UWorld* world = SDK::UWorld::GetWorld();
		if (SDK::ACrMapMenuDataReplicationHelper* helper = FindMapHelper(world))
		{
			const int count = std::clamp(helper->PointOfInterestReplicationData.Num(), 0, kMaxPoiEntries);
			for (int index = 0; index < count; ++index)
			{
				const SDK::FPointOfInterestData& data = helper->PointOfInterestReplicationData[index];
				states[GuidKey(data.PointOfInterestMarkerID)].State = static_cast<int>(data.State);
			}
		}
		SDK::AActor* actor = world
			? SDK::UGameplayStatics::GetActorOfClass(world, SDK::ACrAbandonBaseInfoReplicator::StaticClass())
			: nullptr;
		if (MapStateSdk::TryIsObjectOfClass(actor, SDK::ACrAbandonBaseInfoReplicator::StaticClass()))
		{
			const auto& bases = static_cast<SDK::ACrAbandonBaseInfoReplicator*>(actor)->AbandonBaseDataContainer.AbandonBaseData;
			const int count = std::clamp(bases.Num(), 0, kMaxPoiEntries);
			for (int index = 0; index < count; ++index)
			{
				const SDK::FCrAbandonBaseData& base = bases[index];
				PoiLiveState& state = states[GuidKey(base.AbandonBasePOIId)];
				state.IsBase = true;
				CountStatuses(base.FoundablesCollectedStatus, state.Done[0], state.Total[0]);
				CountStatuses(base.BlueprintChestsCollectedStatus, state.Done[1], state.Total[1]);
				CountStatuses(base.StoryItemsCollectedStatus, state.Done[2], state.Total[2]);
				CountStatuses(base.InfectionCystsDestroyedStatus, state.Done[3], state.Total[3]);
			}
		}
		return states;
	}

	int ReadRadiationLevel()
	{
		SDK::ACrMapMenuDataReplicationHelper* helper = FindMapHelper(SDK::UWorld::GetWorld());
		return helper ? helper->CurrentRadiationLevelReplicated : -1;
	}
}
