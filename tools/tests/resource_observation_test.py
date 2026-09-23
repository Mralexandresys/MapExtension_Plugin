#!/usr/bin/env python3
"""Compile production resource rules with minimal engine stand-ins; no game required.

Run from any directory with Python 3 and g++ (C++20) installed. This checks state,
phase projection, catalog retention, unique depletion matching and wire flags.
It does not validate game object lifetimes, ProcessEvent calls or native hooks.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
src = (ROOT / 'map_state_capture.cpp').read_text()
server = (ROOT / 'server/map_sync_server.cpp').read_text()
client = (ROOT / 'client/map_state_remote_cache.cpp').read_text()


def between(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


rules = between(src, '\tHarvestability ReadStarTearsHarvestability(', '\tbool ObserveTrackedGatherableActor(')
phase = between(src, '\tbool PoiMarkerSortsBefore(', '\tbool CapturePois(')
matching = between(src, '\ttemplate <typename TDepletedEntry>\n\tsize_t MarkDepletedResourceEntries(', '\tvoid MarkReplicatedDepletedResources(')
capture = src[src.index('\tbool CapturePois('):]
retention = between(capture, '\t\tPoiCatalog catalog;', '\n\n\n\t\t// ActorBeginPlay observations')
fill = between(server, '\tvoid FillPoiEntry(', '\tstruct CollectionWireCounts')
decode = between(client, '\t\t\t\tmarker.Depleted = (item.flags', '\t\t\t\tmarker.WorldLocation =')
constants = '\n'.join(line for line in src.splitlines()
                      if 'constexpr double kRupture' in line or 'constexpr double kDepletedPlantMatchRadius' in line)
helpers = r'''
#include "map_state_types.h"
#include "shared/map_sync_protocol.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
using namespace MapStateRuntime::Detail;
using MapResources::Harvestability;
using PoiCatalog = std::unordered_map<std::string, PoiMarker>;
PoiCatalog observed;
bool IsRuptureResource(PoiKind k) { return k==PoiKind::Ignitium || k==PoiKind::StarTears; }
void StoreObservedPoiMarker(SDK::UWorld*, PoiMarker p) { observed.insert_or_assign(p.PublicKey,p); }
std::string BuildLocationKey(const char* prefix,const SDK::FVector& p) { return std::string(prefix)+std::to_string(p.X); }
std::string MakeTaggedPublicKey(const char* prefix,const std::string& key) { return std::string(prefix)+key; }
bool IsTrackedResourceName(const std::string& n) { return n=="Ignitium" || n=="Star Tears" || n=="Hydrobulb"; }
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
PoiMarker Resource(PoiKind kind, const char* name, const char* key, double x=0) {
    PoiMarker p; p.Kind=kind; p.ResourceName=name; p.DisplayName=name; p.PublicKey=key;
    p.Source="actor_observation.gatherable"; p.WorldLocation.X=x; return p;
}
struct MockClass { SDK::UClass* Class; SDK::UClass* Get() { return Class; } };
struct MockSubstages : std::vector<int> {
    bool Contains(int value) const { return std::find(begin(),end(),value)!=end(); }
};
struct MockRule {
    SDK::EEnviroWaveStage WaveStage;
    float EnableInteractionEnviroWaveStageProgressThresholdStart=0.25f;
    float EnableInteractionEnviroWaveStageProgressThresholdEnd=0.75f;
    MockSubstages FadeoutSubstages, GrowbackSubstages;
};
struct MockRuleArray { std::vector<MockRule> InteractivityForWaveStages; };
struct MockPair {
    MockClass Class;
    MockRuleArray Rules;
    const MockClass& Key() const { return Class; }
    const MockRuleArray& Value() const { return Rules; }
};
struct MockInteractivity { std::vector<MockPair> Data; };
struct ResourceReadContext {
    bool HasCycle=false;
    float StageProgress=0;
    MockInteractivity* Interactivity=nullptr;
    struct {
        SDK::EEnviroWaveStage Stage=SDK::EEnviroWaveStage::PreWave;
        int FadeoutSubstage=0, GrowbackSubstage=0;
    } Cycle;
};
struct DepletedEntry { SDK::FVector Location; struct { SDK::FVector Origin; } Bounds; };
'''
wrappers = '''
CargoSnapshot Capture(const CargoSnapshot& previousSnapshot, const std::vector<PoiMarker>& observations,
                      double elapsed, const std::string& worldName="world", bool ruptureResourceGenerationChanged=false) {
''' + retention + '''
    for (const auto& p : observations) catalog.insert_or_assign(p.PublicKey, p);
    CargoSnapshot result; result.WorldName=worldName;
    result.RuptureCycle.Available=elapsed>=0; result.RuptureCycle.HasElapsed=elapsed>=0;
    result.RuptureCycle.ElapsedSeconds=elapsed;
    FinalizePoiCatalog(result,catalog); UpdatePoiCounts(result); ApplyRuptureResourcePhase(result);
    return result;
}
PoiMarker Decode(const MapSyncProtocol::ServerPoiEntry& item) {
    PoiMarker marker;
''' + decode + '''
    return marker;
}
'''
scenarios = r'''
int main() {
    MapResources::GenerationTracker solo;
    Require(!solo.Observe({},false), "initial solo capture must preserve observations");
    Require(solo.Observe({},true), "solo Heat/Moving transition must reset");
    Require(!solo.Observe({},true) && !solo.Observe({},{}), "repeated/missing phase must not reset");
    Require(!solo.Observe({},false) && solo.Observe({},true) && solo.Generation==2, "next solo cycle must reset once");
    MapResources::GenerationTracker joinedDuringFire;
    Require(!joinedDuringFire.Observe({},true), "first mid-cycle observation establishes baseline");
    MapResources::GenerationTracker multiplayer;
    Require(!multiplayer.Observe(7,false) && !multiplayer.Observe(7,true), "seed is authoritative over phase");
    Require(multiplayer.Observe(8,true) && !multiplayer.Observe(8,true), "changed seed resets once");
    Require(!multiplayer.Observe(8,false) && multiplayer.Observe(9,false), "seed reset outside Heat must be detected");
    std::cout << "PASS: solo and replicated generation boundaries\n";

    auto ore=Resource(PoiKind::Ignitium,"Ignitium","ore");
    auto tears=Resource(PoiKind::StarTears,"Star Tears","tears",100);
    auto plant=Resource(PoiKind::PlantResource,"Hydrobulb","plant",1000);
    for (auto h : {Harvestability::Available,Harvestability::Unavailable,Harvestability::Unknown}) {
        ore.Harvestability=h;
        for (bool depleted : {false,true}) {
            ore.Depleted=depleted;
            MapSyncProtocol::ServerPoiEntry entry{}; FillPoiEntry(entry,ore);
            const auto decoded=Decode(entry);
            Require(decoded.Depleted==depleted && decoded.Harvestability==h, "wire must preserve independent state flags");
            Require(!depleted || std::string(MapResources::StateName(decoded.Depleted,decoded.Harvestability))=="depleted", "depletion takes precedence");
        }
    }
    std::cout << "PASS: client/server state roundtrip and depletion priority\n";
    ore.Depleted=false; ore.Harvestability=Harvestability::Unavailable;
    auto stable=Capture({}, {ore}, 800);
    Require(stable.Pois.size()==1 && stable.Pois[0].Kind==PoiKind::StarTears
        && stable.Pois[0].Harvestability==Harvestability::Available, "intact non-mineable ore may project Star Tears");
    const auto repeated=Capture(stable,{ore},800);
    Require(repeated.Pois.size()==1 && repeated.PoiRevision==stable.PoiRevision, "repeated projection must be stable");
    for (auto h : {Harvestability::Available,Harvestability::Unavailable,Harvestability::Unknown}) {
        tears.Harvestability=h;
        for (bool depleted : {false,true}) {
            tears.Depleted=depleted;
            auto actual=Capture(stable,{ore,tears},800);
            Require(actual.Pois.size()==1 && actual.Pois[0].PublicKey=="tears"
                && actual.Pois[0].Harvestability==h && actual.Pois[0].Depleted==depleted,
                "every actual Star Tears state overrides the estimate");
            Require(Capture(actual,{ore,tears},800).PoiRevision==actual.PoiRevision, "estimate must not resurrect");
        }
    }
    ore.Depleted=true;
    Require(Capture(stable,{ore},800).Pois.empty(), "harvested Ignitium must not project Star Tears");
    ore.Depleted=false;
    auto overlap=Capture(stable,{ore,tears},300);
    Require(overlap.Pois.size()==2, "transition permits overlap without cross-depletion");
    auto before=Capture({}, {ore,plant}, 800);
    auto reset=Capture(before,{plant},800,"world",true);
    Require(reset.Pois.size()==1 && reset.Pois[0].Kind==PoiKind::PlantResource, "generation reset preserves plants only");
    Require(Capture(before,{},800,"other").Pois.empty(), "world change clears previous markers");
    for (double phase : {-1.0,0.0,60.0,300.0})
        Require(Capture(stable,{ore},phase).Pois[0].Kind==PoiKind::Ignitium, "stale projection must not survive phase change");
    auto beforeAvailability=Capture({}, {ore},300);
    ore.Harvestability=Harvestability::Available;
    Require(Capture({}, {ore},300).PoiRevision!=beforeAvailability.PoiRevision, "availability must affect page revision");
    std::cout << "PASS: phase projection, state priority, revision and world/generation retention\n";

    SDK::UClass resourceClass, otherClass;
    ResourceReadContext context;
    Require(ReadStarTearsHarvestability(&resourceClass,context)==Harvestability::Unknown, "missing rules/cycle must be unknown");
    context.HasCycle=true;
    MockRule rule{SDK::EEnviroWaveStage::Growback, 0.25f, 0.75f, {}, {}};
    rule.GrowbackSubstages.push_back(2);
    MockInteractivity interactivity{{{{&resourceClass},{{rule}}}}};
    context.Interactivity=&interactivity;
    context.Cycle.Stage=SDK::EEnviroWaveStage::Growback;
    context.Cycle.GrowbackSubstage=2;
    context.StageProgress=0.25f;
    Require(ReadStarTearsHarvestability(&resourceClass,context)==Harvestability::Available, "lower threshold is inclusive");
    Require(ReadStarTearsHarvestability(&otherClass,context)==Harvestability::Unknown, "missing class is unknown");
    context.StageProgress=0.75f;
    Require(ReadStarTearsHarvestability(&resourceClass,context)==Harvestability::Unavailable, "upper threshold is exclusive");
    context.StageProgress=0.5f; context.Cycle.GrowbackSubstage=1;
    Require(ReadStarTearsHarvestability(&resourceClass,context)==Harvestability::Unavailable, "unlisted substage cannot be harvested");
    // The native CanInteract selects the first rule for the stage, not any passing rule.
    auto laterRule=rule; laterRule.GrowbackSubstages.push_back(1);
    interactivity.Data[0].Rules.InteractivityForWaveStages.push_back(laterRule);
    Require(ReadStarTearsHarvestability(&resourceClass,context)==Harvestability::Unavailable, "first matching stage rule wins");
    std::cout << "PASS: missing interaction data, progress boundaries and substage rules\n";

    SDK::UWorld world;
    SDK::TArray<DepletedEntry> entries{{{0,0,0},{{0,0,0}}}};
    PoiCatalog catalog{{"ore",ore},{"tears",tears}};
    Require(MarkDepletedResourceEntries(&world,catalog,entries,{})==1 && !catalog.at("ore").Depleted,
        "depleted Star Tears must not cause overlapping Ignitium to be marked harvested");
    catalog.erase("tears");
    Require(MarkDepletedResourceEntries(&world,catalog,entries,{"ore"})==0 && !catalog.at("ore").Depleted,
        "untyped spatial history cannot override a fresh actor/subsystem observation");
    Require(MarkDepletedResourceEntries(&world,catalog,entries,{})==0 && catalog.at("ore").Depleted
        && observed.at("ore").Depleted, "unique replicated depletion must persist after actor removal");
    catalog={{"ore",ore},{"plant",plant}};
    entries={{{500,0,0},{{1000,0,0}}}};
    MarkDepletedResourceEntries(&world,catalog,entries,{});
    Require(catalog.at("plant").Depleted && !catalog.at("ore").Depleted, "bounds origin fallback must not affect distant resource");
    catalog={{"ore",ore},{"plant",plant}};
    entries={{{0,0,0},{{1000,0,0}}}};
    Require(MarkDepletedResourceEntries(&world,catalog,entries,{})==1 && !catalog.at("plant").Depleted
        && !catalog.at("ore").Depleted, "conflicting location and bounds candidates are ambiguous");
    entries.clear();
    catalog.at("ore").Depleted=true;
    MarkDepletedResourceEntries(&world,catalog,entries,{});
    Require(catalog.at("ore").Depleted, "removed depletion entry does not prove regrowth");
    std::cout << "PASS: unique, overlapping, distant and removed depletion records\n";
}
'''
with tempfile.TemporaryDirectory(prefix='map-resource-tests-') as temp:
    tmp = Path(temp)
    (tmp / 'Basic.hpp').write_text('''#pragma once
#include <vector>
namespace SDK {
struct UWorld {};
struct UClass {};
enum class EEnviroWaveStage { PreWave, Fadeout, Growback };
struct FVector {
    double X=0,Y=0,Z=0;
    FVector operator-(const FVector& r) const { return {X-r.X,Y-r.Y,Z-r.Z}; }
};
struct FVector2f { float X=0,Y=0; };
template<class T> using TArray=std::vector<T>;
}
''')
    (tmp / 'CoreUObject_structs.hpp').write_text('#pragma once\n')
    test = tmp / 'resource_test.cpp'
    test.write_text(helpers + constants + rules + phase + matching + fill + wrappers + scenarios)
    binary = tmp / 'resource_test'
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(tmp),
                    '-I', str(ROOT), str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
