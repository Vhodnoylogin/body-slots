#include "runtime.h"
#include "instances.h"
#include "validation.h"
#include "runtime_saved.h"
#include "zone_geometry.h"
#include "content_policy.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace body_slots::detail {
namespace {
std::atomic<ContentCommitValidator> contentValidator{nullptr};
template<class F> Status GuardRuntime(F&& f) noexcept { try { return f(); } catch (...) { return Status::InternalError; } }
bool HandValid(std::uint32_t h) { return h == Left || h == Right; }
bool Finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
bool TransformValid(const RuntimeTransform& t) {
    if (!Finite3(t.position) || !std::isfinite(t.scale) || t.scale <= 0 || t.scale > 1000000) return false;
    for (float v : t.rotation) if (!std::isfinite(v)) return false;
    for (int i = 0; i < 3; ++i) for (int j = i; j < 3; ++j) {
        double dot = 0; for (int k = 0; k < 3; ++k) dot += double(t.rotation[i*3+k])*t.rotation[j*3+k];
        if (std::abs(dot - (i == j ? 1.0 : 0.0)) > .001) return false;
    }
    const auto* r=t.rotation;
    return std::abs(double(r[0])*(double(r[4])*r[8]-double(r[5])*r[7])-double(r[1])*(double(r[3])*r[8]-double(r[5])*r[6])+double(r[2])*(double(r[3])*r[7]-double(r[4])*r[6])-1.0) <= .001;
}
void Rotate(const float* r, const float* v, float* out) {
    for (int i=0;i<3;++i) out[i]=r[i*3]*v[0]+r[i*3+1]*v[1]+r[i*3+2]*v[2];
}
struct Callback {
    std::mutex mutex; std::condition_variable drained;
    bool active=true; std::uint32_t inFlight=0;
    std::uint64_t id=0, owner=0;
    BehaviorSpec behavior{}; RuntimeEventCallback event=nullptr; void* context=nullptr;
    SourceOfferCallback offers2=nullptr;SourceExecuteCallback execute2=nullptr;
};
thread_local std::vector<const Callback*> callbackStack;
class CallLease {
public:
    explicit CallLease(const std::shared_ptr<Callback>& c): callback_(c) {
        std::lock_guard<std::mutex> lock(c->mutex);
        if (c->active) { callbackStack.push_back(c.get()); ++c->inFlight; entered_=true; }
    }
    ~CallLease() { if (entered_) { callbackStack.pop_back(); std::lock_guard<std::mutex> lock(callback_->mutex); if (!--callback_->inFlight) callback_->drained.notify_all(); } }
    explicit operator bool() const { return entered_; }
private: std::shared_ptr<Callback> callback_; bool entered_=false;
};
struct Pending {
    ActionRequest request{}; std::uint64_t owner=0;
    CommitStage stage=CommitStage::Accepted;
    bool committed=false,handCommitted=false,handCompensated=false,authoritySettled=false,terminalReceipt=false;
    ContentCommit receipt{}, last{};
};
struct State {
    std::mutex mutex;
    std::vector<RuntimeRecord> rows;
    struct ShapeRow { std::uint64_t handle; ZoneShape shape; std::uint32_t overridden; SlotContentPolicy policy{};bool hasPolicy=false; };
    std::vector<ShapeRow> shapes;
    std::vector<ContentPolicySample> policySamples;
    std::vector<std::shared_ptr<Callback>> behaviors, observers, retired;
    std::vector<ActionOffer> offers;
    struct OfferSource {std::uint64_t offerId;IncomingItemContext incoming;};
    std::vector<OfferSource> offerSources;
    std::vector<Pending> pending;
    struct UsedInput { std::uint64_t epoch,sequence; std::uint32_t hand; };
    std::vector<UsedInput> used;
    InstanceView configuration{};
    std::uint64_t epoch=0,frame=0,body=0,revision=1,policy=1,nextId=1;
    std::uint64_t dragSlot=0,dragRequest=0;
    double time=0;
    HandProbe hands[2]{};
};
State& Runtime() { static State s; return s; }
bool Terminal(CommitStage stage);
Status DrainRuntimeClient(std::uint64_t owner) noexcept {
    return GuardRuntime([&] {
        // Never block from any callback: two callbacks can otherwise attempt to
        // drain each other. Callers retry after returning from the callback.
        if(!callbackStack.empty())return Status::Conflict;
        std::vector<std::shared_ptr<Callback>> removed;
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            for(const auto* pool:{&s.behaviors,&s.observers,&s.retired})for(const auto& c:*pool)if(c->owner==owner)removed.push_back(c);
            for(const auto& c:removed){std::lock_guard<std::mutex> callbackLock(c->mutex);c->active=false;}
            for(auto* pool:{&s.behaviors,&s.observers,&s.retired})pool->erase(std::remove_if(pool->begin(),pool->end(),[&](const std::shared_ptr<Callback>& c){return c->owner==owner;}),pool->end());
            ++s.policy;s.offers.clear();for(auto& p:s.pending)if(p.owner==owner&&!Terminal(p.stage))p.stage=CommitStage::Cancelled;
        }
        for(const auto& c:removed){std::unique_lock<std::mutex> lock(c->mutex);c->drained.wait(lock,[&]{return !c->inFlight;});}
        return Status::Ok;
    });
}
std::uint64_t Allocate(State& s) {
    if (!s.nextId || s.nextId >= 0x00ffffffffffffffULL) return 0;
    return 0x0400000000000000ULL | s.nextId++;
}
RuntimeRecord* Find(State& s,std::uint64_t handle) {
    for (auto& row:s.rows) if(row.configured.instance.instanceHandle==handle) return &row;
    return nullptr;
}
const State::ShapeRow* FindShape(const State& s,std::uint64_t handle) {
    for(const auto& row:s.shapes)if(row.handle==handle)return &row;return nullptr;
}
IncomingItemContext IncomingItem(const State& s,std::uint64_t handle,const RuntimeInput& input,std::uint32_t source) {
    IncomingItemContext out{};out.source=source;out.epoch=input.epoch;out.handObservationId=input.handStateRevision;
    if(source==1)out.referenceId=input.heldRefId;else if(source==2)out.equippedFormId=input.equippedFormId;
    for(const auto& sample:s.policySamples)if(sample.instanceHandle==handle&&sample.physicalHand==input.physicalHand&&sample.epoch==input.epoch&&sample.handObservationId==input.handStateRevision&&sample.source==source){out.known=sample.known;out.allowed=sample.allowed;out.category=sample.category;out.baseFormId=sample.baseFormId;break;}return out;
}
bool LegacyPolicyAllows(const State& s,std::uint64_t handle,const RuntimeInput& input) {
    const auto* policy=FindShape(s,handle);if(!input.contentCategory||!policy||!policy->hasPolicy)return true;
    const auto source=input.heldRefId?1u:2u;
    for(const auto& sample:s.policySamples)if(sample.instanceHandle==handle&&sample.epoch==input.epoch&&sample.handObservationId==input.handStateRevision&&sample.physicalHand==input.physicalHand&&(sample.source==0||sample.source==source)&&sample.category==input.contentCategory&&sample.known&&sample.allowed)return true;return false;
}
bool Terminal(CommitStage stage) { return stage==CommitStage::Completed || stage==CommitStage::Failed || stage==CommitStage::Cancelled; }
bool Reserved(const Pending& pending) { return !Terminal(pending.stage)||!pending.authoritySettled; }
void InvalidateOffers(State& s) {
    ++s.policy; s.offers.clear();s.offerSources.clear();
    // A Pending exists only after DispatchSelected starts its assigned executor.
    // Losing geometry cancels offers, not an inventory operation already started
    // elsewhere. Its reservation survives until the provider/HIGGS outcome.
}
struct Configuration {
    std::array<ConfiguredInstanceRecord,kMaxInstances> rows{}; InstanceView view{};RuntimeCustomizationData customizations{};
};
void SweepRetired(State& s) {
    s.retired.erase(std::remove_if(s.retired.begin(),s.retired.end(),[](const std::shared_ptr<Callback>& c){std::lock_guard<std::mutex> lock(c->mutex);return !c->inFlight;}),s.retired.end());
}
void Events(const std::vector<RuntimeEvent>& events);
Status SyncConfiguration() {
    auto copied=std::make_unique<Configuration>();
    auto status=CopyRuntimeConfigurationWithCustomizations(copied->rows.data(),kMaxInstances,&copied->view,&copied->customizations);
    if(status!=Status::Ok) return status;
    std::vector<RuntimeEvent> events;
    auto& s=Runtime(); std::unique_lock<std::mutex> lock(s.mutex);
    SweepRetired(s);
    if(copied->view.saveGeneration<s.configuration.saveGeneration||(copied->view.saveGeneration==s.configuration.saveGeneration&&copied->view.viewRevision<s.configuration.viewRevision))return Status::Conflict;
    if (s.configuration.saveGeneration==copied->view.saveGeneration && s.configuration.viewRevision==copied->view.viewRevision) return Status::Ok;
    std::vector<RuntimeRecord> next; next.reserve(copied->view.count);
    std::vector<State::ShapeRow> shapes;shapes.reserve(copied->view.count);
    const bool sameSave=s.configuration.saveGeneration==copied->view.saveGeneration;
    for(const auto& old:s.rows) {
        events.push_back({static_cast<std::uint32_t>(RuntimeEventKind::Invalidated),0,old.configured.instance.instanceHandle,s.epoch,s.frame,s.revision+1});
        for(auto hand:{Left,Right})if(old.insideHands&hand)events.push_back({static_cast<std::uint32_t>(RuntimeEventKind::Exit),hand,old.configured.instance.instanceHandle,s.epoch,s.frame,s.revision+1});
    }
    for(std::uint32_t i=0;i<copied->view.count;++i) {
        RuntimeRecord row{}; auto* old=sameSave?Find(s,copied->rows[i].instance.instanceHandle):nullptr;
        if(old) row=*old;
        row.binding={};row.binding.structSize=sizeof(RuntimeBindingSpec);row.binding.kind=static_cast<std::uint32_t>(RuntimeBindingKind::BodyNode);
        SlotVisualSpec visual{};visual.structSize=sizeof(SlotVisualSpec);
        InstanceCustomizationKey key{};key.instance=copied->rows[i].instance.key;std::memcpy(key.definitionProvider,copied->rows[i].instance.definitionProvider,64);std::memcpy(key.definitionKey,copied->rows[i].instance.definitionKey,64);
        State::ShapeRow shape{copied->rows[i].instance.instanceHandle,DefaultZoneShape(copied->rows[i].instance.effective),0};
        for(std::uint32_t j=0;j<copied->customizations.count;++j){const auto& saved=copied->customizations.entries[j];if(std::memcmp(&saved.key,&key,sizeof(key)))continue;if(saved.fieldMask&kSavedRuntimeBinding)row.binding=saved.binding;if(saved.fieldMask&kSavedRuntimeVisual)visual=saved.visual;if(saved.fieldMask&kSavedRuntimeShape){shape.shape=copied->customizations.shapes[j];shape.overridden=1;}if(saved.fieldMask&kSavedRuntimePolicy){shape.policy=copied->customizations.policies[j];shape.hasPolicy=true;}break;}
        if(std::memcmp(&row.visual,&visual,sizeof(visual))){row.visual=visual;++row.visualRevision;row.presentationState=static_cast<std::uint32_t>((visual.holderModel[0]||visual.previewModel[0])?PresentationState::Pending:PresentationState::None);row.displayedContentVersion=0;}
        row.configured=copied->rows[i]; row.availability=static_cast<std::uint32_t>(RuntimeAvailability::ConfigurationChanged);
        row.insideHands=0; row.runtimeRevision=s.revision+1; next.push_back(row);shapes.push_back(shape);
    }
    s.rows.swap(next);s.shapes.swap(shapes);s.policySamples.clear(); s.configuration=copied->view; ++s.revision; InvalidateOffers(s);
    if(!sameSave) {
        s.dragSlot=0;s.dragRequest=0;
        s.pending.clear(); s.used.clear();
        for(const auto& c:s.behaviors){std::lock_guard<std::mutex> callbackLock(c->mutex);c->active=false;s.retired.push_back(c);}
        s.behaviors.clear();SweepRetired(s);
    }
    lock.unlock();Events(events);
    return Status::Ok;
}
bool Own(std::uint64_t owner,std::uint64_t handle) { return RuntimeOwnsInstance(owner,handle)==Status::Ok; }
void Events(const std::vector<RuntimeEvent>& events) {
    std::vector<std::shared_ptr<Callback>> observers;
    { auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);observers=s.observers; }
    for(const auto& event:events) for(const auto& c:observers) {
        if(RuntimeClientIdentity(c->owner,false,nullptr)!=Status::Ok) continue;
        CallLease lease(c); if(!lease) continue;
        try { c->event(c->context,&event); } catch (...) { /* isolate observer failure */ }
    }
}
RuntimeEvent Event(State& s,const RuntimeRecord& r,RuntimeEventKind k,std::uint32_t hand=0) {
    return {static_cast<std::uint32_t>(k),hand,r.configured.instance.instanceHandle,s.epoch,s.frame,s.revision};
}
Status BS_CALL CopyRuntime(std::uint64_t reader,RuntimeRecord* records,std::uint32_t capacity,RuntimeView* view) noexcept {
    return GuardRuntime([&] {
        if(!view || capacity>kMaxInstances || (capacity&&!records)) return Status::InvalidArgument;
        if(view->structSize!=sizeof(RuntimeView)||view->version!=kRuntimeApiVersion||view->reserved) return Status::UnsupportedVersion;
        auto status=RuntimeClientIdentity(reader,false,nullptr);if(status!=Status::Ok)return status;
        status=SyncConfiguration();if(status!=Status::Ok)return status;
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
        RuntimeView out{sizeof(RuntimeView),kRuntimeApiVersion,s.epoch,s.frame,s.revision,s.configuration.saveGeneration,s.configuration.viewRevision,static_cast<std::uint32_t>(s.rows.size()),0};
        *view=out; if(capacity<s.rows.size())return Status::BufferTooSmall;
        std::copy(s.rows.begin(),s.rows.end(),records);return Status::Ok;
    });
}
Status BS_CALL SetBinding(std::uint64_t writer,std::uint64_t handle,const RuntimeBindingSpec* spec) noexcept {
    return GuardRuntime([&] {
        if(!spec)return Status::InvalidArgument;auto status=SetSavedRuntimeCustomization(writer,handle,spec,nullptr);if(status!=Status::Ok)return status;return SyncConfiguration();
    });
}
Status RegisterBehaviorRecord(std::uint64_t writer,const BehaviorSpec* spec,std::uint64_t* registration,SourceOfferCallback offers2=nullptr,SourceExecuteCallback execute2=nullptr) noexcept {
    return GuardRuntime([&] {
        SetRuntimeReleaseHook(DrainRuntimeClient);
        if(!spec||!registration||spec->structSize!=sizeof(*spec)||spec->reserved||(!offers2&&(!spec->offers||!spec->execute))||(offers2&&!execute2)||!spec->higgsClient||!spec->higgsExecutor)return Status::InvalidArgument;
        auto status=RuntimeOwnsInstance(writer,spec->instanceHandle);if(status!=Status::Ok)return status;
        status=SyncConfiguration();if(status!=Status::Ok)return status;
        auto callback=std::make_shared<Callback>();callback->owner=writer;callback->behavior=*spec;callback->offers2=offers2;callback->execute2=execute2;
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
        if(!Own(writer,spec->instanceHandle))return Status::Unauthorized;
        SweepRetired(s);if(s.behaviors.size()+s.retired.size()>=kMaxBehaviors)return Status::LimitExceeded;
        for(const auto& c:s.behaviors)if(c->behavior.instanceHandle==spec->instanceHandle)return Status::Conflict;
        callback->id=Allocate(s);if(!callback->id)return Status::LimitExceeded;
        s.behaviors.push_back(callback);InvalidateOffers(s);*registration=callback->id;return Status::Ok;
    });
}
Status BS_CALL RegisterBehavior(std::uint64_t writer,const BehaviorSpec* spec,std::uint64_t* registration) noexcept {return RegisterBehaviorRecord(writer,spec,registration);}
Status BS_CALL RegisterSourceBehavior(std::uint64_t writer,const SourceBehaviorSpec* source,std::uint64_t* registration) noexcept {
    if(!source||source->structSize!=sizeof(*source)||source->reserved||!source->offers||!source->execute)return Status::InvalidArgument;
    BehaviorSpec base{sizeof(BehaviorSpec),0,source->instanceHandle,source->context,nullptr,nullptr,source->higgsClient,source->higgsExecutor};return RegisterBehaviorRecord(writer,&base,registration,source->offers,source->execute);
}
Status RemoveCallback(std::uint64_t owner,std::uint64_t id,bool behavior) {
    if(!callbackStack.empty())return Status::Conflict;
    auto status=RuntimeClientIdentity(owner,behavior,nullptr);if(status!=Status::Ok)return status;
    std::shared_ptr<Callback> callback;
    { auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);auto& pool=behavior?s.behaviors:s.observers;
        auto it=std::find_if(pool.begin(),pool.end(),[&](const std::shared_ptr<Callback>& c){return c->id==id;});
        if(it==pool.end()&&behavior){auto retired=std::find_if(s.retired.begin(),s.retired.end(),[&](const std::shared_ptr<Callback>& c){return c->id==id;});if(retired!=s.retired.end()){if((*retired)->owner!=owner)return Status::Unauthorized;callback=*retired;s.retired.erase(retired);}}
        if(it==pool.end()&&!callback)return Status::NotFound;if(it!=pool.end()&&(*it)->owner!=owner)return Status::Unauthorized;
        if(it!=pool.end())callback=*it;
        {std::lock_guard<std::mutex> callbackLock(callback->mutex);callback->active=false;}
        if(it!=pool.end())pool.erase(it);if(behavior){InvalidateOffers(s);for(auto& p:s.pending)if(p.request.offer.behavior==id&&!Terminal(p.stage))p.stage=CommitStage::Cancelled;}
    }
    std::unique_lock<std::mutex> lock(callback->mutex);callback->drained.wait(lock,[&]{return !callback->inFlight;});return Status::Ok;
}
Status BS_CALL RemoveBehavior(std::uint64_t owner,std::uint64_t id) noexcept {return GuardRuntime([&]{return RemoveCallback(owner,id,true);});}
Status BS_CALL Subscribe(std::uint64_t reader,RuntimeEventCallback event,void* context,std::uint64_t* subscription) noexcept {
    return GuardRuntime([&] {
        SetRuntimeReleaseHook(DrainRuntimeClient);
        if(!event||!subscription)return Status::InvalidArgument;
        auto status=RuntimeClientIdentity(reader,false,nullptr);if(status!=Status::Ok)return status;
        auto c=std::make_shared<Callback>();c->owner=reader;c->event=event;c->context=context;
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);if(RuntimeClientIdentity(reader,false,nullptr)!=Status::Ok)return Status::Unauthorized;if(s.observers.size()>=kMaxRuntimeObservers)return Status::LimitExceeded;
        c->id=Allocate(s);if(!c->id)return Status::LimitExceeded;s.observers.push_back(c);*subscription=c->id;return Status::Ok;
    });
}
Status BS_CALL Unsubscribe(std::uint64_t reader,std::uint64_t id) noexcept {return GuardRuntime([&]{return RemoveCallback(reader,id,false);});}
Status BS_CALL ReportContentCommit(std::uint64_t writer,const ContentCommit* commit) noexcept {
    return GuardRuntime([&] {
        if(!commit||commit->structSize!=sizeof(*commit)||commit->stage<1||commit->stage>5)return Status::InvalidArgument;
        auto status=RuntimeOwnsInstance(writer,commit->instanceHandle);if(status!=Status::Ok)return status;
        status=SyncConfiguration();if(status!=Status::Ok)return status;
        ActionRequest authorityRequest{};
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);auto it=std::find_if(s.pending.begin(),s.pending.end(),[&](const Pending& p){return p.request.requestId==commit->requestId;});if(it==s.pending.end())return Status::NotFound;if(it->owner!=writer)return Status::Unauthorized;authorityRequest=it->request;}
        const auto validator=contentValidator.load(std::memory_order_acquire);if(!validator)return Status::Unavailable;
        status=validator(*commit,authorityRequest);if(status!=Status::Ok)return status;
        status=RuntimeOwnsInstance(writer,commit->instanceHandle);if(status!=Status::Ok)return status;
        status=SyncConfiguration();if(status!=Status::Ok)return status;
        std::vector<RuntimeEvent> events;
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            auto it=std::find_if(s.pending.begin(),s.pending.end(),[&](const Pending& p){return p.request.requestId==commit->requestId;});
            if(it==s.pending.end())return Status::NotFound;auto& p=*it;
            const auto& req=p.request;
            if(p.owner!=writer)return Status::Unauthorized;
            if(commit->epoch!=s.epoch||commit->epoch!=req.grant.epoch||commit->higgsRequestId!=req.grant.requestId||commit->higgsGrantId!=req.grant.grantId||commit->instanceHandle!=req.offer.context.slot.configured.instance.instanceHandle||commit->expectedContentVersion!=req.offer.context.slot.contentVersion)return Status::Conflict;
            if(!std::memcmp(&p.last,commit,sizeof(*commit))||(p.committed&&!std::memcmp(&p.receipt,commit,sizeof(*commit))))return Status::Ok;
            if(p.terminalReceipt)return Status::Conflict;
            auto* row=Find(s,commit->instanceHandle);if(!row)return Status::NotFound;
            const auto stage=static_cast<CommitStage>(commit->stage);
            if(stage==CommitStage::Accepted) {if(p.stage!=CommitStage::Accepted||p.authoritySettled)return Status::Conflict;}
            else if(stage==CommitStage::Committed) {
                if(p.authoritySettled||p.committed||row->contentVersion!=commit->expectedContentVersion||commit->contentVersion!=row->contentVersion+1||!commit->contentVersion||((commit->quantity==0)!=(commit->contentIdentity==0)))return Status::Conflict;
                row->contentVersion=commit->contentVersion;row->contentIdentity=commit->contentIdentity;row->quantity=commit->quantity;
                row->runtimeRevision=++s.revision;row->presentationState=static_cast<std::uint32_t>(PresentationState::Pending);
                p.committed=p.handCommitted=true;p.receipt=*commit;s.offers.clear();events.push_back(Event(s,*row,RuntimeEventKind::ContentChanged));
            } else if(stage==CommitStage::Completed && !p.committed && commit->contentVersion!=row->contentVersion) return Status::Conflict;
            if((stage==CommitStage::Failed||stage==CommitStage::Cancelled)&&p.handCommitted&&!p.committed&&!p.authoritySettled)return Status::Conflict;
            if(Terminal(stage))p.authoritySettled=p.terminalReceipt=true;
            p.stage=stage;p.last=*commit;
        }
        Events(events);return Status::Ok;
    });
}
Status BS_CALL SetVisual(std::uint64_t writer,std::uint64_t handle,const SlotVisualSpec* visual) noexcept {
    return GuardRuntime([&] {
        if(!visual)return Status::InvalidArgument;auto validation=ValidateRuntimeVisual(*visual);if(validation!=Status::Ok)return validation;
        auto status=RuntimeOwnsInstance(writer,handle);if(status!=Status::Ok)return status;status=SyncConfiguration();if(status!=Status::Ok)return status;
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);auto* row=Find(s,handle);if(!row)return Status::NotFound;
        if(visual->contentVersion!=row->contentVersion)return Status::Conflict;
        }
        status=SetSavedRuntimeCustomization(writer,handle,nullptr,visual);if(status!=Status::Ok)return status;return SyncConfiguration();
    });
}
Status BS_CALL PublishContentObservation(std::uint64_t writer,const ContentObservation* input,
                                         std::uint64_t* contentVersion) noexcept {
    return GuardRuntime([&] {
        if(!input||!contentVersion)return Status::InvalidArgument;
        const auto observation=*input;
        if(observation.structSize!=sizeof(observation)||observation.reserved||!observation.epoch||
           !observation.saveGeneration||!observation.instanceHandle||
           ((observation.quantity==0)!=(observation.contentIdentity==0)))return Status::InvalidArgument;
        auto status=RuntimeOwnsInstance(writer,observation.instanceHandle);if(status!=Status::Ok)return status;
        status=SyncConfiguration();if(status!=Status::Ok)return status;
        std::vector<RuntimeEvent> events;
        std::uint64_t version=0;
        {
            auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            if(!Own(writer,observation.instanceHandle))return Status::Unauthorized;
            auto* row=Find(s,observation.instanceHandle);if(!row)return Status::NotFound;
            if(observation.epoch!=s.epoch||observation.saveGeneration!=s.configuration.saveGeneration||
               observation.expectedContentVersion!=row->contentVersion)return Status::Conflict;
            version=row->contentVersion;
            if(observation.contentIdentity!=row->contentIdentity||observation.quantity!=row->quantity) {
                if(s.dragSlot==observation.instanceHandle)return Status::Conflict;
                for(const auto& pending:s.pending)
                    if(pending.request.offer.context.slot.configured.instance.instanceHandle==observation.instanceHandle&&
                       Reserved(pending))return Status::Conflict;
                if(version==UINT64_MAX)return Status::LimitExceeded;
                version=++row->contentVersion;
                row->contentIdentity=observation.contentIdentity;row->quantity=observation.quantity;
                row->runtimeRevision=++s.revision;
                row->presentationState=static_cast<std::uint32_t>(PresentationState::Pending);
                row->displayedContentVersion=0;
                InvalidateOffers(s);
                events.push_back(Event(s,*row,RuntimeEventKind::ContentChanged));
            }
        }
        *contentVersion=version;
        Events(events);return Status::Ok;
    });
}
ActionProgress Progress(const Pending& pending) {
    const auto& slot=pending.request.offer.context.slot;
    ActionProgress result{pending.request.grant,pending.request.requestId,slot.configured.instance.instanceHandle,slot.contentVersion,
        slot.contentVersion,slot.contentIdentity,slot.quantity,static_cast<std::uint32_t>(pending.stage),pending.committed?1u:0u,pending.handCommitted?1u:0u,0};
    if(pending.committed){result.contentVersion=pending.receipt.contentVersion;result.contentIdentity=pending.receipt.contentIdentity;result.quantity=pending.receipt.quantity;}
    return result;
}
Status BS_CALL CopyActionStatus(std::uint64_t writer,std::uint64_t id,ActionProgress* progress) noexcept {
    return GuardRuntime([&]{if(!progress)return Status::InvalidArgument;auto status=RuntimeClientIdentity(writer,true,nullptr);if(status!=Status::Ok)return status;status=SyncConfiguration();if(status!=Status::Ok)return status;auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);for(const auto& p:s.pending)if(p.request.requestId==id){if(p.owner!=writer)return Status::Unauthorized;*progress=Progress(p);return Status::Ok;}return Status::NotFound;});
}
const RuntimeApi api{sizeof(RuntimeApi),kRuntimeApiVersion,7,CopyRuntime,SetBinding,RegisterBehavior,RemoveBehavior,Subscribe,Unsubscribe,ReportContentCommit,SetVisual,CopyActionStatus,CopySavedRuntimeCustomizations,ClearSavedRuntimeCustomization};
Status BS_CALL SetZoneShape(std::uint64_t editor,std::uint64_t handle,const ZoneShape* shape,std::uint64_t save,std::uint64_t view) noexcept {
    return GuardRuntime([&]{const auto status=SetSavedZoneShape(editor,handle,shape,save,view);return status==Status::Ok?SyncConfiguration():status;});
}
Status BS_CALL ClearZoneShape(std::uint64_t editor,const InstanceCustomizationKey* key,std::uint64_t save,std::uint64_t view) noexcept {
    return GuardRuntime([&]{const auto status=ClearSavedZoneShape(editor,key,save,view);return status==Status::Ok?SyncConfiguration():status;});
}
Status BS_CALL QueryZones(std::uint64_t reader,const ZoneProbe* probe,ZoneRecord* records,std::uint32_t capacity,RuntimeView* view) noexcept {
    return GuardRuntime([&]{
        if(!probe||!view||capacity>kMaxInstances||(capacity&&!records)||probe->structSize!=sizeof(*probe)||probe->source<1||probe->source>6||probe->valid>1||probe->reserved||(probe->physicalHand&&!HandValid(probe->physicalHand))||!probe->epoch||!probe->frame||!probe->sampleId||(probe->valid&&!Finite3(probe->world)))return Status::InvalidArgument;
        if(view->structSize!=sizeof(*view)||view->version!=kRuntimeApiVersion||view->reserved)return Status::UnsupportedVersion;
        auto status=RuntimeConfigurationReader(reader);if(status!=Status::Ok)return status;status=SyncConfiguration();if(status!=Status::Ok)return status;
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
        RuntimeView header{sizeof(RuntimeView),kRuntimeApiVersion,s.epoch,s.frame,s.revision,s.configuration.saveGeneration,s.configuration.viewRevision,static_cast<std::uint32_t>(s.rows.size()),0};
        *view=header;if(probe->epoch!=s.epoch||probe->frame!=s.frame)return Status::Conflict;if(capacity<s.rows.size())return Status::BufferTooSmall;
        for(std::uint32_t i=0;i<s.rows.size();++i){const auto& row=s.rows[i];const auto* shape=FindShape(s,row.configured.instance.instanceHandle);if(!shape)return Status::InternalError;records[i]={row.configured.instance.instanceHandle,s.epoch,s.frame,s.revision,row.bodyGeneration,row.anchorGeneration,row.world,shape->shape,row.availability,(probe->valid&&row.availability==0&&ZoneContainsPoint(shape->shape,row.world,probe->world))?1u:0u,row.insideHands,shape->overridden};}return Status::Ok;
    });
}
const ZoneApi zoneApi{sizeof(ZoneApi),kZoneApiVersion,3,SetZoneShape,ClearZoneShape,QueryZones,CopySavedZoneShapes};
Status BS_CALL SetContentPolicy(std::uint64_t editor,std::uint64_t handle,const SlotContentPolicy* policy,std::uint64_t save,std::uint64_t view) noexcept {
    return GuardRuntime([&]{const auto status=SetSavedContentPolicy(editor,handle,policy,save,view);return status==Status::Ok?SyncConfiguration():status;});
}
Status BS_CALL ClearContentPolicy(std::uint64_t editor,const InstanceCustomizationKey* key,std::uint64_t save,std::uint64_t view) noexcept {
    return GuardRuntime([&]{const auto status=ClearSavedContentPolicy(editor,key,save,view);return status==Status::Ok?SyncConfiguration():status;});
}
Status BS_CALL CopyEligibility(std::uint64_t reader,std::uint64_t handle,std::uint32_t hand,std::uint64_t epoch,std::uint64_t observation,ContentEligibility* output) noexcept {
    return GuardRuntime([&]{if(!output||output->structSize!=sizeof(*output)||!HandValid(hand)||!epoch||!observation)return Status::InvalidArgument;if(RuntimeConfigurationReader(reader)!=Status::Ok)return Status::Unauthorized;const auto status=SyncConfiguration();if(status!=Status::Ok)return status;auto& s=Runtime();std::lock_guard lock(s.mutex);const auto* row=FindShape(s,handle);if(!row)return Status::NotFound;if(epoch!=s.epoch)return Status::Conflict;ContentEligibility result{sizeof(ContentEligibility),hand,row->hasPolicy?0u:1u,row->hasPolicy?0u:1u,handle,epoch,observation,0,0};for(const auto& sample:s.policySamples)if(sample.instanceHandle==handle&&sample.physicalHand==hand&&sample.epoch==epoch&&sample.handObservationId==observation&&(sample.source==0||sample.baseFormId)){result.known=sample.known;result.allowed=sample.known?sample.allowed:0;result.baseFormId=sample.baseFormId;result.category=sample.category;break;}*output=result;return Status::Ok;});
}
const ContentPolicyApi contentPolicyApi{sizeof(ContentPolicyApi),kContentPolicyApiVersion,3,SetContentPolicy,ClearContentPolicy,CopySavedContentPolicies,CopyEligibility};
const BehaviorApi behaviorApi{sizeof(BehaviorApi),kBehaviorApiVersion,1,RegisterSourceBehavior};
} // namespace

