#include "planck_region_anchor.h"
#include <chrono>
#include <cmath>
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

bool WorldTransform(const PlanckBodyPhysics& body, float units, RuntimeTransform& out) {
    if (!std::isfinite(units) || units <= 0) return false;
    const auto& p = body.worldPosition;
    const auto& q = body.worldRotation;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double norm = x*x + y*y + z*z + w*w;
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 0.002) return false;
    const double k = 2.0 / norm;
    // PLANCK supplies worldFromBody as xyzw. Body Slots multiplies a
    // column vector by this row-major matrix, in the same world axes.
    out = {{p.x * units, p.y * units, p.z * units},
           {float(1-k*(y*y+z*z)), float(k*(x*y-w*z)), float(k*(x*z+w*y)),
            float(k*(x*y+w*z)), float(1-k*(x*x+z*z)), float(k*(y*z-w*x)),
            float(k*(x*z-w*y)), float(k*(y*z+w*x)), float(1-k*(x*x+y*y))}, 1};
    return std::isfinite(out.position[0]) && std::isfinite(out.position[1]) && std::isfinite(out.position[2]);
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
        actor.size != sizeof(actor) || !SameActor(actor.actor, target.actor) || !actor.sample || !actor.mainFrame)
        return fail(Status::Unavailable);
    const auto& body = scratch[target.index];
    if (body.size != sizeof(body) || !SameActor(body.body.actor, target.actor) || body.body.index != target.index ||
        body.body.bodyGeneration != target.bodyGeneration || body.sample != actor.sample)
        return fail(Status::Unavailable);

    std::uint64_t time{};
    if (planck->CopyActorSampleTime(target_.planckClient, actor.actor, actor.sample, &time) != PLANCK_OK)
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
    if (!WorldTransform(body, target_.skyrimUnitsPerMetre, anchor.world)) return fail(Status::InvalidArgument);
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
