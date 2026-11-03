// Actual Body Slots registry/geometry + actual HIGGS optional detector/broker.
// Scripted copied frames exercise lifecycle independently of engine/hardware.
#include "main/higgs_native_regions.h"
#include "main/interaction_regions.h"
#include "instances.h"
#include "runtime.h"
#include "hand_broker.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using namespace body_slots;
using namespace body_slots::detail;
using body_slots::game::HiggsNativeRegions;
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
namespace {
unsigned checks=0;
#define CHECK(c) do {++checks;if(!(c))throw std::runtime_error(std::to_string(__LINE__)+": " #c);}while(false)
HIGGS_NativeRegionFrame Frame(unsigned hand=1) {
    HIGGS_NativeRegionFrame f{};f.structSize=sizeof(f);f.physicalHand=hand;f.flags=HIGGS_REGION_SOURCE_VALID;
    f.epoch=701;f.sampleId=1;f.observedAtNs=1000000000;f.sourceGeneration=10;f.handGeneration=20;
    f.hmdScale=1;f.hmdRotation[0]=f.hmdRotation[4]=f.hmdRotation[8]=1;
    for(unsigned i=0;i<3;++i){f.spheres[i].worldCenter[0]=10.f*(i+1);f.spheres[i].worldRadius=2;}
    f.palmPosition[0]=10;f.handPosition[0]=20;return f;
}
void Next(HIGGS_NativeRegionFrame& f) {++f.sampleId;f.observedAtNs+=10000000;}
struct Fixture {
    const Api& api=*BodySlots_QueryInterface(1,sizeof(Api));
    std::unique_ptr<HiggsNativeRegions> adapter=std::make_unique<HiggsNativeRegions>(api);
    std::uint64_t reader{};
    Fixture(){InvalidateRuntime();CHECK(api.RegisterObserver(&reader)==Status::Ok);}
    ~Fixture(){adapter.reset();api.ReleaseClient(reader);InvalidateRuntime();}
    std::uint32_t Detect(const HIGGS_NativeRegionFrame& f) {
        HIGGS_NativeRegionResult out{sizeof(out),0xdead,0,0};
        CHECK(adapter->Evaluate(f,out)==HIGGS_HAND_OK);CHECK(out.epoch==f.epoch&&out.sampleId==f.sampleId);
        return out.insideMask;
    }
    unsigned Count() {
        auto rows=std::make_unique<std::array<RegionObservation,kMaxRegionObservations>>();
        RegionsView v{sizeof(v),1};CHECK(GetRegionsApi().CopyObservations(reader,0,rows->data(),kMaxRegionObservations,&v)==Status::Ok);
        for(unsigned i=0;i<v.count;++i) {
            CHECK(!std::strcmp((*rows)[i].anchor.source.provider,"HIGGS"));
            CHECK((*rows)[i].evaluated.saveGeneration==CurrentSaveGeneration());
        }
        return v.count;
    }
};
void GeometryAndSeparateHands() {
    Fixture x;auto f=Frame();CHECK(x.Detect(f)==3);CHECK(x.Count()==3);
    auto right=Frame(2);right.sampleId=2;right.handPosition[0]=30;CHECK(x.Detect(right)==5);CHECK(x.Count()==6);
    Next(f);f.sampleId=3;f.palmPosition[0]=12;f.handPosition[0]=22;
    CHECK(x.Detect(f)==0); // strict action boundary; spatial volume stays closed
    Next(f);f.palmPosition[0]=std::nextafter(12.f,10.f);CHECK(x.Detect(f)==1);
    Next(f);f.palmPosition[0]=std::nextafter(12.f,20.f);CHECK(x.Detect(f)==0);
    Next(f);f.palmPosition[0]=10;f.spheres[1].worldRadius=0;f.spheres[2].worldRadius=-1;
    CHECK(x.Detect(f)==1);CHECK(x.Count()==4); // disabled shoulders do not disable drinking
    Next(f);f.spheres[1].worldRadius=2;f.handPosition[0]=20;CHECK(x.Detect(f)==3);CHECK(x.Count()==5);
    Next(f);f.spheres[0].worldCenter[0]=110;f.palmPosition[0]=110;f.hmdScale=2;
    CHECK(x.Detect(f)==3); // already-world radii must not be scaled again
    Next(f);f.palmPosition[0]=113;CHECK(x.Detect(f)==2);
}
void EvidenceAndSave() {
    Fixture x;auto f=Frame();CHECK(x.Detect(f)==3);
    CHECK(x.Detect(f)==0);CHECK(x.Count()==0); // replay never reuses Ready or falls back
    Next(f);CHECK(x.Detect(f)==3);Next(f);f.observedAtNs=1;CHECK(x.Detect(f)==0);CHECK(x.Count()==0);
    f.observedAtNs=2000000000;Next(f);CHECK(x.Detect(f)==3);
    Next(f);--f.sourceGeneration;CHECK(x.Detect(f)==0);
    ++f.sourceGeneration;Next(f);CHECK(x.Detect(f)==3);
    Next(f);f.flags=0;CHECK(x.Detect(f)==0);CHECK(x.Count()==0);
    f.flags=1;Next(f);CHECK(x.Detect(f)==3);
    Next(f);f.palmPosition[0]=std::numeric_limits<float>::quiet_NaN();CHECK(x.Detect(f)==0);
    f.palmPosition[0]=10;Next(f);CHECK(x.Detect(f)==3);
    auto old=f;x.adapter->Reset();CHECK(x.Count()==0);Next(f);CHECK(x.Detect(f)==0);
    CHECK(ResetSaveScope()==Status::Ok);++f.epoch;Next(f);CHECK(x.Detect(f)==3);
    Next(old);CHECK(x.Detect(old)==0);CHECK(x.Count()==0);
    Next(f);CHECK(x.Detect(f)==3);
    const auto priorSave=CurrentSaveGeneration();CHECK(ResetSaveScope()==Status::Ok);CHECK(CurrentSaveGeneration()!=priorSave);
    CHECK(x.Count()==0);Next(f);CHECK(x.Detect(f)==3); // new native observation uses current registry save
    InvalidateRuntime();CHECK(x.Count()==0);Next(f);CHECK(x.Detect(f)==3);
}
void UnsupportedAndArguments() {
    Fixture x;auto f=Frame();HIGGS_NativeRegionResult out{sizeof(out)};
    f.spheres[1].worldRadius=1000001;
    CHECK(x.adapter->Evaluate(f,out)==HIGGS_HAND_NOT_READY);CHECK(out.epoch==0);
    Next(f);f.spheres[1].worldRadius=2;CHECK(x.Detect(f)==3);
    HIGGS_NativeRegionResult small{};CHECK(x.adapter->Evaluate(f,small)==HIGGS_HAND_INVALID_ARGUMENT);
    CHECK(HiggsNativeRegions::Detect(nullptr,&out,x.adapter.get())==HIGGS_HAND_INVALID_ARGUMENT);
    CHECK(HiggsNativeRegions::Detect(&f,&out,nullptr)==HIGGS_HAND_INVALID_ARGUMENT);
}
HIGGS_HandStatus HIGGS_HAND_CALL Outside(const HIGGS_NativeRegionFrame* f,HIGGS_NativeRegionResult* out,void* context) {
    ++*static_cast<unsigned*>(context);*out={sizeof(*out),0,f->epoch,f->sampleId};return HIGGS_HAND_OK;
}
HIGGS_HandStatus HIGGS_HAND_CALL Unavailable(const HIGGS_NativeRegionFrame*,HIGGS_NativeRegionResult*,void*) {
    return HIGGS_HAND_NOT_READY;
}
HIGGS_HandStatus HIGGS_HAND_CALL Malformed(const HIGGS_NativeRegionFrame* f,HIGGS_NativeRegionResult* out,void*) {
    *out={sizeof(*out),7,f->epoch,f->sampleId-1};return HIGGS_HAND_OK;
}
void ActualBrokerSelection() {
    Fixture x;HiggsHands::Broker broker;std::uint64_t client{},registration{};
    HIGGS_HandClientInfo info{};info.structSize=sizeof(info);std::strcpy(info.name,"BodySlots.Regions");
    CHECK(!broker.Register(info,client));auto clock=HiggsHands::Now();
    std::array<HIGGS_HandRecord,2> hands{};
    for(auto& h:hands)h.observationMask=HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN|HIGGS_OBS_PALM_VALID|HIGGS_OBS_TRACKING_VALID;
    hands[0].heldRefId=0xff;broker.ObserveWorld(1,false,clock);broker.Publish(hands,++clock);
    auto f=Frame();bool used=true;CHECK(broker.EvaluateNativeRegions(f,&used)==3);CHECK(!used);
    CHECK(!broker.NativeRegionDetector(client,&HiggsNativeRegions::Detect,x.adapter.get(),registration));
    f=Frame();CHECK(broker.EvaluateNativeRegions(f,&used)==3);CHECK(used);CHECK(x.Count()==3);
    CHECK(broker.MayNativeRegionAction(1,f.epoch,HiggsHands::Now()));
    broker.SetInputContext(1,HiggsHands::Now());CHECK(!broker.MayNativeRegionAction(1,f.epoch,HiggsHands::Now()));
    broker.SetInputContext(0,HiggsHands::Now());CHECK(!broker.MayNativeRegionAction(1,f.epoch,HiggsHands::Now()));
    f=Frame();CHECK(broker.EvaluateNativeRegions(f,&used)==3);CHECK(used);
    CHECK(!broker.RemoveNativeRegionDetector(client,registration));
    unsigned calls=0;CHECK(!broker.NativeRegionDetector(client,&Outside,&calls,registration));
    f=Frame();CHECK(broker.EvaluateNativeRegions(f,&used)==0);CHECK(used&&calls==1); // handled outside suppresses local inside
    CHECK(!broker.RemoveNativeRegionDetector(client,registration));
    CHECK(!broker.NativeRegionDetector(client,&Unavailable,nullptr,registration));
    f=Frame();CHECK(broker.EvaluateNativeRegions(f,&used)==3);CHECK(!used);
    CHECK(!broker.RemoveNativeRegionDetector(client,registration));
    CHECK(!broker.NativeRegionDetector(client,&Malformed,nullptr,registration));
    f=Frame();CHECK(broker.EvaluateNativeRegions(f,&used)==3);CHECK(!used);
    CHECK(!broker.Unregister(client));f=Frame();CHECK(broker.EvaluateNativeRegions(f,&used)==3);CHECK(!used);
    broker.ObserveWorld(0,true,HiggsHands::Now());f=Frame();CHECK(broker.EvaluateNativeRegions(f,&used)==0);
}
}
int main() {try {
    GeometryAndSeparateHands();EvidenceAndSave();UnsupportedAndArguments();ActualBrokerSelection();
    std::printf("HIGGS/Body Slots native regions: %u checks passed\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
