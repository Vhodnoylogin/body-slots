#pragma once
#include "owned_region_sources.h"
#include "planck_synergy.h"
#include <span>

namespace body_slots::detail {
bool SupportsPlanckAnchors(const PlanckSynergyAPI&) noexcept;
Status ReadPlanckAnchor(const PlanckSynergyAPI&, PlanckToken, const OwnedRegionSource&, float unitsPerMetre,
                        std::span<PlanckBodyPhysics>, RegionAnchorSnapshot&) noexcept;
} // namespace body_slots::detail
