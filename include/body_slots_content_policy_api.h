#pragma once
#include "body_slots_runtime_api.h"
namespace body_slots {
constexpr std::uint32_t kContentPolicyApiVersion=1,kMaxPolicyForms=16;
// This refines the Definition1 category mask for incoming items only. Empty-hand
// withdrawal is not an attempt to insert an item. Forms/form-lists are alternatives;
// all keywords are required, and any-keywords require at least one match.
struct SlotContentPolicy {
    std::uint32_t structSize,baseFormCount,allKeywordCount,anyKeywordCount,formListCount,reserved;
    std::uint32_t baseForms[kMaxPolicyForms],allKeywords[kMaxPolicyForms],anyKeywords[kMaxPolicyForms],formLists[kMaxPolicyForms];
    std::uint32_t unresolved,reserved2; // unresolved saved dependency refuses incoming actions
};
struct ContentPolicyRecord {
    InstanceCustomizationKey key;
    std::uint64_t instanceHandle; //0 for a dormant saved key
    SlotContentPolicy policy;
};
struct ContentEligibility {
    std::uint32_t structSize,physicalHand,known,allowed;
    std::uint64_t instanceHandle,epoch,handObservationId;
    std::uint32_t baseFormId,category;
};
struct ContentPolicyApi {
    std::uint32_t structSize,version;
    std::uint64_t capabilities; // bit0 saved filters, bit1 main-phase engine evidence
    Status (BS_CALL *SetPolicy)(std::uint64_t editor,std::uint64_t instanceHandle,const SlotContentPolicy*,std::uint64_t saveGeneration,std::uint64_t viewRevision);

    Status (BS_CALL *ClearPolicy)(std::uint64_t editor,const InstanceCustomizationKey*,std::uint64_t saveGeneration,std::uint64_t viewRevision);

    Status (BS_CALL *CopyPolicies)(std::uint64_t reader,ContentPolicyRecord*,std::uint32_t capacity,InstanceView*);

    Status (BS_CALL *CopyEligibility)(std::uint64_t reader,std::uint64_t instanceHandle,std::uint32_t physicalHand,std::uint64_t epoch,std::uint64_t handObservationId,ContentEligibility*);
};
using QueryContentPolicyInterface=const ContentPolicyApi* (BS_CALL *)(std::uint32_t,std::uint32_t);
static_assert(sizeof(SlotContentPolicy)==288,"SlotContentPolicy ABI changed");
static_assert(sizeof(ContentPolicyApi)==48,"ContentPolicyApi ABI changed (x64)");
} // namespace body_slots
// Export: BodySlots_QueryContentPolicyInterface(version,sizeof(ContentPolicyApi)).
