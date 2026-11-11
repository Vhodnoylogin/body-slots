#pragma once
#include "body_slots_region_sources_api.h"
#include "PpbTouchAPI.h"
#include <cmath>
#include <span>

namespace body_slots::detail {
inline Status SelectPpbDigest(const PpbRegionQuery& query, const PPBAPI::PpbTouchFrame& frame,
                              std::span<const PPBAPI::PpbTouchContact> contacts,
                              PpbRegionEvidence& output) noexcept {
    if (!frame.epoch || !frame.sequence || frame.copied > contacts.size() || frame.copied > frame.available)
        return Status::Unavailable;
    PpbRegionEvidence evidence{sizeof(evidence)};
    evidence.ppbEpoch = frame.epoch;
    evidence.ppbSequence = frame.sequence;
    evidence.ppbAgeMs = frame.ageMs;
    evidence.available = frame.available;
    evidence.copied = frame.copied;
    const unsigned wand = query.hand == Left ? 1u : 0u;
    for (std::uint32_t i = 0; i < frame.copied; ++i) {
        const auto& c = contacts[i];
        // PPB's head and other probes also use wand=0. Wielded objects have
        // their own contact source. Only the five hand categories qualify.
        const bool handSource = c.sourceKind == PPBAPI::kSourceFinger || c.sourceKind == PPBAPI::kSourcePalm ||
                                c.sourceKind == PPBAPI::kSourceFist || c.sourceKind == PPBAPI::kSourceHand ||
                                c.sourceKind == PPBAPI::kSourceGrab;
        if (!handSource || c.actorFormId != query.actorFormId || c.toucherFormId != 0x14 || c.wand != wand ||
            c.slot != static_cast<int>(query.slot) || c.leftTwin != query.leftTwin || c.child < 0 ||
            static_cast<unsigned>(c.child) != query.child)
            continue;
        if (!std::isfinite(c.distU) || !std::isfinite(c.durationS) || c.durationS < 0)
            return Status::Unavailable;
        evidence.reported = 1;
        evidence.region = c.region;
        evidence.subRegion = c.subRegion;
        evidence.sourceKind = c.sourceKind;
        evidence.retainedEngineContact = c.engineContact != 0;
        evidence.signedDistanceUnits = c.distU;
        evidence.reportedDurationSeconds = c.durationS;
        break;
    }
    output = evidence;
    return Status::Ok;
}
} // namespace body_slots::detail
