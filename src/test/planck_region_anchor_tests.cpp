// Production PLANCK registry and capture-time lookup with synthetic body poses.
// The recording Body Slots sink implements its writer/revision/withdraw protocol.
#include "planck_region_anchor.h"
#include "main/planck_anchor_reader.h"
#include "synergy_runtime.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace body_slots;
using namespace body_slots::examples;
namespace {
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::abort(); } } while (0)

RegionActionsIntegration scope{sizeof(scope), 1, 1, 0, 3, 93, 55, 1};
RegionAnchorSnapshot seen{};
std::uint64_t revision{};
Status BS_CALL CopyScope(std::uint64_t writer, RegionActionsIntegration* out) {
    CHECK(writer == 4);
    *out = scope;
    return Status::Ok;
}
Status BS_CALL Publish(std::uint64_t writer, std::uint64_t id, std::uint64_t expected,
                       const RegionAnchorSnapshot* in, std::uint64_t* next) {
    CHECK(writer == 4 && id == 5 && expected == revision);
    seen = *in;
    if (!in->valid) return Status::InvalidArgument;
    *next = ++revision;
    return Status::Ok;
}
PlanckStatus PLANCK_CALL StaleTime(PlanckToken, PlanckActorRef, std::uint64_t, std::uint64_t*) {
    return PLANCK_STALE;
}
PlanckStatus PLANCK_CALL CopyOriginalPrefix(PlanckToken client, std::uint32_t handle, PlanckActorPhysics* actor,
    PlanckBodyPhysics* bodies, std::uint32_t capacity, std::uint32_t* count) {
    actor->size = 72;
    return PlanckSynergy::GetAPI()->CopyActorPhysics(client, handle, actor, bodies, capacity, count);
}
}

