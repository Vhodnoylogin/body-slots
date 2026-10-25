#pragma once
// Additive compatibility service, separate from authored definitions and cosaves.
#include "body_slots_api.h"

namespace body_slots {
constexpr std::uint32_t kNativePlacementApiVersion = 1;
constexpr std::uint32_t kNativePlacementCount = 14;
constexpr std::uint64_t kNativePlacementCapability = 1;

// Opaque legacy local pose, copied bit for bit. It is not a validated rigid
// Definition, access region, item policy, engine transform or inventory record.
struct NativePlacement {
    float position[3];
    float rotation[9];
};

struct NativePlacementSnapshot {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t revision;
    NativePlacement placements[kNativePlacementCount]; // native slot number minus one
};

struct NativePlacementApi {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t capabilities;
    // One exclusive compatibility owner. Initial revision must be zero. Complete
    // activation/import or no change; duplicate acquisition never steals ownership.
    Status (BS_CALL *AcquireOwner)(const char* identity, const NativePlacementSnapshot* initial,
        std::uint64_t* owner, NativePlacementSnapshot* committed);
    Status (BS_CALL *ReleaseOwner)(std::uint64_t owner);
    Status (BS_CALL *CopySnapshot)(std::uint64_t owner, NativePlacementSnapshot* snapshot);
    // candidate.revision must equal the latest nonzero bank revision. All fourteen
    // poses commit together; failures leave the bank and output untouched.
    Status (BS_CALL *ReplaceSnapshot)(std::uint64_t owner, const NativePlacementSnapshot* candidate,
        NativePlacementSnapshot* committed);
};

static_assert(sizeof(NativePlacement) == 48, "Native placement ABI changed");
static_assert(sizeof(NativePlacementSnapshot) == 688, "Native snapshot ABI changed");
static_assert(sizeof(NativePlacementApi) == 48, "Native table ABI changed (x64)");
static_assert(std::is_trivially_copyable<NativePlacementSnapshot>::value, "Copied ABI only");
using QueryNativePlacementInterface = const NativePlacementApi* (BS_CALL *)(
    std::uint32_t version, std::uint32_t callerSize);
} // namespace body_slots
// Export: BodySlots_QueryNativePlacementInterface(version, sizeof(NativePlacementApi)).
// No automatic load/new-game reset and no serialization: the compatibility owner
// retains its existing global INI persistence. Runtime leases are never saved.
