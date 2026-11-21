#include "game_fields.h"

#include "plugin_config.h"
#include "plugin_helpers.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <excpt.h>
#include <map>
#include <string>

namespace GameFields
{
	namespace
	{
		constexpr size_t kGroupCount = static_cast<size_t>(Group::Count);

		std::array<PluginMap::Feature, kGroupCount> g_features{};
		std::array<bool, kGroupCount> g_declared{};
		bool g_resolved = false;
		bool g_audit = false;

		struct AuditStat
		{
			uint64_t reads = 0;
			uint64_t differences = 0;
			uint64_t faults = 0;
		};
		std::map<const PluginFieldSpec*, AuditStat> g_auditStats;  // game thread only
		std::chrono::steady_clock::time_point g_lastAuditSummary{};

		// POD only: copies `size` bytes at `from`, or returns false on an access fault.
		bool CopyGuarded(void* to, const void* from, uint32_t size)
		{
			__try
			{
				std::memcpy(to, from, size);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		std::string Hex(const void* data, uint32_t size)
		{
			std::string out;
			char byte[4];
			const auto* bytes = static_cast<const uint8_t*>(data);
			const uint32_t shown = size < 16 ? size : 16;
			for (uint32_t i = 0; i < shown; ++i)
			{
				std::snprintf(byte, sizeof(byte), "%02X", bytes[i]);
				out += byte;
			}
			if (shown < size)
			{
				out += "..";
			}
			return out;
		}

		const char* KindName(Kind kind)
		{
			switch (kind)
			{
			case Kind::Member: return "member";
			case Kind::Size: return "size";
			case Kind::Param: return "param";
			case Kind::ParamsSize: return "params";
			}
			return "?";
		}

		// A member or parameter is placed by its offset, a size row by its resolved size.
		bool IsResolved(const Entry& entry)
		{
			return entry.kind == Kind::Size || entry.kind == Kind::ParamsSize
				? entry.spec->resolvedSize != 0
				: entry.spec->offset >= 0;
		}

		bool Moved(const Entry& entry)
		{
			switch (entry.kind)
			{
			case Kind::Member:
				return entry.spec->offset != entry.spec->builtOffset || entry.spec->resolvedSize != entry.compiledSize;
			case Kind::Param:
				return entry.spec->offset != entry.spec->builtOffset;
			case Kind::Size:
				return entry.spec->resolvedSize != entry.compiledSize;
			case Kind::ParamsSize:
				// ParmsSize stops at the last parameter, sizeof also counts the tail padding:
				// only a block that grew, or lost more than that padding, has moved.
				return entry.spec->resolvedSize > entry.compiledSize || entry.spec->resolvedSize + 8 <= entry.compiledSize;
			}
			return false;
		}
	}

	std::vector<Entry>& Registry()
	{
		static std::vector<Entry> registry;
		return registry;
	}

	const char* GroupName(Group group)
	{
		switch (group)
		{
		case Group::Terrain: return "terrain";
		case Group::Markers: return "markers";
		case Group::Logistics: return "logistics";
		case Group::Players: return "players";
		case Group::Rupture: return "rupture";
		case Group::Structures: return "structures";
		case Group::Gatherables: return "gatherables";
		case Group::Items: return "items";
		case Group::Count: break;
		}
		return "?";
	}

	void ResolveAll(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		if (!self || !scanner || !scanner->DeclareFeature || !scanner->ResolveField)
		{
			return;
		}
		static const char* const titles[kGroupCount] = {
			"In-game map: terrain tiles",
			"In-game map: building markers and points of interest",
			"In-game map: cargo links and teleporters",
			"In-game map: players",
			"In-game map: rupture cycle",
			"In-game map: antennas, base cores and abandoned bases",
			"In-game map: resources",
			"In-game map: item names and icons",
		};
		for (size_t group = 0; group < kGroupCount; ++group)
		{
			PluginMap::Feature& feature = g_features[group];
			feature.id = GroupName(static_cast<Group>(group));
			feature.title = titles[group];
			for (const Entry& entry : Registry())
			{
				if (static_cast<size_t>(entry.group) == group)
				{
					feature.keys.push_back(entry.spec->key);
				}
			}
			// A group with no rows declares nothing; one the loader refuses keeps its keys core.
			g_declared[group] = !feature.keys.empty() && PluginMap::DeclareFeature(scanner, self, feature);
		}
		for (Entry& entry : Registry())
		{
			PluginMap::ResolveField(scanner, self, *entry.spec);
		}
		g_resolved = true;
	}

	bool On(Group group)
	{
		const size_t index = static_cast<size_t>(group);
		return g_resolved && index < kGroupCount && (!g_declared[index] || g_features[index].On());
	}

	void LogResolution()
	{
		g_audit = MapExtensionPluginConfig::Config::FieldAudit();
		// A row declared in several files is one key: count and print it once.
		std::vector<const Entry*> unique;
		for (const Entry& entry : Registry())
		{
			bool seen = false;
			for (const Entry* other : unique)
			{
				seen = seen || std::strcmp(other->spec->key, entry.spec->key) == 0;
			}
			if (!seen)
			{
				unique.push_back(&entry);
			}
		}
		struct Count { int rows = 0, resolved = 0, moved = 0; };
		std::array<Count, kGroupCount> counts{};
		for (const Entry* uniqueEntry : unique)
		{
			const Entry& entry = *uniqueEntry;
			Count& count = counts[static_cast<size_t>(entry.group)];
			++count.rows;
			if (IsResolved(entry))
			{
				++count.resolved;
				count.moved += Moved(entry) ? 1 : 0;
			}
		}
		std::string line;
		for (size_t group = 0; group < kGroupCount; ++group)
		{
			const Count& count = counts[group];
			if (count.rows == 0)
			{
				continue;
			}
			char part[96];
			std::snprintf(part, sizeof(part), " %s=%d/%d%s%s", GroupName(static_cast<Group>(group)), count.resolved,
				count.rows, count.moved ? " moved=" : "", count.moved ? std::to_string(count.moved).c_str() : "");
			line += part;
			if (!On(static_cast<Group>(group)))
			{
				line += "(off)";
			}
		}
		LOG_INFO("[GameFields] built for CL-%u, keys=%d:%s", PLUGIN_GAME_BUILT_CL,
			static_cast<int>(unique.size()), line.c_str());

		if (!g_audit)
		{
			return;
		}
		for (const Entry* uniqueEntry : unique)
		{
			const Entry& entry = *uniqueEntry;
			const PluginFieldSpec& spec = *entry.spec;
			const bool resolved = IsResolved(entry);
			const char* status = !resolved ? "MISSING" : (Moved(entry) ? "MOVED" : "same");
			if (entry.kind == Kind::Size || entry.kind == Kind::ParamsSize)
			{
				LOG_INFO("[FieldAudit] %-6s %-9s %-7s %s compiled size=0x%X resolved size=0x%X", KindName(entry.kind),
					GroupName(entry.group), status, spec.key, entry.compiledSize, spec.resolvedSize);
			}
			else
			{
				LOG_INFO("[FieldAudit] %-6s %-9s %-7s %s compiled=0x%X/0x%X resolved=0x%X/0x%X", KindName(entry.kind),
					GroupName(entry.group), status, spec.key, spec.builtOffset, entry.compiledSize,
					spec.offset, spec.resolvedSize);
			}
		}
	}

	bool Invoke(SDK::UObject* object, const char* classShort, const char* function, PluginMap::ParamsBlock& block)
	{
		if (!object || !object->Class || !block)
		{
			return false;
		}
		static std::map<std::string, SDK::UFunction*> functions;  // game thread only
		const std::string key = std::string(classShort) + "." + function;
		auto found = functions.find(key);
		if (found == functions.end())
		{
			found = functions.emplace(key, object->Class->GetFunction(classShort, function)).first;
		}
		SDK::UFunction* ufunction = found->second;
		if (!ufunction)
		{
			return false;
		}
		const auto flags = ufunction->FunctionFlags;
		ufunction->FunctionFlags |= 0x400;  // as the generated SDK does around ProcessEvent
		object->ProcessEvent(ufunction, block.Data());
		ufunction->FunctionFlags = flags;
		return true;
	}

	uint32_t LiveAlignment(const char* shortName)
	{
		static std::map<std::string, uint32_t> cache;
		if (const auto found = cache.find(shortName); found != cache.end())
		{
			return found->second;
		}
		SDK::UObject* found = SDK::UObject::FindObjectFast<SDK::UObject>(shortName, SDK::EClassCastFlags::ScriptStruct);
		const int16_t align = found ? static_cast<SDK::UStruct*>(found)->MinAlignment : 0;
		const uint32_t value = align > 0 ? static_cast<uint32_t>(align) : 0;
		if (value)
		{
			cache.emplace(shortName, value);  // a miss is asked again: the struct may load later
		}
		return value;
	}

	void LogEnum(const char* enumName, const char* entry, int64_t compiled, int64_t live, bool found)
	{
		if (!found)
		{
			LOG_WARN("[GameFields] enum %s has no entry %s on this build", enumName, entry);
		}
		else if (live != compiled)
		{
			LOG_WARN("[GameFields] enum %s::%s moved: compiled=%lld live=%lld", enumName, entry,
				static_cast<long long>(compiled), static_cast<long long>(live));
		}
		else if (g_audit)
		{
			LOG_INFO("[FieldAudit] enum   %s::%s=%lld", enumName, entry, static_cast<long long>(live));
		}
	}

	void WarnValueSize(const PluginFieldSpec& spec, uint32_t compiledSize)
	{
		LOG_WARN("[GameFields] %s is 0x%X bytes on this build, the code reads 0x%X: that value is not read",
			spec.key, spec.resolvedSize, compiledSize);
	}

	bool AuditOn()
	{
		return g_audit;
	}

	void AuditRead(const PluginFieldSpec& spec, const void* resolved, const void* compiled, uint32_t size)
	{
		AuditStat& stat = g_auditStats[&spec];
		++stat.reads;
		uint8_t buffer[256];
		if (size > sizeof(buffer))
		{
			return;
		}
		if (!CopyGuarded(buffer, compiled, size))
		{
			if (stat.faults++ == 0)
			{
				LOG_WARN("[FieldAudit] %s: the compiled offset faulted on this build", spec.key);
			}
			return;
		}
		if (std::memcmp(buffer, resolved, size) != 0)
		{
			if (stat.differences++ == 0)
			{
				LOG_WARN("[FieldAudit] %s differs: resolved=%s compiled=%s (offsets 0x%X / 0x%X)", spec.key,
					Hex(resolved, size).c_str(), Hex(buffer, size).c_str(), spec.offset, spec.builtOffset);
			}
		}
	}

	void AuditTick()
	{
		if (!g_audit)
		{
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - g_lastAuditSummary < std::chrono::seconds(60))
		{
			return;
		}
		g_lastAuditSummary = now;
		uint64_t reads = 0, differing = 0, faulting = 0;
		for (const auto& [spec, stat] : g_auditStats)
		{
			reads += stat.reads;
			differing += stat.differences ? 1 : 0;
			faulting += stat.faults ? 1 : 0;
			if (stat.differences || stat.faults)
			{
				LOG_INFO("[FieldAudit] summary %s reads=%llu differences=%llu faults=%llu", spec->key,
					static_cast<unsigned long long>(stat.reads), static_cast<unsigned long long>(stat.differences),
					static_cast<unsigned long long>(stat.faults));
			}
		}
		LOG_INFO("[FieldAudit] summary members_read=%d reads=%llu members_differing=%llu members_faulting=%llu",
			static_cast<int>(g_auditStats.size()), static_cast<unsigned long long>(reads),
			static_cast<unsigned long long>(differing), static_cast<unsigned long long>(faulting));
	}
}
