#include "runtime.h"
#include "instances.h"
#include "validation.h"
#include "runtime_saved.h"
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
    bool committed=false,handCommitted=false; ContentCommit receipt{}, last{};
};
struct State {
    std::mutex mutex;
    std::vector<RuntimeRecord> rows;
    std::vector<std::shared_ptr<Callback>> behaviors, observers, retired;
    std::vector<ActionOffer> offers;
    std::vector<Pending> pending;
    struct UsedInput { std::uint64_t epoch,sequence; std::uint32_t hand; };
    std::vector<UsedInput> used;
    InstanceView configuration{};
    std::uint64_t epoch=0,frame=0,body=0,revision=1,policy=1,nextId=1;
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
bool Terminal(CommitStage stage) { return stage==CommitStage::Completed || stage==CommitStage::Failed || stage==CommitStage::Cancelled; }
void InvalidateOffers(State& s) {
    ++s.policy; s.offers.clear();
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
        for(std::uint32_t j=0;j<copied->customizations.count;++j){const auto& saved=copied->customizations.entries[j];if(std::memcmp(&saved.key,&key,sizeof(key)))continue;if(saved.fieldMask&kSavedRuntimeBinding)row.binding=saved.binding;if(saved.fieldMask&kSavedRuntimeVisual)visual=saved.visual;break;}
        if(std::memcmp(&row.visual,&visual,sizeof(visual))){row.visual=visual;++row.visualRevision;row.presentationState=static_cast<std::uint32_t>((visual.holderModel[0]||visual.previewModel[0])?PresentationState::Pending:PresentationState::None);row.displayedContentVersion=0;}
        row.configured=copied->rows[i]; row.availability=static_cast<std::uint32_t>(RuntimeAvailability::ConfigurationChanged);
        row.insideHands=0; row.runtimeRevision=s.revision+1; next.push_back(row);
    }
    s.rows.swap(next); s.configuration=copied->view; ++s.revision; InvalidateOffers(s);
    if(!sameSave) {
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
Status BS_CALL RegisterBehavior(std::uint64_t writer,const BehaviorSpec* spec,std::uint64_t* registration) noexcept {
    return GuardRuntime([&] {
        SetRuntimeReleaseHook(DrainRuntimeClient);
        if(!spec||!registration||spec->structSize!=sizeof(*spec)||spec->reserved||!spec->offers||!spec->execute||!spec->higgsClient||!spec->higgsExecutor)return Status::InvalidArgument;
        auto status=RuntimeOwnsInstance(writer,spec->instanceHandle);if(status!=Status::Ok)return status;
        status=SyncConfiguration();if(status!=Status::Ok)return status;
        auto callback=std::make_shared<Callback>();callback->owner=writer;callback->behavior=*spec;
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
        if(!Own(writer,spec->instanceHandle))return Status::Unauthorized;
        SweepRetired(s);if(s.behaviors.size()+s.retired.size()>=kMaxBehaviors)return Status::LimitExceeded;
        for(const auto& c:s.behaviors)if(c->behavior.instanceHandle==spec->instanceHandle)return Status::Conflict;
        callback->id=Allocate(s);if(!callback->id)return Status::LimitExceeded;
        s.behaviors.push_back(callback);InvalidateOffers(s);*registration=callback->id;return Status::Ok;
    });
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
            if(Terminal(p.stage))return Status::Conflict;
            auto* row=Find(s,commit->instanceHandle);if(!row)return Status::NotFound;
            const auto stage=static_cast<CommitStage>(commit->stage);
            if(stage==CommitStage::Accepted) {if(p.stage!=CommitStage::Accepted)return Status::Conflict;}
            else if(stage==CommitStage::Committed) {
                if(p.committed||row->contentVersion!=commit->expectedContentVersion||commit->contentVersion!=row->contentVersion+1||!commit->contentVersion||((commit->quantity==0)!=(commit->contentIdentity==0)))return Status::Conflict;
                row->contentVersion=commit->contentVersion;row->contentIdentity=commit->contentIdentity;row->quantity=commit->quantity;
                row->runtimeRevision=++s.revision;row->presentationState=static_cast<std::uint32_t>(PresentationState::Pending);
                p.committed=true;p.receipt=*commit;s.offers.clear();events.push_back(Event(s,*row,RuntimeEventKind::ContentChanged));
            } else if(stage==CommitStage::Completed && !p.committed && commit->contentVersion!=row->contentVersion) return Status::Conflict;
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
            if(s.epoch!=frame.epoch){s.used.clear();s.pending.clear();}
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
                if(row.availability==0)for(const auto& hand:s.hands)if(hand.valid&&ContainsPoint(row.regionCenter,row.radius,hand.world))row.insideHands|=hand.physicalHand;
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
        struct Work {std::shared_ptr<Callback> callback;ActionContext context;};std::vector<Work> work;std::uint64_t policy=0;
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            if(input.epoch!=s.epoch||input.frame!=s.frame||s.hands[input.physicalHand==Left?0:1].sampleId!=input.poseSample)return Status::Conflict;
            if(!input.available){*count=0;return Status::Ok;}policy=s.policy;
            for(const auto& c:s.behaviors){auto* row=Find(s,c->behavior.instanceHandle);if(!row||row->availability||!(row->insideHands&input.physicalHand)||!(row->configured.instance.effective.handMask&input.physicalHand)||(input.contentCategory&&!(row->configured.instance.effective.contentMask&input.contentCategory)))continue;
                bool reserved=false;for(const auto& p:s.pending)if(!Terminal(p.stage)&&p.request.offer.context.slot.configured.instance.instanceHandle==c->behavior.instanceHandle)reserved=true;
                if(!reserved)work.push_back({c,{*row,input}});
            }
        }
        std::vector<ActionOffer> result;
        for(const auto& w:work) {
            if(!Own(w.callback->owner,w.callback->behavior.instanceHandle))continue;
            std::array<ActionProposal,kMaxOffersPerBehavior> proposals{};std::uint32_t size=0;
            {CallLease lease(w.callback);if(!lease)continue;try {status=w.callback->behavior.offers(w.callback->behavior.context,&w.context,proposals.data(),kMaxOffersPerBehavior,&size);}catch(...){status=Status::InternalError;}}
            if(status!=Status::Ok)continue;if(size>kMaxOffersPerBehavior)return Status::LimitExceeded;
            for(std::uint32_t i=0;i<size;++i){const auto& p=proposals[i];if(!p.action||p.reserved)return Status::InvalidArgument;result.push_back({w.context,p,0,w.callback->id,policy,w.callback->behavior.higgsClient,w.callback->behavior.higgsExecutor});}
        }
        std::sort(result.begin(),result.end(),[](const ActionOffer& a,const ActionOffer& b){
            if(a.proposal.priority!=b.proposal.priority)return a.proposal.priority>b.proposal.priority;
            const auto ah=a.context.slot.configured.instance.instanceHandle,bh=b.context.slot.configured.instance.instanceHandle;
            if(ah!=bh)return ah<bh;if(a.behavior!=b.behavior)return a.behavior<b.behavior;
            if(a.proposal.action!=b.proposal.action)return a.proposal.action<b.proposal.action;return a.proposal.executorAction<b.proposal.executorAction;
        });
        auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);if(s.policy!=policy||s.epoch!=input.epoch||s.frame!=input.frame)return Status::Conflict;
        if(result.size()>capacity){*count=static_cast<std::uint32_t>(result.size());return Status::BufferTooSmall;}
        for(auto& offer:result){offer.offerId=Allocate(s);if(!offer.offerId)return Status::LimitExceeded;}
        for(const auto& offer:result){if(s.offers.size()==kMaxActionOffers)s.offers.erase(s.offers.begin());s.offers.push_back(offer);}
        std::copy(result.begin(),result.end(),output);*count=static_cast<std::uint32_t>(result.size());return Status::Ok;
    });
}
Status DispatchSelected(const ActionOffer& offer,const ActionGrant& grant,std::uint64_t* requestId) noexcept {
    return GuardRuntime([&] {
        if(!requestId||!grant.requestId||!grant.grantId||grant.reserved||!HandValid(grant.physicalHand))return Status::InvalidArgument;
        auto status=SyncConfiguration();if(status!=Status::Ok)return status;std::shared_ptr<Callback> callback;ActionRequest request{};
        {auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);
            auto cached=std::find_if(s.offers.begin(),s.offers.end(),[&](const ActionOffer& o){return o.offerId==offer.offerId;});
            if(cached==s.offers.end()||std::memcmp(&*cached,&offer,sizeof(offer)))return Status::Conflict;
            if(grant.epoch!=s.epoch||grant.epoch!=offer.context.input.epoch||grant.contextId!=offer.offerId||grant.contextGeneration!=s.policy||offer.policyRevision!=s.policy||grant.inputSequence!=offer.context.input.inputSequence||grant.physicalHand!=offer.context.input.physicalHand)return Status::Conflict;
            auto* row=Find(s,offer.context.slot.configured.instance.instanceHandle);
            if(!row||row->availability||!(row->insideHands&grant.physicalHand)||row->bodyGeneration!=offer.context.slot.bodyGeneration||row->anchorGeneration!=offer.context.slot.anchorGeneration||row->contentVersion!=offer.context.slot.contentVersion)return Status::Conflict;
            for(const auto& used:s.used)if(used.epoch==grant.epoch&&used.sequence==grant.inputSequence&&used.hand==grant.physicalHand)return Status::Conflict;
            for(const auto& p:s.pending)if(!Terminal(p.stage)&&p.request.offer.context.slot.configured.instance.instanceHandle==row->configured.instance.instanceHandle)return Status::Conflict;
            for(const auto& c:s.behaviors)if(c->id==offer.behavior)callback=c;
            if(!callback)return Status::NotFound;
            if(s.pending.size()==kMaxPendingActions){auto old=std::find_if(s.pending.begin(),s.pending.end(),[](const Pending& p){return Terminal(p.stage);});if(old==s.pending.end())return Status::LimitExceeded;s.pending.erase(old);}
            request={offer,grant,Allocate(s)};if(!request.requestId)return Status::LimitExceeded;
            if(s.used.size()==kMaxActionOffers)s.used.erase(s.used.begin());s.used.push_back({grant.epoch,grant.inputSequence,grant.physicalHand});
            Pending pending{};pending.request=request;pending.owner=callback->owner;s.pending.push_back(pending);
        }
        if(!Own(callback->owner,callback->behavior.instanceHandle))status=Status::Unauthorized;
        else {CallLease lease(callback);if(!lease)status=Status::Unavailable;else try {status=callback->behavior.execute(callback->behavior.context,&request);}catch(...){status=Status::InternalError;}}
        if(status!=Status::Ok){auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);for(auto& p:s.pending)if(p.request.requestId==request.requestId&&!p.committed&&!Terminal(p.stage))p.stage=CommitStage::Failed;}
        *requestId=request.requestId;return status;
    });
}
void InvalidateRuntime() noexcept {
    try {std::vector<RuntimeEvent> events;auto& s=Runtime();std::unique_lock<std::mutex> lock(s.mutex);++s.revision;InvalidateOffers(s);s.hands[0]={};s.hands[1]={};for(auto& r:s.rows){for(auto hand:{Left,Right})if(r.insideHands&hand)events.push_back(Event(s,r,RuntimeEventKind::Exit,hand));events.push_back(Event(s,r,RuntimeEventKind::Invalidated));r.availability=static_cast<std::uint32_t>(RuntimeAvailability::InvalidObservation);r.insideHands=0;r.runtimeRevision=s.revision;}lock.unlock();Events(events);}catch(...){}
}
Status CopyActionProgress(ActionProgress* output,std::uint32_t capacity,std::uint32_t* count) noexcept {
    // Deliberately precedes configuration synchronization: the adapter must first
    // publish already-confirmed HIGGS commitments before invalidating old poses.
    return GuardRuntime([&]{if(!count||capacity>kMaxPendingActions||(capacity&&!output))return Status::InvalidArgument;auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);*count=static_cast<std::uint32_t>(s.pending.size());if(capacity<s.pending.size())return Status::BufferTooSmall;for(std::size_t i=0;i<s.pending.size();++i)output[i]=Progress(s.pending[i]);return Status::Ok;});
}
void SetContentCommitValidator(ContentCommitValidator validator) noexcept {contentValidator.store(validator,std::memory_order_release);}
Status ApplyHandResult(std::uint64_t request,std::uint64_t epoch,CommitStage stage) noexcept {
    return GuardRuntime([&]{if(!request||!epoch||(stage!=CommitStage::Failed&&stage!=CommitStage::Cancelled&&stage!=CommitStage::Committed&&stage!=CommitStage::Completed))return Status::InvalidArgument;auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);for(auto& pending:s.pending)if(pending.request.grant.requestId==request&&pending.request.grant.epoch==epoch){if(stage==CommitStage::Committed||stage==CommitStage::Completed){if(!Terminal(pending.stage))pending.handCommitted=true;}else if(!Terminal(pending.stage)&&(!pending.handCommitted||pending.committed))pending.stage=stage;return Status::Ok;}return Status::NotFound;});
}
Status ReportPresentation(std::uint64_t handle,std::uint64_t visual,std::uint64_t content,PresentationState state) noexcept {
    return GuardRuntime([&]{if(state!=PresentationState::Ready&&state!=PresentationState::Failed)return Status::InvalidArgument;auto& s=Runtime();std::lock_guard<std::mutex> lock(s.mutex);auto* row=Find(s,handle);if(!row)return Status::NotFound;if(row->visualRevision!=visual||row->contentVersion!=content||row->visual.contentVersion!=content)return Status::Conflict;row->displayedContentVersion=state==PresentationState::Ready?content:0;row->presentationState=static_cast<std::uint32_t>(state);row->runtimeRevision=++s.revision;return Status::Ok;});
}
const RuntimeApi& GetRuntimeApi() noexcept {return api;}
} // namespace body_slots::detail
extern "C" __declspec(dllexport) const body_slots::RuntimeApi* BS_CALL BodySlots_QueryRuntimeInterface(std::uint32_t version,std::uint32_t size) noexcept {
    if(version!=body_slots::kRuntimeApiVersion||size!=sizeof(body_slots::RuntimeApi))return nullptr;return &body_slots::detail::GetRuntimeApi();
}
