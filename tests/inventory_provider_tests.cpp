// Actual Body Slots + public HIGGS services. Only the native engine/pose adapter
// is synthetic; the helper never includes these internal headers.
#include "ordinary_inventory_provider.h"
#include "runtime.h"
#include "instances.h"
#include "configuration.h"
#include "content_policy.h"
#include "hand_broker.h"
#include "hand_transactions.h"
#include "synergy_inventory_native.h"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
using namespace body_slots;
using namespace body_slots::detail;
using namespace body_slots::examples;
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
extern "C" const InstanceApi* BS_CALL BodySlots_QueryInstanceInterface(std::uint32_t,std::uint32_t) noexcept;
namespace {
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x);}while(false)
struct Selection final:HiggsHands::InventorySelectionHandle {std::uint64_t revision;std::uint32_t base;};
struct Engine final:HiggsHands::InventoryEngine {
    unsigned stock=3,selects=0,withdrawals=0,stores=0,compensations=0;std::uint32_t nextRef=800;
    std::uint64_t revision=1;bool refuse=false,uncertain=false;std::map<std::uint32_t,std::uint32_t> world;
    std::uint64_t Capabilities()const override{return 7;}
    HIGGS_HandStatus Select(const HIGGS_InventorySelectionRequest& r,std::shared_ptr<HiggsHands::InventorySelectionHandle>& out,std::uint32_t& container)override{++selects;if(refuse||r.baseFormId!=100||r.stackOrdinal||r.quantity!=1||!stock)return HIGGS_HAND_STALE;auto s=std::make_shared<Selection>();s->revision=revision;s->base=r.baseFormId;out=s;container=0x14;return HIGGS_HAND_OK;}
    bool Current(const std::shared_ptr<HiggsHands::InventorySelectionHandle>& p)override{auto s=std::dynamic_pointer_cast<Selection>(p);return s&&!refuse&&s->revision==revision&&stock;}
    HiggsHands::InventoryOutcome Execute(const HIGGS_TransferExecution& x,const std::shared_ptr<HiggsHands::InventorySelectionHandle>& selection,HiggsHands::InventoryPermit& permit)override{
        if(x.phase!=HIGGS_TX_COMPENSATE&&x.request.kind==HIGGS_TRANSFER_INVENTORY_TO_HAND&&!Current(selection))return {HIGGS_HAND_STALE};
        if(!permit.Begin())return {HIGGS_HAND_CONFLICT};
        if(x.phase==HIGGS_TX_COMPENSATE){++compensations;++stock;++revision;world.erase(x.current.actualRefId);return {HIGGS_HAND_OK,HIGGS_TX_COMPENSATED,0,0,true};}
        if(x.request.kind==HIGGS_TRANSFER_INVENTORY_TO_HAND){++withdrawals;--stock;++revision;const auto ref=nextRef++;world[ref]=x.request.original.baseFormId;if(uncertain)return {HIGGS_HAND_PENDING};return {HIGGS_HAND_OK,HIGGS_TX_INVENTORY_COMMITTED,ref,0,true};}
        if(x.request.kind==HIGGS_TRANSFER_HAND_TO_INVENTORY){++stores;++stock;++revision;world.erase(x.request.original.worldRefId);return {HIGGS_HAND_OK,HIGGS_TX_INVENTORY_COMMITTED,0,0,true};}
        return {HIGGS_HAND_UNSUPPORTED};
    }
    HiggsHands::InventoryOutcome Poll(const HIGGS_TransferExecution&)override{return {HIGGS_HAND_PENDING};}
    void Forget(std::uint64_t)override{}
    void Reset()override{}
};
struct Fixture;
Fixture* current=nullptr;
struct Fixture {
    Engine engine;OrdinaryInventoryProvider provider;InventoryProviderTables tables;
    std::uint64_t writer=0,reader=0,recognizer=0,consumer=0,registration=0,instance=0,epoch=0,frame=0;
    std::array<HIGGS_HandRecord,2> observed{};ActionOffer offer{};HIGGS_InputDecision decision{};
    bool hasAnchor=true,allowGrab=true,policyEnabled=true;std::uint64_t localRequest=0,submittedRequest=0;
    std::uint32_t activeHand=Left;HIGGS_HandStatus submitStatus=HIGGS_HAND_NOT_FOUND;
    static HIGGS_HandStatus Client(const char* name,std::uint64_t& out){HIGGS_HandClientInfo info{};info.structSize=sizeof(info);std::strcpy(info.name,name);return HiggsHands::Service().Register(info,out);}
    static Status BS_CALL Integration(GameIntegration* out){if(!out)return Status::InvalidArgument;*out={sizeof(*out),1,1,0,current->recognizer,current->epoch,current->frame,1};return Status::Ok;}
    static bool Policy(void* data,const SourceActionContext& c,OrdinaryItemPlan& plan){auto& self=*static_cast<Fixture*>(data);if(!self.policyEnabled)return false;plan={};plan.quantity=1;if(c.items[0].referenceId){plan.transferKind=HIGGS_TRANSFER_HAND_TO_INVENTORY;plan.after={100,c.context.slot.quantity+1};}else{plan.transferKind=HIGGS_TRANSFER_INVENTORY_TO_HAND;plan.baseFormId=100;plan.after=self.engine.stock>1?ContentProjection{100,self.engine.stock-1}:ContentProjection{};}return true;}
    explicit Fixture(unsigned index,std::uint32_t storeEdge=HIGGS_INPUT_DOWN,std::uint32_t hand=Left):activeHand(hand){current=this;CHECK(!Client("BodySlots-helper-fixture",recognizer));CHECK(!Client("ordinary-provider-client",consumer));HiggsHands::Service().ObserveWorld(1000+index,false,HiggsHands::Now());for(auto& h:observed)h.observationMask=15;observed[1].heldRefId=77;Publish();epoch=HiggsHands::Service().Epoch();HiggsHands::Inventory().Install(&engine);HiggsHands::Inventory().Drain();
        const auto* base=BodySlots_QueryInterface(1,sizeof(Api));char name[64];std::snprintf(name,sizeof(name),"ordinary-slot-provider-%u",index);CHECK(base->RegisterClient(name,&writer)==Status::Ok);CHECK(base->RegisterObserver(&reader)==Status::Ok);
        Definition d{};d.structSize=sizeof(d);d.flags=kEnabled;std::strcpy(d.provider,name);std::strcpy(d.key,"pocket");std::strcpy(d.anchor,"NPC Pelvis [Pelv]");std::strcpy(d.referenceRig,"skyrimvr-humanoid-v1");std::strcpy(d.kind,"potion");d.radius=5;d.rotation[0]=d.rotation[4]=d.rotation[8]=1;d.handMask=Both;d.contentMask=Potion;SlotRecord definition{};CHECK(base->UpsertDefinition(writer,&d,0,&definition)==Status::Ok);
        const auto* instances=BodySlots_QueryInstanceInterface(1,sizeof(InstanceApi));InstanceView view{sizeof(view),1};CHECK(instances->CopyEffectiveInstances(reader,0,0,nullptr,0,&view)==Status::Ok);InstanceSpec spec{};spec.structSize=sizeof(spec);spec.flags=kInstanceActive;spec.binding=1;std::strcpy(spec.equipmentKey,"worn-pouch");std::strcpy(spec.slotKey,"pocket");std::strcpy(spec.definitionProvider,d.provider);std::strcpy(spec.definitionKey,d.key);spec.definitionRevision=definition.definitionRevision;InstanceRecord created{};created.structSize=sizeof(created);CHECK(instances->UpsertInstance(writer,&spec,view.saveGeneration,0,&created)==Status::Ok);instance=created.instanceHandle;
        CHECK(!HIGGS_QueryHandInterface(1,sizeof(tables.hands),&tables.hands));CHECK(!HIGGS_QueryHandRoutingInterface(1,sizeof(tables.routing),&tables.routing));CHECK(!HIGGS_QueryInventoryInterface(1,sizeof(tables.inventory),&tables.inventory));CHECK(!HIGGS_QueryTransactionsInterface(1,sizeof(tables.transactions),&tables.transactions));tables.slots=&GetRuntimeApi();tables.behaviors=&GetBehaviorApi();static const GameApi game{sizeof(GameApi),1,Integration};tables.game=&game;
        CHECK(!HiggsHands::Service().Recognizer(recognizer,Recognize,this,registration));SetContentCommitValidator(Validate);Frame();InventoryProviderConfig config{writer,instance,consumer,Both,1000,0,Policy,this,storeEdge};CHECK(provider.Attach(tables,config)==Status::Ok);
    }
    void Publish(){HiggsHands::Service().Publish(observed,HiggsHands::Now());}
    void Frame(){RuntimeFrame f{};f.epoch=epoch;f.frame=++frame;f.bodyGeneration=1;f.timeSeconds=double(frame);f.hands[0]={Left,1,frame,{activeHand==Left?0.f:50.f,0,0}};f.hands[1]={Right,1,frame,{activeHand==Right?0.f:50.f,0,0}};AnchorSample anchor{};anchor.instanceHandle=instance;anchor.valid=hasAnchor;anchor.anchorGeneration=1;anchor.world.scale=1;anchor.world.rotation[0]=anchor.world.rotation[4]=anchor.world.rotation[8]=1;f.anchors=&anchor;f.anchorCount=1;CHECK(UpdateRuntime(f)==Status::Ok);
        RuntimeView view{sizeof(view),1};std::array<RuntimeRecord,kMaxInstances> records{};CHECK(GetRuntimeApi().CopyRuntime(reader,records.data(),kMaxInstances,&view)==Status::Ok);const auto snapshot=HiggsHands::Service().CopyPublished();ContentPolicySample sources[2]{};for(unsigned i=0;i<2;++i){auto& s=sources[i];s.instanceHandle=instance;s.epoch=epoch;s.handObservationId=snapshot.observationId;s.physicalHand=activeHand;s.source=i+1;s.known=s.allowed=1;s.category=i==0?Potion:Weapon;s.baseFormId=i==0?100:200;}CHECK(PublishContentPolicySamples(sources,view.count?2u:0u,view.saveGeneration,view.viewRevision)==Status::Ok);
    }
    static void HIGGS_HAND_CALL Recognize(const HIGGS_HandInput* input,void* p){auto& f=*static_cast<Fixture*>(p);RuntimeInput i{};i.structSize=sizeof(i);i.physicalHand=input->physicalHand;i.button=input->button;i.edge=input->edge;i.available=1;i.epoch=input->epoch;i.frame=f.frame;i.inputSequence=input->inputId;i.poseSample=f.frame;const auto& h=input->snapshot.hands[input->physicalHand-1];i.handOccupancy=h.occupancy;i.handObservationMask=h.observationMask;i.heldRefId=h.heldRefId;i.equippedFormId=h.equippedFormId;i.handStateRevision=input->snapshot.observationId;i.contentCategory=h.heldRefId?Potion:h.equippedFormId?Weapon:0;ActionOffer offers[kMaxActionOffers]{};std::uint32_t n=0;CHECK(CollectOffers(i,offers,kMaxActionOffers,&n)==Status::Ok);if(!n)return;CHECK(n==1);f.offer=offers[0];HIGGS_ActionCandidate c{};c.structSize=sizeof(c);c.physicalHands=input->physicalHand;c.inputId=input->inputId;c.epoch=input->epoch;c.handGeneration=input->handGeneration;c.executor=f.offer.higgsExecutor;c.actionId=f.offer.proposal.executorAction;c.contextId=f.offer.offerId;c.contextGeneration=f.offer.policyRevision;c.grantDurationMs=2000;f.submitStatus=f.tables.hands.SubmitCandidate(f.recognizer,&c,&f.submittedRequest);}
    bool Edge(bool down,bool dispatch=true){offer={};submittedRequest=0;submitStatus=HIGGS_HAND_NOT_FOUND;Frame();const bool consumed=HiggsHands::Service().Input(activeHand,HIGGS_INPUT_GRIP,down,HiggsHands::Now());if(!offer.offerId||!dispatch)return consumed;CHECK(submitStatus==HIGGS_HAND_OK);decision={};decision.structSize=sizeof(decision);CHECK(!tables.hands.CopyInputDecision(recognizer,offer.context.input.inputSequence,&decision));CHECK(decision.consumed&&decision.requestId==submittedRequest);ActionGrant grant{decision.epoch,decision.requestId,decision.grant,offer.offerId,offer.policyRevision,decision.inputId,activeHand,0};CHECK(DispatchSelected(offer,grant,&localRequest)==Status::Ok);return consumed;}
    bool Select(){CHECK(HiggsHands::Service().Input(activeHand,HIGGS_INPUT_GRIP,false,HiggsHands::Now())==false);return Edge(true);}
    static Status Validate(const ContentCommit& c,const ActionRequest& r)noexcept{HIGGS_ActionResult action{};action.structSize=sizeof(action);if(current->tables.hands.CopyActionResult(current->recognizer,c.higgsRequestId,&action))return Status::Unavailable;if(action.epoch!=c.epoch||action.executor!=r.offer.higgsExecutor||action.inputId!=r.grant.inputSequence)return Status::Conflict;if(c.stage==1)return action.stage==HIGGS_ACTION_ACCEPTED?Status::Ok:Status::Conflict;if(c.stage==2)return action.stage==HIGGS_ACTION_COMMITTED||action.stage==HIGGS_ACTION_COMPLETED?Status::Ok:Status::Conflict;if(c.stage==3)return action.stage==HIGGS_ACTION_COMPLETED?Status::Ok:Status::Conflict;if(c.stage==4)return action.stage==HIGGS_ACTION_REJECTED?Status::Ok:Status::Conflict;return action.stage==HIGGS_ACTION_CANCELLED?Status::Ok:Status::Conflict;}
    RuntimeRecord Slot(){RuntimeView view{sizeof(view),1};std::array<RuntimeRecord,kMaxInstances> rows{};CHECK(GetRuntimeApi().CopyRuntime(reader,rows.data(),kMaxInstances,&view)==Status::Ok);for(unsigned i=0;i<view.count;++i)if(rows[i].configured.instance.instanceHandle==instance)return rows[i];return {};}
    static bool Dispatch(const HIGGS_PhysicalRequest& request,void* p){auto& f=*static_cast<Fixture*>(p);if(request.kind==HIGGS_PHYSICAL_GRAB&&!f.allowGrab)return false;auto& h=f.observed[request.physicalHand-1];h.heldRefId=request.kind==HIGGS_PHYSICAL_GRAB?request.itemRefId:0;if(h.heldRefId){CHECK(f.engine.world.contains(h.heldRefId));HiggsHands::Transactions().ObserveHeld(request.physicalHand,h.heldRefId+100000,h.heldRefId,f.engine.world[h.heldRefId],1,HiggsHands::Now());}return true;}
    InventoryProviderProgress Pump(){HiggsHands::Inventory().Drain();HiggsHands::Transactions().Drain(HiggsHands::Now());HiggsHands::Service().DrainPhysical(Dispatch,this,HiggsHands::Now());Publish();HiggsHands::Transactions().Drain(HiggsHands::Now());return provider.Tick();}
    void Close(bool recovery=false){if(!recovery)CHECK(provider.Detach()==Status::Ok);else CHECK(provider.Detach()==Status::Conflict);CHECK(!tables.hands.UnregisterClient(consumer));CHECK(!tables.hands.UnregisterClient(recognizer));const auto* base=BodySlots_QueryInterface(1,sizeof(Api));CHECK(base->ReleaseClient(writer)==Status::Ok);CHECK(base->ReleaseClient(reader)==Status::Ok);HiggsHands::Inventory().Install(nullptr);SetContentCommitValidator(nullptr);}
};
void RoundTrip(){Fixture f(1);CHECK(f.Select());CHECK(f.provider.CopyProgress().stage==InventoryProviderStage::Queued);CHECK(f.provider.Tick().stage==InventoryProviderStage::Selecting&&f.engine.stock==3);HiggsHands::Inventory().Drain();auto p=f.provider.Tick();CHECK(p.stage==InventoryProviderStage::Transferring&&p.transfer!=p.higgsAction);HiggsHands::Transactions().Drain(HiggsHands::Now());p=f.provider.Tick();CHECK(p.stage==InventoryProviderStage::Transferring&&p.receipt.stage==HIGGS_TX_WAIT_GRIP&&f.engine.stock==2&&!f.observed[0].heldRefId&&f.Slot().contentVersion==0);HIGGS_ActionResult parent{};parent.structSize=sizeof(parent);CHECK(!f.tables.hands.CopyActionResult(f.consumer,p.higgsAction,&parent)&&parent.stage==HIGGS_ACTION_COMMITTED&&parent.executor==f.offer.higgsExecutor);for(unsigned i=0;i<5&&p.stage!=InventoryProviderStage::Completed;++i)p=f.Pump();CHECK(p.stage==InventoryProviderStage::Completed&&f.Slot().contentVersion==1&&f.Slot().quantity==2&&f.observed[0].heldRefId&&f.observed[1].heldRefId==77&&f.engine.withdrawals==1);
    CHECK(HiggsHands::Service().Input(Left,HIGGS_INPUT_GRIP,false,HiggsHands::Now()));f.observed[0].equippedFormId=200;f.Publish();f.Frame();CHECK(f.Select());CHECK(f.offer.context.input.itemIdentity==f.observed[0].heldRefId&&f.offer.context.input.contentCategory==Potion);p=f.provider.Tick();for(unsigned i=0;i<7&&p.stage!=InventoryProviderStage::Completed;++i)p=f.Pump();CHECK(p.stage==InventoryProviderStage::Completed&&f.engine.stock==3&&f.engine.stores==1&&f.Slot().quantity==3&&!f.observed[0].heldRefId&&f.observed[0].equippedFormId==200&&f.observed[1].heldRefId==77);CHECK(HiggsHands::Service().Input(Left,HIGGS_INPUT_GRIP,false,HiggsHands::Now()));f.Frame();CHECK(!f.Select()&&!f.offer.offerId);f.Close();}
