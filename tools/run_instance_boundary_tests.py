#!/usr/bin/env python3
"""Exercise instance exhaustion and cross-state failure atomicity from real sources.

Uses test-only private counter access to the actual InstanceStore and Registry.
The actual BeginSaveScope/OnLoad methods run against a counted public SKSE
serialization interface; an external negative control removes only the reset
check, while the later batch CAS independently rejects publication. Both saved
stores are included in failure snapshots. No testing ABI or DLL is
published. This is a standalone source fixture, not game validation.

Requires the installed x64 MSVC toolchain and public SDK headers, like build.py.
All generated C++, executables, logs and source-pin manifests go to a new/empty
--output directory outside Git. Production sources remain untouched.

Example:
  python -B tools/run_instance_boundary_tests.py --sdk-root <sdk> --output <evidence>
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

CPP = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <algorithm>
#include <limits>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "common/ITypes.h"
#include "skse64/PluginAPI.h"
// Test-only access to the actual store's exhaustion counters; not a production edit.
#define private public
#include "instances.h"
#undef private
#include "instances.cpp"
#include "registry.cpp"
using namespace body_slots;
using namespace body_slots::detail;
unsigned checks = 0;
#define CHECK(v) do { ++checks; if (!(v)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #v); } while(false)
constexpr auto Max = std::numeric_limits<std::uint64_t>::max();
std::vector<std::uint8_t> StoreBytes(const InstanceStore& store) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&store.state_);
    return {p, p + sizeof(store.state_)};
}
std::vector<std::uint8_t> RegistryBytes() {
    auto& s = State(); std::vector<std::uint8_t> bytes;
    const auto append = [&](const auto& field) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&field);
        bytes.insert(bytes.end(), p, p + sizeof(field));
    };
    append(s.clients); append(s.records); append(s.customizers); append(s.customizations);
    auto saved = std::make_unique<InstanceCustomizationData>();
    s.instanceCustomizations.CopyTo(*saved); append(*saved);
    append(s.instances.state_); append(s.count); append(s.revision); append(s.nextToken);
    append(s.nextSlot); append(s.nextDefinitionRevision); append(s.configurationRevision);
    return bytes;
}
void ResetFixture() {
    auto& s = State(); s.clients={}; s.records={}; s.customizers={}; s.customizations={};
    auto savedReset = std::make_unique<InstanceCustomizationStore::Candidate>();
    s.instanceCustomizations.PrepareReset(*savedReset); s.instanceCustomizations.Apply(*savedReset);
    s.instances.state_={}; s.count=0;
    s.revision=s.nextToken=s.nextSlot=s.nextDefinitionRevision=s.configurationRevision=1;
}
Definition Template() {
    Definition d{}; d.structSize=sizeof(d); d.flags=kEnabled;
    std::strcpy(d.provider,"template"); std::strcpy(d.key,"belt");
    std::strcpy(d.anchor,"NPC Pelvis [Pelv]"); std::strcpy(d.referenceRig,"skyrimvr-humanoid-v1");
    std::strcpy(d.kind,"pouch"); d.rotation[0]=d.rotation[4]=d.rotation[8]=1;
    d.radius=5; d.handMask=Both; d.contentMask=Potion; return d;
}
InstanceSpec Spec(std::uint64_t revision=1,const char* equipment="bag") {
    InstanceSpec spec{}; spec.structSize=sizeof(spec); spec.flags=kInstanceActive;
    std::strcpy(spec.equipmentKey,equipment); std::strcpy(spec.slotKey,"opening");
    std::strcpy(spec.definitionProvider,"template"); std::strcpy(spec.definitionKey,"belt");
    spec.definitionRevision=revision; spec.binding=static_cast<std::uint32_t>(InstanceBinding::BodyAnchor);
    return spec;
}
struct Seed { std::uint64_t author=0, equipment=0, observer=0, customizer=0; SlotRecord definition{}; InstanceRecord instance{}; };
Seed SeedRegistry() {
    ResetFixture(); Seed seed;
    CHECK(RegisterClient("template",&seed.author)==Status::Ok);
    CHECK(RegisterClient("equipment",&seed.equipment)==Status::Ok);
    CHECK(RegisterObserver(&seed.observer)==Status::Ok);
    CHECK(RegisterCustomizer("editor",&seed.customizer)==Status::Ok);
    auto d=Template(); CHECK(UpsertDefinition(seed.author,&d,0,&seed.definition)==Status::Ok);
    Customization c{}; c.structSize=sizeof(c); c.fieldMask=CustomizePosition;
    std::strcpy(c.provider,"template"); std::strcpy(c.key,"belt"); c.position[0]=2;
    EffectiveSlotRecord committed{};
    CHECK(ApplyCustomization(seed.customizer,&c,State().configurationRevision,&committed)==Status::Ok);
    auto spec=Spec(seed.definition.definitionRevision); seed.instance.structSize=sizeof(seed.instance);
    CHECK(UpsertInstance(seed.equipment,&spec,State().instances.Generation(),0,&seed.instance)==Status::Ok);
    return seed;
}
std::vector<std::uint8_t> Payload(float position) {
    auto candidate=State().customizations; candidate.entries[0].position[0]=position;
    std::vector<std::uint8_t> bytes; CHECK(EncodeCustomizations(candidate,bytes)==Status::Ok); return bytes;
}
void PureExhaustion() {
    auto store=std::make_unique<InstanceStore>();
    auto candidate=std::make_unique<InstanceStore::Candidate>();
    auto spec=Spec(); std::uint64_t handle=987654;
    CHECK(ValidateInstanceSpec(spec)==Status::Ok);
    CHECK(store->PrepareUpsert(5,"equipment",spec,1,0,*candidate,handle)==Status::Ok);
    store->Apply(*candidate); CHECK(store->Count()==1);
    const auto existing=store->At(0);
    store->state_.generation=Max;
    auto before=StoreBytes(*store);
    CHECK(store->PrepareReset(*candidate)==Status::LimitExceeded); CHECK(StoreBytes(*store)==before);
    store->state_.generation=1; store->state_.view=Max; before=StoreBytes(*store); handle=987654;
    auto other=Spec(1,"other");
    CHECK(store->PrepareInvalidation(*candidate)==Status::LimitExceeded);
    CHECK(store->PrepareUpsert(5,"equipment",other,1,0,*candidate,handle)==Status::LimitExceeded);
    CHECK(handle==987654 && StoreBytes(*store)==before);
    CHECK(store->PrepareRemove(5,existing.handle,1,existing.revision,*candidate)==Status::LimitExceeded);
    CHECK(store->PrepareRelease(5,false,*candidate)==Status::LimitExceeded);
    CHECK(StoreBytes(*store)==before);
    store->state_.view=1; store->state_.nextHandle=kRuntimeCounterLimit+1; before=StoreBytes(*store);
    CHECK(store->PrepareUpsert(5,"equipment",other,1,0,*candidate,handle)==Status::LimitExceeded);
    CHECK(StoreBytes(*store)==before && handle==987654);
    spec.flags=0;
    CHECK(store->PrepareUpsert(5,"equipment",spec,1,existing.revision,*candidate,handle)==Status::Ok);
    store->Apply(*candidate); CHECK(store->At(0).handle==existing.handle);
    store->state_.nextRevision=Max; before=StoreBytes(*store); spec.flags=kInstanceActive;
    CHECK(store->PrepareUpsert(5,"equipment",spec,1,store->At(0).revision,*candidate,handle)==Status::LimitExceeded);
    CHECK(StoreBytes(*store)==before);
    auto same=store->At(0).spec;
    CHECK(store->PrepareUpsert(5,"equipment",same,1,store->At(0).revision,*candidate,handle)==Status::Ok);
    store->Apply(*candidate); CHECK(StoreBytes(*store)==before);
    store->state_.nextRevision=Max-1;
    CHECK(store->PrepareUpsert(5,"equipment",spec,1,store->At(0).revision,*candidate,handle)==Status::Ok);
    store->Apply(*candidate); CHECK(store->At(0).revision==Max-1 && store->state_.nextRevision==Max);
    store->state_.generation=Max-1; store->state_.view=1;
    CHECK(store->PrepareReset(*candidate)==Status::Ok); store->Apply(*candidate);
    CHECK(store->Generation()==Max && store->Count()==0 && store->state_.nextHandle==kRuntimeCounterLimit+1);
    before=StoreBytes(*store); CHECK(store->PrepareReset(*candidate)==Status::LimitExceeded); CHECK(StoreBytes(*store)==before);
    store->state_.generation=1; store->state_.nextRevision=1; store->state_.nextHandle=kRuntimeCounterLimit;
    CHECK(store->PrepareUpsert(5,"equipment",spec,1,0,*candidate,handle)==Status::Ok); store->Apply(*candidate);
    CHECK(handle==((std::uint64_t(3)<<56)|kRuntimeCounterLimit));
    CHECK(store->state_.nextHandle==kRuntimeCounterLimit+1);
    store->state_.view=Max-1;
    CHECK(store->PrepareInvalidation(*candidate)==Status::Ok); store->Apply(*candidate);
    CHECK(store->ViewRevision()==Max);
    before=StoreBytes(*store); CHECK(store->PrepareInvalidation(*candidate)==Status::LimitExceeded); CHECK(StoreBytes(*store)==before);
}
void RegistryAtomicity() {
    const auto seed=SeedRegistry(); auto payload=Payload(9); auto& s=State();
    s.instances.state_.view=Max;
    const auto before=RegistryBytes(); auto d=Template(); d.radius=7;
    SlotRecord rejected{}; std::memset(&rejected,0xa5,sizeof(rejected)); const auto output=rejected;
    CHECK(UpsertDefinition(seed.author,&d,seed.definition.definitionRevision,&rejected)==Status::LimitExceeded);
    CHECK(std::memcmp(&rejected,&output,sizeof(output))==0 && RegistryBytes()==before);
    CHECK(RemoveDefinition(seed.author,"belt",seed.definition.definitionRevision)==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    CHECK(ReplaceDefinitions(seed.author,&d,1,s.revision)==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    CHECK(ReleaseClient(seed.author)==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    CHECK(ReleaseClient(seed.equipment)==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    Customization c=s.customizations.entries[0]; c.position[0]=9;
    EffectiveSlotRecord effective{}; std::memset(&effective,0xa5,sizeof(effective)); const auto sentinel=effective;
    CHECK(ApplyCustomization(seed.customizer,&c,s.configurationRevision,&effective)==Status::LimitExceeded);
    CHECK(std::memcmp(&effective,&sentinel,sizeof(effective))==0 && RegistryBytes()==before);
    CHECK(ClearCustomization(seed.customizer,"template","belt",s.configurationRevision)==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    CHECK(LoadCustomizations(payload.data(),static_cast<std::uint32_t>(payload.size()))==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    CHECK(ResetCustomizations()==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    CHECK(ResetSaveScope()==Status::LimitExceeded); CHECK(RegistryBytes()==before);
    const auto instanceBefore=StoreBytes(s.instances);
    CHECK(ReleaseClient(seed.observer)==Status::Ok); CHECK(StoreBytes(s.instances)==instanceBefore);
    SeedRegistry(); s.instances.state_.generation=Max;
    const auto generationBefore=RegistryBytes(); CHECK(ResetSaveScope()==Status::LimitExceeded); CHECK(RegistryBytes()==generationBefore);
    SeedRegistry(); s.configurationRevision=Max;
    const auto configBefore=RegistryBytes(); CHECK(ResetSaveScope()==Status::LimitExceeded); CHECK(RegistryBytes()==configBefore);
    auto fresh=SeedRegistry(); s.nextDefinitionRevision=Max;
    const auto definitionBefore=RegistryBytes(); d=Template(); d.radius=8;
    CHECK(UpsertDefinition(fresh.author,&d,fresh.definition.definitionRevision,&rejected)==Status::LimitExceeded); CHECK(RegistryBytes()==definitionBefore);
    fresh=SeedRegistry(); const auto registryRev=s.revision, configRev=s.configurationRevision;
    const auto view=s.instances.ViewRevision();
    CHECK(s.count==1 && s.instances.Count()==1);
    CHECK(ReleaseClient(fresh.equipment)==Status::Ok);
    CHECK(s.count==1 && s.instances.Count()==0 && s.instances.ViewRevision()==view+1);
    CHECK(s.revision==registryRev && s.configurationRevision==configRev && !s.FindClient(fresh.equipment));
}
InstanceCustomization SeedSaved(const Seed& seed) {
    InstanceCustomization value{}; value.structSize=sizeof(value); value.fieldMask=CustomizePosition;
    value.key=CustomizationKey(*State().instances.Find(seed.instance.instanceHandle)); value.position[0]=7;
    ConfiguredInstanceRecord out{}; out.instance.structSize=sizeof(InstanceRecord);
    CHECK(ApplyInstanceCustomization(seed.customizer,&value,State().instances.Generation(),State().instances.ViewRevision(),&out)==Status::Ok);
    CHECK(out.instance.effective.position[0]==7 && out.instanceCustomizationMask==CustomizePosition);
    return value;
}
std::vector<std::uint8_t> SavedPayload(float position) {
    auto data=std::make_unique<InstanceCustomizationData>(); State().instanceCustomizations.CopyTo(*data);
    data->entries[0].position[0]=position; std::vector<std::uint8_t> bytes;
    CHECK(EncodeInstanceCustomizations(*data,bytes)==Status::Ok); return bytes;
}
void SavedInstanceAtomicity() {
    auto seed=SeedRegistry(); auto saved=SeedSaved(seed); auto& s=State();
    const auto definitions=Payload(9), instances=SavedPayload(11);
    CHECK(EffectiveInstance(s,*s.instances.Find(seed.instance.instanceHandle)).effective.position[0]==2);
    const auto oldScope=s.instances.Generation(), oldView=s.instances.ViewRevision();
    const auto beforeStale=RegistryBytes();
    CHECK(LoadConfigurationBatch(&definitions,&instances,oldScope+1,oldView)==Status::Conflict);
    CHECK(RegistryBytes()==beforeStale);
    CHECK(LoadConfigurationBatch(&definitions,&instances,oldScope,oldView+1)==Status::Conflict);
    CHECK(RegistryBytes()==beforeStale);
    auto invalidInstances=instances; invalidInstances.pop_back();
    CHECK(LoadConfigurationBatch(&definitions,&invalidInstances,oldScope,oldView)==Status::InvalidArgument);
    CHECK(RegistryBytes()==beforeStale);
    auto invalidDefinitions=definitions; invalidDefinitions.pop_back();
    CHECK(LoadConfigurationBatch(&invalidDefinitions,&instances,oldScope,oldView)==Status::InvalidArgument);
    CHECK(RegistryBytes()==beforeStale);

    s.instances.state_.view=Max;
    const auto before=RegistryBytes();
    InstanceView rejected{}; std::memset(&rejected,0xa5,sizeof(rejected)); const auto sentinel=rejected;
    CHECK(ResetSaveScope(&rejected)==Status::LimitExceeded);
    CHECK(std::memcmp(&rejected,&sentinel,sizeof(rejected))==0 && RegistryBytes()==before);
    CHECK(LoadConfigurationBatch(&definitions,&instances,s.instances.Generation(),s.instances.ViewRevision())==Status::LimitExceeded);
    CHECK(RegistryBytes()==before);
    auto changed=saved; changed.position[0]=13;
    ConfiguredInstanceRecord output{}; output.instance.structSize=sizeof(InstanceRecord); output.instance.flags=0xa5a5a5a5;
    const auto outputSentinel=output;
    CHECK(ApplyInstanceCustomization(seed.customizer,&changed,s.instances.Generation(),s.instances.ViewRevision(),&output)==Status::LimitExceeded);
    CHECK(std::memcmp(&output,&outputSentinel,sizeof(output))==0 && RegistryBytes()==before);
    CHECK(ClearInstanceCustomization(seed.customizer,&saved.key,s.instances.Generation(),s.instances.ViewRevision())==Status::LimitExceeded);
    CHECK(RegistryBytes()==before);
    CHECK(ApplyInstanceCustomization(seed.customizer,&saved,s.instances.Generation(),s.instances.ViewRevision(),&output)==Status::Ok);
    CHECK(RegistryBytes()==before); // Identical customization needs no new revision.

    seed=SeedRegistry(); saved=SeedSaved(seed); s.instances.state_.generation=Max;
    const auto generationBefore=RegistryBytes(); rejected=sentinel;
    CHECK(ResetSaveScope(&rejected)==Status::LimitExceeded);
    CHECK(RegistryBytes()==generationBefore && std::memcmp(&rejected,&sentinel,sizeof(rejected))==0);
    seed=SeedRegistry(); saved=SeedSaved(seed); s.configurationRevision=Max;
    const auto configurationBefore=RegistryBytes();
    CHECK(ResetSaveScope(&rejected)==Status::LimitExceeded);
    CHECK(RegistryBytes()==configurationBefore);
    CHECK(LoadConfigurationBatch(&definitions,&instances,s.instances.Generation(),s.instances.ViewRevision())==Status::LimitExceeded);
    CHECK(RegistryBytes()==configurationBefore);

    seed=SeedRegistry(); saved=SeedSaved(seed);
    CHECK(ResetCustomizations()==Status::Ok); // Existing internal helper remains definition-only.
    CHECK(s.customizations.count==0 && s.instanceCustomizations.Count()==1);
    CHECK(LoadCustomizations(definitions.data(),static_cast<std::uint32_t>(definitions.size()))==Status::Ok);
    CHECK(s.instanceCustomizations.Count()==1);
    CHECK(EffectiveInstance(s,*s.instances.Find(seed.instance.instanceHandle)).effective.position[0]==9);
    CHECK(ConfiguredInstance(s,*s.instances.Find(seed.instance.instanceHandle)).instance.effective.position[0]==7);
    CHECK(RemoveInstance(seed.equipment,seed.instance.instanceHandle,s.instances.Generation(),seed.instance.instanceRevision)==Status::Ok);
    auto other=Template(); std::strcpy(other.key,"other"); SlotRecord otherRecord{};
    CHECK(UpsertDefinition(seed.author,&other,0,&otherRecord)==Status::Ok);
    auto spec=Spec(otherRecord.definitionRevision); std::strcpy(spec.definitionKey,"other");
    InstanceRecord recreated{}; recreated.structSize=sizeof(recreated);
    CHECK(UpsertInstance(seed.equipment,&spec,s.instances.Generation(),0,&recreated)==Status::Ok);
    CHECK(ConfiguredInstance(s,*s.instances.Find(recreated.instanceHandle)).instanceCustomizationMask==0);
    CHECK(s.instanceCustomizations.Count()==1 && FindCustomizationInstance(s,saved.key)==nullptr);
    CHECK(ClearInstanceCustomization(seed.customizer,&saved.key,s.instances.Generation(),s.instances.ViewRevision())==Status::Ok);
    CHECK(s.instanceCustomizations.Count()==0); // Dormant keys remain clearable.

    seed=SeedRegistry(); saved=SeedSaved(seed);
    InstanceView loadView{}; CHECK(ResetSaveScope(&loadView)==Status::Ok);
    CHECK(s.customizations.count==0 && s.instanceCustomizations.Count()==0 && s.instances.Count()==0);
    CHECK(LoadConfigurationBatch(&definitions,&instances,loadView.saveGeneration,loadView.viewRevision)==Status::Ok);
    CHECK(s.customizations.count==1 && s.customizations.entries[0].position[0]==9);
    CHECK(s.instanceCustomizations.Count()==1 && s.instanceCustomizations.At(0).position[0]==11);
    spec=Spec(seed.definition.definitionRevision); recreated={}; recreated.structSize=sizeof(recreated);
    CHECK(UpsertInstance(seed.equipment,&spec,s.instances.Generation(),0,&recreated)==Status::Ok);
    CHECK(EffectiveInstance(s,*s.instances.Find(recreated.instanceHandle)).effective.position[0]==9);
    CHECK(ConfiguredInstance(s,*s.instances.Find(recreated.instanceHandle)).instance.effective.position[0]==11);
}
namespace plugin_fixture {
struct PluginState { std::filesystem::path packages; };
PluginState& State() { static PluginState state; return state; }
unsigned warnings=0, nextCalls=0, readCalls=0; bool offered=false;
std::vector<std::uint8_t> payload;
constexpr UInt32 kConfigurationRecord=0x46435342;
constexpr UInt32 kInstanceConfigurationRecord=0x49435342;
void Log(const std::filesystem::path&, const std::string&) noexcept { ++warnings; }
bool Next(UInt32* type,UInt32* version,UInt32* length) {
    ++nextCalls; if(offered) return false; offered=true;
    *type=kConfigurationRecord; *version=1; *length=static_cast<UInt32>(payload.size()); return true;
}
UInt32 Read(void* output,UInt32 length) {
    ++readCalls; if(length!=payload.size()) return 0;
    std::memcpy(output,payload.data(),length); return length;
}
@@PLUGIN_CURRENT@@
@@PLUGIN_UNSAFE@@
void Exercise() {
    SeedRegistry(); payload=Payload(9); body_slots::detail::State().instances.state_.generation=Max;
    const auto before=RegistryBytes();
    SKSESerializationInterface api{}; api.GetNextRecordInfo=Next; api.ReadRecordData=Read;
    nextCalls=readCalls=warnings=0; offered=false;
    OnLoad(&api);
    CHECK(nextCalls==0 && readCalls==0 && warnings==1); CHECK(RegistryBytes()==before);
    // Removing the early reset guard still permits record reads. The new exact
    // loadView CAS independently prevents the original cross-scope publication.
    nextCalls=readCalls=warnings=0; offered=false;
    const auto oldConfiguration=body_slots::detail::State().configurationRevision;
    OnLoadUnchecked(&api);
    CHECK(nextCalls==2 && readCalls==1 && warnings==2);
    CHECK(body_slots::detail::State().configurationRevision==oldConfiguration);
    CHECK(RegistryBytes()==before);
    CHECK(body_slots::detail::State().instances.Generation()==Max && body_slots::detail::State().instances.Count()==1);
    SeedRegistry(); payload=Payload(9); nextCalls=readCalls=warnings=0; offered=false;
    const auto oldGeneration=body_slots::detail::State().instances.Generation();
    OnLoad(&api);
    CHECK(nextCalls==2 && readCalls==1 && warnings==0);
    CHECK(body_slots::detail::State().instances.Generation()==oldGeneration+1 && body_slots::detail::State().instances.Count()==0);
    CHECK(body_slots::detail::State().customizations.entries[0].position[0]==9);
}
}
int main() {
    try {
        PureExhaustion(); RegistryAtomicity(); plugin_fixture::Exercise();
        if(checks!=125) throw std::runtime_error("original boundary phase must retain 125 checks");
        std::cout<<"PASS "<<checks<<" checks: original exhaustion/release/reset boundaries; unchecked-reset negative control now also blocked by batch CAS\n";
        const auto previous=checks; SavedInstanceAtomicity();
        std::cout<<"PASS "<<(checks-previous)<<" saved-instance checks: both-store atomic reset/load, exhaustion/CAS, dormant template identity and old inherited-only APIs\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
}
'''

def extract(source, signature):
    start=source.rfind('\n',0,source.index(signature))+1
    opening=source.index('{',source.index(signature))
    depth=0; mode=None; escaped=False; index=opening
    while index<len(source):
        ch=source[index]; pair=source[index:index+2]
        if mode=='line':
            if ch=='\n': mode=None
        elif mode=='comment':
            if pair=='*/': mode=None; index+=1
        elif mode in ('"', "'"):
            if escaped: escaped=False
            elif ch=='\\': escaped=True
            elif ch==mode: mode=None
        elif pair=='//': mode='line'; index+=1
        elif pair=='/*': mode='comment'; index+=1
        elif ch in ('"', "'"): mode=ch
        elif ch=='{': depth+=1
        elif ch=='}':
            depth-=1
            if depth==0: return source[start:index+1]
        index+=1
    raise RuntimeError('unterminated production function')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True,
                        help="directory containing public common/ and sksevr/ headers")
    parser.add_argument("--output", type=Path, required=True,
                        help="new/empty evidence directory outside Git checkouts")
    args = parser.parse_args()
    sdk = args.sdk_root.resolve()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents or any((p / ".git").exists() for p in (output, *output.parents)):
        parser.error("--output must be outside Git checkouts")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error("--output must be new or empty to preserve previous evidence")
    for relative in ("common/ITypes.h", "sksevr/skse64/PluginAPI.h"):
        if not (sdk / relative).is_file():
            parser.error(f"public SDK header not found: {sdk / relative}")
    output.mkdir(parents=True, exist_ok=True)

    helper = ROOT / "tools/build.py"
    pins = [ROOT / relative for relative in (
        "src/instances.h", "src/instances.cpp", "src/registry.cpp", "src/plugin.cpp",
        "src/validation.h", "src/validation.cpp", "src/configuration.h",
        "src/configuration_serialization.cpp", "src/runtime_token.h",
        "src/instance_configuration.h", "src/instance_configuration.cpp",
        "include/body_slots_api.h", "include/body_slots_instance_api.h",
        "include/body_slots_configuration_api.h", "include/body_slots_instance_configuration_api.h",
    )] + [sdk / "sksevr/skse64/PluginAPI.h", helper, Path(__file__).resolve()]
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in pins}
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    plugin = (ROOT / "src/plugin.cpp").read_text(encoding="utf-8")
    begin = extract(plugin, "bool BeginSaveScope(")
    load = extract(plugin, "void OnLoad(")
    guard = "if (!BeginSaveScope(&loadView)) return;"
    if load.count(guard) != 1:
        raise RuntimeError("production OnLoad reset gate changed; review fixture extraction")
    unchecked = load.replace("void OnLoad(", "void OnLoadUnchecked(", 1).replace(
        guard, "(void)BeginSaveScope(&loadView); /* test-only unchecked-reset negative control */", 1)
    fixture = output / "instance_boundary_fixture.cpp"
    fixture.write_text(CPP.replace("@@PLUGIN_CURRENT@@", begin + "\n" + load)
                       .replace("@@PLUGIN_UNSAFE@@", unchecked), encoding="utf-8")

    spec = importlib.util.spec_from_file_location("body_build_helpers", helper)
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    env = helpers.vs_environment()
    compiler = shutil.which("cl.exe", path=env.get("Path", env.get("PATH")))
    if not compiler:
        raise RuntimeError("MSVC x64 compiler unavailable")
    executable = output / "instance_boundary_fixture.exe"
    command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/MD", "/O2", "/W4", "/Bv", "/permissive-",
               "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ROOT / "include"), "/I" + str(ROOT / "src"),
               "/I" + str(sdk), "/I" + str(sdk / "sksevr"), "/Fo" + str(output) + os.sep,
               "/Fe:" + str(executable), str(fixture), str(ROOT / "src/validation.cpp"),
               str(ROOT / "src/configuration_serialization.cpp"), str(ROOT / "src/instance_configuration.cpp"),
               "/link", "/INCREMENTAL:NO",
               "/MACHINE:X64", "/STACK:8388608"]
    compiled = subprocess.run(command, cwd=output, env=env, capture_output=True)
    compile_log = output / "compile.log"
    compile_log.write_text((compiled.stdout + compiled.stderr).decode(errors="replace"), encoding="utf-8")
    if compiled.returncode:
        raise RuntimeError(f"fixture compilation failed; see {compile_log}")
    result = subprocess.run([str(executable)], cwd=output, capture_output=True, text=True)
    (output / "run.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    for path in pins:
        if hashlib.sha256(path.read_bytes()).hexdigest() != hashes[str(path)]:
            raise RuntimeError(f"source changed during fixture build: {path}")
    saved_checks = re.search(r"PASS (\d+) saved-instance checks:", result.stdout)
    manifest = {
        "productionCommit": commit,
        "sourcePins": hashes,
        "fixtureSha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
        "executableSha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "compiler": compiler,
        "compileCommand": command,
        "compileExitCode": compiled.returncode,
        "runExitCode": result.returncode,
        "originalBoundaryChecks": 125,
        "savedInstanceChecks": int(saved_checks.group(1)) if saved_checks else None,
        "sourceInstrumentation": [
            "#define private public only while including actual instances.h",
            "include actual instances.cpp and registry.cpp into the fixture translation unit",
            "extract actual BeginSaveScope/OnLoad; remove only the reset-return guard in the negative control, whose reads are now rejected by the independent batch scope/view CAS",
        ],
        "boundaries": "Standalone production source with forced private exhaustion counters and mocked public SKSESerializationInterface read callbacks/log boundary; no installed game or full plugin/DLL load",
        "gameStarts": 0,
        "fullVrikDllLoads": 0,
        "fullBodySlotsDllLoads": 0,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if result.returncode or "PASS 125 checks:" not in result.stdout or not saved_checks:
        raise RuntimeError(f"instance boundary fixture failed; see {output / 'run.log'}")
    print(result.stdout, end="")
    print(f"Evidence: {output / 'manifest.json'}")


if __name__ == "__main__":
    main()
