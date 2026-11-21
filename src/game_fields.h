#pragma once

// Every game member, struct size and UFunction parameter the plugin reads goes
// through this file, so one DLL runs on any game build the mod loader knows
// (PLUGIN_MAP_MIGRATED). A row is declared once, at namespace scope, with
// MX_FIELD / MX_SIZE / MX_PARAM / MX_PARAMS_SIZE: it registers itself, and
// ResolveAll asks the loader for all of them in OnPluginLoadHooks. Each row
// belongs to a Group; a row the loader cannot place turns off its group only.
//
// Reading: MX(object, Class, Member) gives the member's value on the running
// build, or a value-initialised T when its group is off. Never write
// object->Member for a game class: that is the offset of the build the plugin
// was compiled against.
//
// Audit ([Diagnostics] FieldAudit=1): MX also reads the member at its compiled
// offset and logs the first difference per member, and ResolveAll's results are
// logged as a table (compiled offset and size against the resolved ones).

#include "plugin_interface.h"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"
#include "plugin_map.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace GameFields
{
	enum class Group : uint8_t
	{
		Terrain,      // map terrain tiles and their zoom rules
		Markers,      // building and base-attack markers, points of interest
		Logistics,    // cargo senders/receivers, connections, teleporters
		Players,      // player pawns and states
		Rupture,      // rupture cycle (wave subsystem, interactivity rules)
		Structures,   // antennas, base cores, abandoned bases, monolith replication
		Gatherables,  // resources and their depletion
		Items,        // item data names and icons
		Count
	};

	const char* GroupName(Group group);

	enum class Kind : uint8_t { Member, Size, Param, ParamsSize };

	struct Entry
	{
		PluginFieldSpec* spec;
		Group group;
		Kind kind;
		uint32_t compiledSize;  // sizeof as compiled (member, struct or parameter block)
	};

	std::vector<Entry>& Registry();

	// A member requested at whatever size the running game gives it (PLUGIN_FIELD_ANY_SIZE): a
	// member the plugin only steps into (MX_PTR) may grow without harm. Reading it as a value
	// (MX) needs the compiled size; on another size the read gives T{} and logs once.
	template <typename T>
	struct Field : PluginField<T>
	{
		Field(PluginField<T> field, Group group) : PluginField<T>(field)
		{
			Registry().push_back({ this, group, Kind::Member, static_cast<uint32_t>(sizeof(T)) });
		}
		mutable bool sizeWarned = false;
	};

	struct Spec : PluginFieldSpec
	{
		Spec(PluginFieldSpec spec, Group group, Kind kind, uint32_t compiledSize) : PluginFieldSpec(spec)
		{
			Registry().push_back({ this, group, kind, compiledSize });
		}
	};

	// OnPluginLoadHooks (client): declares one feature per group, then asks for every row.
	void ResolveAll(IPluginSelf* self, IPluginHookScanner* scanner);
	// PluginInit, once the logger and config are up: one summary line, plus the
	// full table when the audit is on.
	void LogResolution();
	// False until ResolveAll ran, or once a row of the group did not resolve.
	bool On(Group group);

	// UStruct::MinAlignment of the game struct `shortName` (no F prefix) as the running game
	// reflects it, 0 when not found. Game thread; cached.
	uint32_t LiveAlignment(const char* shortName);

	bool AuditOn();
	// Once per member: a value read refused because the member changed size on this build.
	void WarnValueSize(const PluginFieldSpec& spec, uint32_t compiledSize);
	// Compares the resolved value with the compiled one (under SEH) and counts.
	void AuditRead(const PluginFieldSpec& spec, const void* resolved, const void* compiled, uint32_t size);
	// Game thread, from the snapshot refresh: a summary line every 60 s while the audit is on.
	void AuditTick();

	template <typename T, typename Compiled>
	T Read(const Field<T>& field, const void* object, Compiled compiledAddress)
	{
		const T* at = field.In(object);
		if (!at)
		{
			return T{};
		}
		if (field.resolvedSize != sizeof(T))
		{
			if (!field.sizeWarned)
			{
				field.sizeWarned = true;
				WarnValueSize(field, static_cast<uint32_t>(sizeof(T)));
			}
			return T{};
		}
		if (AuditOn())
		{
			AuditRead(field, at, compiledAddress(), static_cast<uint32_t>(sizeof(T)));
		}
		return *at;
	}

	// Calls the game UFunction `classShort`.`function` (Dumper-7 names, e.g. "CrEnviroWaveSubsystem",
	// "GetCurrentType") on `object` with a block of the live layout, the way the generated SDK
	// calls it. False, and no call, when the object, the block or the function is missing.
	bool Invoke(SDK::UObject* object, const char* classShort, const char* function, PluginMap::ParamsBlock& block);

	// A UFunction with no argument: its ReturnValue, or a value-initialised R when the call
	// cannot be made on this build (rows unresolved, size mismatch, no function).
	template <typename R>
	R CallReturn(SDK::UObject* object, const char* classShort, const char* function,
		const PluginFieldSpec& params, const PluginFieldSpec& returnValue)
	{
		PluginMap::ParamsBlock block = PluginMap::ParamsBlockOf(params, { &returnValue });
		R value{};
		if (returnValue.resolvedSize == sizeof(R) && Invoke(object, classShort, function, block))
		{
			std::memcpy(&value, block.Param("ReturnValue"), sizeof(R));
		}
		return value;
	}

	// Logs an enum whose numbers moved, and every lookup when the audit is on.
	void LogEnum(const char* enumName, const char* entry, int64_t compiled, int64_t live, bool found);

	// A game enum value read on the running build, translated to the compiled SDK value by
	// entry name, so code keeps comparing with SDK constants: an update that renumbers an
	// enum keeps its names. A live value with no listed entry gives `fallback`. The table is
	// built on first use (game thread; EnumValue walks every loaded object), retried a few
	// times while the enum is not loaded, then left as identity.
	template <typename E>
	class LiveEnum
	{
	public:
		LiveEnum(const char* enumName, E fallback, std::initializer_list<std::pair<const char*, E>> entries)
			: m_name(enumName), m_fallback(fallback), m_entries(entries) {}

		E operator()(E live)
		{
			if (!m_built && m_attempts < 5)
			{
				Build();
			}
			if (!m_built)
			{
				return live;
			}
			for (const auto& [liveValue, compiled] : m_table)
			{
				if (liveValue == static_cast<int64_t>(live))
				{
					return compiled;
				}
			}
			return m_fallback;
		}

	private:
		void Build()
		{
			++m_attempts;
			std::vector<std::pair<int64_t, E>> table;
			for (const auto& [entry, compiled] : m_entries)
			{
				int64_t value = 0;
				const PluginMap::EnumLookup lookup = PluginMap::EnumValue(m_name, entry, value);
				if (lookup == PluginMap::EnumLookup::EnumMissing)
				{
					return;  // not loaded yet
				}
				const bool found = lookup == PluginMap::EnumLookup::Found;
				LogEnum(m_name, entry, static_cast<int64_t>(compiled), value, found);
				if (found)
				{
					table.emplace_back(value, compiled);
				}
			}
			m_table = std::move(table);
			m_built = true;
		}

		const char* m_name;
		E m_fallback;
		std::vector<std::pair<const char*, E>> m_entries;
		std::vector<std::pair<int64_t, E>> m_table;
		bool m_built = false;
		int m_attempts = 0;
	};

	// Element `index` of a game TArray stepped by the struct's resolved size.
	template <typename Element>
	const uint8_t* At(const SDK::TArray<Element>& array, int32_t index, const PluginFieldSpec& size)
	{
		return PluginMap::ElementAt(array, index, size);
	}
}

