#pragma once

#include "body_slots_region_actions_api.h"
#include "planck_synergy.h"
#include <span>

namespace body_slots::examples {

// Supply a newly registered NPC binding and a body selected in the same loaded
// game world. The two APIs have independent epoch counters.
struct PlanckRegionTarget {
    std::uint64_t writer, bindingId, higgsEpoch, saveGeneration;
    PlanckToken planckClient;
    PlanckBodyRef body;
    float skyrimUnitsPerMetre;
    std::uint64_t maxAgeNs;
};

// One publisher per binding, called serially outside provider callbacks.
class PlanckRegionAnchor {
public:
    PlanckRegionAnchor(const RegionActionsApi& actions, PlanckRegionTarget target) noexcept;
    PlanckRegionAnchor(const PlanckRegionAnchor&) = delete;
    PlanckRegionAnchor& operator=(const PlanckRegionAnchor&) = delete;

    // Reuse caller-owned body storage across bindings; 256 entries covers
    // the current PLANCK implementation's maximum bodies for one actor.
    Status Update(const PlanckSynergyAPI* planck, std::span<PlanckBodyPhysics> scratch);
    Status Withdraw();

private:
    const RegionActionsApi& actions_;
    const PlanckRegionTarget target_;
    const std::uint64_t openedAtNs_;
    std::uint64_t revision_{}, sample_{}, frame_{}, time_{};
    bool published_{};
};

} // namespace body_slots::examples
