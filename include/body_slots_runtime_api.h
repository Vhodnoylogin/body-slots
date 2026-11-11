#pragma once
// Independent runtime. No engine pointers, hand classifier, inventory or native14 API.
#include "body_slots_instance_configuration_api.h"

namespace body_slots {
constexpr std::uint32_t kRuntimeApiVersion = 1;
constexpr std::uint32_t kMaxBehaviors = 64, kMaxRuntimeObservers = 64;
constexpr std::uint32_t kMaxOffersPerBehavior = 8, kMaxActionOffers = 512;
constexpr std::uint32_t kMaxPendingActions = 64;
enum class RuntimeBindingKind : std::uint32_t { BodyNode = 1, EquipmentNode = 2, UnresolvedEquipment = 3 };
struct RuntimeBindingSpec {
    std::uint32_t structSize, kind, requiredEquipmentFormId, reserved;
    char node[96]; // exact node; empty uses the authored anchor for BodyNode only
};
struct RuntimeTransform { float position[3], rotation[9], scale; };
enum class RuntimeAvailability : std::uint32_t {
    Available = 0, Inactive = 1, TemplateUnavailable = 2, AnchorUnavailable = 3,
    InvalidObservation = 4, ConfigurationChanged = 5
};
enum class PresentationState : std::uint32_t { None = 0, Pending = 1, Ready = 2, Failed = 3 };
struct SlotVisualSpec {
    std::uint32_t structSize, flags; // bit0: equipment already renders holder
    char holderModel[192], previewModel[192]; // relative mesh paths; preview creates no stock
    std::uint64_t contentVersion;
};
constexpr std::uint32_t kRuntimeBindingOverride=1,kRuntimeVisualOverride=2;
struct RuntimeCustomizationRecord {
    InstanceCustomizationKey key;
    std::uint32_t fieldMask,reserved;
    RuntimeBindingSpec binding;
    SlotVisualSpec visual;
};
struct RuntimeRecord {
    ConfiguredInstanceRecord configured;
    RuntimeBindingSpec binding;
    RuntimeTransform world;
    float regionCenter[3], radius;
    std::uint64_t epoch, frame, bodyGeneration, anchorGeneration, runtimeRevision;
    std::uint64_t contentVersion, contentIdentity, quantity;
    std::uint32_t availability, insideHands;
    SlotVisualSpec visual;
    std::uint64_t visualRevision, displayedContentVersion;
    std::uint32_t presentationState, reserved;
};
struct RuntimeView {
    std::uint32_t structSize, version;
    std::uint64_t epoch, frame, runtimeRevision, saveGeneration, viewRevision;
    std::uint32_t count, reserved;
};
// The internal adapter supplies availability from HIGGS. No local hand classifier.
struct RuntimeInput {
    std::uint32_t structSize, physicalHand, contentCategory, available; // category0: no incoming item; withdrawal may still be offered
    std::uint32_t button, edge; // HIGGS button/edge numeric values, opaque to this core
    std::uint32_t handOccupancy, handObservationMask, heldRefId, equippedFormId;
    std::uint64_t epoch, frame, inputSequence, poseSample, itemIdentity;
    std::uint64_t handStateRevision; // HIGGS observation revision, opaque to this service
};
struct ActionContext { RuntimeRecord slot; RuntimeInput input; };
struct ActionProposal {
    std::uint32_t action, requiredResources;
    std::int32_t priority;
    std::uint32_t reserved;
    std::uint64_t executorAction; // provider's stable action discriminator
};
struct ActionOffer {
    ActionContext context;
    ActionProposal proposal;
    std::uint64_t offerId, behavior, policyRevision;
    std::uint64_t higgsClient, higgsExecutor; // public HIGGS owner label and delegated executor registration
};
struct ActionGrant {
    // grantId correlates the slot request to the public HIGGS selection. Only
    // HIGGS's executor callback provides the bearer used by mutation services.
    std::uint64_t epoch, requestId, grantId;
    std::uint64_t contextId, contextGeneration; // offerId, policyRevision echoed by HIGGS
    std::uint64_t inputSequence;
    std::uint32_t physicalHand, reserved;
};
struct ActionRequest { ActionOffer offer; ActionGrant grant; std::uint64_t requestId; };
enum class CommitStage : std::uint32_t { Accepted = 1, Committed = 2, Completed = 3, Failed = 4, Cancelled = 5 };
struct ContentCommit {
    std::uint32_t structSize, stage;
    std::uint64_t requestId, epoch, higgsRequestId, higgsGrantId;
    std::uint64_t instanceHandle, expectedContentVersion, contentVersion, contentIdentity, quantity;
};
struct ActionProgress {
    ActionGrant grant;
    std::uint64_t requestId, instanceHandle, expectedContentVersion;
    std::uint64_t contentVersion, contentIdentity, quantity;
    std::uint32_t stage, committed;
    std::uint32_t handCommitted, reserved; // irreversible HIGGS evidence, separate from mirrored receipt
};
using OfferCallback = Status (BS_CALL *)(void*, const ActionContext*, ActionProposal*, std::uint32_t, std::uint32_t*);
using ExecuteCallback = Status (BS_CALL *)(void*, const ActionRequest*);
struct BehaviorSpec {
    std::uint32_t structSize, reserved;
    std::uint64_t instanceHandle;
    void* context;
    OfferCallback offers; // immutable eligibility/action description; no inventory mutation
    ExecuteCallback execute; // selected executor only; physical work requires supplied HIGGS grant
    // Register a real provider-owned HIGGS executor and explicitly authorize the
    // Body Slots recognizer before exposing this behavior to the game adapter.
    std::uint64_t higgsClient, higgsExecutor;
};
enum class RuntimeEventKind : std::uint32_t { Enter = 1, Exit = 2, Hover = 3, Invalidated = 4, ContentChanged = 5 };
struct RuntimeEvent {
    std::uint32_t kind, physicalHand;
    std::uint64_t instanceHandle, epoch, frame, runtimeRevision;
};
using RuntimeEventCallback = void (BS_CALL *)(void*, const RuntimeEvent*);
struct RuntimeApi {
    std::uint32_t structSize, version;
    std::uint64_t capabilities; // bit0 geometry, bit1 behavior/commit, bit2 presentation records
    Status (BS_CALL *CopyRuntime)(std::uint64_t reader, RuntimeRecord*, std::uint32_t, RuntimeView*);
    Status (BS_CALL *SetBinding)(std::uint64_t writer, std::uint64_t instanceHandle, const RuntimeBindingSpec*);
    Status (BS_CALL *RegisterBehavior)(std::uint64_t writer, const BehaviorSpec*, std::uint64_t* registration);
    Status (BS_CALL *RemoveBehavior)(std::uint64_t writer, std::uint64_t registration);
    Status (BS_CALL *Subscribe)(std::uint64_t reader, RuntimeEventCallback, void*, std::uint64_t* subscription);
    Status (BS_CALL *Unsubscribe)(std::uint64_t reader, std::uint64_t subscription);
    Status (BS_CALL *ReportContentCommit)(std::uint64_t writer, const ContentCommit*);
    Status (BS_CALL *SetVisual)(std::uint64_t writer, std::uint64_t instanceHandle, const SlotVisualSpec*);
    Status (BS_CALL *CopyActionStatus)(std::uint64_t writer, std::uint64_t requestId, ActionProgress*);
    Status (BS_CALL *CopySavedCustomizations)(std::uint64_t reader, RuntimeCustomizationRecord*, std::uint32_t, InstanceView*);
    Status (BS_CALL *ClearSavedCustomization)(std::uint64_t writer, const InstanceCustomizationKey*,
        std::uint32_t fieldMask, std::uint64_t expectedSaveGeneration, std::uint64_t expectedViewRevision);
};
using QueryRuntimeInterface = const RuntimeApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
static_assert(std::is_trivially_copyable<RuntimeRecord>::value, "Copied runtime ABI only");
static_assert(std::is_trivially_copyable<ActionOffer>::value, "Copied offer ABI only");
} // namespace body_slots
// Export: BodySlots_QueryRuntimeInterface(version, sizeof(RuntimeApi)).
