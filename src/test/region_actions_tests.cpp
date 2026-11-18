// Production Body Slots recognition/maintenance + actual HIGGS service closure.
// Only native observations and consumer effects are scripted; no copied service.
#include "main/region_actions.h"
#include "main/interaction_regions.h"
#include "main/owned_region_sources.h"
#include "instances.h"
#include "hand_broker.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace body_slots;
using namespace body_slots::detail;
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
namespace {
unsigned checks=0;
#define CHECK(c) do {++checks;if(!(c))throw std::runtime_error(std::to_string(__LINE__)+": " #c);}while(false)
struct Fixture;
struct Provider {
    Fixture* fixture{};
    std::uint64_t writer{},client{},anchorRevision{};
    InteractionRegionRecord region{};
    RegionAnchorSnapshot anchor{};
    RegionActionRegistration registration{sizeof(registration),0,0,0,0};
    std::int32_t priority{};
    std::uint32_t resources=Left;
    unsigned effects{},offers{},mode{},terminals{};
    RegionInputTerminal terminal{};
    bool enabled=true;
    RegionActionExecution execution{};
    std::mutex blockMutex;
    std::condition_variable blockCv;
    bool entered=false,proceed=false;
};
struct Fixture {
    static Fixture* active;
    HiggsHands::Broker broker;
    const Api& registry=*BodySlots_QueryInterface(1,sizeof(Api));
    const RegionActionsApi& api=GetRegionActionsApi();
    HIGGS_HandInterface hands{};
    HIGGS_HandRoutingInterface routing{};
    HIGGS_AuthorizedDispatchInterface dispatch{};
    std::array<HIGGS_HandRecord,2> observations{};
    std::array<std::unique_ptr<Provider>,2> providers;
    std::vector<std::uint64_t> submittedActions;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> dwellRequests;
    HIGGS_HandSnapshot snapshot{};
    unsigned fallback{},beforeMode{},invokeCalls{};
    HIGGS_HandStatus invokeStatus=999;
    void (*beforeDispatch)(){};
    bool sequenceUnavailable=false;
    explicit Fixture(std::int32_t providerPriorityB=1,std::uint32_t edgeMask=1,bool sharedOwner=false,bool sequences=false,
        RegionAnchorKind kind=RegionAnchorKind::World,bool noButtonUses=false) {
        CHECK(DisconnectRegionActions()==Status::Ok);active=this;
        hands.structSize=sizeof(hands);hands.abiVersion=HIGGS_HAND_ABI_VERSION;
        hands.capabilities=HIGGS_HAND_CAP_SNAPSHOT|HIGGS_HAND_CAP_INPUT_BROKER|HIGGS_HAND_CAP_OWNED_GRANTS|HIGGS_HAND_CAP_EXECUTOR_RECEIPTS;
        hands.RegisterClient=+[](const HIGGS_HandClientInfo* i,std::uint64_t* o){return active->broker.Register(*i,*o);};
        hands.UnregisterClient=+[](std::uint64_t c){return active->broker.Unregister(c);};
        hands.CopySnapshot=+[](std::uint64_t c,HIGGS_HandSnapshot* o){return active->broker.Copy(c,*o,HiggsHands::Now());};
        hands.SubscribeRecognizer=+[](std::uint64_t c,HIGGS_RecognizerCallback cb,void* ctx,std::uint64_t* o){return active->broker.Recognizer(c,cb,ctx,*o);};
        hands.RegisterExecutor=+[](std::uint64_t c,HIGGS_ExecutorCallback cb,void* ctx,std::uint64_t* o){return active->broker.Executor(c,cb,ctx,*o);};
        hands.RemoveRegistration=+[](std::uint64_t c,std::uint64_t r){return active->broker.Remove(c,r);};
        hands.SubmitCandidate=+[](std::uint64_t c,const HIGGS_ActionCandidate* v,std::uint64_t* o){
            active->submittedActions.push_back(v->actionId);return active->broker.Submit(c,*v,*o,HiggsHands::Now());};
        hands.CopyActionResult=+[](std::uint64_t c,std::uint64_t r,HIGGS_ActionResult* o){return active->broker.Result(c,r,*o);};
        routing={sizeof(routing),HIGGS_HAND_ROUTING_ABI_VERSION,1,
            +[](std::uint64_t c,std::uint64_t e,std::uint64_t r,std::uint32_t h){return active->broker.Authorize(c,e,active->broker.ClientForLabel(r),h);},
            +[](std::uint64_t c,std::uint64_t e,std::uint64_t r){return active->broker.Revoke(c,e,active->broker.ClientForLabel(r));},
            +[](std::uint64_t c,std::uint64_t e,std::uint32_t h){return active->broker.CanRoute(c,e,h);},
            +[](std::uint64_t c,std::uint64_t r,std::uint64_t e,std::uint32_t why){return active->broker.CancelRecognized(c,r,e,why);}};
        dispatch={sizeof(dispatch),HIGGS_AUTHORIZED_DISPATCH_ABI_VERSION,+[](std::uint64_t owner,const HIGGS_AuthorizedDispatchRequest* r,HIGGS_AuthorizedDispatchCallback cb,void* ctx){
            auto& f=*active;++f.invokeCalls;auto* p=f.ProviderFor(r->executor);
            if(f.beforeMode==1){p->anchor.world.position[0]=100;f.Publish(*p);}
            if(f.beforeMode==2){++p->anchor.source.generation;f.Publish(*p);}
            if(f.beforeMode==3){f.observations[0].heldRefId=0xff;f.Frame(false);}
            if(f.beforeMode==4){f.broker.SetInputContext(1,HiggsHands::Now());f.broker.SetInputContext(0,HiggsHands::Now());}
            if(f.beforeMode==5)CHECK(f.api.RemoveBinding(p->writer,p->registration.bindingId)==Status::Ok);
            if(f.beforeMode==6)CHECK(ResetSaveScope()==Status::Ok);
            if(f.beforeMode==7)ResetRegionActions();
            if(f.beforeMode==8)CHECK(f.registry.ReleaseClient(p->writer)==Status::Ok);
            if(f.beforeMode==9){++p->anchor.source.sampleId;CHECK(f.api.PublishAnchor(p->writer,p->registration.bindingId,p->anchorRevision,&p->anchor,&p->anchorRevision)==Status::Conflict);}
            if(f.beforeMode==10){f.observations[0].equippedFormId=0xfa;f.Frame(false);}
            if(f.beforeDispatch)f.beforeDispatch();
            f.invokeStatus=f.broker.InvokeAuthorized(owner,*r,cb,ctx);return f.invokeStatus;
        }};
        const HIGGS_InputRoutesInterface routes{sizeof(routes),1,sequences?HIGGS_ROUTE_TERMINALS:0u,nullptr,
            +[](std::uint64_t c,std::uint64_t press,HIGGS_InputSequence* result){
                return active->sequenceUnavailable ? HIGGS_HAND_NOT_FOUND : active->broker.CopySequence(c,press,*result);},
            +[](std::uint64_t c,std::uint64_t id,std::uint64_t epoch,std::uint32_t mask,std::uint64_t* corr) {
                return active->broker.CheckSelectedAction(c,id,epoch,mask,*corr,HiggsHands::Now());
            },
            +[](std::uint64_t c,std::uint64_t* label)->HIGGS_HandStatus{
                *label=active->broker.ClientLabel(c);return *label?HIGGS_HAND_OK:HIGGS_HAND_INVALID_CLIENT;
            }};
        DwellProvider dwell{};
        dwell.proposals.proposals={sizeof(dwell.proposals),2,HIGGS_HAND_USE_PROPOSALS_CAP_PRIORITY_ROUNDS|HIGGS_HAND_USE_PROPOSALS_CAP_SHARED_WINDOWS,
            +[](std::uint64_t owner,const HIGGS_HandUseProposalRequest* request,std::uint64_t* id) {
                const auto result=active->broker.ProposeHandUse(owner,*request,*id,HiggsHands::Now());
                if(result==HIGGS_HAND_OK)active->dwellRequests.emplace_back(owner,*id);
                return result;
            },
            +[](std::uint64_t owner,std::uint64_t id,HIGGS_HandUseProposalState* out) {
                return active->broker.CopyHandUseProposal(owner,id,*out,HiggsHands::Now());
            },
            +[](std::uint64_t owner,std::uint64_t id,std::uint64_t epoch) {
                return active->broker.CancelHandUseProposal(owner,id,epoch);
            }};
        dwell.proposals.EnableSharedRounds=+[](std::uint64_t client){return active->broker.EnableSharedRounds(client);};
        dwell.proposals.ProposeOrdered=+[](std::uint64_t owner,const HIGGS_HandUseProposalRequest* request,
            std::uint64_t action,std::uint64_t target,std::uint64_t* id) {
            const auto result=active->broker.ProposeOrderedHandUse(owner,*request,action,target,*id,HiggsHands::Now());
            if(result==HIGGS_HAND_OK)active->dwellRequests.emplace_back(owner,*id);
            return result;
        };
        dwell.uses={sizeof(dwell.uses),1,1,nullptr,nullptr,nullptr,
            +[](std::uint64_t owner,std::uint64_t id,std::uint64_t epoch){return active->broker.EndHandUse(owner,id,epoch);}};
        dwell.contexts={sizeof(dwell.contexts),HIGGS_INPUT_CONTEXT_ABI_VERSION,1,
            +[](std::uint64_t owner,HIGGS_InputContextState* ctx){return active->broker.CopyInputContext(owner,*ctx);},nullptr,nullptr};
        dwell.dispatch=dispatch;
        CHECK(ConnectRegionActions(hands,routing,dispatch,&routes,noButtonUses?&dwell:nullptr)==Status::Ok);
        for(auto& h:observations)h.observationMask=HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN|HIGGS_OBS_PALM_VALID|HIGGS_OBS_TRACKING_VALID;
        broker.MarkMainThread();broker.ObserveWorld(1,false,HiggsHands::Now());Frame(false);
        for(unsigned i=0;i<2;++i) {
            auto p=std::make_unique<Provider>();p->fixture=this;p->priority=i?100:10;
            char name[32]{};std::snprintf(name,sizeof(name),"region-provider-%u",i);
            CHECK(registry.RegisterClient(name,&p->writer)==Status::Ok);
            HIGGS_HandClientInfo info{};info.structSize=sizeof(info);std::strcpy(info.name,name);info.userPriority=i?providerPriorityB:0;
            if(i && sharedOwner)p->client=providers[0]->client;
            else CHECK(broker.Register(info,p->client)==HIGGS_HAND_OK);
            InteractionRegionSpec spec{};spec.structSize=sizeof(spec);spec.anchorKind=static_cast<std::uint32_t>(kind);
            std::strcpy(spec.provider,name);std::strcpy(spec.key,"arbitrary-zone");spec.shape.structSize=sizeof(spec.shape);spec.shape.kind=1;spec.shape.radius=5;
            spec.maxSampleAgeSeconds=.1;spec.maxSampleSkewSeconds=.1;spec.maxContinuityGapSeconds=.1;
            CHECK(GetRegionsApi().UpsertRegion(p->writer,&spec,0,&p->region)==Status::Ok);
            RegionActionBindingSpec binding{sizeof(binding),Both,2,edgeMask,p->region.regionId,p->region.revision,p->client,Offers,Execute,p.get()};
            CHECK(api.RegisterBinding(p->writer,&binding,&p->registration)==Status::Ok);
            p->anchor.structSize=sizeof(p->anchor);p->anchor.kind=spec.anchorKind;p->anchor.valid=1;
            auto& src=p->anchor.source;std::strcpy(src.provider,name);std::strcpy(src.source,"world-target");src.identity=i+1;src.generation=1;
            src.units=static_cast<std::uint32_t>(RegionUnits::SkyrimWorld);p->anchor.world.scale=1;p->anchor.world.rotation[0]=p->anchor.world.rotation[4]=p->anchor.world.rotation[8]=1;
            providers[i]=std::move(p);Publish(*providers[i]);
        }
        TickRegionActions();
    }
    ~Fixture() {
        DisconnectRegionActions();
        for(auto& p:providers)if(p){registry.ReleaseClient(p->writer);broker.Unregister(p->client);}
        active=nullptr;
    }
    Provider* ProviderFor(std::uint64_t executor) {for(auto& p:providers)if(p && p->registration.higgsExecutor==executor)return p.get();return nullptr;}
    void Frame(bool targets=true,bool tick=true) {
        broker.Publish(observations,HiggsHands::Now());snapshot=broker.CopyPublished();if(tick)TickRegionActions();
        if(targets)for(auto& p:providers)if(p)Publish(*p);
    }
    void Publish(Provider& p) {
        auto& a=p.anchor.source;++a.sampleId;a.frame+=7; // own target frame domain, never rewritten to HIGGS's
        a.epoch=snapshot.epoch;a.saveGeneration=CurrentSaveGeneration();a.timeSeconds=double(HiggsHands::Now())/1e9;
        CHECK(api.PublishAnchor(p.writer,p.registration.bindingId,p.anchorRevision,&p.anchor,&p.anchorRevision)==Status::Ok);
    }
    bool Input(bool down=true,unsigned button=2) {
        const auto consumed=broker.Input(Left,button,down,HiggsHands::Now());if(!consumed)++fallback;return consumed;
    }
    unsigned Effects() const {return providers[0]->effects+providers[1]->effects;}
    void Settle(Provider& p) {
        const auto& r=p.execution.request;HIGGS_ActionResult result{};result.structSize=sizeof(result);result.stage=HIGGS_ACTION_COMPLETED;
        result.requestId=r.requestId;result.executor=r.executor;result.epoch=r.epoch;result.inputId=r.inputId;
        CHECK(broker.Report(p.client,result)==HIGGS_HAND_OK);CHECK(broker.Release(p.client,r.grant)==HIGGS_HAND_OK);
    }
    static Status BS_CALL Offers(void* context,const RegionActionContext* c,RegionActionProposal* out,std::uint32_t cap,std::uint32_t* count) {
        auto& p=*static_cast<Provider*>(context);++p.offers;
        CHECK(c->input.poseObservationId==c->observation.probe.source.sampleId);
        CHECK(!std::strcmp(c->observation.probe.source.provider,"HIGGS"));
        if(p.mode==4) {
            std::unique_lock lock(p.blockMutex);p.entered=true;p.blockCv.notify_all();p.blockCv.wait(lock,[&]{return p.proceed;});
        }
        if(p.mode==5){CHECK(p.fixture->api.RemoveBinding(p.writer,p.registration.bindingId)==Status::Unavailable);*count=0;return Status::Ok;}
        if(p.mode==6){CHECK(p.fixture->registry.ReleaseClient(p.writer)==Status::Unavailable);*count=0;return Status::Ok;}
        if(!p.enabled){*count=0;return Status::Ok;}
        if(!cap)return Status::BufferTooSmall;
        out[0]={sizeof(RegionActionProposal),p.resources,p.priority,1000,p.region.regionId,0};*count=1;return Status::Ok;
    }
    static HIGGS_HandStatus HIGGS_HAND_CALL Nested(const HIGGS_ActionRequest*,const HIGGS_HandSnapshot*,void*) {throw std::runtime_error("duplicate invoked");}
    static Status BS_CALL Execute(void* context,const RegionActionExecution* e) {
        auto& p=*static_cast<Provider*>(context);auto& f=*p.fixture;p.execution=*e;++p.effects;
        CHECK(e->request.owner==p.client && e->request.executor==p.registration.higgsExecutor);
        HIGGS_ActionResult result{};CHECK(f.broker.Result(p.client,e->request.requestId,result)==HIGGS_HAND_OK);CHECK(result.stage==HIGGS_ACTION_ACCEPTED);
        TickRegionActions(); // same main phase callback cannot recursively dispatch
        const HIGGS_AuthorizedDispatchRequest duplicate{sizeof(duplicate),0,e->request.requestId,e->request.executor,e->request.epoch,e->request.grant};
        CHECK(f.broker.InvokeAuthorized(p.client,duplicate,Nested,nullptr)==HIGGS_HAND_CONFLICT);
        if(p.mode==1)return Status::Unavailable; // an effect happened, no inferred cancellation
        if(p.mode==2)throw std::runtime_error("consumer failed after effect");
        if(p.mode==3)CHECK(f.api.RemoveBinding(p.writer,p.registration.bindingId)==Status::Unavailable);
        return Status::Ok;
    }
};
Fixture* Fixture::active=nullptr;
void ArbitrationAndFallback() {
    Fixture f(0,1,true);auto& a=*f.providers[0];auto& b=*f.providers[1];
    CHECK(f.Input());CHECK(!f.Effects());CHECK(f.submittedActions.size()==2);CHECK(f.submittedActions[0]==b.region.regionId);
    // Equal provider priorities are resolved by HIGGS's stable action key.
    // Changing local offer order cannot change the configured HIGGS winner.
    TickRegionActions();CHECK(a.effects==1 && b.effects==0);
    TickRegionActions();CHECK(f.Effects()==1);f.Settle(a);
    CHECK(f.Input(false));TickRegionActions();CHECK(f.Effects()==1);
    b.priority=-10;f.Frame();CHECK(f.Input());TickRegionActions();CHECK(a.effects==2);f.Settle(a);CHECK(f.Input(false));
    a.enabled=b.enabled=false;f.Frame();CHECK(!f.Input());TickRegionActions();CHECK(f.fallback==1);CHECK(f.Effects()==2);
    CHECK(!f.Input(false));CHECK(!f.Input(true,1));CHECK(f.Effects()==2);
}
void ProviderPriorityAndUp() {
    {Fixture f(-100);auto& a=*f.providers[0];CHECK(f.Input());TickRegionActions();CHECK(a.effects==1);CHECK(!f.providers[1]->effects);}
    {Fixture f(100);CHECK(f.Input());TickRegionActions();CHECK(f.providers[1]->effects==1);CHECK(!f.providers[0]->effects);}
    {Fixture f(1,2);CHECK(!f.Input());CHECK(f.Input(false));TickRegionActions();CHECK(f.providers[1]->effects==1);}
}
void TargetRefreshAndHeld() {
    Fixture f;auto& b=*f.providers[1];f.observations[0].heldRefId=0xa0;f.Frame();
    CHECK(f.Input());const auto offerRevision=b.anchorRevision;
    f.Frame(true,false);CHECK(b.anchorRevision>offerRevision);TickRegionActions();CHECK(b.effects==1);
    CHECK(b.execution.context.anchorRevision==offerRevision);CHECK(b.execution.context.observation.anchor.source.sampleId==b.anchor.source.sampleId);
    CHECK(b.execution.currentSnapshot.hands[0].heldRefId==0xa0);
}
void CancelBeforeEffect() {
    for(unsigned mode=1;mode<=10;++mode) {
        Fixture f;CHECK(f.Input());f.beforeMode=mode;TickRegionActions();CHECK(f.Effects()==0);TickRegionActions();CHECK(f.Effects()==0);
    }
    {Fixture f;CHECK(f.Input());auto& b=*f.providers[1];auto changed=b.region.spec;changed.shape.radius=8;
        CHECK(GetRegionsApi().UpsertRegion(b.writer,&changed,b.region.revision,&b.region)==Status::Ok);TickRegionActions();CHECK(!f.Effects());}
    {Fixture f;CHECK(f.Input());f.broker.ObserveWorld(2,false,HiggsHands::Now());f.Frame(false);CHECK(!f.Effects());}
    {Fixture f;CHECK(f.Input());f.broker.SetInputContext(1,HiggsHands::Now());f.broker.SetInputContext(0,HiggsHands::Now());TickRegionActions();CHECK(!f.Effects());}
}
void ConsumerFailureAndDrain() {
    for(unsigned mode=1;mode<=2;++mode) {
        Fixture f;auto& b=*f.providers[1];b.mode=mode;CHECK(f.Input());TickRegionActions();CHECK(b.effects==1);
        HIGGS_ActionResult result{};CHECK(!f.broker.Result(b.client,b.execution.request.requestId,result));CHECK(result.stage==HIGGS_ACTION_ACCEPTED);
        TickRegionActions();CHECK(b.effects==1);f.Settle(b);
    }
    {Fixture f;auto& b=*f.providers[1];b.mode=3;CHECK(f.Input());TickRegionActions();CHECK(b.effects==1);
        CHECK(f.api.RemoveBinding(b.writer,b.registration.bindingId)==Status::Ok);}
    {Fixture f;auto& b=*f.providers[1];b.mode=5;CHECK(f.Input());TickRegionActions();CHECK(f.providers[0]->effects==1);
        CHECK(f.api.RemoveBinding(b.writer,b.registration.bindingId)==Status::Ok);}
    {Fixture f;auto& a=*f.providers[0];auto& b=*f.providers[1];a.enabled=false;b.mode=4;
        std::thread input([&]{f.Input();});
        {std::unique_lock lock(b.blockMutex);b.blockCv.wait(lock,[&]{return b.entered;});}
        std::atomic<bool> drained=false;Status removal=Status::InternalError;
        std::thread remove([&]{removal=f.api.RemoveBinding(b.writer,b.registration.bindingId);drained=true;});
        std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(!drained.load());
        {std::lock_guard lock(b.blockMutex);b.proceed=true;b.blockCv.notify_all();}
        input.join();remove.join();CHECK(removal==Status::Ok);TickRegionActions();CHECK(!f.Effects());
    }
    {Fixture f;auto& b=*f.providers[1];b.mode=6;CHECK(f.Input());TickRegionActions();CHECK(f.providers[0]->effects==1);
        CHECK(RuntimeClientIdentity(b.writer,true,nullptr)==Status::Ok); // release rolled back while callback was entered
        CHECK(f.registry.ReleaseClient(b.writer)==Status::Ok);}
    {Fixture f;auto& a=*f.providers[0];auto& b=*f.providers[1];a.enabled=false;b.mode=4;
        std::thread input([&]{f.Input();});
        {std::unique_lock lock(b.blockMutex);b.blockCv.wait(lock,[&]{return b.entered;});}
        std::atomic<bool> drained=false;Status release=Status::InternalError;
        std::thread remove([&]{release=f.registry.ReleaseClient(b.writer);drained=true;});
        std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(!drained.load());
        {std::lock_guard lock(b.blockMutex);b.proceed=true;b.blockCv.notify_all();}
        input.join();remove.join();CHECK(release==Status::Ok);TickRegionActions();CHECK(!f.Effects());
        CHECK(RuntimeClientIdentity(b.writer,true,nullptr)==Status::Unauthorized);
    }
}
void ScopeAndEvidence() {
    Fixture f;auto& b=*f.providers[1];RegionActionsIntegration i{sizeof(i),1};CHECK(f.api.CopyIntegration(b.writer,&i)==Status::Ok);CHECK(i.connected&&i.epoch);
    CHECK(i.recognizerClient==b.registration.recognizerClient);
    CHECK(f.broker.Unregister(i.recognizerClient)==HIGGS_HAND_INVALID_CLIENT);
    auto forged=b.anchor;forged.reserved=1;
    CHECK(f.api.PublishAnchor(b.writer,b.registration.bindingId,b.anchorRevision,&forged,&b.anchorRevision)==Status::InvalidArgument);
    f.providers[0]->enabled=false;CHECK(!f.Input());CHECK(!f.Effects());f.Input(false);f.Publish(b);
    ResetRegionActions(); // old epoch is barred until the provider advances it
    f.Frame();CHECK(!f.Input());CHECK(f.api.CopyIntegration(b.writer,&i)==Status::Ok);CHECK(!i.connected&&!i.epoch);
    f.broker.ObserveWorld(2,false,HiggsHands::Now());f.Frame();CHECK(f.Input());TickRegionActions();CHECK(b.effects==1);f.Settle(b);
    f.broker.ObserveWorld(0,true,HiggsHands::Now());TickRegionActions();CHECK(f.api.CopyIntegration(b.writer,&i)==Status::Ok);CHECK(!i.connected&&!i.epoch);
}
void DwellAndOrdering() {
    Fixture f;auto& a=*f.providers[0];auto& b=*f.providers[1];a.enabled=false;
    CHECK(f.api.RemoveBinding(b.writer,b.registration.bindingId)==Status::Ok);
    auto spec=b.region.spec;spec.dwellSeconds=.001;CHECK(GetRegionsApi().UpsertRegion(b.writer,&spec,b.region.revision,&b.region)==Status::Ok);
    RegionActionBindingSpec binding{sizeof(binding),Both,2,1,b.region.regionId,b.region.revision,b.client,Fixture::Offers,Fixture::Execute,&b};
    b.registration={sizeof(b.registration),0,0,0,0};CHECK(f.api.RegisterBinding(b.writer,&binding,&b.registration)==Status::Ok);b.anchorRevision=0;f.Publish(b);
    TickRegionActions();CHECK(!f.Input());f.Input(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));f.Frame(false); // held target sample remains genuine and fresh
    f.Publish(b); // target arrives after BS Tick; the same palm must retain earned dwell
    CHECK(f.Input());TickRegionActions();CHECK(b.effects==1);
}
void RequiredResourcesAndAge() {
    {Fixture f;auto& b=*f.providers[1];b.resources=Both;CHECK(f.Input());TickRegionActions();CHECK(b.effects==1);
        CHECK(b.execution.request.physicalHands==Both);CHECK(b.execution.currentSnapshot.hands[1].grant!=0 && b.execution.currentSnapshot.hands[1].grant!=b.execution.request.grant);}
    {Fixture f;f.providers[0]->enabled=false;f.providers[1]->resources=Both;
        f.observations[1].observationMask&=~HIGGS_OBS_PALM_VALID;f.Frame();CHECK(!f.Input());TickRegionActions();CHECK(!f.Effects());}
    {Fixture f;f.providers[0]->enabled=false;f.providers[1]->resources=Both;
        std::this_thread::sleep_for(std::chrono::milliseconds(110));CHECK(!f.Input());TickRegionActions();CHECK(!f.Effects());}
    {Fixture f;CHECK(f.Input());std::this_thread::sleep_for(std::chrono::milliseconds(110));TickRegionActions();CHECK(!f.Effects());}
    for(unsigned queued=0;queued<2;++queued) {
        Fixture f;auto& b=*f.providers[1];f.providers[0]->enabled=false;
        CHECK(f.api.RemoveBinding(b.writer,b.registration.bindingId)==Status::Ok);
        auto spec=b.region.spec;spec.maxSampleAgeSeconds=.002;spec.maxSampleSkewSeconds=.002;
        CHECK(GetRegionsApi().UpsertRegion(b.writer,&spec,b.region.revision,&b.region)==Status::Ok);
        RegionActionBindingSpec binding{sizeof(binding),Both,2,1,b.region.regionId,b.region.revision,b.client,Fixture::Offers,Fixture::Execute,&b};
        b.registration={sizeof(b.registration),0,0,0,0};CHECK(f.api.RegisterBinding(b.writer,&binding,&b.registration)==Status::Ok);
        b.anchorRevision=0;f.Frame(false);f.Publish(b);if(queued)CHECK(f.Input());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));f.Publish(b); // fresh target cannot refresh an old palm
        if(!queued)CHECK(!f.Input());TickRegionActions();CHECK(!f.Effects());
    }
}
} // namespace
void BS_CALL Ended(void* context, const RegionInputTerminal* terminal) {
    auto& p = *static_cast<Provider*>(context);
    ++p.terminals;
    p.terminal = *terminal;
}
void ConsumedRelease() {
    const auto& continuation = GetRegionContinuationApi();
    {
        Fixture legacy;
        auto& b = *legacy.providers[1];
        CHECK(continuation.Subscribe(b.writer, b.registration.bindingId, Ended) == Status::UnsupportedVersion);
    }
    {
        Fixture f(1, 3, false, true);
        auto& a = *f.providers[0];
        auto& b = *f.providers[1];
        CHECK(continuation.Subscribe(a.writer, b.registration.bindingId, Ended) == Status::Unauthorized);
        CHECK(continuation.Subscribe(a.writer, a.registration.bindingId, Ended) == Status::Ok);
        CHECK(continuation.Subscribe(b.writer, b.registration.bindingId, Ended) == Status::Ok);
        CHECK(f.Input());
        TickRegionActions();
        CHECK(b.effects == 1 && !b.terminals);
        f.Settle(b);
        const auto press = b.execution.request.inputId;
        const auto request = b.execution.request.requestId;
        b.anchor.world.position[0] = 100;
        f.Frame();
        CHECK(f.Input(false));
        TickRegionActions();
        TickRegionActions();
        CHECK(a.effects == 0 && a.terminals == 0 && b.effects == 1 && b.terminals == 1);
        CHECK(b.terminal.end == static_cast<std::uint32_t>(RegionInputEnd::Released) &&
              b.terminal.pressInput == press && b.terminal.request == request && b.terminal.terminalInput > press);
        CHECK(b.terminal.correlation && b.terminal.correlation != b.execution.request.grant);
    }
    {
        Fixture f(1, 1, false, true);
        auto& b = *f.providers[1];
        CHECK(continuation.Subscribe(b.writer, b.registration.bindingId, Ended) == Status::Ok);
        CHECK(f.Input());
        TickRegionActions();
        CHECK(b.effects == 1);
        CHECK(f.api.RemoveBinding(b.writer, b.registration.bindingId) == Status::Ok);
        f.Input(false);
        TickRegionActions();
        CHECK(b.terminals == 0);
    }
    for(unsigned loss=0;loss!=3;++loss) {
        Fixture f(1,1,false,true);
        auto& b=*f.providers[1];
        CHECK(continuation.Subscribe(b.writer,b.registration.bindingId,Ended)==Status::Ok);
        CHECK(f.Input());TickRegionActions();CHECK(b.effects==1);
        f.Settle(b); // The original press survives its short-lived grant.
        const auto press=b.execution.request.inputId;
        if(loss==0)f.broker.SetInputContext(1,HiggsHands::Now());
        if(loss==1)f.sequenceUnavailable=true; // Lost provider history is not a fabricated Up/Cancel.
        if(loss==2)ResetRegionActions();
        TickRegionActions();TickRegionActions();
        CHECK(b.terminals==1 && b.terminal.pressInput==press && !b.terminal.terminalInput);
        CHECK(b.terminal.end==static_cast<std::uint32_t>(loss==0?RegionInputEnd::Cancelled:RegionInputEnd::ObservationLost));
        CHECK(b.terminal.providerReason==(loss==0?HIGGS_HAND_NOT_READY:0u));
        f.sequenceUnavailable=false;f.broker.SetInputContext(0,HiggsHands::Now());
        f.Input(false);TickRegionActions();CHECK(b.terminals==1 && b.effects==1);
    }
}
std::uint64_t ownedSample{},ownedCaptured{};
void CaptureOwned(std::uint64_t sample) {ownedSample=sample;ownedCaptured=HiggsHands::Now();}
Status Sample(const OwnedRegionSource& source, RegionAnchorSnapshot& out) noexcept {
    if (!ownedSample)
        return Status::Unavailable;
    out = {sizeof(out), static_cast<std::uint32_t>(RegionAnchorKind::Npc), 1, 0};
    out.source = {"PLANCK",
                  "actor-body",
                  1,
                  source.body.bodyGeneration,
                  ownedSample,
                  source.higgsEpoch,
                  source.saveGeneration,
                  ownedSample,
                  double(ownedCaptured) / 1e9,
                  static_cast<std::uint32_t>(RegionUnits::SkyrimWorld),
                  0};
    out.world.scale = 1;
    out.world.rotation[0] = out.world.rotation[4] = out.world.rotation[8] = 1;
    return Status::Ok;
}
void SourceOwnership() {
    Fixture f(1, 1, false, false, RegionAnchorKind::Npc);
    auto& b = *f.providers[1];
    const auto& sources = GetRegionSourcesApi();
    const RegionPlanckBody target{sizeof(target), 5, 2, 0, 90, 8, 7};
    CHECK(sources.BindPlanckBody(b.writer, b.registration.bindingId, &target) == Status::UnsupportedVersion);
    SetOwnedPlanckReader(Sample);
    CHECK(sources.BindPlanckBody(f.providers[0]->writer, b.registration.bindingId, &target) ==
          Status::Unauthorized);
    CHECK(sources.BindPlanckBody(b.writer, b.registration.bindingId, &target) == Status::Ok);
    CHECK(f.api.PublishAnchor(b.writer, b.registration.bindingId, b.anchorRevision, &b.anchor, &b.anchorRevision) ==
          Status::Conflict);
    CaptureOwned(1);
    f.Frame(false);
    RegionObservation observed{};
    CHECK(f.api.CopyObservation(b.writer, b.registration.bindingId, Left, &observed) == Status::Ok);
    CHECK(observed.state == static_cast<std::uint32_t>(RegionSpatialState::Ready) &&
          !std::strcmp(observed.anchor.source.provider, "PLANCK"));
    CaptureOwned(0);
    f.Frame(false);
    CHECK(f.api.CopyObservation(b.writer, b.registration.bindingId, Left, &observed) == Status::Ok);
    CHECK(observed.state == static_cast<std::uint32_t>(RegionSpatialState::Unavailable));
    CHECK(sources.UnbindSource(b.writer, b.registration.bindingId) == Status::Ok);
    CHECK(sources.UnbindSource(b.writer, b.registration.bindingId) == Status::NotFound);
    SetOwnedPlanckReader(nullptr);
}
std::uint64_t digestAgeMs{};
Status Digest(const PpbRegionQuery&, PpbRegionEvidence& result) noexcept {
    result.ppbEpoch = 15;
    result.ppbSequence = 17;
    result.ppbAgeMs = digestAgeMs;
    result.reported = 1;
    return Status::Ok;
}

