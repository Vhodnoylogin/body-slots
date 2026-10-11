#include "body_slots_api.h"
#include <cstring>

// A separate consumer DLL uses only the copied public header. It owns no registry code.
extern "C" __declspec(dllexport) int BS_CALL BodySlotsConsumer_Run(const body_slots::Api* api, std::uint64_t observedSlot) noexcept {
    using namespace body_slots;
    if (!api || api->version != kApiVersion) return 1;
    std::uint64_t token = 0;
    if (api->RegisterClient("test.consumer-dll", &token) != Status::Ok) return 2;
    Definition d{}; d.structSize = sizeof(d); d.flags = kEnabled;
    std::strcpy(d.provider, "test.consumer-dll"); std::strcpy(d.key, "right-potion");
    std::strcpy(d.anchor, "NPC Pelvis [Pelv]"); std::strcpy(d.referenceRig, "skyrimvr-humanoid-v1");
    std::strcpy(d.kind, "potion-pouch"); d.rotation[0] = d.rotation[4] = d.rotation[8] = 1;
    d.radius = 8; d.handMask = Right; d.contentMask = Potion;
    SlotRecord record{}; int result = 0;
    if (api->UpsertDefinition(token, &d, 0, &record) != Status::Ok) result = 3;
    SlotRecord copied[kMaxDefinitions]{}; std::uint32_t count = 0; std::uint64_t revision = 0;
    if (!result && api->CopyDefinitions(token, 0, copied, kMaxDefinitions, &count, &revision) != Status::Ok) result = 4;
    bool sawObserved = false, sawOwn = false;
    for (std::uint32_t i = 0; i < count; ++i) { sawObserved |= copied[i].slotId == observedSlot; sawOwn |= copied[i].slotId == record.slotId; }
    if (!result && (!sawObserved || !sawOwn)) result = 5;
    std::uint32_t allowed = 0;
    if (!result && (api->EvaluateEligibility(token, record.slotId, record.definitionRevision, Right, Potion, &allowed) != Status::Ok || !allowed)) result = 6;
    if (!result && (api->EvaluateEligibility(token, record.slotId, record.definitionRevision, Right, Weapon, &allowed) != Status::Ok || allowed)) result = 7;
    if (api->ReleaseClient(token) != Status::Ok && !result) result = 8;
    return result;
}
