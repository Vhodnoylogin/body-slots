#pragma once
#include "body_slots_regions_api.h"
#include "planck_actor_pose.h"
#include <string>

namespace body_slots::examples {

struct PlanckPoseRegionTarget {
    std::uint64_t epoch, saveGeneration;
    std::uint32_t actorHandle;
    std::string boneName;
    std::uint64_t bodyGeneration, maxAgeNs;
    float skyrimUnitsPerMetre;
};

// Samples one NPC body from a serial SKSE main-thread task before acquiring engine locks.
class PlanckPoseRegionAnchor {
public:
    using Clock = std::uint64_t (*)() noexcept;
    // A supplied clock must use the same monotonic nanosecond domain as PLANCK.
    explicit PlanckPoseRegionAnchor(PlanckPoseRegionTarget target, Clock clock = nullptr);
    PlanckPoseRegionAnchor(const PlanckPoseRegionAnchor&) = delete;
    PlanckPoseRegionAnchor& operator=(const PlanckPoseRegionAnchor&) = delete;
    // On failure the output identifies the same logical pair with valid=0.
    // The caller can publish that unavailable sample or omit this pair from its batch.
    Status Sample(const PlanckActorPoseAPI* planck, const RegionsView& scope, RegionAnchorSnapshot& out) noexcept;

private:
    const PlanckPoseRegionTarget target_;
    const Clock clock_;
    const std::uint64_t openedAtNs_;
    RegionAnchorSnapshot last_{};
};
} // namespace body_slots::examples
