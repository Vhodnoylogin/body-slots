// Production Body Slots recognition/maintenance + actual HIGGS service closure.
// Only native observations and consumer effects are scripted; no copied service.
#include "main/region_actions.h"
#include "main/interaction_regions.h"
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
    unsigned effects{},offers{},mode{};
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
    HIGGS_HandSnapshot snapshot{};
    unsigned fallback{},beforeMode{},invokeCalls{};
    HIGGS_HandStatus invokeStatus=999;
    explicit Fixture(std::int32_t providerPriorityB=1,std::uint32_t edgeMask=1,bool sharedOwner=false) {
        CHECK(DisconnectRegionActions()==Status::Ok);active=this;
        hands.structSize=sizeof(hands);hands.abiVersion=1;
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
        routing={sizeof(routing),1,1,
            +[](std::uint64_t c,std::uint64_t e,std::uint64_t r,std::uint32_t h){return active->broker.Authorize(c,e,r,h);},
            +[](std::uint64_t c,std::uint64_t e,std::uint64_t r){return active->broker.Revoke(c,e,r);},
            +[](std::uint64_t c,std::uint64_t e,std::uint32_t h){return active->broker.CanRoute(c,e,h);},
            +[](std::uint64_t c,std::uint64_t r,std::uint64_t e,std::uint32_t why){return active->broker.CancelRecognized(c,r,e,why);}};
        dispatch={sizeof(dispatch),1,+[](std::uint64_t owner,const HIGGS_AuthorizedDispatchRequest* r,HIGGS_AuthorizedDispatchCallback cb,void* ctx){
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
            f.invokeStatus=f.broker.InvokeAuthorized(owner,*r,cb,ctx);return f.invokeStatus;
        }};
        CHECK(ConnectRegionActions(hands,routing,dispatch)==Status::Ok);
        for(auto& h:observations)h.observationMask=HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN|HIGGS_OBS_PALM_VALID|HIGGS_OBS_TRACKING_VALID;
        broker.MarkMainThread();broker.ObserveWorld(1,false,HiggsHands::Now());Frame(false);
        for(unsigned i=0;i<2;++i) {
            auto p=std::make_unique<Provider>();p->fixture=this;p->priority=i?100:10;
            char name[32]{};std::snprintf(name,sizeof(name),"region-provider-%u",i);
            CHECK(registry.RegisterClient(name,&p->writer)==Status::Ok);
            HIGGS_HandClientInfo info{};info.structSize=sizeof(info);std::strcpy(info.name,name);info.userPriority=i?providerPriorityB:0;
            if(i && sharedOwner)p->client=providers[0]->client;
            else CHECK(broker.Register(info,p->client)==HIGGS_HAND_OK);
            InteractionRegionSpec spec{};spec.structSize=sizeof(spec);spec.anchorKind=static_cast<std::uint32_t>(RegionAnchorKind::World);
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
    TickRegionActions();if(!b.effects)std::fprintf(stderr,"dispatch diagnostics: invokes=%u status=%u a=%u b=%u\n",f.invokeCalls,f.invokeStatus,a.effects,b.effects);
    CHECK(b.effects==1 && a.effects==0);TickRegionActions();CHECK(f.Effects()==1);f.Settle(b);
    CHECK(f.Input(false));TickRegionActions();CHECK(f.Effects()==1); // matching Up stays consumed, no second action
    b.priority=-10;f.Frame();CHECK(f.Input());TickRegionActions();CHECK(a.effects==1);f.Settle(a);CHECK(f.Input(false));
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
        CHECK(b.execution.request.physicalHands==Both);CHECK(b.execution.currentSnapshot.hands[1].grant==b.execution.request.grant);}
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
int main(){try {
    ArbitrationAndFallback();ProviderPriorityAndUp();TargetRefreshAndHeld();CancelBeforeEffect();ConsumerFailureAndDrain();ScopeAndEvidence();DwellAndOrdering();RequiredResourcesAndAge();
    std::printf("Body Slots/HIGGS production region actions: %u checks passed; zero game starts\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
