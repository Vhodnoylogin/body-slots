#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "body_slots_instance_activation_api.h"
#include "body_slots_instance_configuration_api.h"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
using namespace body_slots;
namespace {
unsigned checks=0;
#define CHECK(value) do{++checks;if(!(value))throw std::runtime_error("activation DLL line "+std::to_string(__LINE__)+": " #value);}while(false)
struct Snapshot {InstanceView view{48,1,0,0,0,0,0,0};std::array<InstanceRecord,kMaxInstances> rows{};};
Snapshot Copy(const InstanceApi& api,std::uint64_t reader){Snapshot s;CHECK(api.CopyEffectiveInstances(reader,0,0,s.rows.data(),kMaxInstances,&s.view)==Status::Ok);return s;}
const InstanceRecord& Row(const Snapshot& s,std::uint64_t handle){for(unsigned i=0;i<s.view.count;++i)if(s.rows[i].instanceHandle==handle)return s.rows[i];throw std::runtime_error("missing fixture instance");}
void Same(const Snapshot& a,const Snapshot& b){CHECK(!std::memcmp(&a.view,&b.view,48)&&!std::memcmp(a.rows.data(),b.rows.data(),a.view.count*sizeof(InstanceRecord)));}
Definition Template(){Definition d{};d.structSize=408;d.flags=kEnabled;std::strcpy(d.provider,"activation-template");std::strcpy(d.key,"belt");std::strcpy(d.anchor,"NPC Pelvis [Pelv]");
    std::strcpy(d.referenceRig,"skyrimvr-humanoid-v1");std::strcpy(d.kind,"pouch");d.rotation[0]=d.rotation[4]=d.rotation[8]=1;d.radius=5;d.handMask=Both;d.contentMask=AllContent;return d;}
InstanceSpec Spec(const SlotRecord& d,const char* equipment,unsigned flags=0){InstanceSpec s{};s.structSize=280;s.flags=flags;std::strcpy(s.equipmentKey,equipment);std::strcpy(s.slotKey,"opening");
    std::strcpy(s.definitionProvider,d.definition.provider);std::strcpy(s.definitionKey,d.definition.key);s.definitionRevision=d.definitionRevision;s.binding=1;return s;}
ActivationChange Change(const InstanceRecord& r,unsigned flags){return {24,flags,r.instanceHandle,r.instanceRevision};}
}
int main(int argc,char** argv){try{
    CHECK(argc==2);auto dll=LoadLibraryA(argv[1]);CHECK(dll);
    const auto query=reinterpret_cast<QueryInstanceActivationInterface>(GetProcAddress(dll,"BodySlots_QueryInstanceActivationInterface"));
    const auto baseQuery=reinterpret_cast<QueryInterface>(GetProcAddress(dll,"BodySlots_QueryInterface"));
    const auto instanceQuery=reinterpret_cast<QueryInstanceInterface>(GetProcAddress(dll,"BodySlots_QueryInstanceInterface"));
    const auto configurationQuery=reinterpret_cast<QueryConfigurationInterface>(GetProcAddress(dll,"BodySlots_QueryConfigurationInterface"));
    const auto configuredQuery=reinterpret_cast<QueryInstanceConfigurationInterface>(GetProcAddress(dll,"BodySlots_QueryInstanceConfigurationInterface"));
    CHECK(query&&baseQuery&&instanceQuery&&configurationQuery&&configuredQuery);
    CHECK(!query(0,24)&&!query(2,24)&&!query(1,23)&&!query(1,25));
    const auto* api=query(1,24);const auto* base=baseQuery(1,sizeof(Api));const auto* instances=instanceQuery(1,48);
    const auto* configuration=configurationQuery(1,sizeof(ConfigurationApi));const auto* configured=configuredQuery(1,56);
    CHECK(api&&base&&instances&&configuration&&configured&&api->structSize==24&&api->version==1&&api->capabilities==1);
    std::cout<<"negotiated activation table=24 change=24 output=48 caps="<<api->capabilities<<'\n';
    std::uint64_t author=0,owner=0,foreign=0,observer=0,editor=0;
    CHECK(base->RegisterClient("activation-template",&author)==Status::Ok&&base->RegisterClient("activation-equipment",&owner)==Status::Ok);
    CHECK(base->RegisterClient("activation-foreign",&foreign)==Status::Ok&&base->RegisterObserver(&observer)==Status::Ok&&configuration->RegisterCustomizer("activation-editor",&editor)==Status::Ok);
    auto definition=Template();SlotRecord authored{};CHECK(base->UpsertDefinition(author,&definition,0,&authored)==Status::Ok);
    auto before=Copy(*instances,observer);auto a=InstanceRecord{},b=InstanceRecord{},f=InstanceRecord{};a.structSize=b.structSize=f.structSize=784;
    auto sa=Spec(authored,"first",kInstanceActive),sb=Spec(authored,"second"),sf=Spec(authored,"foreign");
    CHECK(instances->UpsertInstance(owner,&sa,before.view.saveGeneration,0,&a)==Status::Ok&&instances->UpsertInstance(owner,&sb,before.view.saveGeneration,0,&b)==Status::Ok);
    CHECK(instances->UpsertInstance(foreign,&sf,before.view.saveGeneration,0,&f)==Status::Ok);
    before=Copy(*instances,observer);InstanceCustomization custom{};custom.structSize=400;custom.fieldMask=CustomizePosition;custom.key.instance=a.key;
    std::memcpy(custom.key.definitionProvider,a.definitionProvider,64);std::memcpy(custom.key.definitionKey,a.definitionKey,64);custom.position[0]=9;
    ConfiguredInstanceRecord customized{};customized.instance.structSize=784;CHECK(configured->ApplyCustomization(editor,&custom,before.view.saveGeneration,before.view.viewRevision,&customized)==Status::Ok);
    std::array<InstanceCustomizationRecord,kMaxInstances> saved{};InstanceView savedView{48,1,0,0,0,0,0,0};
    CHECK(configured->CopyCustomizations(observer,0,0,saved.data(),256,&savedView)==Status::Ok);const auto savedBefore=saved;
    before=Copy(*instances,observer);std::array<ActivationChange,2> changes{{Change(a,0),Change(b,kInstanceActive)}};InstanceView committed{48,1,0,0,0,0,0,0};
    CHECK(api->ApplyActivationChanges(owner,changes.data(),2,before.view.saveGeneration,before.view.viewRevision,&committed)==Status::Ok);
    auto after=Copy(*instances,observer);CHECK(!std::memcmp(&committed,&after.view,48)&&after.view.viewRevision==before.view.viewRevision+1&&after.view.registryRevision==before.view.registryRevision&&after.view.configurationRevision==before.view.configurationRevision);
    CHECK(Row(after,a.instanceHandle).flags==0&&Row(after,b.instanceHandle).flags==1&&Row(after,a.instanceHandle).instanceRevision>a.instanceRevision&&Row(after,b.instanceHandle).instanceRevision>b.instanceRevision);
    CHECK(!std::memcmp(&Row(before,f.instanceHandle),&Row(after,f.instanceHandle),784));
    std::array<ConfiguredInstanceRecord,kMaxInstances> finalRows{};InstanceView finalView{48,1,0,0,0,0,0,0};
    CHECK(configured->CopyConfiguredInstances(observer,committed.saveGeneration,committed.viewRevision,finalRows.data(),256,&finalView)==Status::Ok);
    bool sawOverride=false;for(unsigned i=0;i<finalView.count;++i)if(finalRows[i].instance.instanceHandle==a.instanceHandle){sawOverride=true;CHECK(finalRows[i].instance.flags==0&&finalRows[i].instance.effective.position[0]==9&&finalRows[i].instanceCustomizationMask==CustomizePosition);}CHECK(sawOverride);
    savedView={48,1,0,0,0,0,0,0};CHECK(configured->CopyCustomizations(observer,0,0,saved.data(),256,&savedView)==Status::Ok&&savedView.configurationRevision==before.view.configurationRevision&&!std::memcmp(saved.data(),savedBefore.data(),sizeof(saved)));
    changes={Change(Row(after,a.instanceHandle),0),Change(Row(after,b.instanceHandle),1)};
    CHECK(api->ApplyActivationChanges(owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,&committed)==Status::Ok);Same(after,Copy(*instances,observer));
    const auto reject=[&](const char* label,std::uint64_t writer,const ActivationChange* rows,unsigned count,std::uint64_t scope,std::uint64_t view,Status expected){
        const auto old=Copy(*instances,observer);InstanceView sentinel{48,1,91,92,93,94,95,0};auto output=sentinel;
        const auto status=api->ApplyActivationChanges(writer,rows,count,scope,view,&output);CHECK(status==expected&&!std::memcmp(&output,&sentinel,48));Same(old,Copy(*instances,observer));
        std::cout<<"rejected "<<label<<" status="<<static_cast<unsigned>(status)<<" scope="<<scope<<" view="<<view<<" outputUnchanged=1 stateUnchanged=1\n";};
    reject("null",owner,nullptr,1,after.view.saveGeneration,after.view.viewRevision,Status::InvalidArgument);
    reject("empty",owner,changes.data(),0,after.view.saveGeneration,after.view.viewRevision,Status::InvalidArgument);
    reject("oversized",owner,changes.data(),257,after.view.saveGeneration,after.view.viewRevision,Status::InvalidArgument);
    for(auto writer:{observer,editor,foreign,UINT64_C(0)})reject("authority",writer,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Unauthorized);
    reject("zero-scope",owner,changes.data(),2,0,after.view.viewRevision,Status::Conflict);
    reject("zero-view",owner,changes.data(),2,after.view.saveGeneration,0,Status::Conflict);
    reject("wrong-scope",owner,changes.data(),2,after.view.saveGeneration+1,after.view.viewRevision,Status::Conflict);
    reject("stale-view",owner,changes.data(),2,after.view.saveGeneration,before.view.viewRevision,Status::Conflict);
    auto malformed=changes;malformed[1].expectedInstanceRevision=0;reject("zero-entry-CAS",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Conflict);
    malformed=changes;++malformed[1].expectedInstanceRevision;reject("stale-entry-CAS",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Conflict);
    malformed=changes;malformed[1]=malformed[0];reject("duplicate",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::InvalidArgument);
    malformed=changes;malformed[1].flags=2;reject("flags",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::InvalidArgument);
    malformed=changes;malformed[1].structSize=0;reject("row-size",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::UnsupportedVersion);
    malformed=changes;malformed[1].instanceHandle=0;reject("zero-handle",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::InvalidArgument);
    malformed=changes;malformed[1].instanceHandle=UINT64_MAX;reject("missing",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::NotFound);
    malformed=changes;malformed[0].flags=1;malformed[1]=Change(f,1);reject("foreign-late",owner,malformed.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Unauthorized);
    for(unsigned shape=0;shape<3;++shape){InstanceView bad{48,1,91,92,93,94,95,0};if(shape==0)--bad.structSize;if(shape==1)bad.version=2;if(shape==2)bad.reserved=1;const auto sentinel=bad;
        CHECK(api->ApplyActivationChanges(owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,&bad)==Status::UnsupportedVersion&&!std::memcmp(&bad,&sentinel,48));Same(after,Copy(*instances,observer));}
    CHECK(api->ApplyActivationChanges(owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,nullptr)==Status::InvalidArgument);Same(after,Copy(*instances,observer));
    CHECK(base->RemoveDefinition(author,"belt",authored.definitionRevision)==Status::Ok);after=Copy(*instances,observer);
    changes={Change(Row(after,a.instanceHandle),1),Change(Row(after,b.instanceHandle),0)};reject("missing-template",owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Unavailable);
    auto missingActiveNoop=Change(Row(after,b.instanceHandle),1);reject("missing-active-noop",owner,&missingActiveNoop,1,after.view.saveGeneration,after.view.viewRevision,Status::Unavailable);
    changes[0].flags=0;CHECK(api->ApplyActivationChanges(owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,&committed)==Status::Ok);after=Copy(*instances,observer);
    CHECK(Row(after,b.instanceHandle).flags==0&&Row(after,b.instanceHandle).templateState==1&&Row(after,b.instanceHandle).pinnedDefinitionRevision==authored.definitionRevision);
    CHECK(base->UpsertDefinition(author,&definition,0,&authored)==Status::Ok);after=Copy(*instances,observer);changes={Change(Row(after,a.instanceHandle),1),Change(Row(after,b.instanceHandle),0)};
    reject("changed-template",owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Unavailable);
    changes[0].flags=0;CHECK(api->ApplyActivationChanges(owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,&committed)==Status::Ok);Same(after,Copy(*instances,observer));
    sa.definitionRevision=sb.definitionRevision=authored.definitionRevision;sa.flags=sb.flags=0;a=Row(after,a.instanceHandle);b=Row(after,b.instanceHandle);
    CHECK(instances->UpsertInstance(owner,&sa,after.view.saveGeneration,a.instanceRevision,&a)==Status::Ok&&instances->UpsertInstance(owner,&sb,after.view.saveGeneration,b.instanceRevision,&b)==Status::Ok);
    std::atomic<bool> done=false,okay=true;std::thread reader([&]{do{Snapshot s;if(instances->CopyEffectiveInstances(observer,0,0,s.rows.data(),256,&s.view)!=Status::Ok){okay=false;break;}
        if(Row(s,a.instanceHandle).flags!=Row(s,b.instanceHandle).flags)okay=false;}while(!done);});
    for(unsigned pass=0;pass<500;++pass){auto s=Copy(*instances,observer);changes={Change(Row(s,a.instanceHandle),pass%2),Change(Row(s,b.instanceHandle),pass%2)};
        if(api->ApplyActivationChanges(owner,changes.data(),2,s.view.saveGeneration,s.view.viewRevision,&committed)!=Status::Ok)okay=false;}
    done=true;reader.join();CHECK(okay);
    definition.position[1]=1;CHECK(base->UpsertDefinition(author,&definition,authored.definitionRevision,&authored)==Status::Ok);after=Copy(*instances,observer);
    changes={Change(Row(after,a.instanceHandle),1),Change(Row(after,b.instanceHandle),1)};
    reject("changed-active-noop",owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Unavailable);
    changes[0].flags=changes[1].flags=0;CHECK(api->ApplyActivationChanges(owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,&committed)==Status::Ok);
    auto disabled=Template();std::strcpy(disabled.key,"disabled");disabled.flags=0;SlotRecord disabledTemplate{};
    CHECK(base->UpsertDefinition(author,&disabled,0,&disabledTemplate)==Status::Ok);after=Copy(*instances,observer);
    auto disabledSpec=Spec(disabledTemplate,"disabled");InstanceRecord disabledInstance{};disabledInstance.structSize=784;
    CHECK(instances->UpsertInstance(owner,&disabledSpec,after.view.saveGeneration,0,&disabledInstance)==Status::Ok);after=Copy(*instances,observer);
    auto disabledChange=Change(disabledInstance,1);CHECK(api->ApplyActivationChanges(owner,&disabledChange,1,after.view.saveGeneration,after.view.viewRevision,&committed)==Status::Ok);
    std::uint32_t allowed=99;CHECK(instances->EvaluateInstanceEligibility(observer,disabledInstance.instanceHandle,committed.saveGeneration,committed.viewRevision,Left,Potion,&allowed)==Status::Ok&&allowed==0);
    CHECK(base->ReleaseClient(owner)==Status::Ok);after=Copy(*instances,observer);reject("released-writer",owner,changes.data(),2,after.view.saveGeneration,after.view.viewRevision,Status::Unauthorized);
    CHECK(base->ReleaseClient(foreign)==Status::Ok);std::uint64_t capacityOwner=0;CHECK(base->RegisterClient("activation-capacity",&capacityOwner)==Status::Ok);after=Copy(*instances,observer);
    std::array<ActivationChange,kMaxActivationChanges> all{};
    for(unsigned i=0;i<kMaxActivationChanges;++i){const auto key="capacity-"+std::to_string(i);auto spec=Spec(authored,key.c_str());InstanceRecord record{};record.structSize=784;
        CHECK(instances->UpsertInstance(capacityOwner,&spec,after.view.saveGeneration,0,&record)==Status::Ok);all[i]=Change(record,1);}
    before=Copy(*instances,observer);CHECK(before.view.count==256);
    CHECK(api->ApplyActivationChanges(capacityOwner,all.data(),256,before.view.saveGeneration,before.view.viewRevision,&committed)==Status::Ok);after=Copy(*instances,observer);
    CHECK(after.view.count==256&&after.view.viewRevision==before.view.viewRevision+1&&after.view.registryRevision==before.view.registryRevision&&after.view.configurationRevision==before.view.configurationRevision);
    for(unsigned i=0;i<256;++i)CHECK(after.rows[i].flags==1&&after.rows[i].instanceRevision>Row(before,after.rows[i].instanceHandle).instanceRevision);
    CHECK(base->ReleaseClient(capacityOwner)==Status::Ok&&base->ReleaseClient(author)==Status::Ok&&base->ReleaseClient(observer)==Status::Ok&&configuration->ReleaseCustomizer(editor)==Status::Ok);
    FreeLibrary(dll);std::cout<<"PASS activation actual DLL "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
