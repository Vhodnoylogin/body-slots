#include "planck_region_anchor.h"
#include "body_slots_planck_anchor.h"
#include <chrono>

namespace body_slots::examples {
namespace {
std::uint64_t NowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
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
    if (!planck || !detail::SupportsPlanckAnchors(*planck))
        return fail(Status::UnsupportedVersion);
    if (!target_.writer || !target_.bindingId || !target_.maxAgeNs)
        return fail(Status::InvalidArgument);

    RegionActionsIntegration scope{sizeof(scope), kRegionActionsApiVersion};
    if (actions_.CopyIntegration(target_.writer, &scope) != Status::Ok || !scope.connected ||
        scope.epoch != target_.higgsEpoch || scope.saveGeneration != target_.saveGeneration)
        return fail(Status::Unavailable);

    const detail::PlanckAnchorScope source{target_.body, scope.epoch, scope.saveGeneration, openedAtNs_};
    RegionAnchorSnapshot anchor{};
    std::uint64_t time{};
    const auto copied = detail::ReadPlanckAnchor(*planck, target_.planckClient, source, target_.skyrimUnitsPerMetre,
                                                scratch, anchor, &time);
    if (copied != Status::Ok)
        return fail(copied);
    const auto now = NowNs();
    if (time > now || now-time > target_.maxAgeNs)
        return fail(Status::Unavailable);
    const auto& sample = anchor.source;
    if (sample.sampleId == sample_ && sample.frame == frame_ && time == time_ && published_) return Status::Ok;
    if (sample.sampleId <= sample_ || sample.frame <= frame_ || time < time_) return fail(Status::Conflict);
    std::uint64_t next{};
    const auto status = actions_.PublishAnchor(target_.writer, target_.bindingId, revision_, &anchor, &next);
    if (status != Status::Ok) return fail(status);
    revision_ = next;
    sample_ = sample.sampleId;
    frame_ = sample.frame;
    time_ = time;
    published_ = true;
    return Status::Ok;
}
} // namespace body_slots::examples
