#include "region_dwell.h"
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

namespace body_slots::detail {
namespace {
bool SameSource(const RegionSampleSource& a, const RegionSampleSource& b) {
    return a.identity == b.identity && a.generation == b.generation && a.epoch == b.epoch &&
        a.saveGeneration == b.saveGeneration && !std::strcmp(a.provider,b.provider) && !std::strcmp(a.source,b.source);
}

std::uint64_t Now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool SameVisit(const DwellEvidence& start, const DwellEvidence& current, std::uint32_t resources, std::uint64_t now) {
    const auto& before = start.context;
    const auto& after = current.context;
    if (start.sourceRevision != current.sourceRevision || before.binding != after.binding ||
        after.observation.state != static_cast<std::uint32_t>(RegionSpatialState::Ready) ||
        !SameSource(before.observation.anchor.source, after.observation.anchor.source) ||
        !SameSource(before.observation.probe.source, after.observation.probe.source)) return false;
    for (unsigned hand : {Left,Right}) {
        if (!(resources & hand)) continue;
        const auto& earlier=before.hands.hands[hand-1];
        const auto& actual=after.hands.hands[hand-1];
        constexpr auto required=HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN|HIGGS_OBS_TRACKING_VALID|HIGGS_OBS_PALM_VALID;
        if ((actual.observationMask & required)!=required || actual.unavailableReasons || !actual.poseObservedAtNs ||
            actual.poseObservedAtNs>static_cast<std::uint64_t>(now) || static_cast<std::uint64_t>(now)-actual.poseObservedAtNs>100000000 ||
            !actual.generation || actual.generation!=earlier.generation || actual.heldRefId!=earlier.heldRefId ||
            actual.equippedFormId!=earlier.equippedFormId) return false;
    }
    return true;
}
}

bool DwellProvider::Available() const {
    return proposals.proposals.structSize==sizeof(proposals) && proposals.proposals.abiVersion==2 &&
        (proposals.proposals.capabilities&HIGGS_HAND_USE_PROPOSALS_CAP_SHARED_WINDOWS) && proposals.EnableSharedRounds &&
        proposals.ProposeOrdered && proposals.proposals.Copy && proposals.proposals.Cancel &&
        uses.structSize==sizeof(uses) && uses.abiVersion==1 && uses.EndHandUse &&
        contexts.structSize==sizeof(contexts) && contexts.abiVersion==HIGGS_INPUT_CONTEXT_ABI_VERSION &&
        (contexts.capabilities&1) && contexts.CopyContext && dispatch.structSize==sizeof(dispatch) &&
        dispatch.abiVersion==HIGGS_AUTHORIZED_DISPATCH_ABI_VERSION && dispatch.Invoke;
}

bool ValidDwellSpec(const RegionDwellBindingSpec& s) {
    return s.structSize==sizeof(s) && (s.activationHand==Left || s.activationHand==Right) &&
        s.resourceHands<=Both && (s.resourceHands&s.activationHand) && s.handUseKind>=HIGGS_USE_HAND_GOALS &&
        s.handUseKind<=HIGGS_USE_CONSUMPTION && s.proposalDeadlineMs && s.proposalDeadlineMs<=1000 &&
        s.grantDurationMs && s.grantDurationMs<=60000 && s.regionId && s.regionRevision && s.actionKey &&
        s.client && s.eligible && s.execute;
}

DwellVisit::DwellVisit(RegionDwellBindingSpec spec,DwellProvider provider,std::uint64_t targetKey)
    : spec_(spec),provider_(provider),targetKey_(targetKey) {}

HIGGS_HandStatus DwellVisit::EnableSharedRounds() const {
    return provider_.proposals.EnableSharedRounds(spec_.client);
}

std::uint64_t DwellVisit::Gameplay(std::uint64_t epoch) const {
    HIGGS_InputContextState context{};
    context.structSize=sizeof(context);
    if(provider_.contexts.CopyContext(spec_.client,&context)!=HIGGS_HAND_OK || context.epoch!=epoch ||
        context.kind!=HIGGS_CONTEXT_GAMEPLAY || context.activeMenus)return 0;
    return context.generation;
}

bool DwellVisit::Eligible(const RegionDwellContext& context) const {
    std::uint32_t answer{};
    try { return spec_.eligible(spec_.context,&context,&answer)==Status::Ok && answer==1; }
    catch(...) { return false; }
}

void DwellVisit::Retire(const Attempt& attempt) {
    if(!attempt.proposal)return;
    const auto epoch=attempt.start.context.hands.epoch;
    // Selection may win before Cancel reaches HIGGS. End only our accepted,
    // undispatched use. The public selection label is never a mutation permit.
    if(provider_.proposals.proposals.Cancel(spec_.client,attempt.proposal,epoch)==HIGGS_HAND_CONFLICT)
        provider_.uses.EndHandUse(spec_.client,attempt.proposal,epoch);
}

void DwellVisit::Invalidate(bool removed) {
    std::optional<Attempt> previous;
    {
        std::lock_guard lock(mutex_);
        stopped_=stopped_ || removed;
        previous=std::exchange(pending_,std::nullopt);
    }
    if(previous)Retire(*previous);
}

bool DwellVisit::Claim(std::uint64_t visit) {
    std::lock_guard lock(mutex_);
    if(stopped_ || !pending_ || pending_->start.context.visit!=visit)return false;
    pending_.reset();
    return true;
}

HIGGS_HandStatus HIGGS_HAND_CALL DwellVisit::Execute(const HIGGS_ActionRequest* request,
    const HIGGS_HandSnapshot* snapshot,void* context) noexcept {
    auto& call=*static_cast<Invocation*>(context);
    try {
        auto& self=call.visit;
        DwellEvidence current{};
        if(!request || !snapshot || request->inputId || request->owner!=self.spec_.client ||
            request->requestId!=call.attempt.proposal || request->executor!=call.executor ||
            request->actionId!=self.spec_.actionKey || request->contextId!=self.targetKey_ ||
            request->contextGeneration!=call.attempt.start.context.visit || request->physicalHands!=self.spec_.resourceHands ||
            !request->grant || !call.read(call.binding,*snapshot,current) ||
            !SameVisit(call.attempt.start,current,self.spec_.resourceHands,Now()) ||
            self.Gameplay(request->epoch)!=call.attempt.menuGeneration)return HIGGS_HAND_STALE;
        current.context.visit=call.attempt.start.context.visit;
        if(!self.Eligible(current.context) || !self.Claim(current.context.visit))return HIGGS_HAND_STALE;
        call.entered=true;
        RegionDwellExecution execution{current.context,*request};
        return self.spec_.execute(self.spec_.context,&execution)==Status::Ok ? HIGGS_HAND_OK : HIGGS_HAND_NOT_READY;
    } catch(...) {return HIGGS_HAND_INTERNAL_ERROR;}
}

void DwellVisit::Finish(Invocation& call,const HIGGS_ActionRequest& action) {
    const HIGGS_AuthorizedDispatchRequest request{sizeof(request),0,call.attempt.proposal,call.executor,action.epoch,action.grant};
    provider_.dispatch.Invoke(spec_.client,&request,Execute,&call);
    if(!call.entered && Claim(call.attempt.start.context.visit))Retire(call.attempt);
}

void DwellVisit::Tick(void* binding,HIGGS_HandExecutor executor,const HIGGS_HandSnapshot& snapshot,ReadDwellEvidence read) {
    DwellEvidence evidence{};
    if(!read(binding,snapshot,evidence)) { Invalidate(false); return; }
    const auto spatial=static_cast<RegionSpatialState>(evidence.context.observation.state);
    if(spatial==RegionSpatialState::Outside) {
        Invalidate(false);
        std::lock_guard lock(mutex_);armed_=true;
        return;
    }
    if(spatial!=RegionSpatialState::Ready) { Invalidate(false);return; }
    const auto menu=Gameplay(snapshot.epoch);
    if(!menu) {Invalidate(false);return;}
    Attempt attempt{};
    bool submitting=false;
    {
        std::lock_guard lock(mutex_);
        if(stopped_)return;
        if(pending_)attempt=*pending_;
        else {
            if(!armed_ || visit_==std::numeric_limits<std::uint64_t>::max())return;
            armed_=false;
            evidence.context.visit=++visit_;
            attempt={evidence,menu,0};
            pending_=attempt;
            submitting=true;
        }
    }
    if(!SameVisit(attempt.start,evidence,spec_.resourceHands,Now()) || menu!=attempt.menuGeneration) {
        if(Claim(attempt.start.context.visit))Retire(attempt);
        return;
    }
    if(submitting) {
        if(!Eligible(evidence.context)) {Claim(attempt.start.context.visit);return;}
        HIGGS_HandUseProposalRequest proposal{};
        proposal.structSize=sizeof(proposal);
        proposal.deadlineMs=spec_.proposalDeadlineMs;
        proposal.use={sizeof(proposal.use),spec_.handUseKind,spec_.resourceHands,spec_.grantDurationMs,
            snapshot.epoch,targetKey_,attempt.start.context.visit,executor};
        for(unsigned hand:{Left,Right})if(spec_.resourceHands&hand)proposal.expectedHandGenerations[hand-1]=snapshot.hands[hand-1].generation;
        if(provider_.proposals.ProposeOrdered(spec_.client,&proposal,spec_.actionKey,targetKey_,&attempt.proposal)!=HIGGS_HAND_OK) {
            Claim(attempt.start.context.visit);return;
        }
        {
            std::lock_guard lock(mutex_);
            if(!stopped_ && pending_ && pending_->start.context.visit==attempt.start.context.visit) {
                pending_->proposal=attempt.proposal;
                return;
            }
        }
        Retire(attempt);
        return;
    }
    HIGGS_HandUseProposalState state{};state.structSize=sizeof(state);
    const auto copied=provider_.proposals.proposals.Copy(spec_.client,attempt.proposal,&state);
    if(copied==HIGGS_HAND_OK && state.stage==HIGGS_USE_PROPOSAL_PENDING)return;
    if(copied!=HIGGS_HAND_OK || state.proposalId!=attempt.proposal || state.stage!=HIGGS_USE_PROPOSAL_ACCEPTED ||
       !state.use.active || state.use.epoch!=snapshot.epoch) {
        if(Claim(attempt.start.context.visit))Retire(attempt);
        return;
    }
    // Copy is owner-only; the broker returns the private accepted authority.
    // Every public snapshot and eligibility record remains observation-only.
    Invocation call{*this,binding,read,attempt,executor};
    Finish(call,state.use.authority);
}
}
