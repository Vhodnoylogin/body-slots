#include "planck_pose_region_anchor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifdef BODY_SLOTS_COMPACT_PAIR_TEST
#include "grip_region_action.h"
#include "main/interaction_regions.h"
#include "instances.h"
#include <array>
#endif

using namespace body_slots;
using namespace body_slots::examples;
namespace {
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"failed at %d: %s\n",__LINE__,#x); std::abort(); } } while (false)
RegionsView scope{sizeof(scope),1,93,55};
PlanckBodyPose measured{sizeof(measured),UINT32_MAX,31};
RegionAnchorSnapshot seen{};
std::uint32_t response = PLANCK_POSE_OK;
std::uint64_t fixtureTimeNs=1000000000;
std::uint64_t Now() noexcept { return fixtureTimeNs; }
std::uint64_t NextMeasurement() noexcept { return fixtureTimeNs+=1000000; }
std::uint32_t CopyPose(std::uint32_t actor,const char* name,PlanckBodyPose* out) {
    CHECK(actor==101 && !std::strcmp(name,"NPC Pelvis") && out->size==sizeof(*out));
    *out=measured;
    return response;
}
}

#ifdef BODY_SLOTS_COMPACT_PAIR_TEST
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
namespace {
HiggsGripOffer registeredOffer{};
HiggsGripSelected registeredSelected{};
void* registeredContext{};
std::uint32_t RegisterGrip(const char*,HiggsGripOffer offer,HiggsGripSelected selected,void* context,std::uint64_t* token) {
    registeredOffer=offer; registeredSelected=selected; registeredContext=context; *token=1;
    return HIGGS_GRIP_OK;
}
std::uint32_t RemoveGrip(std::uint64_t) {
    registeredOffer=nullptr; registeredSelected=nullptr; registeredContext=nullptr;
    return HIGGS_GRIP_OK;
}
double ClockSeconds() noexcept { return double(Now())/1e9; }

struct Pocket {
    std::uint64_t writer;
    InteractionRegionRecord region;
    RegionSampleSource wrist;
    PlanckPoseRegionAnchor anchor;
    PlanckActorPoseAPI planck{sizeof(planck),2,CopyPose};
    std::array<RegionObservation,kMaxRegionObservations> rows{};

    static bool Evaluate(void* context,RegionObservation& out) noexcept {
        auto& pocket=*static_cast<Pocket*>(context);
        const auto& regions=detail::GetRegionsApi();
        RegionsView view{sizeof(view),kRegionsApiVersion};
        // Null buffer still returns the accepted world/save scope.
        CHECK(regions.CopyRegions(pocket.writer,0,nullptr,0,&view)==Status::BufferTooSmall);
        RegionEvaluation sample{sizeof(sample),0,pocket.region.regionId,pocket.region.revision};
        const auto anchorStatus=pocket.anchor.Sample(&pocket.planck,view,sample.anchor);
        sample.probe={sizeof(sample.probe),1,{},pocket.wrist,{0,0,0},0};
        RegionEvaluationFrame frame{sizeof(frame),0,view.epoch,view.saveGeneration,pocket.wrist.frame,ClockSeconds()};
        CHECK(regions.Publish(pocket.writer,&frame,&sample,1,&view)==Status::Ok);
        CHECK(regions.CopyObservations(pocket.writer,view.revision,pocket.rows.data(),pocket.rows.size(),&view)==Status::Ok);
        CHECK(view.count==1);
        out=pocket.rows.front();
        return anchorStatus==Status::Ok;
    }
    void Tick() {
        ++measured.frame;
        measured.observedAtNs=NextMeasurement();
        // Synthetic wrist measurement captured in the same game frame. It has its own clock and ID.
        wrist.sampleId++;
        wrist.frame=std::uint64_t(measured.frame)+1;
        wrist.timeSeconds=ClockSeconds();
    }
};

void CompactPocket() {
    const auto* base=BodySlots_QueryInterface(1,sizeof(Api));
    std::uint64_t writer{};
    CHECK(base->RegisterClient("compact-pocket",&writer)==Status::Ok);
    const auto save=detail::CurrentSaveGeneration();
    detail::SetInteractionRegionsScope(93,save);
    const auto& regions=detail::GetRegionsApi();
    InteractionRegionSpec spec{sizeof(spec),static_cast<std::uint32_t>(RegionAnchorKind::Npc),"compact-pocket","rim"};
    spec.shape={sizeof(spec.shape),static_cast<std::uint32_t>(ZoneShapeKind::Sphere)};
    spec.shape.radius=2;
    spec.maxSampleAgeSeconds=spec.maxSampleSkewSeconds=spec.maxContinuityGapSeconds=.5;
    InteractionRegionRecord record{};
    CHECK(regions.UpsertRegion(writer,&spec,0,&record)==Status::Ok);
    const RegionSampleSource wrist{"fixture","left-wrist",18,1,0,93,save,0,0,1,0};
    const RegionSampleSource body{"PLANCK","named-physical-body",31,31};
    Pocket pocket{writer,record,wrist,PlanckPoseRegionAnchor({93,save,101,"NPC Pelvis",31,500000000,70.f},Now)};
    GripRegionAction grip({record.regionId,record.revision,93,save,body,wrist,Left,.5},ClockSeconds);
    HiggsGripActions001 broker{RegisterGrip,RemoveGrip};
    CHECK(grip.Connect(&broker,"pocket.inspect",false)==HIGGS_GRIP_OK);
    measured={sizeof(measured),10,31};
    measured.rotationXyzw[3]=1;
    pocket.Tick();
    CHECK(!grip.Update(Pocket::Evaluate,&pocket));
    const auto press=registeredOffer(1,registeredContext);
    CHECK(press);
    registeredSelected(1,press,registeredContext);
    pocket.Tick();
    CHECK(grip.Update(Pocket::Evaluate,&pocket));
    pocket.Tick();
    CHECK(!grip.Update(Pocket::Evaluate,&pocket)); // one execution
    const auto latePress=registeredOffer(1,registeredContext);
    CHECK(latePress);
    registeredSelected(1,latePress,registeredContext);
    ++measured.generation;
    pocket.Tick();
    CHECK(!grip.Update(Pocket::Evaluate,&pocket) && !registeredOffer(1,registeredContext));
    CHECK(pocket.rows.front().state==static_cast<std::uint32_t>(RegionSpatialState::Unavailable));
    grip.Disconnect();
    CHECK(base->ReleaseClient(writer)==Status::Ok);
    detail::InvalidateInteractionRegions();
    std::puts("PASS compact pose to real Body Slots region evaluation to compact grip, with single activation and body replacement");
}
}
#endif