void AnatomicalEvidenceScope() {
    Fixture f;
    auto& b = *f.providers[1];
    const auto& sources = GetRegionSourcesApi();
    const PpbRegionQuery query{sizeof(query), 53, Left, 2, 0, 1};
    PpbRegionEvidence result{sizeof(result)};
    const auto read = [&] { return sources.CopyPpbDigestEvidence(b.writer,b.registration.bindingId,&query,&result); };
    CHECK(read() == Status::UnsupportedVersion);
    SetPpbDigestReader(Digest);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    digestAgeMs = 0;
    CHECK(read() == Status::Ok);
    CHECK(result.reported && result.ppbEpoch == 15 && result.logicalEpoch == f.snapshot.epoch);
    digestAgeMs = 500;
    CHECK(read() == Status::Unavailable);
    ResetRegionActions();
    digestAgeMs = 0;
    CHECK(read() == Status::Unavailable);
    SetPpbDigestReader(nullptr);
}
struct DwellConsumer {
    Fixture& fixture;
    Provider& owner;
    RegionActionRegistration binding{sizeof(binding)};
    std::uint64_t revision{},sample{};
    unsigned offers{}, effects{};
    HIGGS_ActionRequest delivered{};
    std::uint64_t visit{};
    RegionDwellBindingSpec spec{};

