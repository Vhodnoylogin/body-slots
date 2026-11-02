// Production runtime + production registry linked directly; no engine/SDK fixture.
#include "runtime.h"
#include "instances.h"
#include "runtime_saved.h"
#include "instance_configuration.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace body_slots;
using namespace body_slots::detail;
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
extern "C" const InstanceApi* BS_CALL BodySlots_QueryInstanceInterface(std::uint32_t,std::uint32_t) noexcept;
extern "C" const InstanceConfigurationApi* BS_CALL BodySlots_QueryInstanceConfigurationInterface(std::uint32_t,std::uint32_t) noexcept;
namespace {
std::atomic<unsigned> checks{0};
#define CHECK(c) do{++checks;if(!(c))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #c);}while(false)
const RuntimeApi& runtime=GetRuntimeApi();
std::uint64_t owner=0,observer=0,other=0,registration=0;
std::uint64_t selfHandle=0;
std::atomic<int> calls{0},events{0};
std::atomic<bool> block{false},entered{false},unblock{false};
bool fail=false,commitImmediately=false,selfRemove=false;
bool releaseInside=false;std::uint64_t removeOther=0;
bool rejectCommit=false;
Status selfReleaseResult=Status::Ok,crossRemoveResult=Status::Ok;
Status selfRemoveResult=Status::Ok;
ActionRequest last{};
struct Snapshot {std::vector<RuntimeRecord> rows=std::vector<RuntimeRecord>(kMaxInstances);RuntimeView view{sizeof(RuntimeView),kRuntimeApiVersion};};
Snapshot Copy() {Snapshot result;CHECK(runtime.CopyRuntime(observer,result.rows.data(),kMaxInstances,&result.view)==Status::Ok);return result;}
Status BS_CALL Offers(void*,const ActionContext* context,ActionProposal* output,std::uint32_t cap,std::uint32_t* count) {
    auto copied=Copy();(void)copied; // reentrant read must never hold a runtime mutex
    if(selfRemove&&context->slot.configured.instance.instanceHandle==selfHandle)selfRemoveResult=runtime.RemoveBehavior(owner,registration);
    if(releaseInside)selfReleaseResult=BodySlots_QueryInterface(1,sizeof(Api))->ReleaseClient(owner);
    if(removeOther)crossRemoveResult=runtime.RemoveBehavior(owner,removeOther);
    if(block){entered=true;while(!unblock)std::this_thread::yield();}
    if(cap<2)return Status::BufferTooSmall;
    output[0]={1,1,5,0,10};output[1]={2,1,10,0,20};*count=2;
    if(context->input.heldRefId!=42||context->input.equippedFormId!=43)return Status::InvalidArgument;
    return Status::Ok;
}
ContentCommit Receipt(const ActionRequest& req,CommitStage stage) {
    ContentCommit c{};c.structSize=sizeof(c);c.stage=static_cast<std::uint32_t>(stage);
    c.requestId=req.requestId;c.epoch=req.grant.epoch;c.higgsRequestId=req.grant.requestId;c.higgsGrantId=req.grant.grantId;
    c.instanceHandle=req.offer.context.slot.configured.instance.instanceHandle;c.expectedContentVersion=req.offer.context.slot.contentVersion;
    c.contentVersion=c.expectedContentVersion+1;c.contentIdentity=99;c.quantity=1;return c;
}
Status BS_CALL Execute(void*,const ActionRequest* request) {
    ++calls;last=*request;auto copied=Copy();(void)copied;
    if(commitImmediately){auto receipt=Receipt(*request,CommitStage::Committed);CHECK(runtime.ReportContentCommit(owner,&receipt)==Status::Ok);}
    return fail?Status::Unavailable:Status::Ok;
}
void BS_CALL EventCallback(void*,const RuntimeEvent*) {++events;auto copied=Copy();(void)copied;}
Definition DefinitionFor(const char* key) {
    Definition d{};d.structSize=sizeof(d);d.flags=kEnabled;std::strcpy(d.provider,"runtime-owner");std::strcpy(d.key,key);
    std::strcpy(d.anchor,"Pelvis");std::strcpy(d.referenceRig,"skyrimvr-humanoid-v1");std::strcpy(d.kind,"pouch");
    d.rotation[0]=d.rotation[4]=d.rotation[8]=1;d.radius=3;d.handMask=Both;d.contentMask=Potion;return d;
}
RuntimeTransform Identity() {RuntimeTransform t{};t.scale=1;t.rotation[0]=t.rotation[4]=t.rotation[8]=1;return t;}
std::array<AnchorSample,2> anchors{};std::uint64_t frameNumber=0,epoch=1,body=1;
void Frame(bool leftInside=true,bool available=true,std::uint64_t anchorGeneration=1) {
    RuntimeFrame f{};f.epoch=epoch;f.frame=++frameNumber;f.bodyGeneration=body;f.timeSeconds=double(frameNumber);
    f.hands[0]={Left,1,frameNumber,{leftInside?0.f:20.f,0,0}};f.hands[1]={Right,1,frameNumber,{0,0,0}};
    for(auto& a:anchors){a.valid=available?1u:0u;a.anchorGeneration=anchorGeneration;a.world=Identity();}
    f.anchors=anchors.data();f.anchorCount=2;CHECK(UpdateRuntime(f)==Status::Ok);
}
RuntimeInput Input(std::uint64_t seq=1,std::uint32_t hand=Left) {
    RuntimeInput i{};i.structSize=sizeof(i);i.physicalHand=hand;i.contentCategory=Potion;i.available=1;i.button=1;i.edge=1;
    i.handOccupancy=3;i.handObservationMask=127;i.heldRefId=42;i.equippedFormId=43;i.epoch=epoch;i.frame=frameNumber;
    i.inputSequence=seq;i.poseSample=frameNumber;i.itemIdentity=42;i.handStateRevision=8;return i;
}
std::vector<ActionOffer> GetOffers(std::uint64_t seq=1,std::uint32_t hand=Left) {
    std::array<ActionOffer,kMaxActionOffers> result{};std::uint32_t count=0;auto input=Input(seq,hand);
    auto status=CollectOffers(input,result.data(),kMaxActionOffers,&count);if(status!=Status::Ok)std::cerr<<"offer sequence "<<seq<<" status "<<static_cast<unsigned>(status)<<'\n';CHECK(status==Status::Ok);return {result.begin(),result.begin()+count};
}
ActionGrant Grant(const ActionOffer& offer) {return {offer.context.input.epoch,100+offer.context.input.inputSequence,200+offer.context.input.inputSequence,offer.offerId,offer.policyRevision,offer.context.input.inputSequence,offer.context.input.physicalHand,0};}
bool Resolve(std::uint32_t old,std::uint32_t* next,void* context) {if(context)return false;*next=old+100;return true;}
Status AllowCommit(const ContentCommit&,const ActionRequest&) noexcept {
    if(rejectCommit)return Status::Conflict;
    try {std::vector<RuntimeRecord> records(kMaxInstances);RuntimeView view{sizeof(RuntimeView),1};return runtime.CopyRuntime(observer,records.data(),kMaxInstances,&view);}catch(...){return Status::InternalError;}
}
}
int main() {
    try {
        SetContentCommitValidator(AllowCommit);
        auto* base=BodySlots_QueryInterface(kApiVersion,sizeof(Api));auto* instance=BodySlots_QueryInstanceInterface(kInstanceApiVersion,sizeof(InstanceApi));
        auto* configured=BodySlots_QueryInstanceConfigurationInterface(1,sizeof(InstanceConfigurationApi));CHECK(base&&instance&&configured);
        CHECK(base->RegisterClient("runtime-owner",&owner)==Status::Ok);CHECK(base->RegisterClient("runtime-other",&other)==Status::Ok);CHECK(base->RegisterObserver(&observer)==Status::Ok);
        auto d=DefinitionFor("one");SlotRecord definition{};CHECK(base->UpsertDefinition(owner,&d,0,&definition)==Status::Ok);
        RuntimeTransform a=Identity(),world{};float center[3]{},radius=0;a.scale=2;a.position[0]=10;d.position[0]=3;d.regionCenter[1]=2;
        CHECK(ComposeSlotTransform(d,a,world,center,radius));CHECK(world.position[0]==16&&center[1]==4&&radius==6);float p[3]{16,10,0};CHECK(ContainsPoint(center,radius,p));p[1]=10.01f;CHECK(!ContainsPoint(center,radius,p));
        a.rotation[0]=-1;CHECK(!ComposeSlotTransform(d,a,world,center,radius));
        InstanceView view{sizeof(InstanceView),1};std::array<InstanceRecord,kMaxInstances> existing{};CHECK(instance->CopyEffectiveInstances(observer,0,0,existing.data(),kMaxInstances,&view)==Status::Ok);
        std::array<std::uint64_t,2> handles{};
        for(int j=0;j<2;++j){InstanceSpec spec{};spec.structSize=sizeof(spec);spec.flags=kInstanceActive;std::strcpy(spec.equipmentKey,j?"bag-b":"bag-a");std::strcpy(spec.slotKey,"opening");std::strcpy(spec.definitionProvider,"runtime-owner");std::strcpy(spec.definitionKey,"one");spec.definitionRevision=definition.definitionRevision;spec.binding=1;InstanceRecord out{};out.structSize=sizeof(out);CHECK(instance->UpsertInstance(owner,&spec,view.saveGeneration,0,&out)==Status::Ok);handles[j]=out.instanceHandle;anchors[j].instanceHandle=handles[j];}
        auto initial=Copy();CHECK(initial.view.count==2&&initial.rows[0].availability!=0);
        selfHandle=handles[0];BehaviorSpec behavior{};behavior.structSize=sizeof(behavior);behavior.instanceHandle=handles[0];behavior.offers=Offers;behavior.execute=Execute;behavior.higgsClient=123;behavior.higgsExecutor=456;
        CHECK(runtime.RegisterBehavior(other,&behavior,&registration)==Status::Unauthorized);CHECK(runtime.RegisterBehavior(owner,&behavior,&registration)==Status::Ok);
        std::uint64_t second=0;behavior.instanceHandle=handles[1];CHECK(runtime.RegisterBehavior(owner,&behavior,&second)==Status::Ok);
        std::uint64_t subscription=0;CHECK(runtime.Subscribe(observer,EventCallback,nullptr,&subscription)==Status::Ok);
        Frame();CHECK(calls==0&&events>0);auto rows=Copy();CHECK(rows.rows[0].insideHands==Both&&rows.rows[0].availability==0);
        auto offers=GetOffers();CHECK(offers.size()==4&&offers[0].proposal.priority==10&&offers[0].context.slot.configured.instance.instanceHandle==handles[0]);CHECK(offers[0].higgsClient==123&&offers[0].higgsExecutor==456);
        auto grant=Grant(offers[0]);auto bad=grant;bad.contextId++;std::uint64_t request=777;CHECK(DispatchSelected(offers[0],bad,&request)==Status::Conflict&&request==777);
        Frame(); // same body/anchor/config, current containment permits deferred safe-phase action
        CHECK(DispatchSelected(offers[0],grant,&request)==Status::Ok&&calls==1);CHECK(DispatchSelected(offers[0],grant,&request)==Status::Conflict&&calls==1);
        auto competing=GetOffers(2,Right);CHECK(competing.size()==2&&competing[0].context.slot.configured.instance.instanceHandle==handles[1]);
        auto commit=Receipt(last,CommitStage::Committed);auto stale=commit;stale.epoch++;CHECK(runtime.ReportContentCommit(owner,&stale)==Status::Conflict);CHECK(runtime.ReportContentCommit(other,&commit)==Status::Unauthorized);
        SetContentCommitValidator(nullptr);CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Unavailable);SetContentCommitValidator(AllowCommit);rejectCommit=true;CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Conflict);rejectCommit=false;
        CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Ok);CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Ok);CHECK(Copy().rows[0].contentVersion==1);
        auto completed=commit;completed.stage=static_cast<std::uint32_t>(CommitStage::Completed);CHECK(runtime.ReportContentCommit(owner,&completed)==Status::Ok);CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Ok);
        auto regressive=completed;regressive.stage=1;CHECK(runtime.ReportContentCommit(owner,&regressive)==Status::Conflict);
        ActionProgress progress{};CHECK(runtime.CopyActionStatus(owner,last.requestId,&progress)==Status::Ok&&progress.committed&&progress.stage==3&&progress.contentVersion==1);
        // Confirmed HIGGS commitment is irreversible before the provider's mirror
        // receipt arrives; subsequent loss of the access volume cannot erase it.
        offers=GetOffers(31);auto selected=offers[0];CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Ok);CHECK(ApplyHandResult(last.grant.requestId,last.grant.epoch,CommitStage::Committed)==Status::Ok);Frame(false,false);commit=Receipt(last,CommitStage::Committed);CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Ok);CHECK(runtime.CopyActionStatus(owner,request,&progress)==Status::Ok&&progress.handCommitted&&progress.committed);completed=commit;completed.stage=3;CHECK(runtime.ReportContentCommit(owner,&completed)==Status::Ok);Frame();
        // Started execution keeps its exclusive content reservation even before
        // the main adapter has polled HIGGS's commitment. A configuration read or
        // zone loss must not create a second inventory executor in the other hand.
        offers=GetOffers(32);selected=offers[0];CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Ok);
        SlotVisualSpec whileExecuting{};whileExecuting.structSize=sizeof(whileExecuting);whileExecuting.flags=1;
        whileExecuting.contentVersion=selected.context.slot.contentVersion;
        CHECK(runtime.SetVisual(owner,selected.context.slot.configured.instance.instanceHandle,&whileExecuting)==Status::Ok);
        Frame();auto otherHandOffers=GetOffers(33,Right);
        for(const auto& otherOffer:otherHandOffers)CHECK(otherOffer.context.slot.configured.instance.instanceHandle!=selected.context.slot.configured.instance.instanceHandle);
        Frame(false,false);commit=Receipt(last,CommitStage::Committed);
        CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Ok);
        completed=commit;completed.stage=3;CHECK(runtime.ReportContentCommit(owner,&completed)==Status::Ok);Frame();
        offers=GetOffers(3);selected=offers[0];Frame(false);CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Conflict);
        Frame();offers=GetOffers(4);selected=offers[0];Frame(true,false);CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Conflict);CHECK(GetOffers(5).empty());
        Frame();offers=GetOffers(6);selected=offers[0];Frame(true,true,2);CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Conflict);
        offers=GetOffers(7);selected=offers[0];++epoch;Frame();CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Conflict);
        fail=true;offers=GetOffers(8);selected=offers[0];CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Unavailable);CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Conflict);fail=false;
        CHECK(runtime.CopyActionStatus(owner,request,&progress)==Status::Ok&&progress.stage==static_cast<std::uint32_t>(CommitStage::Failed));
        offers=GetOffers(81);selected=offers[0];CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Ok);CHECK(ApplyHandResult(last.grant.requestId,last.grant.epoch,CommitStage::Cancelled)==Status::Ok);commit=Receipt(last,CommitStage::Committed);CHECK(runtime.ReportContentCommit(owner,&commit)==Status::Conflict);CHECK(runtime.CopyActionStatus(owner,request,&progress)==Status::Ok&&progress.stage==5);
        commitImmediately=true;offers=GetOffers(9);selected=offers[0];CHECK(DispatchSelected(selected,Grant(selected),&request)==Status::Ok);CHECK(runtime.CopyActionStatus(owner,request,&progress)==Status::Ok&&progress.committed);commitImmediately=false;completed=Receipt(last,CommitStage::Completed);CHECK(runtime.ReportContentCommit(owner,&completed)==Status::Ok);
        selfRemove=true;offers=GetOffers(10);CHECK(selfRemoveResult==Status::Conflict);selfRemove=false;
        releaseInside=true;removeOther=second;offers=GetOffers(101);CHECK(selfReleaseResult==Status::Conflict&&crossRemoveResult==Status::Conflict);releaseInside=false;removeOther=0;
        RuntimeBindingSpec binding{sizeof(RuntimeBindingSpec),static_cast<std::uint32_t>(RuntimeBindingKind::EquipmentNode),42,0,{}};std::strcpy(binding.node,"BagNode");CHECK(runtime.SetBinding(other,handles[0],&binding)==Status::Unauthorized);CHECK(runtime.SetBinding(owner,handles[0],&binding)==Status::Ok);CHECK(Copy().rows[0].availability!=0);Frame();
        SlotVisualSpec visual{};visual.structSize=sizeof(visual);visual.contentVersion=Copy().rows[0].contentVersion;std::strcpy(visual.holderModel,"body-slots/holder.nif");CHECK(runtime.SetVisual(owner,handles[0],&visual)==Status::Ok);auto visualRow=Copy().rows[0];CHECK(ReportPresentation(handles[0],visualRow.visualRevision,visualRow.contentVersion,PresentationState::Ready)==Status::Ok);CHECK(ReportPresentation(handles[0],visualRow.visualRevision+1,visualRow.contentVersion,PresentationState::Ready)==Status::Conflict);
        Frame();
        // Remove drains an in-flight immutable offer callback, outside runtime locks.
        block=true;Status collected=Status::InternalError;std::thread caller([&]{std::array<ActionOffer,kMaxActionOffers> out{};std::uint32_t n=0;collected=CollectOffers(Input(11),out.data(),kMaxActionOffers,&n);});while(!entered)std::this_thread::yield();std::atomic<bool> removed{false};Status removal=Status::InternalError;
        std::thread remover([&]{removal=runtime.RemoveBehavior(owner,registration);removed=true;});std::this_thread::sleep_for(std::chrono::milliseconds(15));CHECK(!removed);auto during=Copy();(void)during;unblock=true;caller.join();remover.join();CHECK(removal==Status::Ok&&removed&&collected==Status::Conflict);block=false;
        CHECK(runtime.RemoveBehavior(owner,second)==Status::Ok);CHECK(runtime.Unsubscribe(observer,subscription)==Status::Ok);
        // Save bindings/visuals by durable keys; corrupt third chunks cannot
        // partially replace the existing definition/geometry customization batch.
        std::vector<std::uint8_t> definitions,instanceBytes,runtimeBytes;bool hasInstances=false,hasRuntime=false;
        CHECK(SaveConfigurationBatch(definitions,instanceBytes,hasInstances,runtimeBytes,hasRuntime)==Status::Ok&&hasRuntime);
        RuntimeCustomizationData saved{};CHECK(DecodeRuntimeCustomizations(runtimeBytes.data(),static_cast<std::uint32_t>(runtimeBytes.size()),saved)==Status::Ok&&saved.count==1);
        CHECK(saved.entries[0].binding.requiredEquipmentFormId==42&&saved.entries[0].visual.contentVersion==0);
        auto remapped=runtimeBytes;CHECK(RemapRuntimeCustomizationForms(remapped,Resolve,nullptr)==Status::Ok);RuntimeCustomizationData mapped{};CHECK(DecodeRuntimeCustomizations(remapped.data(),static_cast<std::uint32_t>(remapped.size()),mapped)==Status::Ok&&mapped.entries[0].binding.requiredEquipmentFormId==142);
        remapped=runtimeBytes;CHECK(RemapRuntimeCustomizationForms(remapped,Resolve,&saved)==Status::Ok);CHECK(DecodeRuntimeCustomizations(remapped.data(),static_cast<std::uint32_t>(remapped.size()),mapped)==Status::Ok&&mapped.entries[0].binding.kind==3&&mapped.entries[0].binding.requiredEquipmentFormId==0);
        auto corrupt=runtimeBytes;corrupt[16]=255;auto prior=Copy();CHECK(LoadConfigurationBatch(&definitions,&instanceBytes,&corrupt,prior.view.saveGeneration,prior.view.viewRevision)==Status::InvalidArgument);CHECK(Copy().view.viewRevision==prior.view.viewRevision);
        auto untouched=saved;CHECK(DecodeRuntimeCustomizations(runtimeBytes.data(),static_cast<std::uint32_t>(runtimeBytes.size()-1),untouched)==Status::InvalidArgument);CHECK(!std::memcmp(&untouched,&saved,sizeof(saved)));
        // Removing the runtime instance leaves its saved authored-key overrides dormant.
        auto first=Copy().rows[0].configured.instance;CHECK(instance->RemoveInstance(owner,first.instanceHandle,prior.view.saveGeneration,first.instanceRevision)==Status::Ok);
        std::array<RuntimeCustomizationRecord,kMaxInstances> stored{};InstanceView savedView{sizeof(InstanceView),1};CHECK(runtime.CopySavedCustomizations(observer,stored.data(),kMaxInstances,&savedView)==Status::Ok&&savedView.count==1);
        CHECK(runtime.ClearSavedCustomization(other,&stored[0].key,3,savedView.saveGeneration,savedView.viewRevision)==Status::Unauthorized);
        CHECK(runtime.ClearSavedCustomization(owner,&stored[0].key,3,savedView.saveGeneration,savedView.viewRevision-1)==Status::Conflict);
        CHECK(runtime.ClearSavedCustomization(owner,&stored[0].key,3,savedView.saveGeneration,savedView.viewRevision)==Status::Ok);
        CHECK(LoadConfigurationBatch(&definitions,&instanceBytes,&runtimeBytes,savedView.saveGeneration,Copy().view.viewRevision)==Status::Ok);
        InstanceView reset{};CHECK(ResetSaveScope(&reset)==Status::Ok);CHECK(Copy().view.count==0);
        CHECK(LoadConfigurationBatch(&definitions,&instanceBytes,&remapped,reset.saveGeneration,reset.viewRevision)==Status::Ok);
        InstanceSpec restored{};restored.structSize=sizeof(restored);restored.flags=kInstanceActive;std::strcpy(restored.equipmentKey,"bag-a");std::strcpy(restored.slotKey,"opening");std::strcpy(restored.definitionProvider,"runtime-owner");std::strcpy(restored.definitionKey,"one");restored.definitionRevision=definition.definitionRevision;restored.binding=1;InstanceRecord restoredRecord{};restoredRecord.structSize=sizeof(restoredRecord);
        CHECK(instance->UpsertInstance(owner,&restored,reset.saveGeneration,0,&restoredRecord)==Status::Ok);CHECK(restoredRecord.instanceHandle!=handles[0]);anchors[0].instanceHandle=restoredRecord.instanceHandle;Frame();auto restoredRows=Copy();CHECK(restoredRows.rows[0].binding.kind==3&&restoredRows.rows[0].availability==static_cast<std::uint32_t>(RuntimeAvailability::AnchorUnavailable)&&restoredRows.rows[0].contentVersion==0);CHECK(!std::strcmp(restoredRows.rows[0].visual.holderModel,"body-slots/holder.nif"));
        // Repeated save transitions retire old behaviors without consuming the bounded pool.
        for(int n=0;n<3;++n){behavior.instanceHandle=restoredRecord.instanceHandle;std::uint64_t lease=0;CHECK(runtime.RegisterBehavior(owner,&behavior,&lease)==Status::Ok);CHECK(ResetSaveScope(&reset)==Status::Ok);CHECK(Copy().view.count==0);restoredRecord={};restoredRecord.structSize=sizeof(restoredRecord);CHECK(instance->UpsertInstance(owner,&restored,reset.saveGeneration,0,&restoredRecord)==Status::Ok);}
        behavior.instanceHandle=restoredRecord.instanceHandle;std::uint64_t finalLease=0;CHECK(runtime.RegisterBehavior(owner,&behavior,&finalLease)==Status::Ok);
        // Base client release drains owned runtime code as well as registry state.
        anchors[0].instanceHandle=restoredRecord.instanceHandle;Frame();block=true;entered=false;unblock=false;Status releaseCollected=Status::InternalError;std::thread releaseCaller([&]{std::array<ActionOffer,kMaxActionOffers> out{};std::uint32_t n=0;releaseCollected=CollectOffers(Input(200),out.data(),kMaxActionOffers,&n);});while(!entered)std::this_thread::yield();std::atomic<bool> released{false};Status releaseResult=Status::InternalError;
        std::thread releaser([&]{releaseResult=base->ReleaseClient(owner);released=true;});std::this_thread::sleep_for(std::chrono::milliseconds(15));CHECK(!released);unblock=true;releaseCaller.join();releaser.join();CHECK(releaseResult==Status::Ok&&released&&releaseCollected==Status::Conflict);block=false;owner=0;CHECK(Copy().view.count==0);
        InvalidateRuntime();CHECK(Copy().view.count==0);CHECK(base->ReleaseClient(observer)==Status::Ok);CHECK(base->ReleaseClient(other)==Status::Ok);CHECK(base->ReleaseClient(owner)==Status::Unauthorized);
        std::cout<<"runtime: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
