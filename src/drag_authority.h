#pragma once
#include "manipulation.h"
#include "higgsgrantmutation.h"

namespace body_slots::detail {
struct DragCommitOutcome {
    HIGGS_HandStatus authority=HIGGS_HAND_UNSUPPORTED;
    Status customization=Status::Unavailable;
    HIGGS_ActionResult receipt{};
};

// The sole interactive saved-placement write boundary. Both the native adapter
// and the real-provider protocol fixture use this implementation.
inline DragCommitOutcome CommitDragWithAuthority(const HIGGS_GrantMutationInterface& mutation,
    const HIGGS_HandInterface& hands,HIGGS_HandClient client,const HIGGS_ActionRequest& action,
    std::uint64_t handGeneration)
{
    DragCommitOutcome result{};
    if(!mutation.WithAuthorizedAction||!hands.ReleaseGrant)return result;
    struct Context {
        const HIGGS_ActionRequest* action;
        std::uint64_t generation,client;
        Status status=Status::Unavailable;
    } context{&action,handGeneration,client};
    const auto commit=+[](const HIGGS_ActionRequest* authorized,const HIGGS_HandSnapshot* current,void* raw)->HIGGS_HandStatus {
        auto& data=*static_cast<Context*>(raw);const auto& expected=*data.action;
        if(!authorized||!current||!current->worldReady||expected.physicalHands<1||expected.physicalHands>2||
            authorized->requestId!=expected.requestId||authorized->grant!=expected.grant)return HIGGS_HAND_NOT_READY;
        const auto& hand=current->hands[expected.physicalHands-1];
        constexpr std::uint32_t required=HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN|HIGGS_OBS_TRACKING_VALID|HIGGS_OBS_PALM_VALID;
        if(hand.generation!=data.generation||(hand.observationMask&required)!=required||
            hand.occupancy!=HIGGS_OCCUPANCY_EMPTY||hand.owner!=data.client||hand.grant!=expected.grant||
            (hand.unavailableReasons&~HIGGS_HAND_REASON_EXISTING_USE))return HIGGS_HAND_STALE;
        data.status=FinishAuthorizedDrag(expected.requestId,true);
        return data.status==Status::Ok?HIGGS_HAND_OK:HIGGS_HAND_CONFLICT;
    };
    HIGGS_GrantedMutationRequest request{sizeof(request),HIGGS_ACTION_COMPLETED,action.requestId,action.executor,action.epoch,action.grant};
    result.receipt.structSize=sizeof(result.receipt);
    result.authority=mutation.WithAuthorizedAction(client,&request,commit,&context,&result.receipt);
    result.customization=context.status;
    // Completion intentionally does not release physical holds in HIGGS. A slot
    // edit has no continued hand use, so retire its owned grant explicitly.
    hands.ReleaseGrant(client,action.grant);
    return result;
}
}