void Refusal(){Fixture f(2);CHECK(f.Select());f.provider.Tick();f.engine.refuse=true;HiggsHands::Inventory().Drain();auto p=f.provider.Tick();CHECK(p.stage==InventoryProviderStage::Cancelled&&f.engine.withdrawals==0&&f.Slot().contentVersion==0);CHECK(HiggsHands::Service().Input(Left,HIGGS_INPUT_GRIP,false,HiggsHands::Now()));f.Close();}
void MissingAnchor(){Fixture f(3);CHECK(f.Select());CHECK(f.provider.Tick().stage==InventoryProviderStage::Selecting);f.hasAnchor=false;f.Frame();auto p=f.provider.CopyProgress();for(unsigned i=0;i<7&&p.stage!=InventoryProviderStage::Completed;++i)p=f.Pump();CHECK(p.stage==InventoryProviderStage::Completed&&f.engine.withdrawals==1&&f.Slot().availability!=0&&f.Slot().contentVersion==1);f.Close();}
void LostInstance(){Fixture f(4);CHECK(f.Select());f.provider.Tick();HiggsHands::Inventory().Drain();f.provider.Tick();HiggsHands::Transactions().Drain(HiggsHands::Now());f.provider.Tick();const auto* instances=BodySlots_QueryInstanceInterface(1,sizeof(InstanceApi));InstanceView view{sizeof(view),1};std::array<InstanceRecord,kMaxInstances> rows{};CHECK(instances->CopyEffectiveInstances(f.reader,0,0,rows.data(),kMaxInstances,&view)==Status::Ok);CHECK(instances->RemoveInstance(f.writer,f.instance,view.saveGeneration,rows[0].instanceRevision)==Status::Ok);auto p=f.provider.CopyProgress();for(unsigned i=0;i<7&&p.receipt.stage!=HIGGS_TX_COMPLETED;++i)p=f.Pump();CHECK(p.stage==InventoryProviderStage::RecoveryRequired&&p.receipt.stage==HIGGS_TX_COMPLETED&&p.receipt.disposition==HIGGS_DISPOSITION_HAND&&f.engine.withdrawals==1&&f.engine.compensations==0&&!f.Slot().configured.instance.instanceHandle);f.Close(true);}
void ResetDuringSelection(){Fixture f(5);CHECK(f.Select());f.provider.Tick();HiggsHands::Service().ObserveWorld(2005,false,HiggsHands::Now());f.epoch=HiggsHands::Service().Epoch();f.Publish();CHECK(ResetSaveScope()==Status::Ok);f.Frame();const auto p=f.provider.Tick();CHECK(p.stage==InventoryProviderStage::RecoveryRequired&&!f.engine.withdrawals&&f.Slot().contentVersion==0);f.Close(true);}
void CompensatedWithdrawal(){Fixture f(6);CHECK(f.Select());f.provider.Tick();f.allowGrab=false;auto p=f.provider.CopyProgress();for(unsigned i=0;i<8&&p.stage!=InventoryProviderStage::Cancelled;++i)p=f.Pump();CHECK(p.stage==InventoryProviderStage::Cancelled&&p.receipt.stage==HIGGS_TX_COMPLETED&&(p.receipt.receiptFlags&HIGGS_TX_COMPENSATION_CONFIRMED)&&p.receipt.disposition==HIGGS_DISPOSITION_INVENTORY&&f.engine.stock==3&&f.engine.withdrawals==1&&f.engine.compensations==1&&f.Slot().contentVersion==0);f.Close();}
void UncertainMutation(){Fixture f(7);CHECK(f.Select());f.provider.Tick();f.engine.uncertain=true;auto p=f.Pump();p=f.provider.Tick();HiggsHands::Transactions().Drain(HiggsHands::Now());p=f.provider.Tick();CHECK(f.engine.withdrawals==1&&f.engine.stock==2);f.provider.Cancel();p=f.provider.Tick();CHECK(p.stage==InventoryProviderStage::RecoveryRequired&&p.receipt.stage==HIGGS_TX_RECOVERY_REQUIRED&&(p.receipt.receiptFlags&HIGGS_TX_MUTATION_UNCERTAIN)&&f.Slot().contentVersion==0);for(unsigned i=0;i<3;++i)f.provider.Tick();CHECK(f.engine.withdrawals==1&&f.provider.Detach()==Status::Conflict);
    // The engine/domain owner, not the helper, reconciles actual restored stock.
    ++f.engine.stock;++f.engine.revision;f.engine.world.clear();f.engine.uncertain=false;HIGGS_HandExecutor nativeExecutor=0;CHECK(!f.tables.inventory.GetExecutor(f.consumer,&nativeExecutor));HIGGS_TransferReceipt receipt{};receipt.structSize=sizeof(receipt);receipt.kind=HIGGS_TX_RECONCILED;receipt.requestId=p.transfer;receipt.executor=nativeExecutor;receipt.epoch=p.receipt.epoch;receipt.original=p.receipt.original;receipt.quantity=p.receipt.quantity;receipt.instancePreserved=1;receipt.disposition=HIGGS_DISPOSITION_INVENTORY;CHECK(!f.tables.transactions.ReportReceipt(f.consumer,&receipt));p=f.provider.Tick();CHECK(p.stage==InventoryProviderStage::Cancelled&&(p.receipt.receiptFlags&HIGGS_TX_RECOVERY_RECONCILED)&&f.Slot().contentVersion==0&&f.engine.withdrawals==1);f.Close();}
