#pragma once
#include "body_slots_regions_api.h"

namespace body_slots {
// Copied PLANCK actor/body identity obtained in the current game. A replaced
// world, actor or body needs a new binding. All identity fields are opaque.
struct RegionPlanckBody {
    std::uint32_t structSize, actorHandle, bodyIndex, reserved;
    std::uint64_t worldEpoch, actorGeneration, bodyGeneration;
};
struct PpbRegionQuery {
    std::uint32_t structSize, actorFormId, hand, slot, leftTwin, child;
};
// A digest scan groups observations and can omit anatomical children. No
// reported match is a distinct result from a physical separation or exit.
struct PpbRegionEvidence {
    std::uint32_t structSize, reported, available, copied;
    std::uint64_t logicalEpoch, saveGeneration, ppbEpoch, ppbSequence, ppbAgeMs;
    std::uint32_t region, subRegion, sourceKind, retainedEngineContact;
    float signedDistanceUnits, reportedDurationSeconds;
};
struct RegionSourcesApi {
    std::uint32_t structSize, version;
    Status(BS_CALL* BindPlanckBody)(std::uint64_t writer, std::uint64_t binding, const RegionPlanckBody*);

    Status(BS_CALL* UnbindSource)(std::uint64_t writer, std::uint64_t binding);

    // Call on the main thread after the installed provider phase.
    Status(BS_CALL* CopyPpbDigestEvidence)(std::uint64_t writer, std::uint64_t binding, const PpbRegionQuery*,
                                           PpbRegionEvidence*);
};
using QueryRegionSourcesInterface = const RegionSourcesApi*(BS_CALL*)(std::uint32_t, std::uint32_t);
static_assert(sizeof(RegionPlanckBody) == 40 && sizeof(RegionSourcesApi) == 32 && sizeof(PpbRegionEvidence) == 80);
} // namespace body_slots
