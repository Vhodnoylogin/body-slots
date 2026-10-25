// SDK example only: call from an initialized consumer after Body Slots is loaded.
// This is not an SKSE plugin, startup hook or public ABI declaration.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "body_slots_api.h"
#include <array>

namespace body_slots_example {
struct Definitions {
    std::array<body_slots::SlotRecord, body_slots::kMaxDefinitions> records{};
    std::uint32_t count = 0;
    std::uint64_t registryRevision = 0;
};
body_slots::Status ObserveDefinitions(Definitions& output) {
    using namespace body_slots;
    const auto module = GetModuleHandleW(L"BodySlots.dll"); // Never load another mod implicitly.
    if (!module) return Status::Unavailable;
    const auto query = reinterpret_cast<QueryInterface>(GetProcAddress(module, "BodySlots_QueryInterface"));
    if (!query) return Status::Unavailable;
    const auto* api = query(kApiVersion, sizeof(Api));
    if (!api || api->structSize != sizeof(Api) || api->version != kApiVersion || !(api->capabilities & kSnapshotCapability) ||
        !api->RegisterObserver || !api->CopyDefinitions || !api->ReleaseClient) return Status::UnsupportedVersion;
    std::uint64_t observer = 0;
    const auto registered = api->RegisterObserver(&observer);
    if (registered != Status::Ok) return registered;
    Definitions candidate;
    const auto copied = api->CopyDefinitions(observer, 0, candidate.records.data(), kMaxDefinitions, &candidate.count, &candidate.registryRevision);
    const auto released = api->ReleaseClient(observer);
    if (copied != Status::Ok) return copied;
    if (released != Status::Ok) return released;
    output = candidate; // Publish only the successful copied result.
    return Status::Ok; // A zero count is valid and does not imply visible or playable slots.
}
} // namespace body_slots_example
