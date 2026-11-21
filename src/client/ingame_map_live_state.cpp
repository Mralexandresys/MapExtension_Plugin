#include "ingame_map_live_state.h"
#include "game_fields.h"

#include "../map_state_sdk_helpers.h"
#include "../plugin_helpers.h"

#include "ChimeraUI_classes.hpp"
#include "MassCommon_structs.hpp"
#include "MassEntity_classes.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <map>
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
	// Points d'interet, marqueurs et rayonnement : l'aide de replication de la carte du jeu.
	MX_FIELD(Markers, ACrGameStateBase, MapMenuDataReplicationHelper);
	MX_FIELD(Markers, ACrMapMenuDataReplicationHelper, BuildingsMarkerDataContainer);
	MX_FIELD(Markers, FBuildingsMarkerDataContainer, BuildingsMarkerData);
	MX_SIZE(Markers, FBuildingMarkerDataFastArrayItem);
	MX_FIELD(Markers, FBuildingMarkerDataFastArrayItem, Location);
	MX_FIELD(Markers, FBuildingMarkerDataFastArrayItem, BuildingUniqueName);
	MX_FIELD(Markers, FBuildingMarkerDataFastArrayItem, BuildingName);
	MX_FIELD(Markers, FBuildingMarkerDataFastArrayItem, BuildingHandle);
	MX_FIELD(Markers, ACrMapMenuDataReplicationHelper, BaseAttackMarkerDataContainer);
	MX_FIELD(Markers, FBaseAttackMarkerDataContainer, BaseAttackMarkerData);
	MX_SIZE(Markers, FBaseAttackMarkerDataFastArrayItem);
	MX_FIELD(Markers, FBaseAttackMarkerDataFastArrayItem, Location);
	MX_FIELD(Markers, FBaseAttackMarkerDataFastArrayItem, State);
	MX_FIELD(Markers, FBaseAttackMarkerDataFastArrayItem, AttackLocationIfAny);
	MX_FIELD(Markers, ACrMapMenuDataReplicationHelper, PointOfInterestReplicationData);
	MX_SIZE(Markers, FPointOfInterestData);
	MX_FIELD(Markers, FPointOfInterestData, PointOfInterestMarkerID);
	MX_FIELD(Markers, FPointOfInterestData, State);
	MX_FIELD(Markers, ACrMapMenuDataReplicationHelper, CurrentRadiationLevelReplicated);

	// Antennes, base cores, bases abandonnees et monolithes.
	MX_FIELD(Structures, ACrAntennasOwner, AntennaContainer);
	MX_FIELD(Structures, FCrAntennaContainer, AntennasData);
	MX_SIZE(Structures, FCrAntennaData);
	MX_FIELD(Structures, FCrAntennaData, AntennaLocation);
	MX_FIELD(Structures, FCrAntennaData, State);
	MX_FIELD(Structures, UCrBuildingCustomNameSubsystem, CustomNames);
	MX_SIZE(Structures, FCrMassPersistentEntityID);
	MX_FIELD(Structures, FCrMassPersistentEntityID, ID);
	MX_FIELD(Structures, FCrMassPersistentEntityID, CachedHandle);
	MX_FIELD(Structures, ACrBaseCoreReplicationHelper, BaseCoreDataContainer);
	MX_FIELD(Structures, FCrBaseCoreDataContainer, BaseCoreData);
	MX_SIZE(Structures, FCrBaseCoreData);
	MX_FIELD(Structures, FCrBaseCoreData, BaseCore);
	MX_FIELD(Structures, FCrBaseCoreData, UpgradeLevel);
	MX_FIELD(Structures, FCrBaseCoreData, CurrentCoolingCapacity);
	MX_FIELD(Structures, FCrBaseCoreData, CurrentCoolingCapacityUsed);
	MX_FIELD(Structures, FCrBaseCoreData, InfectionSphere);
	MX_FIELD(Structures, FCrMassEntityReplicationHelper, Entity);
	MX_FIELD(Structures, ACrAbandonBaseInfoReplicator, AbandonBaseDataContainer);
	MX_FIELD(Structures, FCrAbandonBaseDataContainer, AbandonBaseData);
	MX_SIZE(Structures, FCrAbandonBaseData);
	MX_FIELD(Structures, FCrAbandonBaseData, AbandonBasePOIId);
	MX_FIELD(Structures, FCrAbandonBaseData, FoundablesCollectedStatus);
	MX_FIELD(Structures, FCrAbandonBaseData, BlueprintChestsCollectedStatus);
	MX_FIELD(Structures, FCrAbandonBaseData, StoryItemsCollectedStatus);
	MX_FIELD(Structures, FCrAbandonBaseData, InfectionCystsDestroyedStatus);
	MX_SIZE(Structures, FCrAbandonBaseDataCollectedStatus);
	MX_FIELD(Structures, FCrAbandonBaseDataCollectedStatus, Status);
	MX_FIELD(Structures, ACrMassAlienObeliskClientBubbleInfo, AlienObeliskSerializer);
	MX_FIELD(Structures, FCrMassAlienObeliskClientBubbleSerializer, AlienObelisk);
	MX_SIZE(Structures, FCrAlienObeliskFastArrayItem);
	MX_FIELD(Structures, FCrAlienObeliskFastArrayItem, Agent);
	MX_FIELD(Structures, FCrReplicatedAlienObelisk, PositionYaw);
	MX_FIELD(Structures, FCrReplicatedAlienObelisk, AlienObeliskData);
	MX_FIELD(Structures, FReplicatedAgentPositionYawData, Position);
	MX_FIELD(Structures, FCrReplicatedAlienObeliskData, State);
	MX_FIELD(Structures, FCrReplicatedAlienObeliskData, ChargeValue);
	MX_FIELD(Structures, ACrAlienObelisk, PrevChargeValue);
	MX_FIELD(Structures, UCrAlienObeliskTrait, Parameters);
	MX_FIELD(Structures, FCrAlienObeliskParameters, ChargeValueToActivateAttack);
	MX_FIELD(Structures, FCrAlienObeliskFragment, AttackLocation);
	MX_FIELD(Structures, FCrAlienObeliskFragment, State);
	MX_FIELD(Structures, FCrAlienObeliskFragment, ChargeValue);
	MX_FIELD(Structures, FTransformFragment, Transform);

	SDK::ACrMapMenuDataReplicationHelper* FindMapHelper(SDK::UWorld* world)
	{
		SDK::AGameStateBase* gameState = world ? SDK::UGameplayStatics::GetGameState(world) : nullptr;
		if (!MapStateSdk::TryIsObjectOfClass(gameState, SDK::ACrGameStateBase::StaticClass()))
		{
			return nullptr;
		}
		SDK::ACrMapMenuDataReplicationHelper* helper = MX(gameState, ACrGameStateBase, MapMenuDataReplicationHelper);
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

	// Element type left to the caller: the array is stepped by the resolved size, never sizeof.
	template <class StatusArray>
	void CountStatuses(const StatusArray& statuses, int& done, int& total)
	{
		total = std::clamp(statuses.Num(), 0, kMaxPoiEntries);
		done = 0;
		for (int index = 0; index < total; ++index)
		{
			done += MX(MX_AT(statuses, index, FCrAbandonBaseDataCollectedStatus), FCrAbandonBaseDataCollectedStatus, Status) ? 1 : 0;
		}
	}

	// ---- Lecture native des obelisques (entites Mass) ----
	// Aucune source reflechie ne liste les obelisques en solo : leur etat et leur
	// charge vivent dans FCrAlienObeliskFragment. On parcourt le stockage d'entites
	// du FMassEntityManager par une requete Mass (entites ayant un FTransformFragment)
	// et on demande les fragments au jeu. Toutes les adresses et tailles viennent de
	// la table du loader : aucune valeur compilee pour un build.
	constexpr int kMaxNativeObelisks = 256;
	constexpr int kMaxNativeBaseCores = 512;
	constexpr char kKeyEntityManager[] = "UMassEntitySubsystem::EntityManager";
	constexpr char kKeyQueryCtor[] = "FMassEntityQuery::FMassEntityQuery";
	constexpr char kKeyQueryDtor[] = "FMassEntityQuery::~FMassEntityQuery";
	constexpr char kKeyQueryHandles[] = "FMassEntityQuery::GetMatchingEntityHandles";
	constexpr char kKeyRequireTransform[] = "FMassFragmentRequirements::AddRequirement<FTransformFragment>";
	constexpr char kKeyGetFragment[] = "FMassEntityManager::InternalGetFragmentDataPtr";
	constexpr char kKeyFree[] = "FMemory::Free";

	struct EntityHandle { int32_t Index; int32_t SerialNumber; };  // FMassEntityHandle (moteur)
	struct HandleArray { EntityHandle* Data; int32_t Num; int32_t Max; };  // TArray<FMassEntityHandle>
	using QueryCtorFn = void* (*)(void* query, const void* sharedManager);  // const TSharedPtr<FMassEntityManager>&
	using QueryDtorFn = void (*)(void* query);
	using QueryHandlesFn = HandleArray* (*)(void* query, HandleArray* out);  // retour par valeur : pointeur cache
	using RequireFn = void* (*)(void* requirements, uint8_t access, uint8_t presence);
	using GetFragmentFn = void* (*)(const void* manager, EntityHandle entity, const void* fragmentType);
	using FreeFn = void (*)(void* block);
	QueryCtorFn g_queryCtor = nullptr;
	QueryDtorFn g_queryDtor = nullptr;
	QueryHandlesFn g_queryHandles = nullptr;
	RequireFn g_requireTransform = nullptr;
	GetFragmentFn g_getFragment = nullptr;
	FreeFn g_free = nullptr;
	// Type C++ non reflechi : taille servie par la ligne du loader pour le build qui tourne
	// (resolvedSize). Une ligne de taille refuse ANY_SIZE ; 0x350 est la taille des lignes
	// connues, non verifiee (pas de CHECK).
	PluginFieldSpec g_querySize{ "FMassEntityQuery", "FMassEntityQuery", nullptr,
		PLUGIN_FIELD_FLAG_NONE, 0x350, -1 };
	int32_t g_entityManagerOffset = -1;
	bool g_nativeFeatureOn = false;  // ecrit par le loader pendant OnPluginLoadHooks
	bool g_nativeFaulted = false;    // game thread : une lecture a leve une exception, on n'y revient pas

	struct RawObelisk
	{
		double X, Y, AttackX, AttackY;
		int State;
		float Charge;
	};
	struct RawBaseCore
	{
		double X, Y;
		int EntityIndex;  // FMassEntityHandle::Index, pour retrouver le niveau
	};
	struct NativeScan
	{
		RawObelisk Obelisks[kMaxNativeObelisks];
		RawBaseCore BaseCores[kMaxNativeBaseCores];
		int ObeliskCount = 0;
		int BaseCoreCount = 0;
		int Scanned = 0;
	};

	// POD uniquement : __try interdit les objets a destructeur. Un seul parcours pour
	// les obelisques et les base cores. `query` est un bloc de g_querySize.resolvedSize
	// octets, construit et detruit ici. Retourne 0, -2 sur exception. `baseCoreType` peut etre nul.
	// ponytail: une requete sur toutes les entites a transform a chaque appel ; memoriser
	// les handles trouves si scan_ms monte.
	int ScanEntitiesGuarded(const uint8_t* manager, const void* sharedManager, void* query,
		const void* obeliskType, const void* baseCoreType, const void* transformType, NativeScan* out)
	{
		__try
		{
			g_queryCtor(query, sharedManager);
			g_requireTransform(query, 1 /* EMassFragmentAccess::ReadOnly */, 0 /* EMassFragmentPresence::All */);
			HandleArray handles{};
			g_queryHandles(query, &handles);
			for (int i = 0; i < handles.Num; ++i)
			{
				const EntityHandle entity = handles.Data[i];
				const void* obelisk = g_getFragment(manager, entity, obeliskType);
				const bool isBaseCore = !obelisk && baseCoreType && g_getFragment(manager, entity, baseCoreType) != nullptr;
				if (!obelisk && !isBaseCore)
				{
					continue;
				}
				const void* transform = g_getFragment(manager, entity, transformType);
				if (!transform)
				{
					continue;
				}
				if (obelisk && out->ObeliskCount < kMaxNativeObelisks)
				{
					RawObelisk& raw = out->Obelisks[out->ObeliskCount++];
					const SDK::FVector position = MX(transform, FTransformFragment, Transform).Translation;
					const SDK::FVector attack = MX(obelisk, FCrAlienObeliskFragment, AttackLocation);
					raw.X = position.X;
					raw.Y = position.Y;
					raw.AttackX = attack.X;
					raw.AttackY = attack.Y;
					raw.State = static_cast<int>(MapStateSdk::LiveObeliskState(MX(obelisk, FCrAlienObeliskFragment, State)));
					raw.Charge = MX(obelisk, FCrAlienObeliskFragment, ChargeValue);
				}
				else if (isBaseCore && out->BaseCoreCount < kMaxNativeBaseCores)
				{
					RawBaseCore& raw = out->BaseCores[out->BaseCoreCount++];
					const SDK::FVector position = MX(transform, FTransformFragment, Transform).Translation;
					raw.X = position.X;
					raw.Y = position.Y;
					raw.EntityIndex = entity.Index;
				}
			}
			out->Scanned = handles.Num;
			if (handles.Data)
			{
				g_free(handles.Data);
			}
			g_queryDtor(query);
			return 0;
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
			const float value = MX(MX_PTR(object, UCrAlienObeliskTrait, Parameters), FCrAlienObeliskParameters, ChargeValueToActivateAttack);
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

	// Types des fragments lus. Structs natifs enracines : jamais collectes, donc
	// memorisables pour la session. La premiere resolution parcourt GObjects.
	// `baseCoreType` reste nul s'il est introuvable : seuls les base cores sont alors perdus.
	bool ResolveFragmentTypes(SDK::UObject*& obeliskType, SDK::UObject*& baseCoreType, SDK::UObject*& transformType)
	{
		static SDK::UObject* obelisk = nullptr;
		static SDK::UObject* baseCore = nullptr;
		static SDK::UObject* transform = nullptr;
		if (!obelisk || !transform)
		{
			// Recherche par nom court (rapide), nom complet verifie une fois sur le resultat.
			obelisk = SDK::UObject::FindObjectFast<SDK::UObject>("CrAlienObeliskFragment", SDK::EClassCastFlags::ScriptStruct);
			transform = SDK::UObject::FindObjectFast<SDK::UObject>("TransformFragment", SDK::EClassCastFlags::ScriptStruct);
			baseCore = SDK::UObject::FindObjectFast<SDK::UObject>("CrMassBuildingBaseCoreFragment", SDK::EClassCastFlags::ScriptStruct);
			if (baseCore && baseCore->GetFullName() != "ScriptStruct Chimera.CrMassBuildingBaseCoreFragment")
			{
				baseCore = nullptr;
			}
			if (!obelisk || !transform
				|| obelisk->GetFullName() != "ScriptStruct Chimera.CrAlienObeliskFragment"
				|| transform->GetFullName() != "ScriptStruct MassCommon.TransformFragment")
			{
				obelisk = transform = baseCore = nullptr;
				return false;
			}
		}
		obeliskType = obelisk;
		baseCoreType = baseCore;
		transformType = transform;
		return true;
	}

	// Retourne false si la lecture native est indisponible.
	bool CaptureNativeEntities(SDK::UWorld* world, NativeScan& out)
	{
		if (!g_nativeFeatureOn || g_nativeFaulted || g_entityManagerOffset < 0 || g_querySize.resolvedSize == 0
			|| !g_queryCtor || !g_queryDtor || !g_queryHandles || !g_requireTransform || !g_getFragment || !g_free)
		{
			return false;
		}
		SDK::UObject* obeliskType = nullptr;
		SDK::UObject* baseCoreType = nullptr;
		SDK::UObject* transformType = nullptr;
		if (!ResolveFragmentTypes(obeliskType, baseCoreType, transformType))
		{
			return false;
		}
		SDK::UObject* subsystem = SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, SDK::UMassEntitySubsystem::StaticClass());
		if (!MapStateSdk::TryIsObjectOfClass(subsystem, SDK::UMassEntitySubsystem::StaticClass()))
		{
			return false;
		}
		// TSharedPtr<FMassEntityManager> : le premier pointeur est l'objet.
		const void* sharedManager = reinterpret_cast<const uint8_t*>(subsystem) + g_entityManagerOffset;
		const uint8_t* manager = *static_cast<const uint8_t* const*>(sharedManager);
		if (!manager)
		{
			return false;
		}
		PluginMap::StructBuffer query(g_querySize);
		if (!query.Data())
		{
			return false;
		}
		const int result = ScanEntitiesGuarded(manager, sharedManager, query.Data(), obeliskType, baseCoreType, transformType, &out);
		if (result < 0)
		{
			g_nativeFaulted = true;
			LOG_ERROR("[InGameMap] native entity read disabled for this session (%s)",
				result == -2 ? "access fault" : "unexpected result");
			return false;
		}
		return true;
	}

	// Niveau et refroidissement : meme entite, retrouvee par le handle que le jeu met en cache.
	void JoinBaseCoreLevels(SDK::UWorld* world, LiveStructures& live)
	{
		SDK::AActor* coreHelper = SDK::UGameplayStatics::GetActorOfClass(world, SDK::ACrBaseCoreReplicationHelper::StaticClass());
		if (!MapStateSdk::TryIsObjectOfClass(coreHelper, SDK::ACrBaseCoreReplicationHelper::StaticClass()))
		{
			return;
		}
		// Noms donnes par le joueur, par identifiant persistant d'entite (meme source que
		// les noms des cargos dans map_state_capture.cpp). Disponibles sans attendre les marqueurs.
		std::map<uint32_t, std::string> customNames;
		if (auto* names = MapStateSdk::TryGetWorldSubsystem<SDK::UCrBuildingCustomNameSubsystem>(
				world, SDK::UCrBuildingCustomNameSubsystem::StaticClass()))
		{
			const auto map = MX_MAP(MX_PTR(names, UCrBuildingCustomNameSubsystem, CustomNames), FCrMassPersistentEntityID, SDK::FString);
			map.ForEach([&](uint8_t* key, uint8_t* value)
			{
				const uint32_t id = MX(key, FCrMassPersistentEntityID, ID);
				std::string name = reinterpret_cast<const SDK::FString*>(value)->ToString();
				if (id != 0 && !name.empty())
				{
					customNames.emplace(id, std::move(name));
				}
			});
		}
		live.CustomNames = static_cast<int>(customNames.size());
		const auto data = MX(MX_PTR(coreHelper, ACrBaseCoreReplicationHelper, BaseCoreDataContainer), FCrBaseCoreDataContainer, BaseCoreData);
		live.BaseCoreData = std::clamp(data.Num(), 0, kMaxPoiEntries);
		for (int index = 0; index < live.BaseCoreData; ++index)
		{
			const uint8_t* entry = MX_AT(data, index, FCrBaseCoreData);
			const uint8_t* entity = MX_PTR(MX_PTR(entry, FCrBaseCoreData, BaseCore), FCrMassEntityReplicationHelper, Entity);
			const SDK::FMassEntityHandle handle = MX(entity, FCrMassPersistentEntityID, CachedHandle);
			for (LiveBaseCore& core : live.BaseCores)
			{
				// Un seul core et une seule entree : jointure evidente meme sans handle en cache.
				const bool only = live.BaseCoreData == 1 && live.BaseCores.size() == 1;
				// Un core lu nativement n'a pas de numero de serie : l'index suffit, il est unique.
				const bool sameEntity = handle.Index != 0 && core.EntityIndex == handle.Index
					&& (core.EntitySerial == 0 || core.EntitySerial == handle.SerialNumber);
				if (only || sameEntity)
				{
					core.Level = MX(entry, FCrBaseCoreData, UpgradeLevel) + 1;  // le jeu stocke a partir de 0 et affiche a partir de 1
					core.Cooling = MX(entry, FCrBaseCoreData, CurrentCoolingCapacity);
					core.CoolingUsed = MX(entry, FCrBaseCoreData, CurrentCoolingCapacityUsed);
					core.Infected = MX(entry, FCrBaseCoreData, InfectionSphere);
					if (const auto custom = customNames.find(MX(entity, FCrMassPersistentEntityID, ID)); custom != customNames.end())
					{
						core.Name = custom->second;
						++live.BaseCoresNamed;
					}
					++live.BaseCoresJoined;
					break;
				}
			}
		}
	}
}

	void ResolveNativeAccess(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		if (!self || !scanner || !scanner->DeclareFeature || !scanner->Resolve || !scanner->ResolveField)
		{
			return;
		}
		static const char* const keys[] = { kKeyEntityManager, kKeyQueryCtor, kKeyQueryDtor, kKeyQueryHandles,
			kKeyRequireTransform, kKeyGetFragment, kKeyFree, "FMassEntityQuery" };
		PluginFeatureDesc feature = PLUGIN_FEATURE_DESC_INIT;
		feature.id = "ingame-map-monoliths";
		feature.title = "In-game map: monolith state and charge";
		feature.keys = keys;
		feature.keyCount = static_cast<uint32_t>(std::size(keys));
		feature.enabled = &g_nativeFeatureOn;
		// Sans declaration, un echec de resolution refuserait tout le plugin : on ne demande rien.
		if (!scanner->DeclareFeature(self, &feature))
		{
			g_nativeFeatureOn = false;
			return;
		}

		// Table du loader seulement (pas de signature) : une cle absente coupe la fonctionnalite.
		g_queryCtor = PluginMap::FunctionAs<QueryCtorFn>(scanner, self, kKeyQueryCtor, nullptr);
		g_queryDtor = PluginMap::FunctionAs<QueryDtorFn>(scanner, self, kKeyQueryDtor, nullptr);
		g_queryHandles = PluginMap::FunctionAs<QueryHandlesFn>(scanner, self, kKeyQueryHandles, nullptr);
		g_requireTransform = PluginMap::FunctionAs<RequireFn>(scanner, self, kKeyRequireTransform, nullptr);
		g_getFragment = PluginMap::FunctionAs<GetFragmentFn>(scanner, self, kKeyGetFragment, nullptr);
		g_free = PluginMap::FunctionAs<FreeFn>(scanner, self, kKeyFree, nullptr);
		PluginMap::ResolveField(scanner, self, g_querySize);

		// Membre natif (TSharedPtr replie dans du padding par le SDK) : meme ligne que plugin_map.h.
		static const PluginFieldCandidate rows[] = { { 127004, 0x38, 0x10, 0, nullptr } };
		PluginFieldRequest field = PLUGIN_FIELD_REQUEST_INIT;
		field.key = kKeyEntityManager;
		field.className = "UMassEntitySubsystem";
		field.member = "EntityManager";
		field.flags = PLUGIN_FIELD_NATIVE;
		field.size = 0x10;
		// Pas PLUGIN_GAME_BUILT_CL : le loader accepte builtOffset sur builtCL sans
		// verification, et toute la disposition native n'est verifiee que sur ce CL.
		field.builtCL = 127004;
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
		SDK::UWorld* world = PluginSdkRuntime::GetWorld();
		if (!world)
		{
			return live;
		}
		SDK::AActor* owner = SDK::UGameplayStatics::GetActorOfClass(world, SDK::ACrAntennasOwner::StaticClass());
		if (MapStateSdk::TryIsObjectOfClass(owner, SDK::ACrAntennasOwner::StaticClass()))
		{
			const auto antennas = MX(MX_PTR(owner, ACrAntennasOwner, AntennaContainer), FCrAntennaContainer, AntennasData);
			const int count = std::clamp(antennas.Num(), 0, kMaxPoiEntries);
			float extent = 0.0f;
			for (int index = 0; index < count; ++index)
			{
				const uint8_t* data = MX_AT(antennas, index, FCrAntennaData);
				const auto location = MX(data, FCrAntennaData, AntennaLocation);
				live.Antennas.push_back({ location.X, location.Y, static_cast<int>(MapStateSdk::LiveAntennaState(MX(data, FCrAntennaData, State))) });
				extent = (std::max)({ extent, std::abs(location.X), std::abs(location.Y) });
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
			const auto buildings = MX(MX_PTR(helper, ACrMapMenuDataReplicationHelper, BuildingsMarkerDataContainer),
				FBuildingsMarkerDataContainer, BuildingsMarkerData);
			live.BuildingMarkers = std::clamp(buildings.Num(), 0, kMaxPoiEntries);
			float buildingExtent = 0.0f;
			for (int index = 0; index < live.BuildingMarkers; ++index)
			{
				const uint8_t* building = MX_AT(buildings, index, FBuildingMarkerDataFastArrayItem);
				const auto location = MX(building, FBuildingMarkerDataFastArrayItem, Location);
				buildingExtent = (std::max)({ buildingExtent, std::abs(location.X), std::abs(location.Y) });
				const SDK::FName uniqueName = MX(building, FBuildingMarkerDataFastArrayItem, BuildingUniqueName);
				const std::string unique = uniqueName.IsNone() ? std::string() : uniqueName.ToString();
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
				std::string name = MX(building, FBuildingMarkerDataFastArrayItem, BuildingName).ToString();
				const SDK::FMassEntityHandle handle = MX(building, FBuildingMarkerDataFastArrayItem, BuildingHandle);
				LiveBaseCore core{ location.X, location.Y, name.empty() ? unique : name };
				core.EntityIndex = handle.Index;
				core.EntitySerial = handle.SerialNumber;
				live.BaseCores.push_back(std::move(core));
			}
			if (buildingExtent > 0.0f && buildingExtent < 20000.0f)  // metres, meme regle que les antennes
			{
				for (LiveBaseCore& core : live.BaseCores)
				{
					core.X *= 100.0f; core.Y *= 100.0f;
				}
			}
			const auto markers = MX(MX_PTR(helper, ACrMapMenuDataReplicationHelper, BaseAttackMarkerDataContainer),
				FBaseAttackMarkerDataContainer, BaseAttackMarkerData);
			const int count = std::clamp(markers.Num(), 0, kMaxPoiEntries);
			float extent = 0.0f;
			for (int index = 0; index < count; ++index)
			{
				const uint8_t* marker = MX_AT(markers, index, FBaseAttackMarkerDataFastArrayItem);
				const auto location = MX(marker, FBaseAttackMarkerDataFastArrayItem, Location);
				const auto attack = MX(marker, FBaseAttackMarkerDataFastArrayItem, AttackLocationIfAny);
				LiveObelisk obelisk{ location.X, location.Y, static_cast<int>(MapStateSdk::LiveObeliskState(MX(marker, FBaseAttackMarkerDataFastArrayItem, State))), -1.0f };
				obelisk.HasAttack = attack.X != 0.0f || attack.Y != 0.0f;
				obelisk.AttackX = attack.X;
				obelisk.AttackY = attack.Y;
				live.Obelisks.push_back(obelisk);
				extent = (std::max)({ extent, std::abs(location.X), std::abs(location.Y) });
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
			const auto items = MX(MX_PTR(bubbles[bubbleIndex], ACrMassAlienObeliskClientBubbleInfo, AlienObeliskSerializer),
				FCrMassAlienObeliskClientBubbleSerializer, AlienObelisk);
			const int count = std::clamp(items.Num(), 0, kMaxPoiEntries);
			for (int index = 0; index < count; ++index)
			{
				const uint8_t* agent = MX_PTR(MX_AT(items, index, FCrAlienObeliskFastArrayItem), FCrAlienObeliskFastArrayItem, Agent);
				const SDK::FVector position = MX(MX_PTR(agent, FCrReplicatedAlienObelisk, PositionYaw), FReplicatedAgentPositionYawData, Position);
				const uint8_t* data = MX_PTR(agent, FCrReplicatedAlienObelisk, AlienObeliskData);
				merge(static_cast<float>(position.X), static_cast<float>(position.Y),
					static_cast<int>(MapStateSdk::LiveObeliskState(MX(data, FCrReplicatedAlienObeliskData, State))), MX(data, FCrReplicatedAlienObeliskData, ChargeValue));
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
				merge(x, y, -1, MX(obelisk, ACrAlienObelisk, PrevChargeValue));
			}
		}

		// Lecture native : fait autorite sur l'etat et la charge quand elle repond.
		// Hors pile : ~20 Ko de tableaux POD, reutilises d'une passe a l'autre (game thread).
		static NativeScan native;
		native.ObeliskCount = native.BaseCoreCount = native.Scanned = 0;
		if (CaptureNativeEntities(world, native))
		{
			live.EntitiesScanned = native.Scanned;
			live.BaseCoresNative = native.BaseCoreCount;
			// Base cores : les marqueurs du jeu arrivent en retard (16 a 56 s mesures) ; la
			// lecture native les donne tout de suite. Le marqueur, quand il existe, garde le nom.
			for (int index = 0; index < native.BaseCoreCount; ++index)
			{
				const RawBaseCore& raw = native.BaseCores[index];
				const bool known = std::any_of(live.BaseCores.begin(), live.BaseCores.end(),
					[&](const LiveBaseCore& core) { return core.EntityIndex == raw.EntityIndex; });
				if (!known)
				{
					LiveBaseCore core{ static_cast<float>(raw.X), static_cast<float>(raw.Y), "Base core" };
					core.EntityIndex = raw.EntityIndex;
					live.BaseCores.push_back(std::move(core));
				}
			}
			live.ObeliskNative = native.ObeliskCount;
			for (int obeliskIndex = 0; obeliskIndex < native.ObeliskCount; ++obeliskIndex)
			{
				const RawObelisk& raw = native.Obelisks[obeliskIndex];
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
		JoinBaseCoreLevels(world, live);
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
		SDK::UWorld* world = PluginSdkRuntime::GetWorld();
		if (SDK::ACrMapMenuDataReplicationHelper* helper = FindMapHelper(world))
		{
			const auto pois = MX(helper, ACrMapMenuDataReplicationHelper, PointOfInterestReplicationData);
			const int count = std::clamp(pois.Num(), 0, kMaxPoiEntries);
			for (int index = 0; index < count; ++index)
			{
				const uint8_t* data = MX_AT(pois, index, FPointOfInterestData);
				states[GuidKey(MX(data, FPointOfInterestData, PointOfInterestMarkerID))].State =
					static_cast<int>(MapStateSdk::LivePoiState(MX(data, FPointOfInterestData, State)));
			}
		}
		SDK::AActor* actor = world
			? SDK::UGameplayStatics::GetActorOfClass(world, SDK::ACrAbandonBaseInfoReplicator::StaticClass())
			: nullptr;
		if (MapStateSdk::TryIsObjectOfClass(actor, SDK::ACrAbandonBaseInfoReplicator::StaticClass()))
		{
			const auto bases = MX(MX_PTR(actor, ACrAbandonBaseInfoReplicator, AbandonBaseDataContainer), FCrAbandonBaseDataContainer, AbandonBaseData);
			const int count = std::clamp(bases.Num(), 0, kMaxPoiEntries);
			for (int index = 0; index < count; ++index)
			{
				const uint8_t* base = MX_AT(bases, index, FCrAbandonBaseData);
				PoiLiveState& state = states[GuidKey(MX(base, FCrAbandonBaseData, AbandonBasePOIId))];
				state.IsBase = true;
				CountStatuses(MX(base, FCrAbandonBaseData, FoundablesCollectedStatus), state.Done[0], state.Total[0]);
				CountStatuses(MX(base, FCrAbandonBaseData, BlueprintChestsCollectedStatus), state.Done[1], state.Total[1]);
				CountStatuses(MX(base, FCrAbandonBaseData, StoryItemsCollectedStatus), state.Done[2], state.Total[2]);
				CountStatuses(MX(base, FCrAbandonBaseData, InfectionCystsDestroyedStatus), state.Done[3], state.Total[3]);
			}
		}
		return states;
	}

	void WarmUpLiveState()
	{
		const auto started = std::chrono::steady_clock::now();
		// Chaque StaticClass() parcourt GObjects a son premier appel, puis memorise.
		const SDK::UClass* classes[] = {
			SDK::UGameplayStatics::StaticClass(), SDK::USubsystemBlueprintLibrary::StaticClass(),
			SDK::ACrMapMenuDataReplicationHelper::StaticClass(), SDK::ACrAbandonBaseInfoReplicator::StaticClass(),
			SDK::ACrAntennasOwner::StaticClass(), SDK::ACrBaseCoreReplicationHelper::StaticClass(),
			SDK::ACrMassAlienObeliskClientBubbleInfo::StaticClass(), SDK::ACrAlienObelisk::StaticClass(),
			SDK::UMassEntitySubsystem::StaticClass(), SDK::UCrAlienObeliskTrait::StaticClass() };
		int resolved = 0;
		for (const SDK::UClass* cls : classes)
		{
			resolved += cls ? 1 : 0;
		}
		SDK::UObject* obeliskType = nullptr;
		SDK::UObject* transformType = nullptr;
		SDK::UObject* baseCoreType = nullptr;
		const bool fragments = ResolveFragmentTypes(obeliskType, baseCoreType, transformType);
		LOG_INFO("[InGameMap] live state warm-up: classes=%d/%d fragments=%d ms=%.1f", resolved, static_cast<int>(std::size(classes)),
			fragments ? 1 : 0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
	}

	int ReadRadiationLevel()
	{
		SDK::ACrMapMenuDataReplicationHelper* helper = FindMapHelper(PluginSdkRuntime::GetWorld());
		return helper && GameFields::On(GameFields::Group::Markers)
			? static_cast<int>(MX(helper, ACrMapMenuDataReplicationHelper, CurrentRadiationLevelReplicated)) : -1;
	}
}
