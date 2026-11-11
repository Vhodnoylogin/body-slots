#include "planck_anchor_reader.h"
#include "body_slots_planck_transform.h"
#include <cstddef>
#include <limits>

namespace body_slots::detail {
bool SupportsPlanckAnchors(const PlanckSynergyAPI& api) noexcept {
    constexpr auto prefix = offsetof(PlanckSynergyAPI, CopyActorSampleTime) + sizeof(api.CopyActorSampleTime);
    constexpr auto required = PLANCK_CAP_ACTORS | PLANCK_CAP_SAMPLE_TIME;
    return api.version == 2 && api.size >= prefix && (api.capabilities & required) == required &&
           api.CopyActorPhysics && api.CopyActorSampleTime;
}
Status ReadPlanckAnchor(const PlanckSynergyAPI& api, PlanckToken client, const OwnedRegionSource& source,
                        float units, std::span<PlanckBodyPhysics> scratch, RegionAnchorSnapshot& output) noexcept {
    const auto& target = source.body;
    if (!SupportsPlanckAnchors(api))
        return Status::UnsupportedVersion;
    if (!client || scratch.empty() || scratch.size() > std::numeric_limits<std::uint32_t>::max())
        return Status::InvalidArgument;
    const auto matches = [&](const PlanckActorRef& ref) {
        return ref.handle == target.actorHandle && ref.actorGeneration == target.actorGeneration &&
               ref.epoch == target.worldEpoch;
    };
    for (auto& body : scratch)
        body.size = sizeof(body);
    PlanckActorPhysics actor{};
    actor.size = sizeof(actor);
    std::uint32_t count{};
    const auto status = api.CopyActorPhysics(client, target.actorHandle, &actor, scratch.data(),
                                             static_cast<std::uint32_t>(scratch.size()), &count);
    if (status == PLANCK_BUFFER)
        return Status::BufferTooSmall;
    if (status != PLANCK_OK || count != actor.bodyCount || count > scratch.size() || target.bodyIndex >= count ||
        actor.size < offsetof(PlanckActorPhysics, observedAtNs) || !matches(actor.actor) || !actor.sample ||
        !actor.mainFrame || actor.phase != PLANCK_PHASE_MAIN)
        return Status::Unavailable;
    const auto& body = scratch[target.bodyIndex];
    if (body.size != sizeof(body) || !matches(body.body.actor) || body.body.index != target.bodyIndex ||
        body.body.bodyGeneration != target.bodyGeneration || body.sample != actor.sample)
        return Status::Unavailable;
    std::uint64_t captured{};
    if (actor.size >= sizeof(actor))
        captured = actor.observedAtNs;
    else if (api.CopyActorSampleTime(client, actor.actor, actor.sample, &captured) != PLANCK_OK)
        return Status::Unavailable;
    if (captured <= source.openedAtNs)
        return Status::Unavailable;
    RegionAnchorSnapshot anchor{};
    anchor.structSize = sizeof(anchor);
    anchor.kind = static_cast<std::uint32_t>(RegionAnchorKind::Npc);
    if (!WorldFromPlanckPose(body.worldPosition.x, body.worldPosition.y, body.worldPosition.z, body.worldRotation,
                             units, anchor.world))
        return Status::InvalidArgument;
    anchor.valid = 1;
    anchor.source = {"PLANCK",
                     "actor-body",
                     (std::uint64_t(target.actorHandle) << 32) | target.bodyIndex,
                     target.bodyGeneration,
                     actor.sample,
                     source.higgsEpoch,
                     source.saveGeneration,
                     actor.mainFrame,
                     double(captured) / 1e9,
                     static_cast<std::uint32_t>(RegionUnits::SkyrimWorld),
                     0};
    output = anchor;
    return Status::Ok;
}
} // namespace body_slots::detail
