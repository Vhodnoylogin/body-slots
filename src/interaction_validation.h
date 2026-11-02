#pragma once
#include "body_slots_runtime_api.h"
#include "higgshandapi.h"
namespace body_slots::detail {
inline constexpr std::uint32_t kKnownHandObservation = HIGGS_OBS_HELD_KNOWN | HIGGS_OBS_EQUIPMENT_KNOWN |
    HIGGS_OBS_TRACKING_VALID | HIGGS_OBS_PALM_VALID;
inline bool MayOfferSlotAction(const HIGGS_HandRecord& hand) noexcept {
    return (hand.observationMask & kKnownHandObservation) == kKnownHandObservation &&
        !(hand.observationMask & HIGGS_OBS_SECONDARY_SUPPORT) &&
        (hand.availability == HIGGS_AVAILABILITY_READY || (hand.availability == HIGGS_AVAILABILITY_RESERVED && !hand.grant));
}
inline bool GrantMatchesObservations(const HIGGS_HandSnapshot& original, const HIGGS_HandSnapshot& current,
    std::uint32_t resources, HIGGS_HandClient owner, HIGGS_HandGrant grant) noexcept {
    if (!resources || (resources & ~HIGGS_HAND_BOTH) || !owner || !grant || !current.worldReady || current.epoch != original.epoch) return false;
    for (unsigned i = 0; i < 2; ++i) if (resources & (i + 1)) {
        const auto& hand = current.hands[i]; const auto& before = original.hands[i];
        if (hand.grant != grant || hand.owner != owner || hand.generation != before.generation ||
            (hand.observationMask & kKnownHandObservation) != kKnownHandObservation || hand.unavailableReasons ||
            hand.availability != HIGGS_AVAILABILITY_RESERVED || hand.heldRefId != before.heldRefId || hand.equippedFormId != before.equippedFormId) return false;
    }
    return true;
}
inline bool MayDisplayPreview(const RuntimeRecord& record) noexcept {
    return record.visual.previewModel[0] && record.contentIdentity && record.quantity && record.visual.contentVersion == record.contentVersion;
}
}
