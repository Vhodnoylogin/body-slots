#pragma once
#include "body_slots_runtime_api.h"
namespace body_slots {
constexpr std::uint32_t kBehaviorApiVersion=1;
enum class IncomingItemSource : std::uint32_t { None=0,HiggsHeld=1,GameEquipped=2 };
struct IncomingItemContext {
    std::uint32_t source,known,allowed,category;
    std::uint32_t referenceId,baseFormId,equippedFormId,reserved;
    std::uint64_t epoch,handObservationId;
};
struct SourceActionContext {
    ActionContext context;
    IncomingItemContext items[2]; // HIGGS-held then game-equipped, never conflated
};
struct SourceActionProposal {
    ActionProposal proposal;
    std::uint32_t incomingSource,reserved; // None is a withdrawal/other action, no insertion filter
};
struct SourceActionRequest {
    ActionRequest request;
    IncomingItemContext incoming;
};
using SourceOfferCallback=Status (BS_CALL *)(void*,const SourceActionContext*,SourceActionProposal*,std::uint32_t,std::uint32_t*);
using SourceExecuteCallback=Status (BS_CALL *)(void*,const SourceActionRequest*);
struct SourceBehaviorSpec {
    std::uint32_t structSize,reserved;
    std::uint64_t instanceHandle;
    void* context;
    SourceOfferCallback offers;
    SourceExecuteCallback execute;
    std::uint64_t higgsClient,higgsExecutor;
};
struct BehaviorApi {
    std::uint32_t structSize,version;
    std::uint64_t capabilities; // bit0 source-specific immutable action offers
    // Remove using RuntimeApi1.RemoveBehavior, with the same callback drainage.
    Status (BS_CALL *RegisterSourceBehavior)(std::uint64_t writer,const SourceBehaviorSpec*,std::uint64_t* registration);
};
using QueryBehaviorInterface=const BehaviorApi* (BS_CALL *)(std::uint32_t,std::uint32_t);
static_assert(sizeof(BehaviorApi)==24,"BehaviorApi ABI changed (x64)");
static_assert(std::is_trivially_copyable<SourceActionContext>::value,"Copied ABI only");
} // namespace body_slots
// Export: BodySlots_QueryBehaviorInterface(version,sizeof(BehaviorApi)).
