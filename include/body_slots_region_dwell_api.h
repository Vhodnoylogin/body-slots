#pragma once
#include "body_slots_region_actions_api.h"
#include "higgshandapi.h"

namespace body_slots {
// A copied spatial observation. The HIGGS snapshot contains public labels only.
struct RegionDwellContext {
    std::uint32_t structSize, activationHand;
    std::uint64_t binding, visit, actionKey;
    RegionObservation observation;
    HIGGS_HandSnapshot hands;
};

struct RegionDwellExecution {
    RegionDwellContext current;
    HIGGS_ActionRequest authority; // private to the registered gameplay executor
};

using DwellEligibility = Status (BS_CALL *)(void*, const RegionDwellContext*, std::uint32_t* eligible);
using DwellExecution = Status (BS_CALL *)(void*, const RegionDwellExecution*);

// Requires shared HIGGS proposal v2. Registration delegates enabling shared
// mode on client (all controls released); it lasts until that client unregisters.
// Eligible is observation-only. Execute is called once per Ready visit, after
// a granted HIGGS decision and fresh-context checks; only Outside rearms it.
struct RegionDwellBindingSpec {
    std::uint32_t structSize, activationHand, resourceHands, handUseKind;
    std::uint32_t proposalDeadlineMs, grantDurationMs;
    std::uint64_t regionId, regionRevision, actionKey;
    HIGGS_HandClient client; // explicitly delegated to this binding; never published
    DwellEligibility eligible;
    DwellExecution execute;
    void* context;
};

struct RegionDwellApi {
    std::uint32_t structSize, version;

    // Use the ordinary binding removal, observation and anchor operations on
    // the returned binding. The consumer owns its factual effects and receipts.
    Status (BS_CALL *RegisterBinding)(std::uint64_t writer, const RegionDwellBindingSpec*, RegionActionRegistration*);
};
using QueryRegionDwellInterface = const RegionDwellApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
static_assert(sizeof(RegionDwellBindingSpec)==80 && sizeof(RegionDwellApi)==16);
}