bool ComposeSlotTransform(const Definition& d,const RuntimeTransform& anchor,RuntimeTransform& out,float center[3],float& radius) noexcept {
    if(!TransformValid(anchor)||ValidateDefinition(d)!=Status::Ok)return false;
    RuntimeTransform result{};result.scale=anchor.scale;float offset[3];Rotate(anchor.rotation,d.position,offset);
    for(int i=0;i<3;++i)result.position[i]=anchor.position[i]+anchor.scale*offset[i];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)result.rotation[i*3+j]+=anchor.rotation[i*3+k]*d.rotation[k*3+j];
    float region[3];Rotate(result.rotation,d.regionCenter,region);
    for(int i=0;i<3;++i)region[i]=result.position[i]+anchor.scale*region[i];
    const float r=d.radius*anchor.scale;if(!Finite3(result.position)||!Finite3(region)||!std::isfinite(r)||r<=0)return false;
    out=result;std::copy(region,region+3,center);radius=r;return true;
}
bool ContainsPoint(const float center[3],float radius,const float point[3]) noexcept {
    if(!center||!point||!Finite3(center)||!Finite3(point)||!std::isfinite(radius)||radius<=0)return false;
    double distance=0;for(int i=0;i<3;++i){const double d=double(center[i])-point[i];distance+=d*d;}return distance<=double(radius)*radius;
}
Status UpdateRuntime(const RuntimeFrame& frame) noexcept {
    return GuardRuntime([&] {
        if(!frame.epoch||!frame.frame||!std::isfinite(frame.timeSeconds)||frame.timeSeconds<0||frame.anchorCount>kMaxInstances||(frame.anchorCount&&!frame.anchors))return Status::InvalidArgument;
        for(std::uint32_t i=0;i<frame.anchorCount;++i) {
            if(!frame.anchors[i].instanceHandle||frame.anchors[i].reserved||frame.anchors[i].valid>1)return Status::InvalidArgument;
            for(std::uint32_t j=0;j<i;++j)if(frame.anchors[i].instanceHandle==frame.anchors[j].instanceHandle)return Status::InvalidArgument;
        }
        auto status=SyncConfiguration();if(status!=Status::Ok)return status;std::vector<RuntimeEvent> events;
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            if(s.epoch==frame.epoch&&(frame.frame<=s.frame||frame.timeSeconds<s.time))return Status::Conflict;
            bool changed=s.epoch!=frame.epoch||s.body!=frame.bodyGeneration;
            if(s.epoch!=frame.epoch){s.used.clear();s.pending.clear();s.dragSlot=0;s.dragRequest=0;}
            s.epoch=frame.epoch;s.frame=frame.frame;s.body=frame.bodyGeneration;s.time=frame.timeSeconds;++s.revision;
            s.hands[0]={};s.hands[1]={};
            for(const auto& hand:frame.hands)if(HandValid(hand.physicalHand)&&hand.valid==1&&hand.sampleId&&Finite3(hand.world))s.hands[hand.physicalHand==Left?0:1]=hand;
            if(frame.hands[0].physicalHand==frame.hands[1].physicalHand){s.hands[0]={};s.hands[1]={};}
            for(auto& row:s.rows) {
                const auto oldInside=row.insideHands;const auto oldAvailability=row.availability;const auto oldAnchor=row.anchorGeneration;
                const auto& instance=row.configured.instance;row.epoch=s.epoch;row.frame=s.frame;row.bodyGeneration=s.body;row.runtimeRevision=s.revision;row.insideHands=0;
                const AnchorSample* anchor=nullptr;for(std::uint32_t i=0;i<frame.anchorCount;++i)if(frame.anchors[i].instanceHandle==instance.instanceHandle)anchor=&frame.anchors[i];
                row.availability=static_cast<std::uint32_t>(RuntimeAvailability::Available);
                if(instance.templateState!=static_cast<std::uint32_t>(InstanceTemplateState::Current))row.availability=static_cast<std::uint32_t>(RuntimeAvailability::TemplateUnavailable);
                else if(!(instance.flags&kInstanceActive)||!(instance.effective.flags&kEnabled))row.availability=static_cast<std::uint32_t>(RuntimeAvailability::Inactive);
                else if(!frame.bodyGeneration)row.availability=static_cast<std::uint32_t>(RuntimeAvailability::InvalidObservation);
                else if(row.binding.kind==static_cast<std::uint32_t>(RuntimeBindingKind::UnresolvedEquipment)||!anchor||!anchor->valid)row.availability=static_cast<std::uint32_t>(RuntimeAvailability::AnchorUnavailable);
                else if(!anchor->anchorGeneration||!ComposeSlotTransform(instance.effective,anchor->world,row.world,row.regionCenter,row.radius))row.availability=static_cast<std::uint32_t>(RuntimeAvailability::InvalidObservation);
                row.anchorGeneration=anchor?anchor->anchorGeneration:0;
                const auto* shape=FindShape(s,instance.instanceHandle);
                if(row.availability==0&&(!shape||ValidateZoneShape(shape->shape)!=Status::Ok))row.availability=static_cast<std::uint32_t>(RuntimeAvailability::InvalidObservation);
                if(row.availability==0)for(const auto& hand:s.hands)if(hand.valid&&ZoneContainsPoint(shape->shape,row.world,hand.world))row.insideHands|=hand.physicalHand;
                if(row.availability!=oldAvailability||row.anchorGeneration!=oldAnchor){changed=true;events.push_back(Event(s,row,RuntimeEventKind::Invalidated));}
                for(auto hand:{Left,Right}) {
                    if((oldInside&hand)&&!(row.insideHands&hand))events.push_back(Event(s,row,RuntimeEventKind::Exit,hand));
                    if(row.insideHands&hand)events.push_back(Event(s,row,(oldInside&hand)?RuntimeEventKind::Hover:RuntimeEventKind::Enter,hand));
                }
            }
            if(changed)InvalidateOffers(s);
        }
        Events(events);return Status::Ok;
    });
}
Status CollectOffers(const RuntimeInput& input,ActionOffer* output,std::uint32_t capacity,std::uint32_t* count) noexcept {
    return GuardRuntime([&] {
        if(input.structSize!=sizeof(input)||!count||(capacity&&!output)||capacity>kMaxActionOffers||!HandValid(input.physicalHand)||input.available>1||!input.inputSequence||!input.poseSample||(input.contentCategory&&(input.contentCategory&(input.contentCategory-1)))||(input.contentCategory&~AllContent))return Status::InvalidArgument;
        auto status=SyncConfiguration();if(status!=Status::Ok)return status;
        struct Work {std::shared_ptr<Callback> callback;SourceActionContext context;};std::vector<Work> work;std::uint64_t policy=0;
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            if(input.epoch!=s.epoch||input.frame!=s.frame||s.hands[input.physicalHand==Left?0:1].sampleId!=input.poseSample)return Status::Conflict;
            if(!input.available){*count=0;return Status::Ok;}policy=s.policy;
            for(const auto& c:s.behaviors){auto* row=Find(s,c->behavior.instanceHandle);if(!row||row->availability||!(row->insideHands&input.physicalHand)||!(row->configured.instance.effective.handMask&input.physicalHand)||(!c->offers2&&input.contentCategory&&!(row->configured.instance.effective.contentMask&input.contentCategory)))continue;
                if(!c->offers2&&!LegacyPolicyAllows(s,c->behavior.instanceHandle,input))continue;
                bool reserved=s.dragSlot==c->behavior.instanceHandle;for(const auto& p:s.pending)if(Reserved(p)&&p.request.offer.context.slot.configured.instance.instanceHandle==c->behavior.instanceHandle)reserved=true;
                if(!reserved){SourceActionContext context{{*row,input},{IncomingItem(s,c->behavior.instanceHandle,input,1),IncomingItem(s,c->behavior.instanceHandle,input,2)}};work.push_back({c,context});}
            }
        }
        struct Produced {ActionOffer offer;IncomingItemContext incoming;};std::vector<Produced> result;
        for(const auto& w:work) {
            if(!Own(w.callback->owner,w.callback->behavior.instanceHandle))continue;
            std::array<ActionProposal,kMaxOffersPerBehavior> proposals{};std::array<SourceActionProposal,kMaxOffersPerBehavior> sourceProposals{};std::uint32_t size=0;
            {CallLease lease(w.callback);if(!lease)continue;try {if(w.callback->offers2)status=w.callback->offers2(w.callback->behavior.context,&w.context,sourceProposals.data(),kMaxOffersPerBehavior,&size);else status=w.callback->behavior.offers(w.callback->behavior.context,&w.context.context,proposals.data(),kMaxOffersPerBehavior,&size);}catch(...){status=Status::InternalError;}}
            if(status!=Status::Ok)continue;if(size>kMaxOffersPerBehavior)return Status::LimitExceeded;
            for(std::uint32_t i=0;i<size;++i){const auto& p=w.callback->offers2?sourceProposals[i].proposal:proposals[i];if(!p.action||p.reserved)return Status::InvalidArgument;auto context=w.context.context;IncomingItemContext incoming{};
                if(w.callback->offers2){const auto source=sourceProposals[i].incomingSource;if(source>2||sourceProposals[i].reserved)return Status::InvalidArgument;if(source){incoming=w.context.items[source-1];if(!incoming.known||!incoming.allowed||!incoming.category||!(context.slot.configured.instance.effective.contentMask&incoming.category))continue;context.input.contentCategory=incoming.category;context.input.itemIdentity=source==1?context.input.heldRefId:context.input.equippedFormId;}else{context.input.contentCategory=0;context.input.itemIdentity=0;}}
                result.push_back({{context,p,0,w.callback->id,policy,w.callback->behavior.higgsClient,w.callback->behavior.higgsExecutor},incoming});}
        }
        std::sort(result.begin(),result.end(),[](const Produced& ap,const Produced& bp){const auto& a=ap.offer;const auto& b=bp.offer;
            if(a.proposal.priority!=b.proposal.priority)return a.proposal.priority>b.proposal.priority;
            const auto ah=a.context.slot.configured.instance.instanceHandle,bh=b.context.slot.configured.instance.instanceHandle;
            if(ah!=bh)return ah<bh;if(a.behavior!=b.behavior)return a.behavior<b.behavior;
            if(a.proposal.action!=b.proposal.action)return a.proposal.action<b.proposal.action;return a.proposal.executorAction<b.proposal.executorAction;
        });
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);if(s.policy!=policy||s.epoch!=input.epoch||s.frame!=input.frame)return Status::Conflict;
        if(result.size()>capacity){*count=static_cast<std::uint32_t>(result.size());return Status::BufferTooSmall;}
        for(auto& row:result){row.offer.offerId=Allocate(s);if(!row.offer.offerId)return Status::LimitExceeded;}
        for(const auto& row:result){if(s.offers.size()==kMaxActionOffers){const auto retired=s.offers.front().offerId;s.offers.erase(s.offers.begin());s.offerSources.erase(std::remove_if(s.offerSources.begin(),s.offerSources.end(),[&](const State::OfferSource& source){return source.offerId==retired;}),s.offerSources.end());}s.offers.push_back(row.offer);if(row.incoming.source)s.offerSources.push_back({row.offer.offerId,row.incoming});}
        for(std::uint32_t i=0;i<result.size();++i)output[i]=result[i].offer;*count=static_cast<std::uint32_t>(result.size());return Status::Ok;
    });
}
Status DispatchSelected(const ActionOffer& offer,const ActionGrant& grant,std::uint64_t* requestId) noexcept {
    return GuardRuntime([&] {
        if(!requestId||!grant.requestId||!grant.grantId||grant.reserved||!HandValid(grant.physicalHand))return Status::InvalidArgument;
        auto status=SyncConfiguration();if(status!=Status::Ok)return status;std::shared_ptr<Callback> callback;ActionRequest request{};IncomingItemContext incoming{};
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            auto cached=std::find_if(s.offers.begin(),s.offers.end(),[&](const ActionOffer& o){return o.offerId==offer.offerId;});
            if(cached==s.offers.end()||std::memcmp(&*cached,&offer,sizeof(offer)))return Status::Conflict;
            if(grant.epoch!=s.epoch||grant.epoch!=offer.context.input.epoch||grant.contextId!=offer.offerId||grant.contextGeneration!=s.policy||offer.policyRevision!=s.policy||grant.inputSequence!=offer.context.input.inputSequence||grant.physicalHand!=offer.context.input.physicalHand)return Status::Conflict;
            auto* row=Find(s,offer.context.slot.configured.instance.instanceHandle);
            if(!row||row->availability||!(row->insideHands&grant.physicalHand)||row->bodyGeneration!=offer.context.slot.bodyGeneration||row->anchorGeneration!=offer.context.slot.anchorGeneration||row->contentVersion!=offer.context.slot.contentVersion)return Status::Conflict;
            for(const auto& used:s.used)if(used.epoch==grant.epoch&&used.sequence==grant.inputSequence&&used.hand==grant.physicalHand)return Status::Conflict;
            if(s.dragSlot==row->configured.instance.instanceHandle)return Status::Conflict;
            for(const auto& p:s.pending)if(Reserved(p)&&p.request.offer.context.slot.configured.instance.instanceHandle==row->configured.instance.instanceHandle)return Status::Conflict;
            for(const auto& c:s.behaviors)if(c->id==offer.behavior)callback=c;
            if(!callback)return Status::NotFound;
            for(const auto& source:s.offerSources)if(source.offerId==offer.offerId)incoming=source.incoming;
            if(s.pending.size()==kMaxPendingActions){auto old=std::find_if(s.pending.begin(),s.pending.end(),[](const Pending& p){return !Reserved(p);});if(old==s.pending.end())return Status::LimitExceeded;s.pending.erase(old);}
            request={offer,grant,Allocate(s)};if(!request.requestId)return Status::LimitExceeded;
            if(s.used.size()==kMaxActionOffers)s.used.erase(s.used.begin());s.used.push_back({grant.epoch,grant.inputSequence,grant.physicalHand});
            Pending pending{};pending.request=request;pending.owner=callback->owner;s.pending.push_back(pending);
        }
        if(!Own(callback->owner,callback->behavior.instanceHandle))status=Status::Unauthorized;
        else {CallLease lease(callback);if(!lease)status=Status::Unavailable;else try {if(callback->execute2){const SourceActionRequest selected{request,incoming};status=callback->execute2(callback->behavior.context,&selected);}else status=callback->behavior.execute(callback->behavior.context,&request);}catch(...){status=Status::InternalError;}}
        if(status!=Status::Ok){auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);for(auto& p:s.pending)if(p.request.requestId==request.requestId&&!p.committed&&!Terminal(p.stage))p.stage=CommitStage::Failed;}
        *requestId=request.requestId;return status;
    });
}
void InvalidateRuntime() noexcept {
    try {std::vector<RuntimeEvent> events;auto& s=Runtime();std::unique_lock<std::mutex> lock(s.mutex);++s.revision;InvalidateOffers(s);s.dragSlot=0;s.dragRequest=0;s.hands[0]={};s.hands[1]={};for(auto& r:s.rows){for(auto hand:{Left,Right})if(r.insideHands&hand)events.push_back(Event(s,r,RuntimeEventKind::Exit,hand));events.push_back(Event(s,r,RuntimeEventKind::Invalidated));r.availability=static_cast<std::uint32_t>(RuntimeAvailability::InvalidObservation);r.insideHands=0;r.runtimeRevision=s.revision;}lock.unlock();Events(events);}catch(...){}
}
Status ReserveDragSlot(std::uint64_t handle,std::uint64_t request,std::uint64_t epoch,std::uint64_t revision) noexcept {
    return GuardRuntime([&]{auto& s=Runtime();std::lock_guard lock(s.mutex);const auto* row=Find(s,handle);if(!request||!row||s.epoch!=epoch||row->runtimeRevision!=revision||row->availability||s.dragSlot)return Status::Conflict;for(const auto& pending:s.pending)if(Reserved(pending)&&pending.request.offer.context.slot.configured.instance.instanceHandle==handle)return Status::Conflict;s.dragSlot=handle;s.dragRequest=request;InvalidateOffers(s);return Status::Ok;});
}
void ReleaseDragSlot(std::uint64_t request) noexcept {
    try{auto& s=Runtime();std::lock_guard lock(s.mutex);if(s.dragRequest==request){s.dragSlot=0;s.dragRequest=0;InvalidateOffers(s);}}catch(...){}
}
Status PublishContentPolicySamples(const ContentPolicySample* samples,std::uint32_t count,std::uint64_t save,std::uint64_t view) noexcept {
    return GuardRuntime([&]{if(count>kMaxInstances*4||(count&&!samples))return Status::InvalidArgument;std::vector<ContentPolicySample> next;if(count)next.assign(samples,samples+count);for(std::uint32_t i=0;i<count;++i){const auto& sample=next[i];if(!sample.instanceHandle||!sample.epoch||!sample.handObservationId||!HandValid(sample.physicalHand)||sample.known>1||sample.allowed>1||sample.source>2||(!sample.known&&sample.allowed)||(sample.category&&(sample.category&(sample.category-1)))||(sample.category&~AllContent))return Status::InvalidArgument;for(std::uint32_t j=0;j<i;++j)if(next[j].instanceHandle==sample.instanceHandle&&next[j].physicalHand==sample.physicalHand&&next[j].source==sample.source)return Status::InvalidArgument;}
        auto& s=Runtime();std::lock_guard lock(s.mutex);if(save!=s.configuration.saveGeneration||view!=s.configuration.viewRevision)return Status::Conflict;for(const auto& sample:next)if(sample.epoch!=s.epoch||!Find(s,sample.instanceHandle))return Status::Conflict;s.policySamples.swap(next);return Status::Ok;});
}
bool RevalidateOfferContent(const ActionOffer& offer,std::uint64_t observation) noexcept {
    try{auto& s=Runtime();std::lock_guard lock(s.mutex);if(!offer.context.input.contentCategory)return true;const auto* policy=FindShape(s,offer.context.slot.configured.instance.instanceHandle);if(!policy)return false;IncomingItemContext selected{};for(const auto& source:s.offerSources)if(source.offerId==offer.offerId)selected=source.incoming;
        if(!selected.source&&!policy->hasPolicy)return true;const auto kind=selected.source?selected.source:(offer.context.input.heldRefId?1u:2u);
        for(const auto& sample:s.policySamples)if(sample.instanceHandle==offer.context.slot.configured.instance.instanceHandle&&sample.epoch==offer.context.input.epoch&&sample.handObservationId==observation&&sample.physicalHand==offer.context.input.physicalHand&&(sample.source==kind||(!selected.source&&sample.source==0))&&sample.known&&sample.allowed&&sample.category==offer.context.input.contentCategory&&(!selected.source||sample.baseFormId==selected.baseFormId))return true;return false;
    }catch(...){return false;}
}
Status CopyActionProgress(ActionProgress* output,std::uint32_t capacity,std::uint32_t* count) noexcept {
    // Deliberately precedes configuration synchronization: the adapter must first
    // publish already-confirmed HIGGS commitments before invalidating old poses.
    return GuardRuntime([&]{if(!count||capacity>kMaxPendingActions||(capacity&&!output))return Status::InvalidArgument;auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);*count=static_cast<std::uint32_t>(s.pending.size());if(capacity<s.pending.size())return Status::BufferTooSmall;for(std::size_t i=0;i<s.pending.size();++i)output[i]=Progress(s.pending[i]);return Status::Ok;});
}
void SetContentCommitValidator(ContentCommitValidator validator) noexcept {contentValidator.store(validator,std::memory_order_release);}
Status CopyPendingHandActions(PendingHandAction* output,std::uint32_t capacity,std::uint32_t* count) noexcept {
    return GuardRuntime([&]{
        if(!count||capacity>kMaxPendingActions||(capacity&&!output))return Status::InvalidArgument;
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
        *count=static_cast<std::uint32_t>(std::count_if(s.pending.begin(),s.pending.end(),[](const Pending& p){return !p.authoritySettled;}));
        if(capacity<*count)return Status::BufferTooSmall;
        std::uint32_t index=0;for(const auto& p:s.pending)if(!p.authoritySettled)output[index++]={p.request.grant,p.request.offer.higgsExecutor};
        return Status::Ok;
    });
}
Status ApplyHandResult(std::uint64_t request,std::uint64_t epoch,CommitStage stage,bool compensated,bool noncommitConfirmed) noexcept {
    return GuardRuntime([&]{
        if(!request||!epoch||(stage!=CommitStage::Failed&&stage!=CommitStage::Cancelled&&stage!=CommitStage::Committed&&stage!=CommitStage::Completed))return Status::InvalidArgument;
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
        for(auto& pending:s.pending)if(pending.request.grant.requestId==request&&pending.request.grant.epoch==epoch){
            if(stage==CommitStage::Committed||stage==CommitStage::Completed){
                pending.handCommitted=true; // An irreversible fact survives local cancellation.
                if(!pending.committed&&!pending.terminalReceipt&&!pending.handCompensated)pending.authoritySettled=false;
            }
            if(compensated&&Terminal(stage)){
                pending.handCompensated=true;
                pending.authoritySettled=true;
                if(!pending.terminalReceipt)pending.stage=CommitStage::Cancelled;
            }else if(stage==CommitStage::Failed||stage==CommitStage::Cancelled){
                if(noncommitConfirmed&&(!pending.handCommitted||pending.committed))pending.authoritySettled=true;
                if(!pending.terminalReceipt)pending.stage=stage;
            }
            return Status::Ok;
        }
        return Status::NotFound;
    });
}
Status ReportPresentation(std::uint64_t handle,std::uint64_t visual,std::uint64_t content,PresentationState state) noexcept {
    return GuardRuntime([&]{if(state!=PresentationState::Ready&&state!=PresentationState::Failed)return Status::InvalidArgument;auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);auto* row=Find(s,handle);if(!row)return Status::NotFound;if(row->visualRevision!=visual||row->contentVersion!=content||row->visual.contentVersion!=content)return Status::Conflict;row->displayedContentVersion=state==PresentationState::Ready?content:0;row->presentationState=static_cast<std::uint32_t>(state);row->runtimeRevision=++s.revision;return Status::Ok;});
}
const RuntimeApi& GetRuntimeApi() noexcept {return api;}
const ZoneApi& GetZoneApi() noexcept {return zoneApi;}
const ContentPolicyApi& GetContentPolicyApi() noexcept{return contentPolicyApi;}
const BehaviorApi& GetBehaviorApi() noexcept{return behaviorApi;}
const ContentObservationApi& GetContentObservationApi() noexcept {
    static const ContentObservationApi table{sizeof(ContentObservationApi),kContentObservationApiVersion,1,
                                              PublishContentObservation};
    return table;
}
} // namespace body_slots::detail
extern "C" __declspec(dllexport) const body_slots::RuntimeApi* BS_CALL BodySlots_QueryRuntimeInterface(std::uint32_t version,std::uint32_t size) noexcept {
    if(version!=body_slots::kRuntimeApiVersion||size!=sizeof(body_slots::RuntimeApi))return nullptr;return &body_slots::detail::GetRuntimeApi();
}
extern "C" __declspec(dllexport) const body_slots::ZoneApi* BS_CALL BodySlots_QueryZoneInterface(std::uint32_t version,std::uint32_t size) {
    if(version!=body_slots::kZoneApiVersion||size!=sizeof(body_slots::ZoneApi))return nullptr;return &body_slots::detail::GetZoneApi();
}
extern "C" __declspec(dllexport) const body_slots::ContentPolicyApi* BS_CALL BodySlots_QueryContentPolicyInterface(std::uint32_t version,std::uint32_t size) noexcept {
    return version==body_slots::kContentPolicyApiVersion&&size==sizeof(body_slots::ContentPolicyApi)?&body_slots::detail::GetContentPolicyApi():nullptr;
}
extern "C" __declspec(dllexport) const body_slots::BehaviorApi* BS_CALL BodySlots_QueryBehaviorInterface(std::uint32_t version,std::uint32_t size) noexcept {
    return version==body_slots::kBehaviorApiVersion&&size==sizeof(body_slots::BehaviorApi)?&body_slots::detail::GetBehaviorApi():nullptr;
}
extern "C" __declspec(dllexport) const body_slots::ContentObservationApi* BS_CALL BodySlots_QueryContentObservationInterface(std::uint32_t version,std::uint32_t size) noexcept {
    return version==body_slots::kContentObservationApiVersion&&size==sizeof(body_slots::ContentObservationApi)?
        &body_slots::detail::GetContentObservationApi():nullptr;
}
