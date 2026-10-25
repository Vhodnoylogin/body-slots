#pragma once
// Additive saved instance placement. Existing instance copies retain inherited semantics.
#include "body_slots_instance_api.h"
#include "body_slots_configuration_api.h"

namespace body_slots {
constexpr std::uint32_t kInstanceConfigurationApiVersion = 1;
constexpr std::uint32_t kMaxInstanceCustomizations = 256;
constexpr std::uint64_t kConfiguredInstancesCapability = 1;
constexpr std::uint64_t kSavedInstanceCustomizationCapability = 2;
struct InstanceCustomizationKey {
    InstanceKey instance;
    char definitionProvider[64];
    char definitionKey[64];
};
struct InstanceCustomization {
    std::uint32_t structSize;
    std::uint32_t fieldMask; // existing CustomizationField bits; replaces previous mask
    InstanceCustomizationKey key;
    float position[3];
    float rotation[9];
    float radius;
    float regionCenter[3];
    std::uint32_t enabled;
    std::uint32_t reserved;
};
struct InstanceCustomizationRecord {
    InstanceCustomization customization;
    std::uint32_t available; // matching Current runtime instance, even if inactive
    std::uint32_t reserved;
};
struct ConfiguredInstanceRecord {
    // In this NEW table, instance.effective is the final definition+instance result.
    // instance.customizationMask remains the DEFINITION-scoped provenance mask.
    InstanceRecord instance;
    std::uint32_t instanceCustomizationMask;
    std::uint32_t reserved;
};
struct InstanceConfigurationApi {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t capabilities;
    // Dedicated customizer lease and exact NONZERO scope/view. Apply requires a
    // matching Current runtime instance; inactive instances may be edited. Complete
    // output storage and committed.instance.structSize must be initialized.
    // Rejected operations leave outputs/state untouched; identical values are no-op.
    Status (BS_CALL *ApplyCustomization)(std::uint64_t customizer, const InstanceCustomization*,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision,
        ConfiguredInstanceRecord* committed);
    // Clearing an existing dormant key is supported. Unselected Apply fields must
    // be zero; omitting a field from the replacement mask restores inheritance.
    Status (BS_CALL *ClearCustomization)(std::uint64_t customizer, const InstanceCustomizationKey*,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision);
    // Writer/observer/customizer reads. Zero expectations select latest. Initialize
    // view's exact size/version and zero reserved field. Conflict/capacity failures
    // update only the header; records are all-or-none. Other failures change nothing.
    Status (BS_CALL *CopyConfiguredInstances)(std::uint64_t reader,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision,
        ConfiguredInstanceRecord*, std::uint32_t capacity, InstanceView* view);
    Status (BS_CALL *CopyCustomizations)(std::uint64_t reader,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision,
        InstanceCustomizationRecord*, std::uint32_t capacity, InstanceView* view);
    Status (BS_CALL *EvaluateConfiguredEligibility)(std::uint64_t reader, std::uint64_t instanceHandle,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision,
        std::uint32_t physicalHand, std::uint32_t contentCategory, std::uint32_t* allowed);
};
static_assert(sizeof(InstanceCustomizationKey) == 320, "Instance customization key ABI changed");
static_assert(sizeof(InstanceCustomization) == 400, "Instance customization ABI changed");
static_assert(sizeof(InstanceCustomizationRecord) == 408, "Instance customization copy ABI changed");
static_assert(sizeof(ConfiguredInstanceRecord) == 792, "Configured instance ABI changed");
static_assert(sizeof(InstanceConfigurationApi) == 56, "Instance configuration table ABI changed (x64)");
static_assert(std::is_trivially_copyable<ConfiguredInstanceRecord>::value, "Copied ABI only");
using QueryInstanceConfigurationInterface = const InstanceConfigurationApi* (BS_CALL *)(std::uint32_t version, std::uint32_t callerSize);
} // namespace body_slots
// Export: BodySlots_QueryInstanceConfigurationInterface(version, sizeof(InstanceConfigurationApi)).
// Saved records contain stable keys/selected placement fields only. No handles,
// activation, leases, process revisions, body bindings or equipment identities inferred by the service.