    DwellConsumer(Fixture& f,unsigned index):fixture(f),owner(*f.providers[index]) {
        spec={sizeof(spec),Left,Left,HIGGS_USE_HAND_GOALS,500,1000,
            owner.region.regionId,owner.region.revision,
            0x4457454c4c,owner.client,Eligible,Execute,this};
    }
    static Status BS_CALL Eligible(void* raw,const RegionDwellContext* context,std::uint32_t* yes) {
        auto& self=*static_cast<DwellConsumer*>(raw);
        ++self.offers;
        *yes=context->visit && context->observation.state==static_cast<std::uint32_t>(RegionSpatialState::Ready);
        return Status::Ok;
    }
    static Status BS_CALL Execute(void* raw,const RegionDwellExecution* execution) {
        auto& self=*static_cast<DwellConsumer*>(raw);
        CHECK(!execution->authority.inputId && execution->authority.owner==self.owner.client);
        CHECK(execution->authority.executor==self.binding.higgsExecutor && execution->authority.actionId==self.spec.actionKey);
        CHECK(execution->authority.grant && execution->authority.grant!=execution->current.hands.hands[0].grant);
        CHECK(!std::strcmp(execution->current.observation.probe.source.provider,"HIGGS"));
        CHECK(execution->current.actionKey==self.spec.actionKey && execution->current.visit>self.visit);
        self.delivered=execution->authority;self.visit=execution->current.visit;++self.effects;
        return Status::Ok;
    }
    Status Register() {return GetRegionDwellApi().RegisterBinding(owner.writer,&spec,&binding);}
    void Anchor(float x=0) {
        auto anchor=owner.anchor;anchor.world.position[0]=x;
        anchor.source.sampleId=anchor.source.frame=++sample;
        anchor.source.timeSeconds=double(HiggsHands::Now())/1e9;
        CHECK(fixture.api.PublishAnchor(owner.writer,binding.bindingId,revision,&anchor,&revision)==Status::Ok);
    }
    void Finish() {
        HIGGS_ActionResult result{sizeof(result)};
        result.stage=HIGGS_ACTION_COMPLETED;result.requestId=delivered.requestId;result.executor=delivered.executor;
        result.epoch=delivered.epoch;CHECK(fixture.broker.Report(owner.client,result)==HIGGS_HAND_OK);
        CHECK(fixture.broker.EndHandUse(owner.client,delivered.requestId,delivered.epoch)==HIGGS_HAND_OK);
    }
};
std::uint64_t ownedGeneration=7;
float ownedPosition{};
unsigned ownedChange{};
Status LifetimeSample(const OwnedRegionSource& source,RegionAnchorSnapshot& out) noexcept {
    if(source.body.bodyGeneration!=ownedGeneration)return Status::Unavailable;
    const auto result=Sample(source,out);
    out.world.position[0]=ownedPosition;
    return result;
}
void ChangeOwnedBeforeDispatch() {
    if(ownedChange==1)CaptureOwned(0);
    if(ownedChange==2)++ownedGeneration;
    if(ownedChange>=3) {CaptureOwned(ownedSample+1);ownedPosition=ownedChange==3?20.f:0.f;}
}
void OwnedDispatchLifetime() {
    // The current reader boundary changes after frame refresh but before HIGGS
    // enters its authorized callback. No replacement PLANCK registry is copied.
    for(unsigned dwell=0;dwell!=2;++dwell)for(unsigned change=0;change!=5;++change) {
        Fixture f(1,1,false,false,RegionAnchorKind::Npc,true);
        auto& b=*f.providers[1];f.providers[0]->enabled=false;
        DwellConsumer d(f,1);
        if(dwell) {b.enabled=false;CHECK(d.Register()==Status::Ok);}
        ownedGeneration=7;ownedPosition=0;ownedChange=change;
        SetOwnedPlanckReader(LifetimeSample);
        const RegionPlanckBody target{sizeof(target),5,2,0,90,8,ownedGeneration};
        const auto binding=dwell?d.binding.bindingId:b.registration.bindingId;
        CHECK(GetRegionSourcesApi().BindPlanckBody(b.writer,binding,&target)==Status::Ok);
        CaptureOwned(1);f.Frame(false);
        if(!dwell)CHECK(f.Input());
        f.broker.DrainHandUses();
        f.beforeDispatch=ChangeOwnedBeforeDispatch;
        TickRegionActions();
        CHECK(f.invokeCalls==1);
        const bool execute=change==0 || change==4;
        CHECK((dwell?d.effects:b.effects)==(execute?1u:0u));
        if(execute) {if(dwell)d.Finish();else f.Settle(b);}
        f.beforeDispatch=nullptr;TickRegionActions();
        CHECK((dwell?d.effects:b.effects)==(execute?1u:0u));
        SetOwnedPlanckReader(nullptr);
    }
}
void DwellRounds() {
    {Fixture f;DwellConsumer d(f,0);CHECK(d.Register()==Status::UnsupportedVersion);}
    Fixture f(10,1,false,false,RegionAnchorKind::World,true);
    DwellConsumer low(f,0),high(f,1);
    CHECK(low.Register()==Status::Ok && high.Register()==Status::Ok);
    CHECK(!low.binding.recognizerClient && !high.binding.recognizerClient);
    low.Anchor();high.Anchor();TickRegionActions();
    CHECK(f.dwellRequests.size()==2 && high.effects==0 && low.effects==0);
    f.broker.DrainHandUses();TickRegionActions();
    CHECK(high.effects==1 && low.effects==0);high.Finish();
    f.Frame(false);f.broker.DrainHandUses();TickRegionActions();
    CHECK(f.dwellRequests.size()==2 && high.effects==1);
    high.Anchor(20);f.Frame(false);high.Anchor();f.Frame(false);
    CHECK(f.dwellRequests.size()==3);f.broker.DrainHandUses();TickRegionActions();
    CHECK(high.effects==2 && low.effects==0);high.Finish();
}
void DwellInvalidations() {
    for(unsigned mode=0; mode!=6; ++mode) {
        Fixture f(0,1,false,false,RegionAnchorKind::World,true);
        DwellConsumer d(f,0);CHECK(d.Register()==Status::Ok);d.Anchor();TickRegionActions();
        CHECK(f.dwellRequests.size()==1);
        const auto [owner,proposal]=f.dwellRequests.front();
        if(mode!=0)f.broker.DrainHandUses();
        if(mode==0 || mode==1)d.Anchor(20);
        if(mode==2)ResetRegionActions();
        if(mode==3){f.broker.SetInputContext(1,HiggsHands::Now());f.broker.SetInputContext(0,HiggsHands::Now());}
        if(mode==4)CHECK(f.api.RemoveBinding(d.owner.writer,d.binding.bindingId)==Status::Ok);
        if(mode==5){
            auto anchor=d.owner.anchor;anchor.source.generation++;anchor.source.sampleId=anchor.source.frame=++d.sample;
            anchor.source.timeSeconds=double(HiggsHands::Now())/1e9;
            CHECK(f.api.PublishAnchor(d.owner.writer,d.binding.bindingId,d.revision,&anchor,&d.revision)==Status::Ok);
        }
        TickRegionActions();f.broker.DrainHandUses();TickRegionActions();
        CHECK(!d.effects);
        HIGGS_HandUseProposalState state{sizeof(state)};
        CHECK(f.broker.CopyHandUseProposal(owner,proposal,state,HiggsHands::Now())==HIGGS_HAND_OK);
        CHECK(state.stage!=HIGGS_USE_PROPOSAL_PENDING && !state.use.active);
    }
}
void MixedAdmissionBoundaries() {
    // Both orders of publication enter one common window; higher provider priority wins.
    for(unsigned buttonFirst=0;buttonFirst!=2;++buttonFirst) {
        Fixture f(100,1,false,false,RegionAnchorKind::World,true);
        f.providers[1]->enabled=false;
        DwellConsumer d(f,1);CHECK(d.Register()==Status::Ok);d.Anchor();
        if(buttonFirst)CHECK(f.Input());
        TickRegionActions();
        if(!buttonFirst)CHECK(f.Input());
        CHECK(f.dwellRequests.size()==1 && !f.Effects() && !d.effects);
        f.broker.DrainHandUses();TickRegionActions();
        CHECK(d.effects==1 && !f.Effects());d.Finish();
    }
    // Button priority also wins over a lower-priority dwell: no category preference.
    {
        Fixture f(100,1,false,false,RegionAnchorKind::World,true);
        f.providers[0]->enabled=false;
        DwellConsumer d(f,0);CHECK(d.Register()==Status::Ok);d.Anchor();TickRegionActions();
        CHECK(f.Input());f.broker.DrainHandUses();TickRegionActions();
        CHECK(f.providers[1]->effects==1 && !d.effects);f.Settle(*f.providers[1]);
    }
    // Native acquisition participates, rather than starting beside the dwell winner.
    {
        Fixture f(100,1,false,false,RegionAnchorKind::World,true);
        f.providers[0]->enabled=f.providers[1]->enabled=false;
        f.observations[0].observationMask|=HIGGS_OBS_NATIVE_GRIP_TARGET;f.Frame(false);
        f.broker.SetNativeTargetKeys(Left,0x1234,0x1234);
        DwellConsumer d(f,1);CHECK(d.Register()==Status::Ok);d.Anchor();TickRegionActions();
        bool native=false;CHECK(f.broker.Input(Left,HIGGS_INPUT_GRIP,true,HiggsHands::Now(),&native) && !native);
        f.broker.DrainHandUses();TickRegionActions();CHECK(d.effects==1 && !f.Effects());d.Finish();
    }
    // A shared connection alone cannot swallow unrelated default controls.
    {
        Fixture f(0,1,false,false,RegionAnchorKind::World,true);
        f.providers[0]->enabled=f.providers[1]->enabled=false;
        CHECK(!f.Input());f.broker.DrainHandUses();TickRegionActions();CHECK(!f.Effects());
    }
}

int main(){try {
    OwnedDispatchLifetime();
    DwellRounds();DwellInvalidations();MixedAdmissionBoundaries();
    ConsumedRelease();SourceOwnership();AnatomicalEvidenceScope();
    ArbitrationAndFallback();ProviderPriorityAndUp();TargetRefreshAndHeld();CancelBeforeEffect();ConsumerFailureAndDrain();ScopeAndEvidence();DwellAndOrdering();RequiredResourcesAndAge();
    std::printf("Body Slots/HIGGS production region actions: %u checks passed; zero game starts\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