int main() {
    PlanckActorPoseAPI api{sizeof(api),2,CopyPose};
    measured.positionMetres[0]=1.5f;
    measured.rotationXyzw[2]=measured.rotationXyzw[3]=float(std::sqrt(.5));
    measured.observedAtNs=Now()-1;
    PlanckPoseRegionTarget target{93,55,101,"NPC Pelvis",31,1000000000,70.f};
    PlanckPoseRegionAnchor anchor(target,Now);
    auto update=[&]{return anchor.Sample(&api,scope,seen);};
    CHECK(update()==Status::Unavailable && !seen.valid);
    measured.observedAtNs=NextMeasurement();
    CHECK(update()==Status::Ok && seen.valid);
    CHECK(seen.world.position[0]==105.f && std::abs(seen.world.rotation[3]-1.f)<1e-6);
    CHECK(seen.source.generation==31 && seen.source.epoch==93 && seen.source.saveGeneration==55);
    CHECK(seen.source.frame==uint64_t(UINT32_MAX)+1 && seen.source.sampleId==measured.observedAtNs);
    const auto originalTime=seen.source.timeSeconds;
    measured.observedAtNs=NextMeasurement();
    CHECK(update()==Status::Ok && seen.source.timeSeconds==originalTime);

    ++measured.frame; // Genuine engine frame wraps to zero.
    measured.observedAtNs=seen.source.sampleId;
    CHECK(update()==Status::Conflict && !seen.valid); // A new frame with the previous capture time.
    measured.observedAtNs=NextMeasurement();
    CHECK(update()==Status::Ok && seen.source.frame==uint64_t(UINT32_MAX)+2);
    --measured.frame;
    CHECK(update()==Status::Conflict && !seen.valid);
    ++measured.frame;
    CHECK(update()==Status::Conflict && !seen.valid); // Cannot revive a withdrawn frame.
    ++measured.frame;
    measured.observedAtNs=NextMeasurement();
    CHECK(update()==Status::Ok && seen.valid);
    response=PLANCK_POSE_BUSY;
    CHECK(update()==Status::Unavailable && !seen.valid && seen.source.identity==target.bodyGeneration);
    response=PLANCK_POSE_OK;

    ++measured.frame;measured.observedAtNs=NextMeasurement();
    CHECK(update()==Status::Ok);
    ++measured.generation;
    CHECK(update()==Status::Unavailable && !seen.valid);
    --measured.generation;
    ++measured.frame;measured.observedAtNs=NextMeasurement();
    CHECK(update()==Status::Ok);
    ++scope.saveGeneration;
    CHECK(update()==Status::Unavailable && !seen.valid);
    --scope.saveGeneration;
    ++measured.frame;measured.observedAtNs=NextMeasurement();
    CHECK(update()==Status::Ok);
    measured.observedAtNs=UINT64_MAX;
    CHECK(update()==Status::Unavailable && !seen.valid);
    ++measured.frame;measured.observedAtNs=NextMeasurement();
    measured.positionMetres[0]=std::numeric_limits<float>::infinity();
    CHECK(update()==Status::InvalidArgument && !seen.valid);
    --api.version;
    CHECK(update()==Status::UnsupportedVersion);
    ++api.version;api.size=8;
    CHECK(update()==Status::UnsupportedVersion);

    target.maxAgeNs=50000000;
    PlanckPoseRegionAnchor shortLived(target,Now);
    api.size=sizeof(api);
    measured.positionMetres[0]=1;
    measured.observedAtNs=NextMeasurement();
    CHECK(shortLived.Sample(&api,scope,seen)==Status::Ok && seen.valid);
    fixtureTimeNs+=65000000;
    measured.observedAtNs=NextMeasurement(); // Reading an unchanged frame cannot buy another 50 ms.
    CHECK(shortLived.Sample(&api,scope,seen)==Status::Unavailable && !seen.valid);
#ifdef BODY_SLOTS_COMPACT_PAIR_TEST
    CompactPocket();
#endif
    std::puts("PASS named PLANCK pose to Body Slots NPC anchor, lifetime, scope, frame wrap, freshness and withdrawal");
}
