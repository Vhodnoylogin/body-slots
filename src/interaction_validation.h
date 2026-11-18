#pragma once
#include "body_slots_runtime_api.h"
#include "higgshandapi.h"
#include <cstring>
#include <span>
namespace body_slots::detail {
// Stable target identity for HIGGS arbitration, independent of registration
// order, transient instance handles and the current save's allocation order.
inline std::uint64_t SlotTargetKey(const InstanceKey& key) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const char* const parts[] = {"BodySlots.Instance", key.ownerProvider, key.equipmentKey, key.slotKey};
    for (const char* part : parts) {
        for (const char* cursor = part; *cursor; ++cursor)
            hash = (hash ^ static_cast<unsigned char>(*cursor)) * 1099511628211ULL;
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1;
}

inline bool SlotTargetPrecedes(const InstanceKey& a, const InstanceKey& b) noexcept {
    const char* const left[] = {a.ownerProvider, a.equipmentKey, a.slotKey};
    const char* const right[] = {b.ownerProvider, b.equipmentKey, b.slotKey};
    for (unsigned i = 0; i < 3; ++i)
        if (const auto comparison = std::strcmp(left[i], right[i]); comparison)
            return comparison < 0;
    return false;
}

inline const RuntimeRecord* SelectDragTarget(std::span<const RuntimeRecord> records, std::uint32_t hand) noexcept {
    if (hand != Left && hand != Right) return nullptr;
    const RuntimeRecord* target = nullptr;
    for (const auto& row : records)
        if (row.availability == static_cast<std::uint32_t>(RuntimeAvailability::Available) && (row.insideHands & hand) &&
            (!target || SlotTargetPrecedes(row.configured.instance.key, target->configured.instance.key)))
            target = &row;
    return target;
}

enum class SlotAdmission { Waiting, Selected, Discard };
inline SlotAdmission ClassifySlotAdmission(HIGGS_HandStatus status, const HIGGS_InputDecision& decision,
    std::uint64_t epoch, std::uint64_t input, std::uint64_t request, std::uint64_t owner,
    std::uint64_t createdAt, std::uint64_t now) noexcept {
    if (!epoch || !input || !request || !owner || now < createdAt || now - createdAt >= 100000000)
        return SlotAdmission::Discard;
    if (status != HIGGS_HAND_OK)
        return status == HIGGS_HAND_NOT_FOUND || status == HIGGS_HAND_PENDING || status == HIGGS_HAND_NOT_READY
            ? SlotAdmission::Waiting : SlotAdmission::Discard;
    if (decision.structSize != sizeof(decision) || decision.epoch != epoch || decision.inputId != input)
        return SlotAdmission::Discard;
    // A successfully copied pending decision has no selected owner or grant.
    // It must survive until HIGGS closes the common admission window.
    if (decision.reason & HIGGS_DECISION_PENDING)
        return SlotAdmission::Waiting;
    return decision.consumed && decision.requestId == request && decision.owner == owner && decision.grant
        ? SlotAdmission::Selected : SlotAdmission::Discard;
}

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
