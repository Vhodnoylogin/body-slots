#pragma once
#include "body_slots_runtime_api.h"

namespace body_slots {
constexpr std::uint32_t kContentObservationApiVersion = 1;
struct ContentObservation {
    std::uint32_t structSize, reserved;
    std::uint64_t epoch, saveGeneration, instanceHandle, expectedContentVersion;
    std::uint64_t contentIdentity, quantity;
};
struct ContentObservationApi {
    std::uint32_t structSize, version;
    std::uint64_t capabilities; // bit0: owned, revision-checked logical projection
    Status (BS_CALL *Publish)(std::uint64_t writer, const ContentObservation*, std::uint64_t* contentVersion);
};
using QueryContentObservationInterface = const ContentObservationApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
// The instance owner publishes its logical assignment/stock observation. This is
// not an inventory mutation, a physical identity, a grant or a transfer receipt.
// Pin epoch/save/handle/contentVersion when observing domain state. A delayed
// result must not be rebased onto a fresh version without fresh domain observation.
// Changed values advance contentVersion and invalidate old offers/presentation.
// Identical values with the current expected version are a no-op. In-flight slot
// actions, unresolved hand commitments and drag prevent a changed projection.
// Observations are transient; provider assignment persistence stays with its owner.
// quantity==0 iff contentIdentity==0. Exact v1 copied POD; output only on success.
static_assert(sizeof(ContentObservation)==56,"content observation ABI");
static_assert(sizeof(ContentObservationApi)==24,"content observation table ABI (x64)");
static_assert(std::is_trivially_copyable<ContentObservation>::value,"copied content observation only");
}
// Export: BodySlots_QueryContentObservationInterface(version, sizeof(ContentObservationApi)).
