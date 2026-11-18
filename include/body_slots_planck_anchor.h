#pragma once
#include "body_slots_planck_transform.h"
#include "body_slots_regions_api.h"
#include "planck_synergy.h"
#include <cstddef>
#include <limits>
#include <span>

namespace body_slots::detail {
struct PlanckAnchorScope {
    PlanckBodyRef body;
    std::uint64_t higgsEpoch, saveGeneration, openedAtNs;
};

inline bool SupportsPlanckAnchors(const PlanckSynergyAPI& api) noexcept {
    constexpr auto prefix = offsetof(PlanckSynergyAPI, CopyActorSampleTime) + sizeof(api.CopyActorSampleTime);
    constexpr auto required = PLANCK_CAP_ACTORS | PLANCK_CAP_SAMPLE_TIME;
    return api.version == 2 && api.size >= prefix && (api.capabilities & required) == required &&
           api.CopyActorPhysics && api.CopyActorSampleTime;
}

template<class Actor>
inline std::uint64_t PlanckCaptureTime(const PlanckSynergyAPI& api, PlanckToken client, const Actor& actor) noexcept {
    if constexpr (requires { actor.observedAtNs; }) {
        if (actor.size >= offsetof(Actor, observedAtNs) + sizeof(actor.observedAtNs))
            return actor.observedAtNs;
    }
    // Older revision-2 providers return a shorter prefix. Request the clock of this exact sample.
    std::uint64_t captured{};
    return api.CopyActorSampleTime(client, actor.actor, actor.sample, &captured) == PLANCK_OK ? captured : 0;
}

inline Status ReadPlanckAnchor(const PlanckSynergyAPI& api, PlanckToken client, const PlanckAnchorScope& source,
                               float units, std::span<PlanckBodyPhysics> scratch, RegionAnchorSnapshot& output,
                               std::uint64_t* captureTimeNs = nullptr) noexcept {
    if (!SupportsPlanckAnchors(api))
        return Status::UnsupportedVersion;
    const auto& target = source.body;
    if (!client || !target.actor.handle || !target.actor.epoch || !target.actor.actorGeneration ||
        !target.bodyGeneration || !source.higgsEpoch || !source.saveGeneration || scratch.empty() ||
        scratch.size() > (std::numeric_limits<std::uint32_t>::max)())
        return Status::InvalidArgument;
    const auto matches = [&](const PlanckActorRef& ref) {
        return ref.handle == target.actor.handle && ref.actorGeneration == target.actor.actorGeneration &&
               ref.epoch == target.actor.epoch;
    };
    for (auto& body : scratch)
        body.size = sizeof(body);
    PlanckActorPhysics actor{};
    actor.size = sizeof(actor);
    std::uint32_t count{};
    const auto status = api.CopyActorPhysics(client, target.actor.handle, &actor, scratch.data(),
                                             static_cast<std::uint32_t>(scratch.size()), &count);
    if (status == PLANCK_BUFFER)
        return Status::BufferTooSmall;
    if (status != PLANCK_OK || count != actor.bodyCount || count > scratch.size() || target.index >= count ||
        actor.size < offsetof(PlanckActorPhysics, ragdolled) + sizeof(actor.ragdolled) || !matches(actor.actor) ||
        !actor.sample || !actor.mainFrame || actor.phase != PLANCK_PHASE_MAIN)
        return Status::Unavailable;
    const auto& body = scratch[target.index];
    if (body.size != sizeof(body) || !matches(body.body.actor) || body.body.index != target.index ||
        body.body.bodyGeneration != target.bodyGeneration || body.sample != actor.sample)
        return Status::Unavailable;
    const auto captured = PlanckCaptureTime(api, client, actor);
    if (captured <= source.openedAtNs)
        return Status::Unavailable;
    RegionAnchorSnapshot anchor{};
    anchor.structSize = sizeof(anchor);
    anchor.kind = static_cast<std::uint32_t>(RegionAnchorKind::Npc);
    if (!WorldFromPlanckPose(body.worldPosition.x, body.worldPosition.y, body.worldPosition.z, body.worldRotation,
                             units, anchor.world))
        return Status::InvalidArgument;
    anchor.valid = 1;
    anchor.source = {"PLANCK", "actor-body", (std::uint64_t(target.actor.handle) << 32) | target.index,
                     target.bodyGeneration, actor.sample, source.higgsEpoch, source.saveGeneration, actor.mainFrame,
                     double(captured) / 1e9, static_cast<std::uint32_t>(RegionUnits::SkyrimWorld), 0};
    output = anchor;
    if (captureTimeNs)
        *captureTimeNs = captured;
    return Status::Ok;
}
} // namespace body_slots::detail
