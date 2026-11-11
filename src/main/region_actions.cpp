#include "region_actions.h"
#include "interaction_regions.h"
#include "owned_region_sources.h"
#include "instances.h"
#include "validation.h"
#include "zone_geometry.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace body_slots::detail {
namespace {
constexpr std::uint32_t kKnown=HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN|HIGGS_OBS_PALM_VALID|HIGGS_OBS_TRACKING_VALID;
constexpr std::uint64_t kInputAgeNs=100000000, kMax=std::numeric_limits<std::uint64_t>::max();
thread_local unsigned callbackDepth=0;
struct Binding {
    std::uint64_t id{}, writer{}, anchorRevision{}, openedAt{};
    std::shared_ptr<const OwnedRegionSource> source;
    RegionActionBindingSpec spec{};
    InteractionRegionRecord region{};
    RegionAnchorSnapshot anchor{};
    HIGGS_HandExecutor executor{};
    RegionInputEnded onInputEnd{};
    bool active=true, registering=true;
    unsigned inFlight{};
    std::array<RegionObservation,2> observations{};
};
struct Candidate {
    std::shared_ptr<Binding> binding;
    RegionActionContext context{};
    RegionActionProposal proposal{};
    std::shared_ptr<const OwnedRegionSource> source;
    std::uint64_t token{}, requestId{};
    std::uint64_t correlation{};
    HIGGS_ActionRequest selected{};
    bool claimed=false, dispatchEntered=false;
};
struct Service {
    std::mutex mutex;
    std::condition_variable drained;
    HIGGS_HandInterface hands{};
    HIGGS_HandRoutingInterface routing{};
    HIGGS_AuthorizedDispatchInterface dispatch{};
    HIGGS_InputRoutesInterface routes{};
    HIGGS_HandClient recognizer{};
    std::uint64_t recognizerLabel{};
    HIGGS_HandRegistration registration{};
    HIGGS_HandSnapshot snapshot{};
    std::uint64_t epoch{}, save{}, barredEpoch{}, nextBinding=1, nextCandidate=1, sequence{};
    std::uint64_t observationHighWater{}, timeHighWater{}, generationHighWater{};
    bool connecting=false, ticking=false;
    std::vector<std::shared_ptr<Binding>> bindings;
    std::vector<std::shared_ptr<Candidate>> candidates;
    std::vector<std::shared_ptr<Candidate>> held;
    using BoundSource = std::pair<std::shared_ptr<Binding>,std::shared_ptr<const OwnedRegionSource>>;
    using Sources = std::vector<BoundSource>;
    std::shared_ptr<const Sources> sources=std::make_shared<Sources>();
    ReadOwnedAnchor readPlanck{};
    ReadPpbDigest readPpb{};
    std::uint64_t scopeOpenedAt{};
};
Service& State() { static auto* s=new Service; return *s; }
template<class F> Status Guard(F&& f) noexcept {try{return f();}catch(...){return Status::InternalError;}}
std::uint64_t Now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
Status Convert(HIGGS_HandStatus s) {
    switch(s) {
    case HIGGS_HAND_OK:return Status::Ok;
    case HIGGS_HAND_INVALID_ARGUMENT:return Status::InvalidArgument;
    case HIGGS_HAND_INVALID_CLIENT:case HIGGS_HAND_WRONG_OWNER:return Status::Unauthorized;
    case HIGGS_HAND_CAPACITY:return Status::LimitExceeded;
    case HIGGS_HAND_NOT_FOUND:return Status::NotFound;
    case HIGGS_HAND_CONFLICT:case HIGGS_HAND_STALE:return Status::Conflict;
    default:return Status::Unavailable;
    }
}
std::shared_ptr<Binding> Find(Service& s,std::uint64_t id) {
    for(auto& b:s.bindings)if(b->id==id)return b;
    return {};
}
void SetSource(Service& s, const std::shared_ptr<Binding>& b, std::shared_ptr<const OwnedRegionSource> source) {
    auto updated = std::make_shared<Service::Sources>(*s.sources);
    std::erase_if(*updated, [&](const auto& entry) {
        return entry.first == b;
    });
    if (source)
        updated->emplace_back(b, source);
    s.sources = std::move(updated);
    b->source = std::move(source);
    b->anchor = {};
    b->observations = {};
}
bool Fresh(std::uint64_t sample,std::uint64_t now,std::uint64_t maxAge=kInputAgeNs) {
    return sample && sample<=now && now-sample<=maxAge;
}
bool ValidHand(const HIGGS_HandRecord& h,unsigned hand) {
    if(h.physicalHand!=hand || (h.observationMask&kKnown)!=kKnown || !h.generation || !h.poseObservationId ||
       !h.poseObservedAtNs || h.unavailableReasons || (h.observationMask&HIGGS_OBS_SECONDARY_SUPPORT))return false;
    return std::isfinite(h.palmPosition[0])&&std::isfinite(h.palmPosition[1])&&std::isfinite(h.palmPosition[2]);
}
bool Scope(Service& s,const HIGGS_HandSnapshot& snap) {
    const auto save=CurrentSaveGeneration();
    if(!save || snap.structSize!=sizeof(snap) || !snap.worldReady || !snap.epoch || snap.epoch<=s.barredEpoch ||
       !snap.observationId || !snap.sampledAtNs || snap.epoch<s.epoch)return false;
    if(s.epoch!=snap.epoch || s.save!=save) {
        if(s.epoch!=snap.epoch)s.observationHighWater=s.timeHighWater=s.generationHighWater=0;
        s.epoch=snap.epoch;s.save=save;s.scopeOpenedAt=Now();
        for(auto& b:s.bindings)b->observations={};
    }
    if(snap.observationId<s.observationHighWater || snap.sampledAtNs<s.timeHighWater || snap.generation<s.generationHighWater ||
       (snap.observationId==s.observationHighWater && (snap.sampledAtNs!=s.timeHighWater || snap.generation!=s.generationHighWater)))return false;
    s.observationHighWater=snap.observationId;s.timeHighWater=snap.sampledAtNs;s.generationHighWater=snap.generation;
    return true;
}
bool Owned(Binding& b) {
    InteractionRegionRecord region{};
    return b.active && !b.registering && CopyOwnedInteractionRegion(b.writer,b.spec.regionId,b.spec.regionRevision,region)==Status::Ok;
}
RegionObservation Observe(Service& s,Binding& b,const HIGGS_HandSnapshot& snap,unsigned hand) {
    const auto& h=snap.hands[hand-1]; auto& old=b.observations[hand-1];
    if(old.evaluated.epoch==snap.epoch && old.evaluated.saveGeneration==s.save &&
       old.evaluated.frame==snap.observationId && old.anchor.source.sampleId==b.anchor.source.sampleId &&
       old.anchor.source.generation==b.anchor.source.generation && old.probe.source.sampleId==h.poseObservationId)
        return old;
    RegionEvaluation e{};e.structSize=sizeof(e);e.regionId=b.region.regionId;e.expectedRegionRevision=b.region.revision;e.anchor=b.anchor;
    e.probe.structSize=sizeof(e.probe);e.probe.valid=ValidHand(h,hand)?1u:0u;
    auto& p=e.probe.source;std::strcpy(p.provider,"HIGGS");std::strcpy(p.source,"resolved-palm");
    p.identity=hand;p.generation=h.generation;p.sampleId=h.poseObservationId;p.epoch=snap.epoch;p.saveGeneration=s.save;
    p.frame=snap.observationId;p.timeSeconds=double(h.poseObservedAtNs)/1e9;p.units=static_cast<std::uint32_t>(RegionUnits::SkyrimWorld);
    std::copy(h.palmPosition,h.palmPosition+3,e.probe.world);
    const RegionEvaluationFrame frame{sizeof(frame),0,snap.epoch,s.save,snap.observationId,double(Now())/1e9};
    // A newly published target may be evaluated against the same genuine palm,
    // but cannot advance its dwell from the same probe sample twice.
    const auto* previous=&old;
    old=EvaluateInteractionRegion(b.region,frame,e,previous,++s.sequence,true);return old;
}
bool Ready(Service& s,Binding& b,const HIGGS_HandSnapshot& snap,unsigned hand) {
    const auto now=double(Now())/1e9;
    const auto palmTime=double(snap.hands[hand-1].poseObservedAtNs)/1e9;
    if(!Owned(b) || !Scope(s,snap) || !ValidHand(snap.hands[hand-1],hand) ||
       !Fresh(snap.sampledAtNs,Now()) || !Fresh(snap.hands[hand-1].poseObservedAtNs,Now()) ||
       !b.anchor.valid || b.anchor.source.timeSeconds>now || now-b.anchor.source.timeSeconds>b.region.spec.maxSampleAgeSeconds ||
       palmTime>now || now-palmTime>b.region.spec.maxSampleAgeSeconds)return false;
    auto o=Observe(s,b,snap,hand);
    return o.state==static_cast<std::uint32_t>(RegionSpatialState::Ready);
}
struct Lease {
    std::shared_ptr<Binding> binding;
    explicit Lease(std::shared_ptr<Binding> b) : binding(std::move(b)) {++callbackDepth;}
    ~Lease() {
        --callbackDepth;auto& s=State();std::lock_guard lock(s.mutex);
        --binding->inFlight;s.drained.notify_all();
    }
};
void Cancel(const std::vector<std::shared_ptr<Candidate>>& candidates,const HIGGS_HandRoutingInterface& routing,HIGGS_HandClient recognizer) {
    for(const auto& c:candidates)if(c->requestId)
        routing.CancelRecognizedAction(recognizer,c->requestId,c->context.input.epoch,1);
}
void HIGGS_HAND_CALL Selected(const HIGGS_ActionRequest* request,void* context) noexcept {
    try {
        if(!request || request->structSize!=sizeof(*request) || !context)return;
        auto& s=State();std::lock_guard lock(s.mutex);
        for(auto& c:s.candidates)if(c->binding.get()==context && c->token==request->contextId &&
            c->requestId==request->requestId && c->binding->active && !c->claimed &&
            request->executor==c->binding->executor && request->owner==c->binding->spec.higgsClient &&
            request->epoch==c->context.input.epoch && request->inputId==c->context.input.inputId &&
            request->actionId==c->proposal.actionId && request->contextGeneration==c->context.anchorRevision) {
            c->selected=*request;break;
        }
    }catch(...){}
}
std::uint64_t TargetKey(const InteractionRegionSpec& region);
void HIGGS_HAND_CALL Recognize(const HIGGS_HandInput* input,void*) noexcept {
    try {
        if(!input || input->structSize!=sizeof(*input) || input->physicalHand<1 || input->physicalHand>2 ||
           (input->edge!=1 && input->edge!=2) || input->snapshot.structSize!=sizeof(input->snapshot) ||
           input->snapshot.epoch!=input->epoch || !Fresh(input->observedAtNs,Now()))return;
        const auto& hand=input->snapshot.hands[input->physicalHand-1];
        if(input->handGeneration!=hand.generation || input->poseObservationId!=hand.poseObservationId)return;
        auto& s=State();HIGGS_HandInterface hands{};HIGGS_InputRoutesInterface routes{};HIGGS_HandClient recognizer{};
        std::vector<std::shared_ptr<Binding>> bindings;
        {
            std::lock_guard lock(s.mutex);
            if(!s.recognizer || !Scope(s,input->snapshot) ||
               s.snapshot.epoch!=input->epoch || s.snapshot.observationId!=input->snapshot.observationId)return;
            hands=s.hands;routes=s.routes;recognizer=s.recognizer;bindings=s.bindings;
        }
        std::vector<std::shared_ptr<Candidate>> offered;
        for(auto& b:bindings) {
            RegionActionContext context{};
            std::shared_ptr<const OwnedRegionSource> source;
            {
                std::lock_guard lock(s.mutex);
                if(!(b->spec.physicalHands&input->physicalHand) || b->spec.button!=input->button ||
                   !(b->spec.edgeMask&input->edge) || !Ready(s,*b,input->snapshot,input->physicalHand) ||
                   hand.grant || (hand.availability!=HIGGS_AVAILABILITY_READY && hand.availability!=HIGGS_AVAILABILITY_RESERVED))continue;
                context={sizeof(context),0,b->id,b->anchorRevision,b->observations[input->physicalHand-1],*input};
                source=b->source;
                ++b->inFlight;
            }
            Lease lease(b);std::array<RegionActionProposal,kMaxRegionActionProposals> proposals{};std::uint32_t count=0;
            Status status=Status::InternalError;
            try {status=b->spec.offers(b->spec.context,&context,proposals.data(),static_cast<std::uint32_t>(proposals.size()),&count);}catch(...){}
            if(status!=Status::Ok || count>proposals.size())continue;
            for(unsigned i=0;i<count;++i) {
                const auto& p=proposals[i];
                if(p.structSize!=sizeof(p) || !p.actionId || !p.physicalHands || p.physicalHands>Both ||
                   !(p.physicalHands&input->physicalHand) || (p.physicalHands&~b->spec.physicalHands) || p.reserved ||
                   !p.grantDurationMs || p.grantDurationMs>60000)continue;
                bool resources=true;
                for(unsigned h=1;h<=2;++h)if(p.physicalHands&h) {
                    const auto& resource=input->snapshot.hands[h-1];
                    resources=resources && ValidHand(resource,h) && Fresh(resource.poseObservedAtNs,Now()) && !resource.grant &&
                        (resource.availability==HIGGS_AVAILABILITY_READY || resource.availability==HIGGS_AVAILABILITY_RESERVED);
                }
                if(!resources)continue;
                auto c=std::make_shared<Candidate>();c->binding=b;c->context=context;c->proposal=p;c->source=source;offered.push_back(c);
            }
        }
        std::stable_sort(offered.begin(),offered.end(),[](const auto& a,const auto& b){return a->proposal.priority>b->proposal.priority;});
        for(auto& c:offered) {
            {
                std::lock_guard lock(s.mutex);
                if(!c->binding->active || s.nextCandidate==kMax || s.candidates.size()>=256 ||
                   c->binding->anchorRevision!=c->context.anchorRevision || c->binding->source!=c->source)continue;
                c->token=s.nextCandidate++;s.candidates.push_back(c);
            }
            HIGGS_ActionCandidate candidate{};candidate.structSize=sizeof(candidate);candidate.physicalHands=c->proposal.physicalHands;
            candidate.inputId=input->inputId;candidate.epoch=input->epoch;candidate.handGeneration=input->handGeneration;
            candidate.executor=c->binding->executor;candidate.actionId=c->proposal.actionId;candidate.contextId=c->token;
            candidate.contextGeneration=c->context.anchorRevision;candidate.grantDurationMs=c->proposal.grantDurationMs;
            HIGGS_HandRequestId request{};
            const auto result=routes.SubmitOrderedCandidate
                ? routes.SubmitOrderedCandidate(recognizer,&candidate,TargetKey(c->binding->region.spec),&request)
                : hands.SubmitCandidate(recognizer,&candidate,&request);
            std::lock_guard lock(s.mutex);
            if(result==HIGGS_HAND_OK)c->requestId=request;
            else s.candidates.erase(std::remove(s.candidates.begin(),s.candidates.end(),c),s.candidates.end());
        }
    }catch(...){}
}
bool SameHand(const HIGGS_HandRecord& before,const HIGGS_HandRecord& after) {
    return before.generation==after.generation && before.heldRefId==after.heldRefId && before.equippedFormId==after.equippedFormId &&
        (before.observationMask&kKnown)==(after.observationMask&kKnown) && ValidHand(after,after.physicalHand);
}
bool SameTarget(const RegionSampleSource& before,const RegionSampleSource& after) {
    return before.identity==after.identity && before.generation==after.generation && before.epoch==after.epoch &&
        before.saveGeneration==after.saveGeneration && !std::strcmp(before.provider,after.provider) && !std::strcmp(before.source,after.source);
}
std::uint64_t TargetKey(const InteractionRegionSpec& region) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char* part : {region.provider, region.key}) {
        for (const char* cursor = part; *cursor; ++cursor)
            hash = (hash ^ static_cast<unsigned char>(*cursor)) * 1099511628211ULL;
        hash *= 1099511628211ULL; // delimiter between the two stable public names
    }
    return hash ? hash : 1;
}
HIGGS_HandStatus HIGGS_HAND_CALL ExecuteAuthorized(const HIGGS_ActionRequest* request,const HIGGS_HandSnapshot* snapshot,void* context) noexcept {
    try {
        if(!request || !snapshot || !context)return HIGGS_HAND_INVALID_ARGUMENT;
        auto& c=*static_cast<Candidate*>(context);auto& s=State();auto b=c.binding;
        RegionActionExecution execution{};
        {
            std::lock_guard lock(s.mutex);
            if(!b->active || !c.claimed || request->requestId!=c.selected.requestId || request->executor!=b->executor ||
               request->grant!=c.selected.grant || request->epoch!=c.context.input.epoch ||
               s.save!=c.context.observation.evaluated.saveGeneration || CurrentSaveGeneration()!=s.save ||
               b->source!=c.source || !SameTarget(c.context.observation.anchor.source,b->anchor.source) || !Fresh(c.context.input.observedAtNs,Now()) ||
               !Ready(s,*b,*snapshot,c.context.input.physicalHand))return HIGGS_HAND_STALE;
            // New pose samples of the same target are re-evaluated. Replacement
            // source generations invalidate the original offer.
            for(unsigned hand=1;hand<=2;++hand)if(c.proposal.physicalHands&hand)
                if(!SameHand(c.context.input.snapshot.hands[hand-1],snapshot->hands[hand-1]) ||
                   !Fresh(c.context.input.snapshot.hands[hand-1].poseObservedAtNs,Now()) || !Fresh(snapshot->hands[hand-1].poseObservedAtNs,Now()))
                    return HIGGS_HAND_STALE;
            execution.context=c.context;execution.context.observation=b->observations[c.context.input.physicalHand-1];
            execution.proposal=c.proposal;execution.request=*request;execution.currentSnapshot=*snapshot;
            c.dispatchEntered=true;++b->inFlight;
        }
        Lease lease(b);Status status=Status::InternalError;
        try {status=b->spec.execute(b->spec.context,&execution);}catch(...){}
        return status==Status::Ok ? HIGGS_HAND_OK : HIGGS_HAND_NOT_READY;
    }catch(...){return HIGGS_HAND_INTERNAL_ERROR;}
}
Status BS_CALL Register(std::uint64_t writer,const RegionActionBindingSpec* input,RegionActionRegistration* output) noexcept {
    return Guard([&] {
        SetRegionActionsReleaseHook(DrainRegionActionsClient);
        if(!input || !output || output->structSize!=sizeof(*output) || output->reserved)return Status::InvalidArgument;
        const auto spec=*input;
        if(spec.structSize!=sizeof(spec) || !spec.physicalHands || spec.physicalHands>Both || spec.button<1 || spec.button>2 ||
           !spec.edgeMask || spec.edgeMask>3 || !spec.higgsClient || !spec.offers || !spec.execute)return Status::InvalidArgument;
        InteractionRegionRecord region{};auto result=CopyOwnedInteractionRegion(writer,spec.regionId,spec.regionRevision,region);
        if(result!=Status::Ok)return result;
        auto& s=State();auto b=std::make_shared<Binding>();b->writer=writer;b->spec=spec;b->region=region;
        HIGGS_HandInterface hands{};HIGGS_HandRoutingInterface routing{};HIGGS_HandClient recognizer{};
        {
            std::lock_guard lock(s.mutex);
            if(!s.recognizer)return Status::Unavailable;
            if(s.bindings.size()>=kMaxRegionActionBindings || s.nextBinding==kMax)return Status::LimitExceeded;
            b->id=s.nextBinding++;b->openedAt=Now();s.bindings.push_back(b);hands=s.hands;routing=s.routing;recognizer=s.recognizerLabel;
        }
        HIGGS_HandExecutor executor{};
        auto status=hands.RegisterExecutor(spec.higgsClient,Selected,b.get(),&executor);
        if(status==HIGGS_HAND_OK)status=routing.AuthorizeRecognizer(spec.higgsClient,executor,recognizer,spec.physicalHands);
        if(status!=HIGGS_HAND_OK) {
            if(executor)hands.RemoveRegistration(spec.higgsClient,executor);
            std::lock_guard lock(s.mutex);s.bindings.erase(std::remove(s.bindings.begin(),s.bindings.end(),b),s.bindings.end());return Convert(status);
        }
        bool cancelled=false;
        {
            std::lock_guard lock(s.mutex);b->executor=executor;b->registering=false;
            InteractionRegionRecord current{};
            cancelled=!b->active || s.recognizerLabel!=recognizer ||
                CopyOwnedInteractionRegion(writer,spec.regionId,spec.regionRevision,current)!=Status::Ok;
            if(!cancelled)*output={sizeof(*output),0,b->id,executor,recognizer};
        }
        if(cancelled) {
            routing.RevokeRecognizer(spec.higgsClient,executor,recognizer);
            hands.RemoveRegistration(spec.higgsClient,executor);
            std::lock_guard lock(s.mutex);s.bindings.erase(std::remove(s.bindings.begin(),s.bindings.end(),b),s.bindings.end());return Status::Unavailable;
        }
        return Status::Ok;
    });
}
Status BS_CALL Remove(std::uint64_t writer,std::uint64_t id) noexcept {
    return Guard([&] {
        auto& s=State();std::shared_ptr<Binding> b;std::vector<std::shared_ptr<Candidate>> cancelled;
        HIGGS_HandInterface hands{};HIGGS_HandRoutingInterface routing{};HIGGS_HandClient recognizer{};std::uint64_t label{};
        {
            std::lock_guard lock(s.mutex);b=Find(s,id);if(!b)return Status::NotFound;
            if(b->writer!=writer)return Status::Unauthorized;
            b->active=false;b->observations={};if(b->source)SetSource(s,b,{});if(b->registering)return Status::Unavailable;
            hands=s.hands;routing=s.routing;recognizer=s.recognizer;label=s.recognizerLabel;
            for(auto& c:s.candidates)if(c->binding==b)cancelled.push_back(c);
        }
        Cancel(cancelled,routing,recognizer);
        const auto route=routing.RevokeRecognizer(b->spec.higgsClient,b->executor,label);
        const auto removed=hands.RemoveRegistration(b->spec.higgsClient,b->executor);
        if(callbackDepth || route==HIGGS_HAND_PENDING || removed==HIGGS_HAND_PENDING)return Status::Unavailable;
        if(removed!=HIGGS_HAND_OK && removed!=HIGGS_HAND_NOT_FOUND && removed!=HIGGS_HAND_INVALID_CLIENT)return Convert(removed);
        std::unique_lock lock(s.mutex);s.drained.wait(lock,[&]{return !b->inFlight;});
        s.candidates.erase(std::remove_if(s.candidates.begin(),s.candidates.end(),[&](const auto& c){return c->binding==b;}),s.candidates.end());
        s.held.erase(std::remove_if(s.held.begin(),s.held.end(),[&](const auto& c){return c->binding==b;}),s.held.end());
        s.bindings.erase(std::remove(s.bindings.begin(),s.bindings.end(),b),s.bindings.end());return Status::Ok;
    });
}
bool ValidAnchor(const RegionAnchorSnapshot& a,const Binding& b,const Service& s) {
    const auto& p=a.source;
    if(a.structSize!=sizeof(a) || a.valid!=1 || a.reserved || a.kind!=b.region.spec.anchorKind ||
       !ValidString(p.provider,sizeof(p.provider),true) || !ValidString(p.source,sizeof(p.source),true) ||
       !p.identity || !p.generation || !p.sampleId || !p.frame || p.reserved || p.units!=static_cast<std::uint32_t>(RegionUnits::SkyrimWorld) ||
       p.epoch!=s.epoch || p.saveGeneration!=s.save || !std::isfinite(p.timeSeconds) || p.timeSeconds<0)return false;
    // Target provenance is a declared claim. It never supplies the probe or
    // authenticates HIGGS input; those come exclusively from the retained table.
    return std::isfinite(ZonePointSignedDistance(b.region.spec.shape,a.world,a.world.position));
}
Status BS_CALL Publish(std::uint64_t writer,std::uint64_t id,std::uint64_t expected,const RegionAnchorSnapshot* input,std::uint64_t* output) noexcept {
    return Guard([&] {
        if(!input || !output)return Status::InvalidArgument;
        const auto anchor=*input;auto& s=State();std::lock_guard lock(s.mutex);auto b=Find(s,id);
        if(!b)return Status::NotFound;
        if(b->writer!=writer)return Status::Unauthorized;
        if(!Owned(*b) || !s.epoch || CurrentSaveGeneration()!=s.save)return Status::Unavailable;
        if(b->source)return Status::Conflict;
        if(expected!=b->anchorRevision)return Status::Conflict;
        if(!ValidAnchor(anchor,*b,s)) {b->anchor.valid=0;b->observations={};return Status::InvalidArgument;}
        if(b->anchorRevision) {
            const auto& old=b->anchor.source;const auto& p=anchor.source;
            const bool identity=old.identity==p.identity && !std::strcmp(old.provider,p.provider) && !std::strcmp(old.source,p.source);
            const bool scope=old.epoch==p.epoch && old.saveGeneration==p.saveGeneration;
            if(scope && (!identity || p.generation<old.generation || p.timeSeconds<old.timeSeconds ||
               (p.generation==old.generation && (p.sampleId<=old.sampleId || p.frame<=old.frame)))) {
                b->anchor.valid=0;b->observations={};return Status::Conflict;
            }
        }
        if(b->anchorRevision==kMax)return Status::LimitExceeded;
        b->anchor=anchor;*output=++b->anchorRevision;return Status::Ok;
    });
}
Status BS_CALL Integration(std::uint64_t reader,RegionActionsIntegration* output) noexcept {
    return Guard([&] {
        if(!output || output->structSize!=sizeof(*output) || output->version!=1 || output->reserved)return Status::InvalidArgument;
        auto result=RuntimeClientIdentity(reader,false,nullptr);if(result!=Status::Ok)return result;
        auto& s=State();std::lock_guard lock(s.mutex);
        const bool ready=s.recognizer && s.epoch>s.barredEpoch && s.save==CurrentSaveGeneration() &&
            s.snapshot.worldReady && s.snapshot.epoch==s.epoch && Fresh(s.snapshot.sampledAtNs,Now());
        *output={sizeof(*output),1,ready?1u:0u,0,s.recognizerLabel,ready?s.epoch:0,ready?s.save:0,ready?s.snapshot.observationId:0};return Status::Ok;
    });
}
Status BS_CALL Observation(std::uint64_t writer,std::uint64_t id,std::uint32_t hand,RegionObservation* output) noexcept {
    return Guard([&] {
        if(hand<1 || hand>2 || !output)return Status::InvalidArgument;
        auto& s=State();std::lock_guard lock(s.mutex);auto b=Find(s,id);if(!b)return Status::NotFound;
        if(b->writer!=writer)return Status::Unauthorized;
        if(!Owned(*b) || CurrentSaveGeneration()!=s.save)return Status::Unavailable;
        *output=b->observations[hand-1];return Status::Ok;
    });
}
const RegionActionsApi api{sizeof(api),1,1,Register,Remove,Publish,Integration,Observation};
Status BS_CALL BindPlanck(std::uint64_t writer, std::uint64_t id, const RegionPlanckBody* target) noexcept {
    return Guard([&] {
        if (!target || target->structSize != sizeof(*target) || target->reserved || !target->worldEpoch ||
            !target->actorHandle || !target->actorGeneration || !target->bodyGeneration)
            return Status::InvalidArgument;
        auto& s = State();
        std::lock_guard lock(s.mutex);
        const auto b = Find(s, id);
        if (!b)
            return Status::NotFound;
        if (b->writer != writer)
            return Status::Unauthorized;
        if (b->region.spec.anchorKind != static_cast<std::uint32_t>(RegionAnchorKind::Npc))
            return Status::InvalidArgument;
        if (!s.readPlanck)
            return Status::UnsupportedVersion;
        if (!Owned(*b) || s.epoch <= s.barredEpoch || s.save != CurrentSaveGeneration() || !s.snapshot.worldReady ||
            !Fresh(s.snapshot.sampledAtNs, Now()))
            return Status::Unavailable;
        if (b->source)
            return Status::Conflict;
        SetSource(s, b, std::make_shared<OwnedRegionSource>(OwnedRegionSource{*target, s.epoch, s.save, Now()}));
        return Status::Ok;
    });
}
Status BS_CALL Unbind(std::uint64_t writer, std::uint64_t id) noexcept {
    return Guard([&] {
        auto& s = State();
        std::lock_guard lock(s.mutex);
        const auto b = Find(s, id);
        if (!b)
            return Status::NotFound;
        if (b->writer != writer)
            return Status::Unauthorized;
        if (!b->source)
            return Status::NotFound;
        SetSource(s, b, {});
        return Status::Ok;
    });
}
Status BS_CALL CopyPpbEvidence(std::uint64_t writer, std::uint64_t id, const PpbRegionQuery* query,
                               PpbRegionEvidence* out) noexcept {
    return Guard([&] {
        if (!query || query->structSize != sizeof(*query) || !query->actorFormId ||
            (query->hand != Left && query->hand != Right) || query->slot > 11 || query->leftTwin > 1 || !out ||
            out->structSize != sizeof(*out))
            return Status::InvalidArgument;
        auto& s = State();
        std::shared_ptr<Binding> binding;
        ReadPpbDigest read{};
        std::uint64_t epoch{}, save{}, opened{};
        {
            std::lock_guard lock(s.mutex);
            binding = Find(s, id);
            if (!binding)
                return Status::NotFound;
            if (binding->writer != writer)
                return Status::Unauthorized;
            if (!s.readPpb)
                return Status::UnsupportedVersion;
            if (!Owned(*binding) || s.epoch <= s.barredEpoch || !s.snapshot.worldReady ||
                !Fresh(s.snapshot.sampledAtNs, Now()) || s.save != CurrentSaveGeneration())
                return Status::Unavailable;
            read = s.readPpb;
            epoch = s.epoch;
            save = s.save;
            opened = std::max(s.scopeOpenedAt, binding->openedAt);
        }
        const auto start = Now();
        PpbRegionEvidence result{};
        result.structSize = sizeof(result);
        auto status = read(*query, result);
        if (status != Status::Ok)
            return status;
        const auto ageLimit = binding->region.spec.maxSampleAgeSeconds * 1000;
        // PPB reports integer milliseconds since scan start. Use the older
        // end of the one-ms uncertainty and reject scans preceding this scope.
        if (start < opened || result.ppbAgeMs >= ((start - opened) / 1000000) ||
            static_cast<double>(result.ppbAgeMs) + 1 > ageLimit)
            return Status::Unavailable;
        std::lock_guard lock(s.mutex);
        if (!Owned(*binding) || s.epoch != epoch || s.barredEpoch >= epoch || s.save != save ||
            save != CurrentSaveGeneration())
            return Status::Unavailable;
        result.logicalEpoch = epoch;
        result.saveGeneration = save;
        *out = result;
        return Status::Ok;
    });
}
const RegionSourcesApi sourcesApi{sizeof(sourcesApi),1,BindPlanck,Unbind,CopyPpbEvidence};

