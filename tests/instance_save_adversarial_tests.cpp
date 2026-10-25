// Public interfaces + actual DLL callbacks in a disposable synthetic SKSE host.
// No registry sources, game objects, VRIK DLL or installed files are used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "body_slots_instance_configuration_api.h"
#include "common/ITypes.h"
#include "skse64/PluginAPI.h"
#include "skse64_common/skse_version.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace body_slots;
namespace {
unsigned checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error("line "+std::to_string(__LINE__)+": " #c); } while(false)
constexpr UInt32 BSCF = 0x46435342, BSCI = 0x49435342;
struct Record { UInt32 type=0, version=0, advertised=0; std::vector<std::uint8_t> bytes; };
std::vector<Record> written, incoming;
unsigned writeCalls=0, failWrite=0, readIndex=0, activeRead=0;
enum class Retain { None, Half, Complete };
Retain retainFailure=Retain::None;
enum class Injection { None, View, Scope };
Injection readInjection=Injection::None;
bool writeInjection=false, injectionAttempted=false, injectionCompleted=false;
SKSESerializationInterface::EventCallback onSave=nullptr, onLoad=nullptr, onRevert=nullptr;
SKSESerializationInterface serialization{};
SKSEMessagingInterface messaging{};
void inject(Injection);
void uniqueId(PluginHandle,UInt32) {}
void saved(PluginHandle,SKSESerializationInterface::EventCallback cb) { onSave=cb; }
void loaded(PluginHandle,SKSESerializationInterface::EventCallback cb) { onLoad=cb; }
void reverted(PluginHandle,SKSESerializationInterface::EventCallback cb) { onRevert=cb; }
bool writeRecord(UInt32 type,UInt32 version,const void* data,UInt32 length) {
    ++writeCalls;
    const bool failed=failWrite&&writeCalls==failWrite;
    if (!failed || retainFailure!=Retain::None) {
        const auto count=failed&&retainFailure==Retain::Half ? length/2 : length;
        const auto* bytes=static_cast<const std::uint8_t*>(data);
        written.push_back({type,version,length,{bytes,bytes+count}});
    }
    if (!failed && writeInjection && writeCalls==1) { writeInjection=false; inject(Injection::View); }
    return !failed;
}
bool nextRecord(UInt32* type,UInt32* version,UInt32* length) {
    if (readIndex==incoming.size()) return false;
    activeRead=readIndex++;
    const auto& row=incoming[activeRead]; *type=row.type;*version=row.version;*length=row.advertised;
    return true;
}
UInt32 readRecord(void* data,UInt32 length) {
    const auto& row=incoming[activeRead];
    const auto count=std::min(length,static_cast<UInt32>(row.bytes.size()));
    if(count)std::memcpy(data,row.bytes.data(),count);
    if(readInjection!=Injection::None) {const auto mode=readInjection;readInjection=Injection::None;inject(mode);}
    return count;
}
bool listen(PluginHandle,const char*,SKSEMessagingInterface::EventCallback) {return true;}
PluginHandle handle() {return 42;}
void* queryHost(UInt32 id) {
    if(id==kInterface_Messaging)return &messaging;
    return id==kInterface_Serialization?&serialization:nullptr;
}
struct Fixture {
    const Api* base=nullptr;
    const ConfigurationApi* definitions=nullptr;
    const InstanceApi* instances=nullptr;
    const InstanceConfigurationApi* configured=nullptr;
    std::uint64_t author=0,owner=0,observer=0,editor=0;
    SlotRecord templateA{},templateB{};
    Fixture(const char* path) {
        const auto dll=LoadLibraryA(path);CHECK(dll);
        const auto baseQuery=reinterpret_cast<QueryInterface>(GetProcAddress(dll,"BodySlots_QueryInterface"));
        const auto definitionQuery=reinterpret_cast<QueryConfigurationInterface>(GetProcAddress(dll,"BodySlots_QueryConfigurationInterface"));
        const auto instanceQuery=reinterpret_cast<QueryInstanceInterface>(GetProcAddress(dll,"BodySlots_QueryInstanceInterface"));
        const auto configuredQuery=reinterpret_cast<QueryInstanceConfigurationInterface>(GetProcAddress(dll,"BodySlots_QueryInstanceConfigurationInterface"));
        CHECK(baseQuery&&definitionQuery&&instanceQuery&&configuredQuery);
        CHECK(!configuredQuery(0,sizeof(InstanceConfigurationApi))&&!configuredQuery(1,sizeof(InstanceConfigurationApi)-1));
        base=baseQuery(1,sizeof(Api));definitions=definitionQuery(1,sizeof(ConfigurationApi));
        instances=instanceQuery(1,sizeof(InstanceApi));configured=configuredQuery(1,sizeof(InstanceConfigurationApi));
        CHECK(base&&definitions&&instances&&configured);
        CHECK(sizeof(InstanceCustomization)==400&&sizeof(InstanceCustomizationRecord)==408&&sizeof(ConfiguredInstanceRecord)==792);
        CHECK(sizeof(InstanceConfigurationApi)==56&&configured->version==1&&configured->capabilities==3);
        messaging.interfaceVersion=SKSEMessagingInterface::kInterfaceVersion;messaging.RegisterListener=listen;
        serialization.version=SKSESerializationInterface::kVersion;serialization.SetUniqueID=uniqueId;
        serialization.SetSaveCallback=saved;serialization.SetLoadCallback=loaded;serialization.SetRevertCallback=reverted;
        serialization.WriteRecord=writeRecord;serialization.GetNextRecordInfo=nextRecord;serialization.ReadRecordData=readRecord;
        SKSEInterface skse{};skse.runtimeVersion=RUNTIME_VR_VERSION_1_4_15;skse.skseVersion=PACKED_SKSE_VERSION;
        skse.QueryInterface=queryHost;skse.GetPluginHandle=handle;
        const auto load=reinterpret_cast<_SKSEPlugin_Load>(GetProcAddress(dll,"SKSEPlugin_Load"));
        CHECK(load&&load(&skse)&&onSave&&onLoad&&onRevert);
        CHECK(base->RegisterClient("adversarial-template",&author)==Status::Ok);
        CHECK(base->RegisterClient("adversarial-equipment",&owner)==Status::Ok);
        CHECK(base->RegisterObserver(&observer)==Status::Ok);
        CHECK(definitions->RegisterCustomizer("adversarial-editor",&editor)==Status::Ok);
        auto first=DefinitionFor("template-a",2),second=DefinitionFor("template-b",20);
        CHECK(base->UpsertDefinition(author,&first,0,&templateA)==Status::Ok);
        CHECK(base->UpsertDefinition(author,&second,0,&templateB)==Status::Ok);
        // DLL owns registered callbacks until process exit; no hot unload.
    }
    static Definition DefinitionFor(const char* key,float position) {
        Definition out{};out.structSize=sizeof(out);out.flags=kEnabled;
        std::strcpy(out.provider,"adversarial-template");std::strcpy(out.key,key);
        std::strcpy(out.anchor,"NPC Pelvis [Pelv]");std::strcpy(out.referenceRig,"skyrimvr-humanoid-v1");
        std::strcpy(out.kind,"pouch");out.position[0]=position;out.rotation[0]=out.rotation[4]=out.rotation[8]=1;
        out.radius=5;out.handMask=Left;out.contentMask=Potion;return out;
    }
    InstanceView View() const {
        InstanceView view{sizeof(view),1,0,0,0,0,0,0};
        const auto status=instances->CopyEffectiveInstances(observer,0,0,nullptr,0,&view);
        CHECK(status==Status::Ok||status==Status::BufferTooSmall);return view;
    }
    std::vector<InstanceCustomizationRecord> Stored() const {
        std::vector<InstanceCustomizationRecord> out(kMaxInstanceCustomizations);
        InstanceView view{sizeof(view),1,0,0,0,0,0,0};
        CHECK(configured->CopyCustomizations(observer,0,0,out.data(),kMaxInstanceCustomizations,&view)==Status::Ok);
        out.resize(view.count);return out;
    }
    std::vector<ConfiguredInstanceRecord> Final() const {
        std::vector<ConfiguredInstanceRecord> out(kMaxInstances);
        InstanceView view{sizeof(view),1,0,0,0,0,0,0};
        CHECK(configured->CopyConfiguredInstances(observer,0,0,out.data(),kMaxInstances,&view)==Status::Ok);
        out.resize(view.count);return out;
    }
    std::vector<InstanceRecord> Old() const {
        std::vector<InstanceRecord> out(kMaxInstances);
        InstanceView view{sizeof(view),1,0,0,0,0,0,0};
        CHECK(instances->CopyEffectiveInstances(observer,0,0,out.data(),kMaxInstances,&view)==Status::Ok);
        out.resize(view.count);return out;
    }
    float DefinitionPosition() const {
        EffectiveSlotRecord out[2]{};std::uint32_t count=0;std::uint64_t revision=0;
        CHECK(definitions->CopyEffectiveDefinitions(observer,0,out,2,&count,&revision)==Status::Ok&&count==2);
        for(const auto& row:out)if(!std::strcmp(row.slot.definition.key,"template-a"))return row.slot.definition.position[0];
        throw std::runtime_error("template-a missing");
    }
    void DefinitionEdit(float position) const {
        Customization edit{};edit.structSize=sizeof(edit);edit.fieldMask=CustomizePosition;
        std::strcpy(edit.provider,"adversarial-template");std::strcpy(edit.key,"template-a");edit.position[0]=position;
        EffectiveSlotRecord out{};
        CHECK(definitions->ApplyCustomization(editor,&edit,View().configurationRevision,&out)==Status::Ok);
    }
    InstanceSpec Spec(bool second=false) const {
        const auto& source=second?templateB:templateA;
        InstanceSpec spec{};spec.structSize=sizeof(spec);spec.flags=kInstanceActive;
        std::strcpy(spec.equipmentKey,"same-bag");std::strcpy(spec.slotKey,"opening");
        std::memcpy(spec.definitionProvider,source.definition.provider,64);std::memcpy(spec.definitionKey,source.definition.key,64);
        spec.definitionRevision=source.definitionRevision;spec.binding=std::uint32_t(InstanceBinding::BodyAnchor);return spec;
    }
    InstanceRecord Put(bool second=false) const {
        const auto spec=Spec(second);auto view=View();InstanceRecord out{};out.structSize=sizeof(out);
        CHECK(instances->UpsertInstance(owner,&spec,view.saveGeneration,0,&out)==Status::Ok);return out;
    }
    void InstanceEdit() const {
        InstanceCustomization edit{};edit.structSize=sizeof(edit);edit.fieldMask=CustomizePosition|CustomizeEnabled;
        std::strcpy(edit.key.instance.ownerProvider,"adversarial-equipment");
        std::strcpy(edit.key.instance.equipmentKey,"same-bag");std::strcpy(edit.key.instance.slotKey,"opening");
        std::strcpy(edit.key.definitionProvider,"adversarial-template");std::strcpy(edit.key.definitionKey,"template-a");
        edit.position[0]=9;edit.enabled=0;auto view=View();ConfiguredInstanceRecord out{};out.instance.structSize=sizeof(InstanceRecord);
        CHECK(configured->ApplyCustomization(editor,&edit,view.saveGeneration,view.viewRevision,&out)==Status::Ok);
        CHECK(out.instance.effective.position[0]==9&&!(out.instance.effective.flags&kEnabled));
    }
    void Seed() const {onRevert(&serialization);DefinitionEdit(4);Put();InstanceEdit();}
    void CheckOriginalSemantics(bool overridden=true) const {
        const auto old=Old();const auto final=Final();const auto view=View();CHECK(old.size()==1&&final.size()==1);
        CHECK(old[0].effective.position[0]==4&&(old[0].effective.flags&kEnabled));
        CHECK(old[0].customizationMask==CustomizePosition&&final[0].instance.customizationMask==CustomizePosition);
        CHECK(final[0].instance.effective.position[0]==(overridden?9.f:4.f));
        CHECK(final[0].instanceCustomizationMask==(overridden?(CustomizePosition|CustomizeEnabled):0));
        std::uint32_t allowed=99;
        CHECK(instances->EvaluateInstanceEligibility(observer,old[0].instanceHandle,view.saveGeneration,view.viewRevision,Left,Potion,&allowed)==Status::Ok&&allowed==1);
        CHECK(configured->EvaluateConfiguredEligibility(observer,old[0].instanceHandle,view.saveGeneration,view.viewRevision,Left,Potion,&allowed)==Status::Ok&&allowed==unsigned(!overridden));
    }
    void CheckRejected() const {
        CHECK(Stored().empty()&&Final().empty()&&Old().empty());CHECK(DefinitionPosition()==2);
    }
};
Fixture* fixture=nullptr;
void inject(Injection mode) {
    injectionAttempted=true;
    if(mode==Injection::Scope)onRevert(&serialization);
    fixture->DefinitionEdit(mode==Injection::Scope?88.f:77.f);
    injectionCompleted=true;
}
std::vector<Record> Save(unsigned failure=0,Retain retain=Retain::None,bool mutate=false) {
    written.clear();writeCalls=0;failWrite=failure;retainFailure=retain;writeInjection=mutate;
    injectionAttempted=injectionCompleted=false;onSave(&serialization);writeInjection=false;failWrite=0;
    return written;
}
void Load(const std::vector<Record>& records,Injection mode=Injection::None) {
    incoming=records;readIndex=0;readInjection=mode;injectionAttempted=injectionCompleted=false;
    onLoad(&serialization);readInjection=Injection::None;
}
void CheckPartnerLoad(Fixture& f,const std::vector<Record>& records) {
    Load(records);const auto stored=f.Stored();CHECK(stored.size()==1&&!stored[0].available);
    CHECK(stored[0].customization.position[0]==9&&f.DefinitionPosition()==4&&f.Final().empty());
    f.Put();f.CheckOriginalSemantics();
}
void TestRetarget(Fixture& f,const std::vector<Record>& baseline) {
    CheckPartnerLoad(f,baseline);
    auto old=f.Old()[0];auto view=f.View();
    CHECK(f.instances->RemoveInstance(f.owner,old.instanceHandle,view.saveGeneration,old.instanceRevision)==Status::Ok);
    const auto alternate=f.Put(true);const auto configured=f.Final();const auto inherited=f.Old();
    CHECK(configured.size()==1&&configured[0].instance.effective.position[0]==20);
    CHECK(!configured[0].instanceCustomizationMask&&inherited[0].effective.position[0]==20);
    const auto stored=f.Stored();CHECK(stored.size()==1&&!stored[0].available);
    CHECK(!std::strcmp(stored[0].customization.key.definitionKey,"template-a"));
    view=f.View();std::uint32_t allowed=99;
    CHECK(f.configured->EvaluateConfiguredEligibility(f.observer,alternate.instanceHandle,view.saveGeneration,view.viewRevision,Left,Potion,&allowed)==Status::Ok&&allowed==1);
    CHECK(f.instances->EvaluateInstanceEligibility(f.observer,alternate.instanceHandle,view.saveGeneration,view.viewRevision,Left,Potion,&allowed)==Status::Ok&&allowed==1);
    CHECK(f.instances->RemoveInstance(f.owner,alternate.instanceHandle,view.saveGeneration,alternate.instanceRevision)==Status::Ok);
    const auto resumed=f.Put();CHECK(resumed.instanceHandle!=old.instanceHandle);
    f.CheckOriginalSemantics();CHECK(f.Stored()[0].available==1);
}
} // namespace
int main(int argc,char** argv) {
    try {
        CHECK(argc==2);Fixture f(argv[1]);fixture=&f;f.Seed();f.CheckOriginalSemantics();
        const auto baseline=Save();CHECK(writeCalls==2&&baseline.size()==2);
        CHECK(baseline[0].type==BSCI&&baseline[1].type==BSCF&&baseline[0].version==1&&baseline[1].version==1);
        CHECK(baseline[0].advertised==416&&baseline[1].advertised==224);
        CheckPartnerLoad(f,baseline);
        // The two encoded records must describe one capture even if callbacks
        // change the live definition state between the two IO writes.
        f.Seed();const auto coherent=Save(0,Retain::None,true);
        CHECK(injectionAttempted&&injectionCompleted&&f.DefinitionPosition()==77);
        CHECK(coherent.size()==baseline.size());
        for(unsigned i=0;i<2;++i)CHECK(coherent[i].type==baseline[i].type&&coherent[i].bytes==baseline[i].bytes);
        CheckPartnerLoad(f,coherent);
        for(unsigned failure=1;failure<=2;++failure)for(const auto retention:{Retain::None,Retain::Half,Retain::Complete}) {
            f.Seed();const auto partial=Save(failure,retention);
            CHECK(writeCalls==failure);f.CheckOriginalSemantics();
            if(failure==2&&retention==Retain::Complete) {
                // A false IO return with two complete retained records cannot be
                // rolled back by this API. The resulting batch remains coherent.
                CheckPartnerLoad(f,partial);
            } else {Load(partial);f.CheckRejected();}
        }
        std::vector<std::vector<Record>> invalid;
        invalid.push_back({baseline[0]}); // BSCI cannot load without BSCF
        auto bad=baseline;bad[1].bytes[0]^=1;invalid.push_back(bad);
        bad={baseline[1],baseline[0]};bad[1].bytes[0]^=1;invalid.push_back(bad);
        bad=baseline;bad.push_back(baseline[0]);invalid.push_back(bad);
        bad=baseline;bad.push_back({0x12345678,1,0,{}});invalid.push_back(bad);
        bad=baseline;bad[1].bytes.pop_back();invalid.push_back(bad);
        bad=baseline;bad[1].version=2;invalid.push_back(bad);
        for(const auto& records:invalid) {f.Seed();Load(records);f.CheckRejected();}
        // Legacy definition-only cosaves remain valid and do not import instance data.
        Load({baseline[1]});CHECK(f.Stored().empty()&&f.DefinitionPosition()==4);f.Put();f.CheckOriginalSemantics(false);
        for(const auto mode:{Injection::View,Injection::Scope}) {
            const auto before=f.View();Load(baseline,mode);
            CHECK(injectionAttempted&&injectionCompleted);
            CHECK(f.Stored().empty()&&f.Final().empty());
            CHECK(f.DefinitionPosition()==(mode==Injection::Scope?88.f:77.f));
            CHECK(f.View().saveGeneration>=before.saveGeneration+(mode==Injection::Scope?2:1));
        }
        TestRetarget(f,baseline);
        std::cout<<"INSTANCE SAVE ADVERSARIAL PASS: "<<checks<<" checks; actual DLL callbacks; full VRIK loads=0; game starts=0\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"INSTANCE SAVE ADVERSARIAL FAIL: "<<error.what()<<'\n';return 1;}
}
