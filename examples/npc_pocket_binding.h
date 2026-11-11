#pragma once
#include "body_slots_region_actions_api.h"
#include "body_slots_region_continuation_api.h"
#include "body_slots_region_sources_api.h"

namespace body_slots::examples {
// The mod first registers its own NPC-shaped logical region and HIGGS client.
// Keep this object alive through a successful Detach, including an unsuccessful
// Attach if the registration still needs to drain.
class NpcPocketBinding {
  public:
    using ShowPocket = Status (*)(void*, const RegionActionExecution&);
    using EndPreview = void (*)(void*, const RegionInputTerminal&);

    Status Attach(const RegionActionsApi&, const RegionSourcesApi&, const RegionContinuationApi&,
                  std::uint64_t writer, HIGGS_HandClient, const InteractionRegionRecord&, const RegionPlanckBody&,
                  ShowPocket, EndPreview, void* context);

    Status Detach();

    NpcPocketBinding() = default;
    NpcPocketBinding(const NpcPocketBinding&) = delete;
    NpcPocketBinding& operator=(const NpcPocketBinding&) = delete;

  private:
    static Status BS_CALL Offer(void*, const RegionActionContext*, RegionActionProposal*, std::uint32_t,
                                std::uint32_t*);
    static Status BS_CALL Open(void*, const RegionActionExecution*);
    static void BS_CALL End(void*, const RegionInputTerminal*);
    const RegionActionsApi* actions_{};
    RegionActionRegistration handle_{sizeof(handle_)};
    std::uint64_t writer_{};
    void* context_{};
    ShowPocket open_{};
    EndPreview end_{};
};
} // namespace body_slots::examples