void RefreshSources(Service& s, const HIGGS_HandSnapshot& snapshot) {
    std::shared_ptr<const Service::Sources> sources;
    ReadOwnedAnchor read{};
    {
        std::lock_guard lock(s.mutex);
        sources = s.sources;
        read = s.readPlanck;
    }
    for (const auto& [b, source] : *sources) {
        RegionAnchorSnapshot anchor{};
        const bool same = source->higgsEpoch == snapshot.epoch && source->saveGeneration == CurrentSaveGeneration();
        const bool available = same && read && read(*source, anchor) == Status::Ok;
        std::lock_guard lock(s.mutex);
        if (!b->active || b->source != source)
            continue;
        const double now = double(Now()) / 1e9;
        if (!available || !ValidAnchor(anchor, *b, s) || anchor.source.timeSeconds > now ||
            now - anchor.source.timeSeconds > b->region.spec.maxSampleAgeSeconds || s.epoch <= s.barredEpoch ||
            source->saveGeneration != CurrentSaveGeneration()) {
            b->anchor.valid = 0;
            b->observations = {};
            continue;
        }
        const auto& old = b->anchor.source;
        const auto& next = anchor.source;
        if (old.sampleId == next.sampleId && old.frame == next.frame && old.timeSeconds == next.timeSeconds)
            continue;
        if (next.sampleId <= old.sampleId || next.frame <= old.frame || next.timeSeconds < old.timeSeconds ||
            b->anchorRevision == kMax) {
            b->anchor.valid = 0;
            b->observations = {};
            continue;
        }
        b->anchor = anchor;
        ++b->anchorRevision;
    }
}
Status BS_CALL SubscribeEnd(std::uint64_t writer, std::uint64_t id, RegionInputEnded callback) noexcept {
    return Guard([&] {
        if (!callback)
            return Status::InvalidArgument;
        auto& s = State();
        std::lock_guard lock(s.mutex);
        const auto b = Find(s, id);
        if (!b)
            return Status::NotFound;
        if (b->writer != writer)
            return Status::Unauthorized;
        if (!Owned(*b))
            return Status::Unavailable;
        if (!(b->spec.edgeMask & 1))
            return Status::InvalidArgument;
        if (!s.routes.CopySequence || !s.routes.CheckSelectedAction)
            return Status::UnsupportedVersion;
        if (b->onInputEnd)
            return Status::Conflict;
        b->onInputEnd = callback;
        return Status::Ok;
    });
}
const RegionContinuationApi continuation{sizeof(continuation),1,SubscribeEnd};

