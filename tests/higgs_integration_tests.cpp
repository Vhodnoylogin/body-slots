// The real HIGGS broker and Body Slots registry/runtime in one process. No game
// simulation: this checks the provider protocol with explicit copied observations.
#include "runtime.h"
#include "instances.h"
#include "hand_broker.h"
#include "interaction_validation.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" const body_slots::Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
extern "C" const body_slots::InstanceApi* BS_CALL BodySlots_QueryInstanceInterface(std::uint32_t,std::uint32_t) noexcept;
using namespace body_slots;
using namespace body_slots::detail;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(false)
namespace {
struct Pilot {
    HiggsHands::Broker broker;
    std::uint64_t clock = HiggsHands::Now(), recognizer{}, provider{}, executor{}, registration{}, writer{}, reader{}, instance{}, behavior{};
    std::uint64_t frame = 0, epoch = 0, input = 0, candidate = 0;
    std::array<HIGGS_HandRecord,2> hands{};
    ActionOffer offer{}; HIGGS_ActionRequest selected{}; ActionRequest executed{};
    unsigned offers{}, notifications{}, executions{};
    Pilot() {
        HIGGS_HandClientInfo info{}; info.structSize=sizeof(info); std::strcpy(info.name,"BodySlots");
        CHECK(!broker.Register(info,recognizer)); std::strcpy(info.name,"potion provider"); CHECK(!broker.Register(info,provider));
        CHECK(!broker.Executor(provider,Selected,this,executor));
        CHECK(!broker.Recognizer(recognizer,Recognize,this,registration));
        for(auto& h:hands)h.observationMask=HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN|HIGGS_OBS_TRACKING_VALID|HIGGS_OBS_PALM_VALID;
        hands[1].heldRefId=77; // Other-hand possession must remain untouched.
        broker.ObserveWorld(1,false,clock); broker.Publish(hands,++clock);
        HIGGS_HandSnapshot snap{}; CHECK(!broker.Copy(recognizer,snap,clock)); epoch=snap.epoch;
        auto* base=BodySlots_QueryInterface(1,sizeof(Api)); CHECK(base->RegisterClient("pilot-provider",&writer)==Status::Ok);
        CHECK(base->RegisterObserver(&reader)==Status::Ok);
        Definition d{};d.structSize=sizeof(d);d.flags=kEnabled;std::strcpy(d.provider,"pilot-provider");std::strcpy(d.key,"potion-pocket");
        std::strcpy(d.anchor,"NPC Pelvis [Pelv]");std::strcpy(d.referenceRig,"skyrimvr-humanoid-v1");std::strcpy(d.kind,"potion");
        d.radius=5;d.rotation[0]=d.rotation[4]=d.rotation[8]=1;d.handMask=Both;d.contentMask=Potion;
        SlotRecord definition{};CHECK(base->UpsertDefinition(writer,&d,0,&definition)==Status::Ok);
        auto* api=BodySlots_QueryInstanceInterface(1,sizeof(InstanceApi));InstanceView view{sizeof(view),1};
        CHECK(api->CopyEffectiveInstances(reader,0,0,nullptr,0,&view)==Status::Ok);
        InstanceSpec spec{};spec.structSize=sizeof(spec);spec.flags=kInstanceActive;std::strcpy(spec.equipmentKey,"body");std::strcpy(spec.slotKey,"pocket");
        std::strcpy(spec.definitionProvider,d.provider);std::strcpy(spec.definitionKey,d.key);spec.definitionRevision=definition.definitionRevision;spec.binding=1;
        InstanceRecord record{};record.structSize=sizeof(record);CHECK(api->UpsertInstance(writer,&spec,view.saveGeneration,0,&record)==Status::Ok);instance=record.instanceHandle;
        BehaviorSpec b{};b.structSize=sizeof(b);b.instanceHandle=instance;b.context=this;b.offers=Offers;b.execute=Execute;b.higgsClient=provider;b.higgsExecutor=executor;
        CHECK(GetRuntimeApi().RegisterBehavior(writer,&b,&behavior)==Status::Ok);
    }
    void Frame(bool inside=true) {
        RuntimeFrame f{};f.epoch=epoch;f.frame=++frame;f.bodyGeneration=1;f.timeSeconds=double(frame);
        f.hands[0]={Left,1,frame,{inside?0.f:20.f,0,0}};f.hands[1]={Right,1,frame,{30,0,0}};
        AnchorSample a{};a.instanceHandle=instance;a.anchorGeneration=1;a.valid=1;a.world.scale=1;a.world.rotation[0]=a.world.rotation[4]=a.world.rotation[8]=1;
        f.anchors=&a;f.anchorCount=1;CHECK(UpdateRuntime(f)==Status::Ok);
    }
    static Status BS_CALL Offers(void* context,const ActionContext*,ActionProposal* out,std::uint32_t cap,std::uint32_t* count) {
        auto& p=*static_cast<Pilot*>(context);++p.offers;if(!cap)return Status::BufferTooSmall;out[0]={1,Left,100,0,900};*count=1;return Status::Ok;
    }
    static Status BS_CALL Execute(void* context,const ActionRequest* request) {auto& p=*static_cast<Pilot*>(context);++p.executions;p.executed=*request;return Status::Ok;}
    static void HIGGS_HAND_CALL Selected(const HIGGS_ActionRequest* request,void* context) {auto& p=*static_cast<Pilot*>(context);++p.notifications;p.selected=*request;}
    static void HIGGS_HAND_CALL Recognize(const HIGGS_HandInput* input,void* context) {
        auto& p=*static_cast<Pilot*>(context);if(input->edge!=HIGGS_INPUT_DOWN)return;
        RuntimeInput i{};i.structSize=sizeof(i);i.physicalHand=input->physicalHand;i.button=input->button;i.edge=input->edge;i.contentCategory=0; // empty-hand withdrawal from a potion-only slot
        i.available=input->snapshot.hands[0].availability==HIGGS_AVAILABILITY_READY;i.epoch=input->epoch;i.frame=p.frame;i.inputSequence=input->inputId;i.poseSample=p.frame;
        std::array<ActionOffer,kMaxActionOffers> offered{};std::uint32_t count{};
        CHECK(CollectOffers(i,offered.data(),offered.size(),&count)==Status::Ok);if(!count)return;p.offer=offered[0];p.input=input->inputId;
        HIGGS_ActionCandidate c{};c.structSize=sizeof(c);c.physicalHands=Left;c.inputId=input->inputId;c.epoch=input->epoch;c.handGeneration=input->handGeneration;
        c.executor=p.executor;c.actionId=p.offer.proposal.executorAction;c.contextId=p.offer.offerId;c.contextGeneration=p.offer.policyRevision;c.grantDurationMs=1000;
        const auto status=p.broker.Submit(p.recognizer,c,p.candidate,++p.clock);
        CHECK(status==HIGGS_HAND_OK||status==HIGGS_HAND_WRONG_OWNER);
    }
    Status Dispatch() {
        ActionGrant grant{selected.epoch,selected.requestId,selected.grant,selected.contextId,selected.contextGeneration,selected.inputId,Left,0};
        std::uint64_t id{};return DispatchSelected(offer,grant,&id);
    }
};
Pilot* active=nullptr;
Status Validate(const ContentCommit& c,const ActionRequest& request) noexcept {
    HIGGS_ActionResult result{};
    if(active->broker.Result(active->recognizer,request.grant.requestId,result))return Status::Unavailable;
    if(result.epoch!=c.epoch||result.executor!=request.offer.higgsExecutor)return Status::Conflict;
    return result.stage==HIGGS_ACTION_COMMITTED||result.stage==HIGGS_ACTION_COMPLETED?Status::Ok:Status::Conflict;
}
}
int main() {
    HIGGS_HandRecord held{};held.observationMask=kKnownHandObservation;held.availability=HIGGS_AVAILABILITY_RESERVED;held.heldRefId=42;
    CHECK(MayOfferSlotAction(held));held.grant=3;CHECK(!MayOfferSlotAction(held));held.grant=0;
    held.observationMask|=HIGGS_OBS_SECONDARY_SUPPORT;CHECK(!MayOfferSlotAction(held));held.observationMask=kKnownHandObservation&~HIGGS_OBS_TRACKING_VALID;CHECK(!MayOfferSlotAction(held));
    RuntimeRecord preview{};std::strcpy(preview.visual.previewModel,"potion.nif");CHECK(!MayDisplayPreview(preview));
    preview.contentIdentity=42;preview.quantity=1;CHECK(MayDisplayPreview(preview));preview.contentVersion=1;CHECK(!MayDisplayPreview(preview));
    HIGGS_HandSnapshot before{},after{};before.epoch=after.epoch=1;after.worldReady=1;
    for(auto& h:after.hands){h.observationMask=kKnownHandObservation;h.availability=HIGGS_AVAILABILITY_RESERVED;h.owner=7;h.grant=8;}
    CHECK(GrantMatchesObservations(before,after,Both,7,8));after.hands[1].unavailableReasons=HIGGS_HAND_REASON_MENU;CHECK(!GrantMatchesObservations(before,after,Both,7,8));
    CHECK(GrantMatchesObservations(before,after,Left,7,8));after.hands[1].unavailableReasons=0;after.hands[1].generation=1;CHECK(!GrantMatchesObservations(before,after,Both,7,8));
    Pilot pilot;active=&pilot;SetContentCommitValidator(Validate);pilot.Frame();
    CHECK(!pilot.executions); // Enter/hover never authorizes an action.
    CHECK(!pilot.broker.Input(Left,HIGGS_INPUT_TRIGGER,true,++pilot.clock));CHECK(!pilot.notifications);
    CHECK(!pilot.broker.Authorize(pilot.provider,pilot.executor,pilot.recognizer,Left));
    pilot.broker.Input(Left,HIGGS_INPUT_TRIGGER,false,++pilot.clock);
    CHECK(pilot.broker.Input(Left,HIGGS_INPUT_TRIGGER,true,++pilot.clock));CHECK(pilot.notifications==1);
    pilot.Frame(false);CHECK(pilot.Dispatch()==Status::Conflict);
    CHECK(!pilot.broker.CancelRecognized(pilot.recognizer,pilot.selected.requestId,pilot.epoch,1));CHECK(!pilot.executions);
    pilot.broker.Input(Left,HIGGS_INPUT_TRIGGER,false,++pilot.clock);pilot.Frame();
    CHECK(pilot.broker.Input(Left,HIGGS_INPUT_TRIGGER,true,++pilot.clock));CHECK(pilot.notifications==2);CHECK(pilot.Dispatch()==Status::Ok);CHECK(pilot.executions==1);
    CHECK(pilot.Dispatch()==Status::Conflict);
    ContentCommit receipt{};receipt.structSize=sizeof(receipt);receipt.stage=static_cast<std::uint32_t>(CommitStage::Committed);
    receipt.requestId=pilot.executed.requestId;receipt.epoch=pilot.epoch;receipt.higgsRequestId=pilot.selected.requestId;receipt.higgsGrantId=pilot.selected.grant;
    receipt.instanceHandle=pilot.instance;receipt.contentVersion=1;receipt.contentIdentity=999;receipt.quantity=1;
    CHECK(GetRuntimeApi().ReportContentCommit(pilot.writer,&receipt)==Status::Conflict);
    HIGGS_ActionResult result{};result.structSize=sizeof(result);result.stage=HIGGS_ACTION_COMMITTED;result.requestId=pilot.selected.requestId;result.executor=pilot.executor;
    result.epoch=pilot.epoch;result.inputId=pilot.selected.inputId;result.receiptFlags=HIGGS_RECEIPT_INVENTORY_COMMITTED;result.itemRefId=999;result.quantity=1;
    CHECK(!pilot.broker.Report(pilot.provider,result));CHECK(GetRuntimeApi().ReportContentCommit(pilot.writer,&receipt)==Status::Ok);
    CHECK(GetRuntimeApi().ReportContentCommit(pilot.writer,&receipt)==Status::Ok);
    HIGGS_HandSnapshot snapshot{};CHECK(!pilot.broker.Copy(pilot.recognizer,snapshot,++pilot.clock));CHECK(snapshot.hands[1].heldRefId==77);
    result.stage=HIGGS_ACTION_COMPLETED;CHECK(!pilot.broker.Report(pilot.provider,result));
    CHECK(pilot.broker.Input(Left,HIGGS_INPUT_TRIGGER,false,++pilot.clock)); // consumed Up cannot become another action
    pilot.broker.ObserveWorld(0,true,++pilot.clock);pilot.epoch=pilot.broker.Epoch();pilot.Frame();
    CHECK(GetRuntimeApi().ReportContentCommit(pilot.writer,&receipt)!=Status::Ok);
    CHECK(BodySlots_QueryInterface(1,sizeof(Api))->ReleaseClient(pilot.writer)==Status::Ok);
    CHECK(BodySlots_QueryInterface(1,sizeof(Api))->ReleaseClient(pilot.reader)==Status::Ok);
    std::puts("PASS real HIGGS/BodySlots routed authority, deferred spatial validation, once-only execution, committed content and epoch invalidation; no game qualification");
}