void ResetAfterCommit(){Fixture f(8);CHECK(f.Select());f.provider.Tick();HiggsHands::Inventory().Drain();f.provider.Tick();HiggsHands::Transactions().Drain(HiggsHands::Now());auto p=f.provider.Tick();CHECK(p.receipt.stage==HIGGS_TX_WAIT_GRIP&&f.engine.stock==2&&f.engine.withdrawals==1);const auto old=f.Slot().configured.instance;HiggsHands::Service().ObserveWorld(2008,false,HiggsHands::Now());f.epoch=HiggsHands::Service().Epoch();f.observed[0].heldRefId=0;f.Publish();CHECK(ResetSaveScope()==Status::Ok);
    const auto* api=BodySlots_QueryInstanceInterface(1,sizeof(InstanceApi));InstanceView view{sizeof(view),1};CHECK(api->CopyEffectiveInstances(f.reader,0,0,nullptr,0,&view)==Status::Ok);InstanceSpec spec{};spec.structSize=sizeof(spec);spec.flags=kInstanceActive;spec.binding=1;std::strcpy(spec.equipmentKey,old.key.equipmentKey);std::strcpy(spec.slotKey,old.key.slotKey);std::strcpy(spec.definitionProvider,old.definitionProvider);std::strcpy(spec.definitionKey,old.definitionKey);spec.definitionRevision=old.pinnedDefinitionRevision;InstanceRecord recreated{};recreated.structSize=sizeof(recreated);CHECK(api->UpsertInstance(f.writer,&spec,view.saveGeneration,0,&recreated)==Status::Ok&&recreated.instanceHandle!=old.instanceHandle);f.instance=recreated.instanceHandle;f.Frame();p=f.provider.Tick();CHECK(p.stage==InventoryProviderStage::RecoveryRequired&&p.receipt.stage==HIGGS_TX_RECOVERY_REQUIRED&&(p.receipt.receiptFlags&HIGGS_TX_DOMAIN_ATTESTED)&&f.engine.withdrawals==1&&f.engine.compensations==0&&f.Slot().contentVersion==0);f.Close(true);}
