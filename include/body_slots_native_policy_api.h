#pragma once
// Raw native14 INI compatibility policy, not generic content categories.
#include "body_slots_native_placement_api.h"

namespace body_slots {
constexpr std::uint32_t kNativePolicyApiVersion = 1;
constexpr std::uint64_t kNativePolicyCapability = 2;
enum class NativePolicyField : std::uint32_t {
    Visible = 0, Hand = 1, Small = 2, Medium = 3, Large = 4,
    Ranged = 5, Shield = 6, Torch = 7, HoverHaptics = 8
};
enum class NativeWeaponClass : std::uint32_t { None, Small, Medium, Large, Ranged, Shield, Torch };
constexpr std::uint32_t kNativePolicyFields = 9;
struct NativePolicy { double values[kNativePolicyFields]; };
struct NativePolicySnapshot {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t revision;
    NativePolicy policies[kNativePlacementCount];
};

// These pure compatibility predicates preserve legacy comparisons, including NaN.
inline bool NativePolicyVisible(const NativePolicy& policy) noexcept {
    return policy.values[static_cast<unsigned>(NativePolicyField::Visible)] > 0.5;
}
inline bool NativePolicyAllowsWeapon(const NativePolicy& policy, NativeWeaponClass category) noexcept {
    const auto value = static_cast<unsigned>(category);
    if (!value) return true;
    return value <= 6 && !(policy.values[value + 1] < 0.5);
}

struct NativePolicyApi {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t capabilities;
    Status (BS_CALL *AcquireOwner)(const char*, const NativePolicySnapshot* initial,
        std::uint64_t* owner, NativePolicySnapshot* committed);
    Status (BS_CALL *ReleaseOwner)(std::uint64_t owner);
    // expectedOwner=0 is public copied introspection, retaining no resources.
    // A nonzero expectedOwner also validates the caller's still-live ownership.
    // Unavailable when no owner is active; no output is changed on failure.
    Status (BS_CALL *CopySnapshot)(std::uint64_t expectedOwner, NativePolicySnapshot* snapshot);
    Status (BS_CALL *ReplaceSnapshot)(std::uint64_t owner, const NativePolicySnapshot* candidate,
        NativePolicySnapshot* committed);
};
static_assert(sizeof(NativePolicy) == 72, "Native policy ABI changed");
static_assert(sizeof(NativePolicySnapshot) == 1024, "Native policy snapshot ABI changed");
static_assert(sizeof(NativePolicyApi) == 48, "Native policy table ABI changed (x64)");
static_assert(std::is_trivially_copyable<NativePolicySnapshot>::value, "Copied ABI only");
using QueryNativePolicyInterface = const NativePolicyApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
} // namespace body_slots
// Export: BodySlots_QueryNativePolicyInterface(version, sizeof(NativePolicyApi)).
// Global compatibility state has no cosave and is not reset by load/new game.
