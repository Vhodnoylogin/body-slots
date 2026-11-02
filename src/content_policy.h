#pragma once
#include "body_slots_content_policy_api.h"
namespace body_slots::detail {
Status ValidateContentPolicy(const SlotContentPolicy&) noexcept;
// Resolves each requested dependency on the main engine phase. No callback runs
// from a controller-thread action recognizer. Resolver must not mutate inventory.
struct ContentPolicyResolver {
    bool (*IsBaseForm)(void*,std::uint32_t expectedBaseForm);
    bool (*HasKeyword)(void*,std::uint32_t keywordForm);
    bool (*IsInFormList)(void*,std::uint32_t listForm);
    bool (*DependencyAvailable)(void*,std::uint32_t form,std::uint32_t kind); //1 base,2 keyword,3 list
    void* context;
};
bool EvaluateContentPolicy(const SlotContentPolicy&,const ContentPolicyResolver&,bool& known) noexcept;
struct ContentPolicySample {
    std::uint64_t instanceHandle,epoch,handObservationId;
    std::uint32_t physicalHand,baseFormId,category,known,allowed;
    std::uint32_t source=0; //0 legacy primary,1 HIGGS held,2 game-equipped
};
Status PublishContentPolicySamples(const ContentPolicySample*,std::uint32_t,std::uint64_t save,std::uint64_t view) noexcept;
Status SetSavedContentPolicy(std::uint64_t,std::uint64_t,const SlotContentPolicy*,std::uint64_t,std::uint64_t) noexcept;
Status ClearSavedContentPolicy(std::uint64_t,const InstanceCustomizationKey*,std::uint64_t,std::uint64_t) noexcept;
Status CopySavedContentPolicies(std::uint64_t,ContentPolicyRecord*,std::uint32_t,InstanceView*) noexcept;
const ContentPolicyApi& GetContentPolicyApi() noexcept;
} // namespace body_slots::detail