void ExactHeldIdentity(){
    unsigned index=40;
    for(const auto edge:{HIGGS_INPUT_DOWN,HIGGS_INPUT_UP})for(const auto hand:{Left,Right})for(unsigned mode=0;mode<3;++mode){Fixture f(index++,edge,hand);f.engine.world[900]=100;f.observed[hand-1].heldRefId=900;f.Publish();const auto offered=HiggsHands::Transactions().ObserveHeld(hand,500001,900,100,1,HiggsHands::Now());f.Frame();
        if(edge==HIGGS_INPUT_UP){CHECK(!f.Edge(true));CHECK(!f.offer.offerId&&f.provider.CopyProgress().stage==InventoryProviderStage::Ready&&f.engine.stores==0);CHECK(f.Edge(false));CHECK(f.offer.context.input.edge==HIGGS_INPUT_UP);}else CHECK(f.Select());
        CHECK(f.provider.CopyProgress().stage==InventoryProviderStage::Queued&&f.engine.stores==0);
        if(mode==1){const auto replacement=HiggsHands::Transactions().ObserveHeld(hand,500002,900,100,1,HiggsHands::Now());CHECK(replacement!=offered);}
        if(mode==2){CHECK(HiggsHands::Transactions().ObserveHeld(hand,500001,900,101,1,HiggsHands::Now())==offered);const auto changed=HiggsHands::Transactions().ObserveHeld(hand,500001,900,100,1,HiggsHands::Now());CHECK(changed==offered);}
        auto p=f.provider.Tick();for(unsigned i=0;i<8&&p.stage!=InventoryProviderStage::Completed&&p.stage!=InventoryProviderStage::Cancelled;++i)p=f.Pump();
        if(mode==0)CHECK(p.stage==InventoryProviderStage::Completed&&p.receipt.original.instance==offered&&p.receipt.disposition==HIGGS_DISPOSITION_INVENTORY&&(p.receipt.receiptFlags&HIGGS_TX_NATIVE_RELEASE_CONFIRMED)&&f.engine.stores==1&&f.engine.stock==4&&!f.observed[hand-1].heldRefId&&f.Slot().contentVersion==1);
        else CHECK(p.stage==InventoryProviderStage::Cancelled&&!p.transfer&&f.engine.stores==0&&f.engine.withdrawals==0&&f.engine.stock==3&&f.observed[hand-1].heldRefId==900&&f.Slot().contentVersion==0);
        f.Close();
    }
}
void UnselectedRelease(){
    for(unsigned mode=0;mode<2;++mode){Fixture f(60+mode,HIGGS_INPUT_UP);if(!mode){f.engine.world[900]=100;f.observed[0].heldRefId=900;f.Publish();HiggsHands::Transactions().ObserveHeld(Left,500001,900,100,1,HiggsHands::Now());}
        f.policyEnabled=false;CHECK(!f.Edge(true));CHECK(!f.offer.offerId);
        // An empty-hand Up must not turn into a withdrawal even when policy offers it.
        if(mode)f.policyEnabled=true;
        CHECK(!f.Edge(false));CHECK(!f.offer.offerId&&f.provider.CopyProgress().stage==InventoryProviderStage::Ready);
        f.Pump();CHECK(f.engine.stores==0&&f.engine.withdrawals==0&&f.engine.selects==0&&f.Slot().contentVersion==0);f.Close();
    }
}
void ConsumedWithdrawalRelease(){
    Fixture f(62,HIGGS_INPUT_UP);CHECK(f.Select());auto p=f.provider.CopyProgress();
    for(unsigned i=0;i<8&&p.stage!=InventoryProviderStage::Completed;++i)p=f.Pump();
    CHECK(p.stage==InventoryProviderStage::Completed&&f.engine.withdrawals==1&&f.engine.stores==0&&f.engine.stock==2&&f.observed[0].heldRefId&&f.Slot().contentVersion==1);
    // The helper may offer storage, but the real broker rejects the Up owned by
    // that consumed withdrawal Down, even after its original grant was released.
    CHECK(f.Edge(false,false));CHECK(f.offer.offerId&&f.offer.context.input.edge==HIGGS_INPUT_UP&&f.submitStatus==HIGGS_HAND_CONFLICT&&!f.submittedRequest);
    CHECK(f.provider.CopyProgress().stage==InventoryProviderStage::Completed);
    for(unsigned i=0;i<3;++i)f.Pump();
    CHECK(f.engine.withdrawals==1&&f.engine.stores==0&&f.engine.stock==2&&f.Slot().contentVersion==1);
    // A later native/unconsumed grip sequence while holding is a different input.
    CHECK(!f.Edge(true));CHECK(!f.offer.offerId);CHECK(f.Edge(false));p=f.provider.CopyProgress();
    for(unsigned i=0;i<8&&p.stage!=InventoryProviderStage::Completed;++i)p=f.Pump();
    CHECK(p.stage==InventoryProviderStage::Completed&&f.engine.withdrawals==1&&f.engine.stores==1&&f.engine.stock==3&&!f.observed[0].heldRefId&&f.Slot().contentVersion==2);f.Close();
}
void InvalidStoreEdge(){
    Fixture f(63);CHECK(f.provider.Detach()==Status::Ok);
    InventoryProviderConfig config{f.writer,f.instance,f.consumer,Both,1000,0,Fixture::Policy,&f};
    CHECK(config.storeEdge==HIGGS_INPUT_DOWN);
    for(const auto invalid:{0u,3u}){config.storeEdge=invalid;CHECK(f.provider.Attach(f.tables,config)==Status::InvalidArgument);CHECK(f.provider.CopyProgress().stage==InventoryProviderStage::Detached);}
    f.Close();
}
void MenuAcrossWithdrawal(){
    for(unsigned mode=0;mode<2;++mode){Fixture f(20+mode);CHECK(f.Select());
        CHECK(f.provider.Tick().stage==InventoryProviderStage::Selecting);
        if(mode==1){HiggsHands::Inventory().Drain();f.provider.Tick();
            HiggsHands::Transactions().Drain(HiggsHands::Now());const auto p=f.provider.Tick();
            CHECK(p.receipt.stage==HIGGS_TX_WAIT_GRIP&&f.engine.withdrawals==1&&f.engine.stock==2&&
                  f.Slot().contentVersion==0&&!f.observed[0].heldRefId);}
        HiggsHands::Service().SetInputContext(1,HiggsHands::Now());
        auto p=f.provider.CopyProgress();for(unsigned i=0;i<10&&p.stage!=InventoryProviderStage::Cancelled;++i)p=f.Pump();
        CHECK(p.stage==InventoryProviderStage::Cancelled&&f.Slot().contentVersion==0&&f.engine.stock==3&&
              f.engine.withdrawals==mode&&f.engine.compensations==mode&&!f.observed[0].heldRefId&&f.engine.stores==0);
        if(mode==1)CHECK((p.receipt.receiptFlags&HIGGS_TX_COMPENSATION_CONFIRMED)&&
                        p.receipt.disposition==HIGGS_DISPOSITION_INVENTORY);
        // Closing UI cannot resume the old action or perform a second withdrawal.
        HiggsHands::Service().SetInputContext(0,HiggsHands::Now());
        for(unsigned i=0;i<3;++i)f.Pump();
        CHECK(f.engine.stock==3&&f.engine.withdrawals==mode&&f.engine.compensations==mode&&f.Slot().contentVersion==0);
        HiggsHands::Service().Input(Left,HIGGS_INPUT_GRIP,false,HiggsHands::Now());f.Close();
    }
}
}
int main(){try{RoundTrip();Refusal();MissingAnchor();LostInstance();ResetDuringSelection();CompensatedWithdrawal();UncertainMutation();ResetAfterCommit();ExactHeldIdentity();UnselectedRelease();ConsumedWithdrawalRelease();InvalidStoreEdge();MenuAcrossWithdrawal();std::cout<<"PASS ordinary public inventory provider "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
