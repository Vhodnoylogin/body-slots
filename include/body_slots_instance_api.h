#pragma once
// Additive transient equipment-provider instances. No engine objects or save records.
#include "body_slots_api.h"

namespace body_slots {
constexpr std::uint32_t kInstanceApiVersion = 1;
constexpr std::uint32_t kMaxInstances = 256;
constexpr std::uint64_t kRuntimeInstancesCapability = 1;
constexpr std::uint64_t kBodyAnchorInstancesCapability = 2;
enum class InstanceBinding : std::uint32_t { BodyAnchor = 1 };
enum class InstanceTemplateState : std::uint32_t { Current = 0, Missing = 1, Changed = 2 };
constexpr std::uint32_t kInstanceActive = 1;

// Canonical bounded identifiers. The writer's registered provider supplies ownership.
// Equipment keys are caller-declared durable identities, never inferred Skyrim IDs.
struct InstanceKey {
    char ownerProvider[64];
    char equipmentKey[64];
    char slotKey[64];
};
struct InstanceSpec {
    std::uint32_t structSize;
    std::uint32_t flags; // kInstanceActive only; requested activation is not binding readiness
    char equipmentKey[64];
    char slotKey[64];
    char definitionProvider[64];
    char definitionKey[64];
    std::uint64_t definitionRevision; // exact current authored revision, nonzero
    std::uint32_t binding; // BodyAnchor only; equipment-node binding is unsupported
    std::uint32_t reserved;
};
struct InstanceRecord {
    std::uint32_t structSize;
    std::uint32_t flags;
    InstanceKey key;
    char definitionProvider[64];
    char definitionKey[64];
    std::uint64_t instanceHandle;
    std::uint64_t instanceRevision;
    std::uint64_t pinnedDefinitionRevision;
    std::uint64_t currentDefinitionRevision; // zero when missing
    std::uint32_t binding;
    std::uint32_t templateState;
    Definition effective; // zero unless templateState is Current; may be disabled
    std::uint32_t customizationMask;
    std::uint32_t reserved;
};
struct InstanceView {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t saveGeneration;
    std::uint64_t viewRevision;
    std::uint64_t registryRevision;
    std::uint64_t configurationRevision;
    std::uint32_t count;
    std::uint32_t reserved;
};
struct InstanceApi {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t capabilities;
    // Writer leases only. Scope must match exactly and be nonzero. Revision zero
    // creates only; updates require the existing exact instance revision. Ownership
    // is derived from the writer. A live key cannot change template keys/binding.
    // committed.structSize must be initialized; failures leave it untouched.
    Status (BS_CALL *UpsertInstance)(std::uint64_t writer, const InstanceSpec*,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedInstanceRevision,
        InstanceRecord* committed);
    Status (BS_CALL *RemoveInstance)(std::uint64_t writer, std::uint64_t instanceHandle,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedInstanceRevision);
    // Readers accept writer/observer/customizer leases. Zero expected values read
    // latest. Initialize view's exact size/version and zero reserved field. Conflict
    // and capacity failures update only the header; records are all-or-none.
    Status (BS_CALL *CopyEffectiveInstances)(std::uint64_t reader,
        std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision,
        InstanceRecord*, std::uint32_t capacity, InstanceView* view);
    // Exact nonzero scope/view, one physical hand/category. Missing/changed/inactive
    // or effectively disabled templates are ineligible. No inventory mutation.
    Status (BS_CALL *EvaluateInstanceEligibility)(std::uint64_t reader,
        std::uint64_t instanceHandle, std::uint64_t expectedSaveGeneration,
        std::uint64_t expectedViewRevision, std::uint32_t physicalHand,
        std::uint32_t contentCategory, std::uint32_t* allowed);
};
static_assert(sizeof(InstanceSpec) == 280, "Instance spec ABI changed");
static_assert(sizeof(InstanceRecord) == 784, "Instance record ABI changed");
static_assert(sizeof(InstanceView) == 48, "Instance view ABI changed");
static_assert(sizeof(InstanceApi) == 48, "Instance table ABI changed (x64)");
static_assert(std::is_trivially_copyable<InstanceRecord>::value, "Copied ABI only");
using QueryInstanceInterface = const InstanceApi* (BS_CALL *)(std::uint32_t version, std::uint32_t callerSize);
} // namespace body_slots
// Export: BodySlots_QueryInstanceInterface(version, sizeof(InstanceApi)).
// Activation/handles/revisions are transient. Definitions and their existing saved
// configuration are inherited; per-instance overrides/persistence are not provided.
