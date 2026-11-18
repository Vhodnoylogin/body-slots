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
    std::uint64_t higgsClient,higgsExecutor; // public HIGGS owner label and executor registration
};
struct BehaviorApi {
    std::uint32_t structSize,version;
    std::uint64_t capabilities; // bit0 source-specific immutable action offers
    // Remove using RuntimeApi1.RemoveBehavior, with the same callback drainage.
    Status (BS_CALL *RegisterSourceBehavior)(std::uint64_t writer,const SourceBehaviorSpec*,std::uint64_t* registration);
};
// Additive table. The v1 table and all existing offer/context layouts stay exact.
// Duration is an upper bound for the selected HIGGS grant, not an input hold or
// permission to keep a stale source. Consumers must still revalidate before work.
constexpr std::uint32_t kBehaviorHeldReleaseInput=1;
constexpr std::uint32_t kBehaviorDeferredExit=2;
struct SourceBehaviorOptions {
    std::uint32_t structSize,grantDurationMs; // 1..60000; v1 registrations use 2000
    // DeferredExit allows empty-hand Down dispatch just outside the original
    // source; executor must still validate the same held sequence before work.
    std::uint32_t inputFlags,reserved; // kBehaviorHeldReleaseInput | kBehaviorDeferredExit
};
struct BehaviorApi2 {
    BehaviorApi base; // exact v1 prefix, with version=2 and structSize=sizeof(BehaviorApi2)
    Status (BS_CALL *RegisterSourceBehaviorWithOptions)(std::uint64_t writer,
        const SourceBehaviorSpec*,const SourceBehaviorOptions*,std::uint64_t* registration);
    // Uncommitted deferred work only: same behavior/policy/save/content/body/anchor,
    // active equipment binding and available geometry. The hand may be outside.
    Status (BS_CALL *CopyDeferredSource)(std::uint64_t writer,std::uint64_t requestId,RuntimeRecord*);
};
using QueryBehaviorInterface=const BehaviorApi* (BS_CALL *)(std::uint32_t,std::uint32_t);
static_assert(sizeof(BehaviorApi)==24,"BehaviorApi ABI changed (x64)");
static_assert(sizeof(BehaviorApi2)==40,"BehaviorApi2 ABI changed (x64)");
static_assert(std::is_trivially_copyable<SourceActionContext>::value,"Copied ABI only");
} // namespace body_slots
// Export: BodySlots_QueryBehaviorInterface(version,sizeof(BehaviorApi)).
