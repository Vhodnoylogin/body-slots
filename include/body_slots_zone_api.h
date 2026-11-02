#pragma once
// Additive geometry/customization ABI. RuntimeApi1 and Definition1 stay unchanged.
#include "body_slots_runtime_api.h"

namespace body_slots {
constexpr std::uint32_t kZoneApiVersion = 1;
enum class ZoneShapeKind : std::uint32_t { Sphere = 1, Capsule = 2, Box = 3 };
// Slot-local Skyrim units. The slot rotation/positive uniform anchor scale apply
// to every field. A capsule with coincident ends is a sphere. Bounds are closed.
struct ZoneShape {
    std::uint32_t structSize, kind;
    float center[3], end[3], halfExtents[3], radius;
    std::uint32_t reserved[2];
};
enum class ProbeSource : std::uint32_t { VirtualHand = 1, ResolvedPalm = 2, Controller = 3, ItemAnchor = 4, WeaponTip = 5, Custom = 6 };
struct ZoneProbe {
    std::uint32_t structSize, source, physicalHand, valid;
    std::uint64_t epoch, frame, sampleId;
    float world[3];
    std::uint32_t reserved;
};
struct ZoneRecord {
    std::uint64_t instanceHandle, epoch, frame, runtimeRevision;
    std::uint64_t bodyGeneration, anchorGeneration;
    RuntimeTransform slotWorld;
    ZoneShape shape;
    std::uint32_t availability, containsProbe, insideHands, shapeOverride;
};
struct SavedZoneShape {
    InstanceCustomizationKey key;
    ZoneShape shape;
};
struct ZoneApi {
    std::uint32_t structSize, version;
    std::uint64_t capabilities; // bit0 shape override/save, bit1 immutable probe queries
    // Owner writer OR dedicated configuration customizer. Exact nonzero save/view
    // protects against races. Missing overrides use the effective Definition1 sphere.
    Status (BS_CALL *SetShape)(std::uint64_t editor, std::uint64_t instanceHandle,
        const ZoneShape*, std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision);

    Status (BS_CALL *ClearShape)(std::uint64_t editor, const InstanceCustomizationKey*,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision);

    // Query never publishes tracking/input authority. It tests the supplied point
    // against a coherent current zone snapshot. Epoch/frame must match exactly.
    Status (BS_CALL *QueryZones)(std::uint64_t reader, const ZoneProbe*,
        ZoneRecord*, std::uint32_t capacity, RuntimeView*);

    Status (BS_CALL *CopySavedShapes)(std::uint64_t reader, SavedZoneShape*,
        std::uint32_t capacity, InstanceView*);
};
using QueryZoneInterface = const ZoneApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
static_assert(sizeof(ZoneShape)==56, "ZoneShape ABI changed");
static_assert(sizeof(ZoneProbe)==56, "ZoneProbe ABI changed");
static_assert(sizeof(ZoneApi)==48, "ZoneApi ABI changed (x64)");
static_assert(std::is_trivially_copyable<ZoneRecord>::value, "Copied ABI only");
} // namespace body_slots
// Export: BodySlots_QueryZoneInterface(version, sizeof(ZoneApi)).
