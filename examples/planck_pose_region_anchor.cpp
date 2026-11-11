#include "planck_pose_region_anchor.h"
#include "planck_anchor_transform.h"
#include <chrono>
#include <limits>
#include <utility>

namespace body_slots::examples {
namespace {
std::uint64_t NowNs() noexcept {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
}

PlanckPoseRegionAnchor::PlanckPoseRegionAnchor(PlanckPoseRegionTarget target, Clock clock)
    : target_(std::move(target)), clock_(clock ? clock : NowNs), openedAtNs_(clock_()) {
    last_.structSize = sizeof(last_);
    last_.kind = static_cast<std::uint32_t>(RegionAnchorKind::Npc);
    last_.source = {"PLANCK","named-physical-body",target_.bodyGeneration,target_.bodyGeneration,0,
        target_.epoch,target_.saveGeneration,0,0,static_cast<std::uint32_t>(RegionUnits::SkyrimWorld),0};
}

Status PlanckPoseRegionAnchor::Sample(const PlanckActorPoseAPI* planck, const RegionsView& scope,
    RegionAnchorSnapshot& out) noexcept {
    auto fail = [&](Status reason) {
        last_.valid = 0;
        out = last_;
        return reason;
    };
    if (!planck || planck->size < sizeof(*planck) || planck->version != 2 || !planck->CopyBodyPose)
        return fail(Status::UnsupportedVersion);
    if (!target_.epoch || !target_.saveGeneration ||
        !target_.actorHandle || target_.boneName.empty() || target_.boneName.find('\0') != std::string::npos ||
        !target_.bodyGeneration || !target_.maxAgeNs)
        return fail(Status::InvalidArgument);

    if (scope.structSize != sizeof(scope) || scope.version != kRegionsApiVersion || scope.reserved ||
        scope.epoch != target_.epoch || scope.saveGeneration != target_.saveGeneration)
        return fail(Status::Unavailable);

    PlanckBodyPose pose{sizeof(pose)};
    const auto copied = planck->CopyBodyPose(target_.actorHandle, target_.boneName.c_str(), &pose);
    if (copied != PLANCK_POSE_OK || pose.size < sizeof(pose) || pose.generation != target_.bodyGeneration)
        return fail(Status::Unavailable);
    const auto now = clock_();
    if (pose.observedAtNs <= openedAtNs_ || pose.observedAtNs > now || now-pose.observedAtNs > target_.maxAgeNs)
        return fail(Status::Unavailable);

    auto anchor = last_;
    anchor.valid = 1;
    const auto& p = pose.positionMetres;
    if (!WorldFromPlanckPose(p[0],p[1],p[2],pose.rotationXyzw,target_.skyrimUnitsPerMetre,anchor.world))
        return fail(Status::InvalidArgument);

    // Region frames must be nonzero and increase. Extend the genuine 32-bit
    // game frame across wrap. Re-reading within a frame cannot renew its age.
    auto frame = std::uint64_t(pose.frame) + 1;
    const auto& prior = last_.source;
    if (prior.frame) {
        if (pose.observedAtNs < prior.sampleId) return fail(Status::Conflict);
        const auto previousRaw = static_cast<std::uint32_t>(prior.frame - 1);
        const std::uint32_t elapsed = pose.frame - previousRaw;
        if (!elapsed) {
            if (!last_.valid) return fail(Status::Conflict);
            if (now-prior.sampleId > target_.maxAgeNs) return fail(Status::Unavailable);
            out = last_;
            return Status::Ok;
        }
        if (elapsed >= (std::uint32_t{1} << 31) || pose.observedAtNs == prior.sampleId ||
            (std::numeric_limits<std::uint64_t>::max)()-prior.frame < elapsed) return fail(Status::Conflict);
        frame = prior.frame + elapsed;
    }
    anchor.source.sampleId = pose.observedAtNs;
    anchor.source.frame = frame;
    anchor.source.timeSeconds = double(pose.observedAtNs)/1e9;
    last_ = anchor;
    out = anchor;
    return Status::Ok;
}
} // namespace body_slots::examples
