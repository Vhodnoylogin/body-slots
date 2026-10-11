#pragma once
// Additive experimental configuration service. The v1 definition ABI is unchanged.
#include "body_slots_api.h"

namespace body_slots {
constexpr std::uint32_t kConfigurationApiVersion = 1;
constexpr std::uint32_t kMaxCustomizations = 256;
constexpr std::uint64_t kEffectiveConfigurationCapability = 1;
constexpr std::uint64_t kSavedCustomizationCapability = 2;
enum CustomizationField : std::uint32_t {
    CustomizePosition = 1, CustomizeRotation = 2, CustomizeAccessRegion = 4,
    CustomizeEnabled = 8, AllCustomizationFields = 15
};
// Selected fields REPLACE the previous override. Unselected fields must be zero.
// Units/axes/rotation and spherical access geometry are those of Definition v1.
// There is intentionally no anchor, rig, hand-mask, content policy or inventory field.
struct Customization {
    std::uint32_t structSize;
    std::uint32_t fieldMask;
    char provider[64];
    char key[64];
    float position[3];
    float rotation[9];
    float radius;
    float regionCenter[3];
    std::uint32_t enabled;
    std::uint32_t reserved;
};
struct EffectiveSlotRecord {
    SlotRecord slot; // copied EFFECTIVE Definition; definitionRevision remains authored
    std::uint64_t configurationRevision; // global snapshot revision, distinct from author revision
    std::uint32_t customizationMask;
    std::uint32_t reserved;
};
struct CustomizationRecord {
    Customization customization;
    std::uint64_t configurationRevision;
    std::uint32_t available; // 0=dormant missing authored provider/key, never erased by detach
    std::uint32_t reserved;
};
static_assert(sizeof(Customization) == 208, "Customization ABI changed");
static_assert(sizeof(EffectiveSlotRecord) == 440, "Effective configuration ABI changed");
static_assert(sizeof(CustomizationRecord) == 224, "Customization snapshot ABI changed");
static_assert(std::is_trivially_copyable<EffectiveSlotRecord>::value, "Copied ABI only");
struct ConfigurationApi {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t capabilities;
    // A dedicated declared customization authority, never a v1 writer/observer token.
    // At most 32 unique identities. No callbacks or supported hot-DLL-unload protocol.
    Status (BS_CALL *RegisterCustomizer)(const char* identity, std::uint64_t* lease);
    Status (BS_CALL *ReleaseCustomizer)(std::uint64_t lease);
    // Synchronous commitment under the registry lock. Invoke outside frame callbacks.
    // Apply requires a currently authored key and exact NONZERO global revision.
    // It cannot create definitions. Clear also accepts an existing dormant override.
    Status (BS_CALL *ApplyCustomization)(std::uint64_t lease, const Customization*,
        std::uint64_t expectedConfigurationRevision, EffectiveSlotRecord* committed);
    Status (BS_CALL *ClearCustomization)(std::uint64_t lease, const char* provider,
        const char* key, std::uint64_t expectedConfigurationRevision);
    // Reads accept v1 writer/observer leases or a dedicated customizer lease.
    // expected=0 reads latest. Conflict/capacity failure writes no partial records.
    // count/revision return retry information. All records carry the same revision.
    Status (BS_CALL *CopyEffectiveDefinitions)(std::uint64_t reader, std::uint64_t expected,
        EffectiveSlotRecord*, std::uint32_t capacity, std::uint32_t* count, std::uint64_t* revision);
    Status (BS_CALL *CopyCustomizations)(std::uint64_t reader, std::uint64_t expected,
        CustomizationRecord*, std::uint32_t capacity, std::uint32_t* count, std::uint64_t* revision);
    Status (BS_CALL *EvaluateEffectiveEligibility)(std::uint64_t reader, std::uint64_t slotId,
        std::uint64_t expectedConfigurationRevision, std::uint32_t physicalHand,
        std::uint32_t contentCategory, std::uint32_t* allowed);
};
static_assert(sizeof(Api) == 80, "v1 table must remain unchanged (x64)");
static_assert(sizeof(ConfigurationApi) == 72, "Configuration table ABI changed (x64)");
using QueryConfigurationInterface = const ConfigurationApi* (BS_CALL *)(std::uint32_t version, std::uint32_t callerSize);
} // namespace body_slots
// Export: BodySlots_QueryConfigurationInterface(version, sizeof(ConfigurationApi)).
// Saves contain only overrides keyed by provider/key. No leases, revisions, slot IDs,
// runtime instances or equipment identities are persisted. Those are later services.