int main() {
    auto* planck = PlanckSynergy::GetAPI();
    RegionActionsApi actions{sizeof(actions), 1, 1, nullptr, nullptr, Publish, CopyScope, nullptr};
    PlanckClientIdentity id{sizeof(id), 0, "PlanckRegionAnchorTest"};
    PlanckToken client{};
    CHECK(planck->RegisterClient(&id, &client) == PLANCK_OK);

    std::vector<PlanckSynergy::BodySample> bodies(1);
    auto& body = bodies[0];
    body.identity = 11; body.lineage = 12;
    body.state.worldPosition = {1,2,-3};
    body.state.worldRotation[2] = body.state.worldRotation[3] = float(std::sqrt(0.5));
    auto capture = [&](std::uint64_t frame, std::uint64_t ns) {
        PlanckSynergy::PublishActor(101, 102, false, frame, 1, 9.25, bodies, nullptr, ns);
    };
    capture(1, PlanckSynergy::MonotonicNowNs());
    PlanckActorPhysics actor{}; actor.size = sizeof(actor);
    std::array<PlanckBodyPhysics,1> scratch{}; scratch[0].size = sizeof(scratch[0]);
    std::uint32_t count{};
    CHECK(planck->CopyActorPhysics(client, 101, &actor, scratch.data(), 1, &count) == PLANCK_OK && count == 1);
    CHECK(scope.epoch != actor.actor.epoch);
    PlanckRegionTarget target{4, 5, scope.epoch, scope.saveGeneration, client, scratch[0].body, 70.f, 1000000000};
    PlanckRegionAnchor anchor(actions, target);
    auto update = [&]{ return anchor.Update(planck, scratch); };
    CHECK(update() == Status::Unavailable); // Capture predates this cross-provider binding.
    const auto timestamp = PlanckSynergy::MonotonicNowNs();
    capture(2, timestamp);
    CHECK(update() == Status::Ok && seen.valid && revision == 1);
    const detail::OwnedRegionSource owned{{sizeof(RegionPlanckBody),target.body.actor.handle,target.body.index,0,
        target.body.actor.epoch,target.body.actor.actorGeneration,target.body.bodyGeneration},scope.epoch,scope.saveGeneration,timestamp-1};
    RegionAnchorSnapshot real{};
    CHECK(detail::ReadPlanckAnchor(*planck,client,owned,70,scratch,real)==Status::Ok);
    CHECK(real.world.position[0]==70 && real.source.sampleId==seen.source.sampleId && real.source.timeSeconds==double(timestamp)/1e9);
    CHECK(seen.source.epoch == 93 && seen.source.saveGeneration == 55 && seen.source.frame == 2);
    CHECK(seen.source.timeSeconds == double(timestamp)/1e9 && seen.source.sampleId == scratch[0].sample);
    CHECK(seen.source.generation == target.body.bodyGeneration && seen.source.identity == (std::uint64_t(101) << 32));
    CHECK(seen.world.position[0] == 70 && seen.world.position[1] == 140 && seen.world.position[2] == -210);
    CHECK(std::abs(seen.world.rotation[0]) < 1e-6 && std::abs(seen.world.rotation[3]-1) < 1e-6);
    CHECK(std::abs(seen.world.rotation[1]+1) < 1e-6 && seen.world.rotation[8] == 1 && seen.world.scale == 1);
    CHECK(update() == Status::Ok && revision == 1); // Re-reading doesn't restamp or advance.

    PlanckSynergyAPI stale = *planck; stale.CopyActorSampleTime = StaleTime;
    if (sizeof(PlanckActorPhysics) > 72) {
        CHECK(anchor.Update(&stale, scratch) == Status::Ok && seen.valid && revision == 1);
    }
    stale.CopyActorPhysics = CopyOriginalPrefix;
    auto legacy = *planck; legacy.CopyActorPhysics = CopyOriginalPrefix;
    CHECK(anchor.Update(&legacy, scratch) == Status::Ok && seen.valid && revision == 1);
    CHECK(detail::ReadPlanckAnchor(legacy,client,owned,70,scratch,real)==Status::Ok);
    CHECK(detail::ReadPlanckAnchor(stale,client,owned,70,scratch,real)==Status::Unavailable);
    CHECK(anchor.Update(&stale, scratch) == Status::Unavailable && !seen.valid);
    CHECK(update() == Status::Conflict && !seen.valid); // Cannot resurrect a withdrawn sample.
    capture(3, PlanckSynergy::MonotonicNowNs());
    CHECK(update() == Status::Ok && seen.valid);
    PlanckSynergyAPI older = *planck; older.size = 128;
    CHECK(anchor.Update(&older, scratch) == Status::UnsupportedVersion && !seen.valid);
    CHECK(detail::ReadPlanckAnchor(older,client,owned,70,scratch,real)==Status::UnsupportedVersion);
    older = *planck; older.capabilities &= ~PLANCK_CAP_SAMPLE_TIME;
    CHECK(anchor.Update(&older, scratch) == Status::UnsupportedVersion);
    capture(4, PlanckSynergy::MonotonicNowNs());
    CHECK(update() == Status::Ok && seen.valid);
    capture(3, PlanckSynergy::MonotonicNowNs()); // New sample with regressed frame.
    CHECK(update() == Status::Conflict && !seen.valid);
    capture(5, PlanckSynergy::MonotonicNowNs());
    CHECK(update() == Status::Ok && seen.valid);
    ++scope.saveGeneration;
    CHECK(update() == Status::Unavailable && !seen.valid);
    --scope.saveGeneration;
    capture(6, PlanckSynergy::MonotonicNowNs());
    CHECK(update() == Status::Ok && seen.valid);
    ++body.lineage; // Same actor handle and body index, new physical generation.
    capture(7, PlanckSynergy::MonotonicNowNs());
    CHECK(detail::ReadPlanckAnchor(*planck,client,owned,70,scratch,real)==Status::Unavailable);
    CHECK(update() == Status::Unavailable && !seen.valid);
    CHECK(planck->ReleaseClient(client) == PLANCK_OK);
    std::puts("PASS PLANCK actor pose to Body Slots region anchor");
}
