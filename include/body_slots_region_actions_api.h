#pragma once
// Additive region/input arbitration. All evidence is copied; no engine pointers.
#include "body_slots_regions_api.h"
#include "higgshandapi.h"

namespace body_slots {
constexpr std::uint32_t kRegionActionsApiVersion = 1;
constexpr std::uint32_t kMaxRegionActionBindings = 128, kMaxRegionActionProposals = 8;
struct RegionActionProposal {
    std::uint32_t structSize, physicalHands;
    std::int32_t priority; // stable descending order within this recognizer only
    std::uint32_t grantDurationMs;
    std::uint64_t actionId, reserved;
};
struct RegionActionContext {
    std::uint32_t structSize, reserved;
    std::uint64_t bindingId, anchorRevision;
    RegionObservation observation;
    HIGGS_HandInput input;
};
struct RegionActionExecution {
    RegionActionContext context;
    RegionActionProposal proposal;
    HIGGS_ActionRequest request;
    HIGGS_HandSnapshot currentSnapshot;
};
using RegionActionOffers = Status (BS_CALL *)(void*, const RegionActionContext*,
    RegionActionProposal*, std::uint32_t capacity, std::uint32_t* count);
using RegionActionExecute = Status (BS_CALL *)(void*, const RegionActionExecution*);
struct RegionActionBindingSpec {
    std::uint32_t structSize, physicalHands, button, edgeMask; // grip=1, trigger=2; Down=1, Up=2
    std::uint64_t regionId, regionRevision;
    // Explicit delegated bearer permission to register/remove this client's
    // executor and authorize the independent Body Slots recognizer to route it.
    HIGGS_HandClient higgsClient;
    RegionActionOffers offers;
    RegionActionExecute execute;
    void* context;
};
struct RegionActionRegistration {
    std::uint32_t structSize, reserved;
    std::uint64_t bindingId;
    HIGGS_HandExecutor higgsExecutor;
    std::uint64_t recognizerClient; // public recognizer label for HIGGS HandRouting 2 delegation
};
struct RegionActionsIntegration {
    std::uint32_t structSize, version, connected, reserved;
    std::uint64_t recognizerClient; // public, never usable as a client credential
    std::uint64_t epoch, saveGeneration, observationId;
};
struct RegionActionsApi {
    std::uint32_t structSize, version;
    std::uint64_t capabilities;
    Status (BS_CALL *RegisterBinding)(std::uint64_t writer, const RegionActionBindingSpec*, RegionActionRegistration*);
    // Deactivates immediately. Unavailable from callbacks means retry outside
    // callbacks for confirmed drainage before releasing callback code/context.
    Status (BS_CALL *RemoveBinding)(std::uint64_t writer, std::uint64_t bindingId);
    // Exact revision; every publication copies target evidence. Sources must
    // advance in their own genuine frame/time domain. Target provenance is a
    // declaration, not authentication; no caller-supplied palm/probe exists.
    Status (BS_CALL *PublishAnchor)(std::uint64_t writer, std::uint64_t bindingId,
        std::uint64_t expectedAnchorRevision, const RegionAnchorSnapshot*, std::uint64_t* anchorRevision);
    Status (BS_CALL *CopyIntegration)(std::uint64_t reader, RegionActionsIntegration*);
    Status (BS_CALL *CopyObservation)(std::uint64_t writer, std::uint64_t bindingId,
        std::uint32_t physicalHand, RegionObservation*);
};
using QueryRegionActionsInterface = const RegionActionsApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
static_assert(sizeof(RegionActionProposal)==32 && sizeof(RegionActionBindingSpec)==64, "region action ABI");
static_assert(sizeof(RegionActionRegistration)==32 && sizeof(RegionActionsIntegration)==48, "region action integration ABI");
static_assert(sizeof(RegionActionsApi)==56, "region actions table ABI (x64)");
static_assert(std::is_standard_layout<RegionActionExecution>::value && std::is_trivially_copyable<RegionActionExecution>::value, "copied ABI only");
// Offers run synchronously inside HIGGS recognition and may only describe pure
// eligibility/actions. Execute runs under HIGGS's main-phase authorized dispatch
// fence. Ok acknowledges dispatch ONLY: the consumer reports factual results to
// HIGGS after return, or uses ordinary physical/transaction routes. Body Slots
// invents no inventory, commit, completion or rollback receipt.
} // namespace body_slots
// Export: BodySlots_QueryRegionActionsInterface(version, sizeof(RegionActionsApi)).