#define MX_ROW_NAME(Class, Member) Class##__##Member

// A member of a game class or struct.
#define MX_FIELD(group, Class, Member) \
	static GameFields::Field<decltype(SDK::Class::Member)> MX_ROW_NAME(Class, Member){ \
		PluginField<decltype(SDK::Class::Member)>{ SR_FIELD_ANY_SIZE(Class, Member, 0) }, GameFields::Group::group }

// The size of a game struct as the running game reflects it, for stepping an array.
#define MX_SIZE(group, Class) \
	static GameFields::Spec MX_ROW_NAME(Class, Size){ SR_SIZE(Class), GameFields::Group::group, \
		GameFields::Kind::Size, static_cast<uint32_t>(sizeof(SDK::Class)) }

// A UFunction's parameter block size and one of its parameters (Class without its prefix letter).
#define MX_PARAMS_SIZE(group, Class, Function) \
	static GameFields::Spec MX_ROW_NAME(Class##_##Function, Params){ SR_PARAMS_SIZE(Class, Function), \
		GameFields::Group::group, GameFields::Kind::ParamsSize, static_cast<uint32_t>(sizeof(SDK::Params::Class##_##Function)) }
#define MX_PARAM(group, Class, Function, Name) \
	static GameFields::Spec MX_ROW_NAME(Class##_##Function, Name){ SR_PARAM(Class, Function, Name), \
		GameFields::Group::group, GameFields::Kind::Param, \
		static_cast<uint32_t>(sizeof(decltype(SDK::Params::Class##_##Function::Name))) }

// The rows of a no-argument UFunction returning a value, and the call.
#define MX_RETURN_ROWS(group, Class, Function) \
	MX_PARAMS_SIZE(group, Class, Function); MX_PARAM(group, Class, Function, ReturnValue)
#define MX_RETURN(R, object, Class, Function) \
	GameFields::CallReturn<R>(object, #Class, #Function, MX_ROW_NAME(Class##_##Function, Params), \
		MX_ROW_NAME(Class##_##Function, ReturnValue))
// One row of a UFunction's parameter block, for PluginMap::ParamsBlockOf.
#define MX_PARAMS(Class, Function) MX_ROW_NAME(Class##_##Function, Params)
#define MX_ARG(Class, Function, Name) MX_ROW_NAME(Class##_##Function, Name)

// The value of `object`'s Member on the running build (`object` points at an SDK::Class
// or at raw memory holding one).
#define MX(object, Class, Member) \
	GameFields::Read(MX_ROW_NAME(Class, Member), static_cast<const void*>(object), \
		[&]() -> const void* { return &reinterpret_cast<const SDK::Class*>(object)->Member; })

// The address of `object`'s Member on the running build (null when unresolved or `object` is
// null), to chain into a nested game struct: MX(MX_PTR(a, FOuter, Inner), FInner, Value).
// Never take a nested game struct by value: its members would be read at compiled offsets.
#define MX_PTR(object, Class, Member) \
	reinterpret_cast<const uint8_t*>(MX_ROW_NAME(Class, Member).In(static_cast<const void*>(object)))

// The alignment of game struct Class on the running build (0 when not found).
#define MX_LIVE_ALIGN(Class) GameFields::LiveAlignment(#Class + 1)

// The size of game struct Class on the running build (0 when its MX_SIZE row did not resolve).
#define MX_LIVE_SIZE(Class) (MX_ROW_NAME(Class, Size).resolvedSize)

// Element `index` of a game TArray of SDK::Class, as raw memory for MX.
#define MX_AT(array, index, Class) GameFields::At((array), (index), MX_ROW_NAME(Class, Size))

// A walk over a game TMap whose key is the game struct KeyClass (MX_SIZE row) and whose
// value is the engine type ValueType: .ForEach([](uint8_t* key, uint8_t* value) { ... }).
#define MX_MAP(mapAddress, KeyClass, ValueType) \
	PluginMap::ScriptMapView(static_cast<const void*>(mapAddress), PluginMap::MapLayout( \
		MX_ROW_NAME(KeyClass, Size).resolvedSize, GameFields::LiveAlignment(#KeyClass + 1), \
		static_cast<uint32_t>(sizeof(ValueType)), static_cast<uint32_t>(alignof(ValueType))))
