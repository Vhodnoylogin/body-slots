#pragma once
#include "body_slots_region_sources_api.h"

namespace body_slots::detail {
struct OwnedRegionSource {
    RegionPlanckBody body{};
    std::uint64_t higgsEpoch{}, saveGeneration{}, openedAtNs{};
};
using ReadOwnedAnchor = Status (*)(const OwnedRegionSource&, RegionAnchorSnapshot&) noexcept;
void SetOwnedPlanckReader(ReadOwnedAnchor reader) noexcept;
using ReadPpbDigest = Status (*)(const PpbRegionQuery&, PpbRegionEvidence&) noexcept;
void SetPpbDigestReader(ReadPpbDigest reader) noexcept;
const RegionSourcesApi& GetRegionSourcesApi() noexcept;
} // namespace body_slots::detail
