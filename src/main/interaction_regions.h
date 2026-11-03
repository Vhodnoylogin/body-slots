#pragma once
#include "body_slots_regions_api.h"

namespace body_slots::detail {
const RegionsApi& GetRegionsApi() noexcept;
// Only the trusted runtime adapter selects the accepted save/epoch scope. Changes
// clear all observations and timers. Definitions live until lease release/removal.
void SetInteractionRegionsScope(std::uint64_t epoch, std::uint64_t saveGeneration) noexcept;
void InvalidateInteractionRegions() noexcept;
// Optional eager release integration; every public operation also prunes dead leases.
void ReleaseInteractionRegionsClient(std::uint64_t client) noexcept;
Status ValidateInteractionRegion(const InteractionRegionSpec&) noexcept;
} // namespace body_slots::detail
