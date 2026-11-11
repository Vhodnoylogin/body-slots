// Optional runtime discovery only. HIGGS supplies the safe update boundary and
// input authority; no VRIK slot implementation or controller hooks are imported.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <SKSE/SKSE.h>
#include <Windows.h>
#include "region_actions.h"
#include "higgsinterface001.h"
#include <mutex>

namespace body_slots::game {
namespace {
void OnRegionActionsFrame() noexcept { detail::TickRegionActions(); }
}
void ConnectHiggsRegionActions(void* handle) noexcept {
    try {
        if (!handle) return;
        static std::mutex connectionMutex;
        static bool connected = false;
        std::lock_guard lock(connectionMutex);
        if (connected) return;
        const auto module = static_cast<HMODULE>(handle);
        const auto queryHands = reinterpret_cast<HIGGS_QueryHandInterfaceFn>(GetProcAddress(module, "HIGGS_QueryHandInterface"));
        const auto queryRouting = reinterpret_cast<HIGGS_QueryHandRoutingInterfaceFn>(GetProcAddress(module, "HIGGS_QueryHandRoutingInterface"));
        const auto queryDispatch = reinterpret_cast<HIGGS_QueryAuthorizedDispatchInterfaceFn>(GetProcAddress(module, "HIGGS_QueryAuthorizedDispatchInterface"));
        HIGGS_HandInterface hands{};
        HIGGS_HandRoutingInterface routing{};
        HIGGS_AuthorizedDispatchInterface dispatch{};
        if (!queryHands || !queryRouting || !queryDispatch ||
            queryHands(HIGGS_HAND_ABI_VERSION, sizeof(hands), &hands) != HIGGS_HAND_OK ||
            queryRouting(HIGGS_HAND_ROUTING_ABI_VERSION, sizeof(routing), &routing) != HIGGS_HAND_OK ||
            queryDispatch(HIGGS_AUTHORIZED_DISPATCH_ABI_VERSION, sizeof(dispatch), &dispatch) != HIGGS_HAND_OK) return;
        struct PhaseRequest { void* (*getInterface)(unsigned int) = nullptr; } phase;
        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging || !messaging->Dispatch(0xF9279A57, &phase, sizeof(phase), "HIGGS")) return;
        auto* phases = phase.getInterface ? static_cast<HiggsPluginAPI::IHiggsInterface001*>(phase.getInterface(1)) : nullptr;
        HIGGS_InputRoutesInterface routes{};
        const auto queryRoutes = reinterpret_cast<HIGGS_QueryInputRoutesInterfaceFn>(GetProcAddress(module, "HIGGS_QueryInputRoutesInterface"));
        const bool hasRoutes = queryRoutes && queryRoutes(1, sizeof(routes), &routes) == HIGGS_HAND_OK;
        if (!phases || detail::ConnectRegionActions(hands, routing, dispatch, hasRoutes ? &routes : nullptr) != Status::Ok) return;
        phases->AddPostVrikPostHiggsCallback(&OnRegionActionsFrame);
        connected = true;
    } catch (...) {}
}
void ResetHiggsRegionActions() noexcept { detail::ResetRegionActions(); }
} // namespace body_slots::game
