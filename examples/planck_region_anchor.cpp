#include "planck_region_anchor.h"
#include "planck_anchor_transform.h"
#include <chrono>
#include <cstddef>
#include <limits>

namespace body_slots::examples {
namespace {
constexpr auto requiredSize = offsetof(PlanckSynergyAPI, CopyActorSampleTime) + sizeof(PlanckSynergyAPI::CopyActorSampleTime);
static_assert(requiredSize == 136 && PLANCK_CAP_SAMPLE_TIME == 32);

std::uint64_t NowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

bool SameActor(const PlanckActorRef& a, const PlanckActorRef& b) {
    return a.handle == b.handle && a.actorGeneration == b.actorGeneration && a.epoch == b.epoch;
}

template<class Actor>
bool ReadCaptureTime(const PlanckSynergyAPI& planck, PlanckToken client, const Actor& actor, std::uint64_t& time) {
    if constexpr (requires { actor.observedAtNs; }) {
        if (actor.size >= offsetof(Actor, observedAtNs) + sizeof(actor.observedAtNs)) {
            time = actor.observedAtNs;
            return time != 0;
        }
    }
    // Earlier revision-2 providers return the 72-byte prefix and require this exact-sample lookup.
    return planck.CopyActorSampleTime(client, actor.actor, actor.sample, &time) == PLANCK_OK;
}
}

PlanckRegionAnchor::PlanckRegionAnchor(const RegionActionsApi& actions, PlanckRegionTarget target) noexcept
    : actions_(actions), target_(target), openedAtNs_(NowNs()) {}

Status PlanckRegionAnchor::Withdraw() {
    if (!published_) return Status::Ok;
    RegionAnchorSnapshot invalid{};
    invalid.structSize = sizeof(invalid);
    std::uint64_t unused{};
    const auto status = actions_.PublishAnchor(target_.writer, target_.bindingId, revision_, &invalid, &unused);
    // v1 clears validity at this exact revision, reports InvalidArgument and
    // retains its high-water marks. Propagate ownership or CAS failures.
    if (status != Status::InvalidArgument) return status;
    published_ = false;
    return Status::Ok;
}

Status PlanckRegionAnchor::Update(const PlanckSynergyAPI* planck, std::span<PlanckBodyPhysics> scratch) {
    if (actions_.structSize < sizeof(RegionActionsApi) || actions_.version != kRegionActionsApiVersion ||
        !(actions_.capabilities & 1) || !actions_.CopyIntegration || !actions_.PublishAnchor)
        return Status::UnsupportedVersion;
    auto fail = [&](Status reason) {
        const auto withdrawn = Withdraw();
        return withdrawn == Status::Ok ? reason : withdrawn;
    };
    constexpr auto requiredCaps = PLANCK_CAP_ACTORS | PLANCK_CAP_SAMPLE_TIME;
    if (!planck || planck->size < requiredSize || planck->version != 2 ||
        (planck->capabilities & requiredCaps) != requiredCaps || !planck->CopyActorPhysics || !planck->CopyActorSampleTime)
        return fail(Status::UnsupportedVersion);
    const auto& target = target_.body;
    if (!target_.planckClient || !target_.writer || !target_.bindingId || !target_.higgsEpoch || !target_.saveGeneration ||
        !target.actor.handle || !target.actor.epoch || !target.actor.actorGeneration || !target.bodyGeneration ||
        !target_.maxAgeNs || scratch.empty() || scratch.size() > (std::numeric_limits<std::uint32_t>::max)())
        return fail(Status::InvalidArgument);

    RegionActionsIntegration scope{sizeof(scope), kRegionActionsApiVersion};
    if (actions_.CopyIntegration(target_.writer, &scope) != Status::Ok || !scope.connected ||
        scope.epoch != target_.higgsEpoch || scope.saveGeneration != target_.saveGeneration)
        return fail(Status::Unavailable);

    for (auto& body : scratch) body.size = sizeof(body);
    PlanckActorPhysics actor{};
    actor.size = sizeof(actor);
    std::uint32_t count{};
    const auto copied = planck->CopyActorPhysics(target_.planckClient, target.actor.handle, &actor,
        scratch.data(), static_cast<std::uint32_t>(scratch.size()), &count);
    if (copied == PLANCK_BUFFER) return fail(Status::BufferTooSmall);
    if (copied != PLANCK_OK || count > scratch.size() || count != actor.bodyCount || target.index >= count ||
        actor.size < offsetof(PlanckActorPhysics, ragdolled) + sizeof(actor.ragdolled) || !SameActor(actor.actor, target.actor) || !actor.sample || !actor.mainFrame)
        return fail(Status::Unavailable);
    const auto& body = scratch[target.index];
    if (body.size != sizeof(body) || !SameActor(body.body.actor, target.actor) || body.body.index != target.index ||
        body.body.bodyGeneration != target.bodyGeneration || body.sample != actor.sample)
        return fail(Status::Unavailable);

    std::uint64_t time{};
    if (!ReadCaptureTime(*planck, target_.planckClient, actor, time))
        return fail(Status::Unavailable);
    const auto now = NowNs();
    if (time <= openedAtNs_ || time > now || now-time > target_.maxAgeNs)
        return fail(Status::Unavailable);
    if (actor.sample == sample_ && actor.mainFrame == frame_ && time == time_ && published_) return Status::Ok;
    if (actor.sample <= sample_ || actor.mainFrame <= frame_ || time < time_) return fail(Status::Conflict);

    RegionAnchorSnapshot anchor{};
    anchor.structSize = sizeof(anchor);
    anchor.kind = static_cast<std::uint32_t>(RegionAnchorKind::Npc);
    anchor.valid = 1;
    if (!WorldFromPlanckPose(body.worldPosition.x, body.worldPosition.y, body.worldPosition.z,
                             body.worldRotation, target_.skyrimUnitsPerMetre, anchor.world)) return fail(Status::InvalidArgument);
    anchor.source = {"PLANCK", "actor-body", (std::uint64_t(target.actor.handle) << 32) | target.index,
        target.bodyGeneration, actor.sample, scope.epoch, scope.saveGeneration, actor.mainFrame,
        double(time) / 1e9, static_cast<std::uint32_t>(RegionUnits::SkyrimWorld), 0};
    std::uint64_t next{};
    const auto status = actions_.PublishAnchor(target_.writer, target_.bindingId, revision_, &anchor, &next);
    if (status != Status::Ok) return fail(status);
    revision_ = next;
    sample_ = actor.sample;
    frame_ = actor.mainFrame;
    time_ = time;
    published_ = true;
    return Status::Ok;
}
} // namespace body_slots::examples
