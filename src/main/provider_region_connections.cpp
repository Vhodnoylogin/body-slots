#include "owned_region_sources.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <array>
#include <thread>
#if defined(BODY_SLOTS_WITH_PPB_SOURCE)
#include "ppb_digest.h"
#include <cmath>
#endif
#if defined(BODY_SLOTS_WITH_PLANCK_SOURCE)
#include "planck_anchor_reader.h"
#endif

namespace body_slots::game {
#if defined(BODY_SLOTS_WITH_PLANCK_SOURCE)
namespace {
struct PlanckConnection {
    const PlanckSynergyAPI* api{};
    PlanckToken client{};
};
PlanckConnection planck;
Status ReadBody(const detail::OwnedRegionSource& target, RegionAnchorSnapshot& anchor) noexcept {
    // PLANCK copies while holding its own short-lived mutex. This data is
    // consumed after the copy returns; no provider object escapes this phase.
    static thread_local std::array<PlanckBodyPhysics, 256> bodies{};
    return detail::ReadPlanckAnchor(*planck.api, planck.client, target, RE::bhkWorld::GetWorldScaleInverse(),
                                    bodies, anchor);
}
} // namespace
#endif
#if defined(BODY_SLOTS_WITH_PLANCK_SOURCE)
void ConnectPlanck() noexcept {
    if (planck.client)
        return;
    const auto* bus = SKSE::GetMessagingInterface();
    struct Request {
        void* (*getApi)(unsigned int){};
    } request;
    if (!bus || !bus->Dispatch(0x92F38745, &request, sizeof(request), "PLANCK_SKSE") || !request.getApi)
        return;
    const auto* api = static_cast<const PlanckSynergyAPI*>(request.getApi(2));
    if (!api || !detail::SupportsPlanckAnchors(*api) || !api->RegisterClient || !api->ReleaseClient)
        return;
    const PlanckClientIdentity identity{sizeof(identity), 0, "BodySlots.RegionAnchors"};
    PlanckToken client{};
    if (api->RegisterClient(&identity, &client) != PLANCK_OK || !client)
        return;
    planck = {api, client};
    detail::SetOwnedPlanckReader(ReadBody);
}
#endif

#if defined(BODY_SLOTS_WITH_PPB_SOURCE)
namespace {
PPBAPI::IPpbObservationInterface2* ppb{};
std::thread::id mainThread;
Status ReadDigest(const PpbRegionQuery& query, PpbRegionEvidence& output) noexcept {
    if (std::this_thread::get_id() != mainThread)
        return Status::Unavailable;
    std::array<PPBAPI::PpbTouchContact, 64> contacts{};
    PPBAPI::PpbTouchFrame frame{};
    if (!ppb->CopyTouchFrame(false, contacts.data(), static_cast<int>(contacts.size()), &frame) || !frame.epoch ||
        !frame.sequence || frame.copied > contacts.size() || frame.copied > frame.available)
        return Status::Unavailable;
    return detail::SelectPpbDigest(query, frame, contacts, output);
}
void ConnectPpb() noexcept {
    if (ppb)
        return;
    const auto* bus = SKSE::GetMessagingInterface();
    PPBAPI::PpbMessage request{};
    if (!bus || !bus->Dispatch(request.kGetObservationInterface, &request, sizeof(request), "PPB") ||
        !request.GetApiFunction)
        return;
    auto* api = static_cast<PPBAPI::IPpbObservationInterface2*>(request.GetApiFunction(2));
    if (!api || !(api->GetCapabilities() & PPBAPI::kObservationGroupedFrames))
        return;
    mainThread = std::this_thread::get_id();
    ppb = api;
    detail::SetPpbDigestReader(ReadDigest);
}
} // namespace
#endif
void ConnectRegionProviders() noexcept {
#if defined(BODY_SLOTS_WITH_PLANCK_SOURCE)
    ConnectPlanck();
#endif
#if defined(BODY_SLOTS_WITH_PPB_SOURCE)
    ConnectPpb();
#endif
}
} // namespace body_slots::game
