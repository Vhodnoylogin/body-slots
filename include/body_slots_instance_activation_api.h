#pragma once
// Additive atomic activation patch for existing instances. No persistence or equipment inference.
#include "body_slots_instance_api.h"

namespace body_slots {
constexpr std::uint32_t kInstanceActivationApiVersion = 1;
constexpr std::uint32_t kMaxActivationChanges = kMaxInstances;
constexpr std::uint64_t kAtomicInstanceActivationCapability = 1;
struct ActivationChange {
    std::uint32_t structSize;
    std::uint32_t flags; // Desired requested activation: zero or kInstanceActive only.
    std::uint64_t instanceHandle;
    std::uint64_t expectedInstanceRevision; // Exact and nonzero, including unchanged rows.
};
struct InstanceActivationApi {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t capabilities;
    // One writer's existing instances only. Nonempty patch, count <= 256, no
    // duplicate handles/keys. Exact NONZERO scope/view and every entry revision.
    // Desired active requires a Current pinned template, including active no-op;
    // desired inactive supports retained Missing/Changed templates without repin.
    // Requested activation does not prove worn equipment/binding/presentation.
    // Initialize committed's exact existing InstanceView size/version/reserved.
    // ALL failures leave output/state untouched. Success returns total instance
    // count and the post-commit view; recopy records pinned to that scope/view.
    // No-op preserves all counters. Changed rows each allocate one existing
    // revision sequence value; the batch advances view once, with no new handles.
    Status (BS_CALL *ApplyActivationChanges)(std::uint64_t writer, const ActivationChange*, std::uint32_t count,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision, InstanceView* committed);
};
static_assert(sizeof(ActivationChange) == 24, "Activation patch ABI changed");
static_assert(sizeof(InstanceActivationApi) == 24, "Activation table ABI changed (x64)");
static_assert(std::is_trivially_copyable<ActivationChange>::value, "Copied ABI only");
using QueryInstanceActivationInterface = const InstanceActivationApi* (BS_CALL *)(std::uint32_t version, std::uint32_t callerSize);
} // namespace body_slots
// Export: BodySlots_QueryInstanceActivationInterface(version, sizeof(InstanceActivationApi)).
// Accessible stable input/output storage must not overlap or be mutated concurrently.
