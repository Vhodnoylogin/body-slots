// Actual bounded interaction-region core + registry leases. No engine doubles.
#include "main/interaction_regions.h"
#include "instances.h"
#include "zone_geometry.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace body_slots;
using namespace body_slots::detail;
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
namespace {
unsigned checks=0;
#define CHECK(c) do { ++checks; if(!(c)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #c); } while(false)
static_assert(sizeof(Api)==80 && sizeof(RuntimeApi)==104 && sizeof(ZoneApi)==48,"existing tables unchanged");
static_assert(sizeof(RegionsApi)==56 && sizeof(RegionObservation)==576,"new table copied layout");
const auto& regions=GetRegionsApi();
RuntimeTransform Identity() { RuntimeTransform t{}; t.scale=1; t.rotation[0]=t.rotation[4]=t.rotation[8]=1; return t; }
RegionsView View() { return {sizeof(RegionsView),kRegionsApiVersion}; }
struct Fixture {
    const Api* base=BodySlots_QueryInterface(1,sizeof(Api));
    std::uint64_t owner=0,other=0,reader=0;
    std::uint64_t epoch=91,save=0,frame=0;
    double time=0;
    InteractionRegionRecord region{};
    Fixture() {
        CHECK(base); CHECK(base->RegisterClient("regions-owner",&owner)==Status::Ok);
        CHECK(base->RegisterClient("regions-other",&other)==Status::Ok);
        CHECK(base->RegisterObserver(&reader)==Status::Ok);
        save=CurrentSaveGeneration(); InvalidateInteractionRegions(); SetInteractionRegionsScope(epoch,save);
        auto spec=Spec(); CHECK(regions.UpsertRegion(owner,&spec,0,&region)==Status::Ok);
    }
    ~Fixture() {
        if(owner) base->ReleaseClient(owner); if(other) base->ReleaseClient(other); if(reader) base->ReleaseClient(reader);
        InvalidateInteractionRegions();
    }
    InteractionRegionSpec Spec() const {
        InteractionRegionSpec s{}; s.structSize=sizeof(s); s.anchorKind=static_cast<std::uint32_t>(RegionAnchorKind::Npc);
        std::strcpy(s.provider,"regions-owner"); std::strcpy(s.key,"chest");
        s.shape.structSize=sizeof(s.shape); s.shape.kind=static_cast<std::uint32_t>(ZoneShapeKind::Sphere); s.shape.radius=2;
        s.enterInset=.25f; s.exitOutset=.5f; s.dwellSeconds=.2; s.maxSampleAgeSeconds=.15; s.maxSampleSkewSeconds=.01; s.maxContinuityGapSeconds=.15;
        return s;
    }
    RegionSampleSource Source(std::uint64_t identity,const char* source) const {
        RegionSampleSource s{}; std::strcpy(s.provider,"external-pose"); std::strcpy(s.source,source);
        s.identity=identity; s.generation=1; s.sampleId=frame; s.epoch=epoch; s.saveGeneration=save; s.frame=frame;
        s.timeSeconds=time; s.units=static_cast<std::uint32_t>(RegionUnits::SkyrimWorld); return s;
    }
    RegionEvaluation Evaluation(float x=0,float y=0,float z=0,std::uint64_t target=100,std::uint64_t probe=200) const {
        RegionEvaluation e{}; e.structSize=sizeof(e); e.regionId=region.regionId; e.expectedRegionRevision=region.revision;
        e.anchor.structSize=sizeof(e.anchor); e.anchor.kind=region.spec.anchorKind; e.anchor.valid=1; e.anchor.source=Source(target,"npc-chest"); e.anchor.world=Identity();
        e.probe.structSize=sizeof(e.probe); e.probe.valid=1; e.probe.source=Source(probe,"point"); e.probe.world[0]=x; e.probe.world[1]=y; e.probe.world[2]=z;
        return e;
    }
    void Tick(double delta=.1) { ++frame; time+=delta; }
    Status Publish(const std::vector<RegionEvaluation>& batch) {
        RegionEvaluationFrame f{sizeof(RegionEvaluationFrame),0,epoch,save,frame,time}; auto view=View();
        return regions.Publish(owner,&f,batch.data(),static_cast<std::uint32_t>(batch.size()),&view);
    }
    std::vector<RegionObservation> Copy() const {
        std::vector<RegionObservation> out(kMaxRegionObservations); auto view=View();
        CHECK(regions.CopyObservations(reader,0,out.data(),kMaxRegionObservations,&view)==Status::Ok); out.resize(view.count); return out;
    }
    RegionObservation Row() const { auto rows=Copy(); CHECK(rows.size()==1); return rows[0]; }
    void Update(const InteractionRegionSpec& spec) { CHECK(regions.UpsertRegion(owner,&spec,region.revision,&region)==Status::Ok); }
    void State(RegionSpatialState expected) const { CHECK(Row().state==static_cast<std::uint32_t>(expected)); }
    void Reason(RegionUnavailableReason expected) const { auto r=Row(); CHECK(r.state==0); CHECK(r.unavailableReason==static_cast<std::uint32_t>(expected)); CHECK(r.dwellElapsedSeconds==0); }
};
void GeometryAndHysteresis() {
    Fixture f; auto s=f.region.spec; s.shape.kind=4; s.shape.center[2]=-1; s.shape.end[2]=1; s.shape.radius=1; s.enterInset=0; s.exitOutset=0; s.dwellSeconds=0; f.Update(s);
    f.Tick(); CHECK(f.Publish({f.Evaluation(0,0,1.01f)})==Status::Ok); f.State(RegionSpatialState::Outside);
    s=f.region.spec; s.shape.kind=static_cast<std::uint32_t>(ZoneShapeKind::Capsule); f.Update(s);
    f.Tick(); CHECK(f.Publish({f.Evaluation(0,0,1.01f)})==Status::Ok); f.State(RegionSpatialState::Ready);
    s=f.region.spec; s.shape.kind=4; f.Update(s);
    f.Tick(); auto e=f.Evaluation(10,0,2); e.anchor.world=Identity(); e.anchor.world.position[0]=10; e.anchor.world.scale=2;
    // Rotate local Z onto world X: inverse transform maps world (12,0,0) to local Z=1.
    const float rotation[9]={0,0,1,0,1,0,-1,0,0}; std::memcpy(e.anchor.world.rotation,rotation,sizeof(rotation));
    e.probe.world[0]=12; e.probe.world[2]=0;
    CHECK(f.Publish({e})==Status::Ok); f.State(RegionSpatialState::Ready);
    f.Tick(); e=f.Evaluation(12.01f,0,0); e.anchor.world.position[0]=10; e.anchor.world.scale=2; std::memcpy(e.anchor.world.rotation,rotation,sizeof(rotation));
    CHECK(f.Publish({e})==Status::Ok); f.State(RegionSpatialState::Outside);
    s=f.Spec(); s.shape.kind=static_cast<std::uint32_t>(ZoneShapeKind::Box); s.shape.radius=0; s.shape.halfExtents[0]=s.shape.halfExtents[1]=s.shape.halfExtents[2]=2; f.Update(s);
    f.Tick(); CHECK(f.Publish({f.Evaluation(1.8f)})==Status::Ok); f.State(RegionSpatialState::Outside); // outside entry inset
    f.Tick(); CHECK(f.Publish({f.Evaluation(1.7f)})==Status::Ok); f.State(RegionSpatialState::Inside);
    f.Tick(); CHECK(f.Publish({f.Evaluation(2.4f)})==Status::Ok); f.State(RegionSpatialState::Dwelling); // retained by exit outset
    f.Tick(); CHECK(f.Publish({f.Evaluation(2.51f)})==Status::Ok); f.State(RegionSpatialState::Outside);
}
void DwellAndDiscontinuities() {
    Fixture f;
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside);
    const auto sequence=f.Row().sequence;
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Dwelling);
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Ready); CHECK(f.Row().sequence>sequence);
    // Readiness is just a spatial enum: no grant, eligibility, actuation or hand classification enters this contract.
    f.Tick(); auto e=f.Evaluation(); e.anchor.source.generation=2; CHECK(f.Publish({e})==Status::Ok); f.State(RegionSpatialState::Inside); CHECK(f.Row().dwellElapsedSeconds==0);
    f.Tick(); e=f.Evaluation(); e.anchor.source.generation=2; e.probe.source.generation=2; CHECK(f.Publish({e})==Status::Ok); f.State(RegionSpatialState::Inside);
    f.Tick(); e=f.Evaluation(); e.anchor.valid=0; CHECK(f.Publish({e})==Status::Ok); f.Reason(RegionUnavailableReason::Missing);
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside);
    f.Tick(); CHECK(f.Publish({})==Status::Ok); CHECK(f.Copy().empty());
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside);
    f.frame+=2; f.time+=.1; CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside); // missed frame
    f.Tick(.3); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside); // time gap
    f.Tick(); e=f.Evaluation(); e.anchor.source.timeSeconds-=.2; e.probe.source.timeSeconds-=.2;
    CHECK(f.Publish({e})==Status::Ok); f.Reason(RegionUnavailableReason::Stale);
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside);
    f.Tick(); e=f.Evaluation(); e.anchor.source.frame-=1; e.anchor.source.sampleId-=1; e.probe.source.frame-=1; e.probe.source.sampleId-=1;
    CHECK(f.Publish({e})==Status::Ok); f.Reason(RegionUnavailableReason::RepeatedSample);
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside);
    f.time-=.5; CHECK(f.Publish({f.Evaluation()})==Status::Conflict); CHECK(f.Copy().empty()); // rollback loses all dwell
    f.time+=.6; f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside);
    CHECK(f.Publish({f.Evaluation()})==Status::Conflict); CHECK(f.Copy().empty()); // repeated batch frame
}
void InvalidSamplesAndScopes() {
    Fixture f;
    auto invalid=[&](auto mutate,RegionUnavailableReason reason) { f.Tick(); auto e=f.Evaluation(); mutate(e); CHECK(f.Publish({e})==Status::Ok); f.Reason(reason); };
    invalid([](auto& e){ e.anchor.world.rotation[0]=2; },RegionUnavailableReason::InvalidSnapshot);
    invalid([](auto& e){ e.anchor.world.rotation[0]=-1; },RegionUnavailableReason::InvalidSnapshot); // reflection
    invalid([](auto& e){ e.anchor.world.rotation[0]=std::numeric_limits<float>::quiet_NaN(); },RegionUnavailableReason::InvalidSnapshot);
    invalid([](auto& e){ e.anchor.world.scale=0; },RegionUnavailableReason::InvalidSnapshot);
    invalid([](auto& e){ e.anchor.world.scale=-1; },RegionUnavailableReason::InvalidSnapshot);
    invalid([](auto& e){ e.probe.world[1]=std::numeric_limits<float>::infinity(); },RegionUnavailableReason::InvalidSnapshot);
    invalid([](auto& e){ e.probe.source.units=99; },RegionUnavailableReason::InvalidSnapshot);
    invalid([](auto& e){ e.probe.source.generation=0; },RegionUnavailableReason::InvalidSnapshot);
    invalid([](auto& e){ ++e.anchor.source.epoch; },RegionUnavailableReason::ScopeMismatch);
    invalid([](auto& e){ ++e.probe.source.saveGeneration; },RegionUnavailableReason::ScopeMismatch);
    invalid([](auto& e){ --e.anchor.source.frame; },RegionUnavailableReason::Incoherent);
    invalid([](auto& e){ e.anchor.source.timeSeconds-=.02; },RegionUnavailableReason::Incoherent);
    invalid([](auto& e){ e.probe.source.timeSeconds+=.01; },RegionUnavailableReason::Incoherent);
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok);
    const auto evaluated=f.Row(); const auto copyAgain=f.Row(); CHECK(copyAgain.sequence==evaluated.sequence && copyAgain.evaluated.timeSeconds==evaluated.evaluated.timeSeconds);
    InvalidateInteractionRegions(); CHECK(f.Copy().empty()); f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Unavailable);
    SetInteractionRegionsScope(f.epoch+1,f.save); CHECK(f.Publish({f.Evaluation()})==Status::Conflict);
    ++f.epoch; CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Inside);
}
void RegistrySaveReset() {
    Fixture f; auto spec=f.region.spec; spec.dwellSeconds=0; f.Update(spec);
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Ready);
    const auto oldSave=f.save; CHECK(ResetSaveScope()==Status::Ok); CHECK(CurrentSaveGeneration()!=oldSave);
    CHECK(f.Copy().empty()); f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Unavailable);
    SetInteractionRegionsScope(f.epoch,oldSave); CHECK(f.Publish({f.Evaluation()})==Status::Unavailable);
    f.save=CurrentSaveGeneration(); ++f.epoch; SetInteractionRegionsScope(f.epoch,f.save);
    CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Ready); CHECK(f.Row().dwellElapsedSeconds==0);
    SetInteractionRegionsScope(f.epoch-1,oldSave); f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok);
}
void OwnershipRevisionAndCopies() {
    Fixture f; auto s=f.Spec(); InteractionRegionRecord unchanged{}; unchanged.regionId=999;
    CHECK(regions.UpsertRegion(f.other,&s,0,&unchanged)==Status::Unauthorized); CHECK(unchanged.regionId==999);
    CHECK(regions.UpsertRegion(f.reader,&s,0,&unchanged)==Status::Unauthorized);
    CHECK(regions.UpsertRegion(f.owner,&s,0,&unchanged)==Status::Conflict);
    CHECK(regions.RemoveRegion(f.other,f.region.regionId,f.region.revision)==Status::Unauthorized);
    CHECK(regions.RemoveRegion(f.owner,f.region.regionId,f.region.revision+1)==Status::Conflict);
    auto view=View(); InteractionRegionRecord record{}; record.regionId=999;
    CHECK(regions.CopyRegions(f.reader,0,nullptr,0,&view)==Status::BufferTooSmall); CHECK(view.count==1);
    const auto revision=view.revision;
    CHECK(regions.CopyRegions(f.reader,revision+1,&record,1,&view)==Status::Conflict); CHECK(record.regionId==999);
    CHECK(regions.CopyRegions(f.reader,revision,&record,1,&view)==Status::Ok); CHECK(record.regionId==f.region.regionId);
    f.Tick(); auto e=f.Evaluation(); ++e.expectedRegionRevision; CHECK(f.Publish({e})==Status::Conflict); CHECK(f.Copy().empty());
    e=f.Evaluation(); CHECK(f.Publish({e,e})==Status::InvalidArgument); CHECK(f.Copy().empty());
    CHECK(f.Publish({e})==Status::Ok); const auto oldRevision=f.region.revision;
    s.dwellSeconds=.5; f.Update(s); CHECK(f.region.revision>oldRevision); CHECK(f.Copy().empty());
    f.Tick(); CHECK(f.Publish({e})==Status::Conflict);
    CHECK(regions.RemoveRegion(f.owner,f.region.regionId,f.region.revision)==Status::Ok);
    f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::NotFound);
    s.enterInset=-1; CHECK(regions.UpsertRegion(f.owner,&s,0,&record)==Status::InvalidArgument);
    s=f.Spec(); s.shape.kind=4; CHECK(regions.UpsertRegion(f.owner,&s,0,&record)==Status::InvalidArgument); // coincident finite cylinder caps
    s=f.Spec(); s.maxSampleAgeSeconds=0; CHECK(regions.UpsertRegion(f.owner,&s,0,&record)==Status::InvalidArgument);
}
void IndependentPairsAndLeaseRelease() {
    Fixture f; f.Tick(); CHECK(f.Publish({f.Evaluation(0,0,0,10,20),f.Evaluation(10,0,0,11,20),f.Evaluation(0,0,0,10,21)})==Status::Ok);
    auto rows=f.Copy(); CHECK(rows.size()==3); CHECK(rows[0].state==2 && rows[1].state==1 && rows[2].state==2);
    f.Tick(); auto changed=f.Evaluation(0,0,0,10,20); ++changed.anchor.source.generation;
    CHECK(f.Publish({changed,f.Evaluation(10,0,0,11,20),f.Evaluation(0,0,0,10,21)})==Status::Ok);
    rows=f.Copy(); CHECK(rows[0].state==2 && rows[1].state==1 && rows[2].state==3);
    const auto oldId=f.region.regionId; const auto dead=f.owner; CHECK(f.base->ReleaseClient(dead)==Status::Ok); f.owner=0;
    CHECK(f.Copy().empty()); auto view=View(); CHECK(regions.CopyRegions(f.reader,0,nullptr,0,&view)==Status::Ok); CHECK(view.count==0);
    CHECK(f.base->RegisterClient("regions-owner",&f.owner)==Status::Ok); CHECK(f.owner!=dead);
    auto s=f.Spec(); CHECK(regions.UpsertRegion(f.owner,&s,0,&f.region)==Status::Ok); CHECK(f.region.regionId!=oldId);
    f.Tick(); auto frame=RegionEvaluationFrame{sizeof(RegionEvaluationFrame),0,f.epoch,f.save,f.frame,f.time}; auto e=f.Evaluation();
    CHECK(regions.Publish(dead,&frame,&e,1,&view)==Status::Unauthorized);
    CHECK(f.Publish({e})==Status::Ok); f.State(RegionSpatialState::Inside);
}
std::uint64_t releasingReader=0;
std::uint32_t releasingRegionCount=0,releasingObservationCount=0;
Status releasingCopyStatus=Status::InternalError,releasingPublishStatus=Status::InternalError;
Status FailedReleaseHook(std::uint64_t writer) noexcept {
    auto view=View();
    releasingCopyStatus=regions.CopyRegions(releasingReader,0,nullptr,0,&view); releasingRegionCount=view.count;
    view=View(); regions.CopyObservations(releasingReader,0,nullptr,0,&view); releasingObservationCount=view.count;
    auto frame=RegionEvaluationFrame{sizeof(RegionEvaluationFrame),0,91,CurrentSaveGeneration(),99,99}; view=View();
    releasingPublishStatus=regions.Publish(writer,&frame,nullptr,0,&view);
    return Status::Conflict;
}
void RolledBackReleasePreservesRows() {
    Fixture f; f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok);
    releasingReader=f.reader; SetRuntimeReleaseHook(FailedReleaseHook);
    const auto released=f.base->ReleaseClient(f.owner); SetRuntimeReleaseHook(nullptr);
    CHECK(released==Status::Conflict); CHECK(releasingCopyStatus==Status::BufferTooSmall);
    CHECK(releasingRegionCount==1 && releasingObservationCount==1); CHECK(releasingPublishStatus==Status::Unauthorized);
    CHECK(f.Copy().size()==1); f.Tick(); CHECK(f.Publish({f.Evaluation()})==Status::Ok); f.State(RegionSpatialState::Dwelling);
}
void Capacities() {
    Fixture f; auto s=f.Spec(); InteractionRegionRecord record{};
    for(unsigned i=1;i<kMaxInteractionRegions;++i) { std::snprintf(s.key,sizeof(s.key),"region-%u",i); CHECK(regions.UpsertRegion(f.owner,&s,0,&record)==Status::Ok); }
    std::memset(s.key,0,sizeof(s.key)); std::strcpy(s.key,"overflow"); CHECK(regions.UpsertRegion(f.owner,&s,0,&record)==Status::LimitExceeded);
    // Free one region so the independent writer can own its own target.
    CHECK(regions.RemoveRegion(f.owner,record.regionId,record.revision)==Status::Ok);
    std::strcpy(s.provider,"regions-other"); CHECK(regions.UpsertRegion(f.other,&s,0,&record)==Status::Ok);
    f.Tick(); std::vector<RegionEvaluation> batch; batch.reserve(kMaxRegionObservations);
    for(unsigned i=0;i<kMaxRegionObservations;++i) batch.push_back(f.Evaluation(0,0,0,100,1000+i));
    CHECK(f.Publish(batch)==Status::Ok); CHECK(f.Copy().size()==kMaxRegionObservations);
    auto e=f.Evaluation(); e.regionId=record.regionId; e.expectedRegionRevision=record.revision;
    auto frame=RegionEvaluationFrame{sizeof(RegionEvaluationFrame),0,f.epoch,f.save,f.frame,f.time}; auto view=View();
    CHECK(regions.Publish(f.other,&frame,&e,1,&view)==Status::LimitExceeded);
    RegionObservation untouched{}; untouched.regionId=999;
    CHECK(regions.CopyObservations(f.reader,0,&untouched,1,&view)==Status::BufferTooSmall); CHECK(untouched.regionId==999 && view.count==kMaxRegionObservations);
    f.Tick(); CHECK(f.Publish({})==Status::Ok); CHECK(f.Copy().empty());
    CHECK(regions.Publish(f.other,&frame,&e,1,&view)==Status::Ok); CHECK(f.Copy().size()==1);
}
} // namespace
int main() {
    try {
        GeometryAndHysteresis(); DwellAndDiscontinuities(); InvalidSamplesAndScopes(); RegistrySaveReset(); OwnershipRevisionAndCopies(); IndependentPairsAndLeaseRelease(); RolledBackReleasePreservesRows(); Capacities();
        std::cout<<"interaction region checks: "<<checks<<" passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