void DeliverTerminals(Service& s, const HIGGS_InputRoutesInterface& routes) {
    std::vector<std::shared_ptr<Candidate>> tracked;
    {
        std::lock_guard lock(s.mutex);
        tracked = s.held;
    }
    for (const auto& c : tracked) {
        const auto& input = c->context.input;
        auto b = c->binding;
        HIGGS_InputSequence sequence{};
        sequence.structSize = sizeof(sequence);
        const auto status = routes.CopySequence(b->spec.higgsClient, input.inputId, &sequence);
        const bool same = status == HIGGS_HAND_OK && sequence.structSize == sizeof(sequence) &&
                          sequence.epoch == input.epoch && sequence.pressInputId == input.inputId &&
                          sequence.requestId == c->requestId && sequence.executor == b->executor &&
                          sequence.correlation == c->correlation && sequence.actionId == c->proposal.actionId &&
                          sequence.physicalHand == input.physicalHand && sequence.button == input.button;
        RegionInputTerminal terminal{sizeof(terminal),
                                     static_cast<std::uint32_t>(RegionInputEnd::ObservationLost),
                                     input.physicalHand,
                                     input.button,
                                     b->id,
                                     input.epoch,
                                     input.inputId,
                                     0,
                                     c->requestId,
                                     c->proposal.actionId,
                                     c->correlation,
                                     0,
                                     0};
        {
            std::lock_guard lock(s.mutex);
            const auto found = std::find(s.held.begin(), s.held.end(), c);
            if (found == s.held.end())
                continue;
            const bool live =
                s.barredEpoch < input.epoch && s.epoch == input.epoch && s.save == CurrentSaveGeneration();
            if (live && same && sequence.phase == HIGGS_SEQUENCE_HELD)
                continue;
            if (same && ((sequence.phase == HIGGS_SEQUENCE_RELEASED && sequence.terminalInputId) ||
                         (sequence.phase == HIGGS_SEQUENCE_CANCELLED && !sequence.terminalInputId))) {
                terminal.end = static_cast<std::uint32_t>(sequence.phase == HIGGS_SEQUENCE_RELEASED
                                                              ? RegionInputEnd::Released
                                                              : RegionInputEnd::Cancelled);
                terminal.terminalInput = sequence.terminalInputId;
                terminal.providerReason = sequence.reason;
            }
            s.held.erase(found);
            if (!Owned(*b))
                continue;
            ++b->inFlight;
        }
        Lease lease(b);
        try {
            b->onInputEnd(b->spec.context, &terminal);
        } catch (...) {
        }
    }
}
} // namespace
const RegionActionsApi& GetRegionActionsApi() noexcept {return api;}
const RegionContinuationApi& GetRegionContinuationApi() noexcept {return continuation;}
const RegionSourcesApi& GetRegionSourcesApi() noexcept {return sourcesApi;}
void SetOwnedPlanckReader(ReadOwnedAnchor read) noexcept {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    s.readPlanck = read;
}
void SetPpbDigestReader(ReadPpbDigest read) noexcept {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    s.readPpb = read;
}
Status DrainRegionActionsClient(std::uint64_t writer) noexcept {
    return Guard([&] {
        auto& s=State();std::vector<std::shared_ptr<Binding>> bindings;
        {
            std::lock_guard lock(s.mutex);
            for(auto& b:s.bindings)if(b->writer==writer) {
                b->active=false;b->observations={};bindings.push_back(b);
            }
        }
        Status drained=Status::Ok;
        for(auto& b:bindings) {
            const auto result=Remove(writer,b->id);
            if(result!=Status::Ok && result!=Status::NotFound)drained=result;
        }
        return drained;
    });
}
Status ConnectRegionActions(const HIGGS_HandInterface& hands,const HIGGS_HandRoutingInterface& routing,
    const HIGGS_AuthorizedDispatchInterface& dispatch,const HIGGS_InputRoutesInterface* routes) noexcept {
    return Guard([&] {
        constexpr auto required=HIGGS_HAND_CAP_SNAPSHOT|HIGGS_HAND_CAP_INPUT_BROKER|HIGGS_HAND_CAP_OWNED_GRANTS|HIGGS_HAND_CAP_EXECUTOR_RECEIPTS;
        if(!routes || routes->structSize!=sizeof(*routes) || routes->abiVersion!=1 || !routes->CopyClientLabel ||
           hands.structSize!=sizeof(hands) || hands.abiVersion!=HIGGS_HAND_ABI_VERSION || (hands.capabilities&required)!=required || !hands.RegisterClient || !hands.UnregisterClient || !hands.CopySnapshot ||
           !hands.SubscribeRecognizer || !hands.RegisterExecutor || !hands.RemoveRegistration || !hands.SubmitCandidate || !hands.CopyActionResult ||
           routing.structSize!=sizeof(routing) || routing.abiVersion!=HIGGS_HAND_ROUTING_ABI_VERSION || !routing.AuthorizeRecognizer || !routing.RevokeRecognizer ||
           !routing.CancelRecognizedAction || dispatch.structSize!=sizeof(dispatch) || dispatch.abiVersion!=HIGGS_AUTHORIZED_DISPATCH_ABI_VERSION || !dispatch.Invoke)return Status::Unavailable;
        auto& s=State();{
            std::lock_guard lock(s.mutex);if(s.recognizer)return Status::Ok;if(s.connecting)return Status::Unavailable;s.connecting=true;
        }
        HIGGS_HandClientInfo info{};info.structSize=sizeof(info);std::strcpy(info.name,"BodySlots.RegionActions");
        HIGGS_HandClient client{};HIGGS_HandRegistration registration{};auto result=hands.RegisterClient(&info,&client);
        std::uint64_t label{};
        if(result==HIGGS_HAND_OK)result=routes->CopyClientLabel(client,&label);
        if(result==HIGGS_HAND_OK && (!label || label==client))result=HIGGS_HAND_UNSUPPORTED;
        if(result==HIGGS_HAND_OK)result=hands.SubscribeRecognizer(client,Recognize,nullptr,&registration);
        if(result!=HIGGS_HAND_OK && client)hands.UnregisterClient(client);
        std::lock_guard lock(s.mutex);s.connecting=false;
        if(result!=HIGGS_HAND_OK)return Convert(result);
        s.routes={};
        if(routes && routes->structSize==sizeof(*routes) && routes->abiVersion==1) {
            if((routes->capabilities&HIGGS_ROUTE_TERMINALS) && routes->CopySequence && routes->CheckSelectedAction) {
                s.routes.CopySequence=routes->CopySequence;s.routes.CheckSelectedAction=routes->CheckSelectedAction;
            }
            if(routes->capabilities&HIGGS_ROUTE_ORDERED_CANDIDATES)s.routes.SubmitOrderedCandidate=routes->SubmitOrderedCandidate;
        }
        s.hands=hands;s.routing=routing;s.dispatch=dispatch;s.recognizer=client;s.recognizerLabel=label;s.registration=registration;return Status::Ok;
    });
}
void TickRegionActions() noexcept {
    try {
        auto& s=State();HIGGS_HandInterface hands{};HIGGS_HandRoutingInterface routing{};
        HIGGS_AuthorizedDispatchInterface dispatch{};HIGGS_InputRoutesInterface routes{};HIGGS_HandClient recognizer{};
        {
            std::lock_guard lock(s.mutex);if(!s.recognizer || s.ticking)return;s.ticking=true;
            hands=s.hands;routing=s.routing;dispatch=s.dispatch;routes=s.routes;recognizer=s.recognizer;
        }
        struct TickExit {Service& s;~TickExit(){std::lock_guard lock(s.mutex);s.ticking=false;}} exit{s};
        HIGGS_HandSnapshot snap{};snap.structSize=sizeof(snap);const auto copied=hands.CopySnapshot(recognizer,&snap);
        if(copied==HIGGS_HAND_OK && snap.worldReady && Fresh(snap.sampledAtNs,Now()))RefreshSources(s,snap);
        std::vector<std::shared_ptr<Candidate>> pending,cancelled;
        {
            std::lock_guard lock(s.mutex);
            const bool valid=copied==HIGGS_HAND_OK && Scope(s,snap) && Fresh(snap.sampledAtNs,Now()) &&
                (!s.snapshot.epoch || snap.epoch>s.snapshot.epoch || snap.observationId>=s.snapshot.observationId);
            if(valid) {
                s.snapshot=snap;
                for(auto& b:s.bindings)if(Owned(*b))for(unsigned hand=1;hand<=2;++hand)
                    if(b->spec.physicalHands&hand)Observe(s,*b,snap,hand);
            } else {s.snapshot={};for(auto& b:s.bindings)b->observations={};}
            for(auto& c:s.candidates) {
                if(!c->claimed && c->selected.requestId) {c->claimed=true;if(valid)pending.push_back(c);else cancelled.push_back(c);}
                else if(!c->claimed && (!valid || !c->binding->active || c->context.input.epoch!=s.epoch ||
                    c->context.observation.evaluated.saveGeneration!=s.save || !Fresh(c->context.input.observedAtNs,Now()))) {
                    c->claimed=true;cancelled.push_back(c);
                }
            }
        }
        Cancel(cancelled,routing,recognizer);
        for(auto& c:pending) {
            HIGGS_ActionResult result{};result.structSize=sizeof(result);
            const auto copiedResult=hands.CopyActionResult(recognizer,c->selected.requestId,&result);
            HIGGS_HandStatus invoked=HIGGS_HAND_STALE;
            if(copiedResult==HIGGS_HAND_OK && result.stage==HIGGS_ACTION_ACCEPTED && result.executor==c->binding->executor &&
               result.epoch==c->selected.epoch && result.inputId==c->selected.inputId) {
                const HIGGS_AuthorizedDispatchRequest request{sizeof(request),0,c->selected.requestId,c->selected.executor,c->selected.epoch,c->selected.grant};
                if(!routes.CheckSelectedAction || routes.CheckSelectedAction(recognizer,request.requestId,request.epoch,
                        c->proposal.physicalHands,&c->correlation)==HIGGS_HAND_OK)
                    invoked=dispatch.Invoke(c->binding->spec.higgsClient,&request,ExecuteAuthorized,c.get());
            }
            bool entered=false;{
                std::lock_guard lock(s.mutex);entered=c->dispatchEntered;
                if(entered && c->correlation && c->context.input.edge==1 && c->binding->active && c->binding->onInputEnd)
                    s.held.push_back(c);
            }
            if(!entered && invoked!=HIGGS_HAND_OK && copiedResult==HIGGS_HAND_OK && result.stage==HIGGS_ACTION_ACCEPTED)
                routing.CancelRecognizedAction(recognizer,c->selected.requestId,c->selected.epoch,1);
        }
        if(routes.CopySequence)DeliverTerminals(s,routes);
        // Bounded losing candidates are observations, not unsettled effects.
        // Selected dispatched records are never replayed and are not auto-settled.
        std::vector<std::shared_ptr<Candidate>> inspect;
        {std::lock_guard lock(s.mutex);inspect=s.candidates;}
        for(auto& c:inspect) {
            HIGGS_ActionResult result{};result.structSize=sizeof(result);
            const auto status=c->requestId?hands.CopyActionResult(recognizer,c->requestId,&result):HIGGS_HAND_NOT_FOUND;
            if(c->claimed || (status==HIGGS_HAND_OK && result.stage!=HIGGS_ACTION_ACCEPTED)) {
                std::lock_guard lock(s.mutex);s.candidates.erase(std::remove(s.candidates.begin(),s.candidates.end(),c),s.candidates.end());
            }
        }
    }catch(...){}
}
void ResetRegionActions() noexcept {
    try {
        auto& s=State();std::vector<std::shared_ptr<Candidate>> cancelled;HIGGS_HandRoutingInterface routing{};HIGGS_HandClient recognizer{};
        {
            std::lock_guard lock(s.mutex);s.barredEpoch=std::max(s.barredEpoch,s.epoch);s.snapshot={};
            for(auto& b:s.bindings)b->observations={};
            for(auto& c:s.candidates)if(!c->claimed){c->claimed=true;cancelled.push_back(c);}
            routing=s.routing;recognizer=s.recognizer;
        }
        if(recognizer)Cancel(cancelled,routing,recognizer);
    }catch(...){}
}
Status DisconnectRegionActions() noexcept {
    return Guard([&] {
        auto& s=State();std::vector<std::shared_ptr<Binding>> bindings;HIGGS_HandInterface hands{};HIGGS_HandClient recognizer{};
        {std::lock_guard lock(s.mutex);bindings=s.bindings;hands=s.hands;recognizer=s.recognizer;}
        if(!recognizer)return Status::Ok;
        for(auto& b:bindings){auto result=Remove(b->writer,b->id);if(result!=Status::Ok && result!=Status::NotFound)return result;}
        const auto result=hands.UnregisterClient(recognizer);if(result!=HIGGS_HAND_OK)return Convert(result);
        std::lock_guard lock(s.mutex);s.recognizer=0;s.recognizerLabel=0;s.registration=0;s.snapshot={};s.epoch=s.save=s.barredEpoch=0;
        s.observationHighWater=s.timeHighWater=s.generationHighWater=0;s.candidates.clear();s.held.clear();s.routes={};return Status::Ok;
    });
}
} // namespace body_slots::detail

#if defined(_WIN32)
#define BS_REGION_EXPORT __declspec(dllexport)
#else
#define BS_REGION_EXPORT
#endif
extern "C" BS_REGION_EXPORT const body_slots::RegionContinuationApi* BS_CALL BodySlots_QueryRegionContinuationInterface(
    std::uint32_t version,std::uint32_t size) noexcept {
    return version==1 && size==sizeof(body_slots::RegionContinuationApi) ? &body_slots::detail::GetRegionContinuationApi() : nullptr;
}
extern "C" BS_REGION_EXPORT const body_slots::RegionSourcesApi* BS_CALL BodySlots_QueryRegionSourcesInterface(
    std::uint32_t version,std::uint32_t size) noexcept {
    return version==1 && size==sizeof(body_slots::RegionSourcesApi) ? &body_slots::detail::GetRegionSourcesApi() : nullptr;
}
extern "C" BS_REGION_EXPORT const body_slots::RegionActionsApi* BS_CALL BodySlots_QueryRegionActionsInterface(
    std::uint32_t version,std::uint32_t size) noexcept {
    return version==body_slots::kRegionActionsApiVersion && size==sizeof(body_slots::RegionActionsApi)
        ? &body_slots::detail::GetRegionActionsApi() : nullptr;
}
