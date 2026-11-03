#pragma once
#include "body_slots_region_actions_api.h"
#include "higgshandrouting.h"
#include "higgsauthorizeddispatch.h"

namespace body_slots::detail {
const RegionActionsApi& GetRegionActionsApi() noexcept;
Status ConnectRegionActions(const HIGGS_HandInterface&, const HIGGS_HandRoutingInterface&,
    const HIGGS_AuthorizedDispatchInterface&) noexcept;
// Called after a real HIGGS published hand phase; no VRIK/native-mouth dependency.
void TickRegionActions() noexcept;
void ResetRegionActions() noexcept;
Status DisconnectRegionActions() noexcept;
// Registry release hook; revokes every binding and drains callback contexts.
Status DrainRegionActionsClient(std::uint64_t writer) noexcept;
} // namespace body_slots::detail
