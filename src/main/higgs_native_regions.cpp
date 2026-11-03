#include "higgs_native_regions.h"
#include "instances.h"
#include "runtime.h"
#include "interaction_regions.h"
#include "zone_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace body_slots::game {
namespace {
constexpr const char* providers[]={"higgs-native-left","higgs-native-right"};
constexpr const char* keys[]={"mouth","stash-right","stash-left"};
bool Finite3(const float* p) { return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]); }
RuntimeTransform Anchor(const float* center) {
    RuntimeTransform t{};t.scale=1;t.rotation[0]=t.rotation[4]=t.rotation[8]=1;
    std::copy(center,center+3,t.position);return t;
}
bool ValidFrame(const HIGGS_NativeRegionFrame& f) {
    if(f.structSize!=sizeof(f)||f.physicalHand<1||f.physicalHand>2||f.reserved||
       f.flags!=HIGGS_REGION_SOURCE_VALID||!f.epoch||!f.sampleId||!f.observedAtNs||
       !f.sourceGeneration||!f.handGeneration||!Finite3(f.hmdPosition)||
       !Finite3(f.handPosition)||!Finite3(f.palmPosition)||!std::isfinite(f.hmdScale)||f.hmdScale<=0)return false;
    // The source owns HMD-to-sphere conversion. Validate its declared rotation,
    // without applying HMD scale to already-world-space sphere radii again.
    ZoneShape unit{};unit.structSize=sizeof(unit);unit.kind=1;unit.radius=1;
    auto hmd=Anchor(f.hmdPosition);std::copy(f.hmdRotation,f.hmdRotation+9,hmd.rotation);
    if(!std::isfinite(detail::ZonePointSignedDistance(unit,hmd,f.hmdPosition)))return false;
    for(const auto& sphere:f.spheres)
        if(!Finite3(sphere.worldCenter)||!std::isfinite(sphere.worldRadius))return false;
    return true;
}
RegionSampleSource Source(const HIGGS_NativeRegionFrame& f,const char* source,
                          std::uint64_t identity,std::uint64_t generation,std::uint64_t save) {
    RegionSampleSource s{};std::strcpy(s.provider,"HIGGS");std::strcpy(s.source,source);
    s.identity=identity;s.generation=generation;s.sampleId=f.sampleId;s.epoch=f.epoch;
    s.saveGeneration=save;s.frame=f.sampleId;s.timeSeconds=double(f.observedAtNs)/1e9;
    s.units=static_cast<std::uint32_t>(RegionUnits::SkyrimWorld);return s;
}
InteractionRegionSpec Spec(unsigned hand,unsigned region,float radius) {
    InteractionRegionSpec s{};s.structSize=sizeof(s);s.anchorKind=static_cast<std::uint32_t>(RegionAnchorKind::Player);
    std::strcpy(s.provider,providers[hand]);std::strcpy(s.key,keys[region]);
    s.shape.structSize=sizeof(s.shape);s.shape.kind=1;s.shape.radius=radius;
    s.maxSampleAgeSeconds=.1;s.maxSampleSkewSeconds=0;s.maxContinuityGapSeconds=.1;
    return s;
}
}
HiggsNativeRegions::~HiggsNativeRegions() {
    // The game singleton has process lifetime. Scoped fixtures remove the HIGGS
    // detector before destroying this context; registry leases own region rows.
    for(const auto& hand:hands_)if(hand.writer)registry_.ReleaseClient(hand.writer);
}
void HiggsNativeRegions::Reset() noexcept {
    try {std::lock_guard lock(mutex_);barredEpoch_=std::max(barredEpoch_,epoch_);
        detail::InvalidateInteractionRegions();}catch(...){}
}
HIGGS_HandStatus HIGGS_HAND_CALL HiggsNativeRegions::Detect(const HIGGS_NativeRegionFrame* frame,
    HIGGS_NativeRegionResult* output,void* context) noexcept {
    if(!frame||!output||!context)return HIGGS_HAND_INVALID_ARGUMENT;
    return static_cast<HiggsNativeRegions*>(context)->Evaluate(*frame,*output);
}
HIGGS_HandStatus HiggsNativeRegions::Evaluate(const HIGGS_NativeRegionFrame& f,
    HIGGS_NativeRegionResult& output) noexcept {
    try {
        if(output.structSize!=sizeof(output)||f.structSize!=sizeof(f)||f.physicalHand<1||f.physicalHand>2)
            return HIGGS_HAND_INVALID_ARGUMENT;
        std::lock_guard lock(mutex_);
        const auto handled=[&](std::uint32_t mask){output={sizeof(output),mask,f.epoch,f.sampleId};return HIGGS_HAND_OK;};
        auto& h=hands_[f.physicalHand-1];
        // Never fall back to a second detector for rejected old/missing evidence.
        // The synchronous caller can still select ordinary release, without eat/stash.
        if(!ValidFrame(f)||f.epoch<=barredEpoch_||f.epoch<epoch_) {
            detail::ReleaseInteractionRegionsClient(h.writer);h.regions={};
            return handled(0);
        }
        if(f.epoch!=epoch_) {
            epoch_=f.epoch;
            for(auto& state:hands_){state.sample=state.time=state.source=state.hand=0;}
        }
        if(f.sampleId<=h.sample||f.observedAtNs<h.time||f.sourceGeneration<h.source||f.handGeneration<h.hand) {
            detail::ReleaseInteractionRegionsClient(h.writer);h.regions={};return handled(0);
        }
        h.sample=f.sampleId;h.time=f.observedAtNs;h.source=f.sourceGeneration;h.hand=f.handGeneration;
        // An unsupported positive size delegates the whole frame to HIGGS's
        // native detector. Nonpositive radii disable only their own region.
        for(const auto& sphere:f.spheres)if(sphere.worldRadius>1000000)return HIGGS_HAND_NOT_READY;
        if(!h.writer&&registry_.RegisterClient(providers[f.physicalHand-1],&h.writer)!=Status::Ok)
            return HIGGS_HAND_NOT_READY;
        std::array<RegionEvaluation,3> evaluations{};
        std::array<unsigned,3> indices{};std::uint32_t count=0;
        const auto save=detail::CurrentSaveGeneration();
        if(!save)return HIGGS_HAND_NOT_READY;
        for(unsigned i=0;i<3;++i) {
            if(f.spheres[i].worldRadius<=0) {
                auto& record=h.regions[i];
                if(record.regionId) {
                    if(detail::GetRegionsApi().RemoveRegion(h.writer,record.regionId,record.revision)!=Status::Ok)
                        return HIGGS_HAND_NOT_READY;
                    record={};
                }
                continue;
            }
            const auto spec=Spec(f.physicalHand-1,i,f.spheres[i].worldRadius);
            auto& record=h.regions[i];
            if(!record.regionId||record.spec.shape.radius!=spec.shape.radius) {
                if(detail::GetRegionsApi().UpsertRegion(h.writer,&spec,record.revision,&record)!=Status::Ok)
                    return HIGGS_HAND_NOT_READY;
            }
            indices[count]=i;auto& e=evaluations[count++];e.structSize=sizeof(e);e.regionId=record.regionId;e.expectedRegionRevision=record.revision;
            e.anchor.structSize=sizeof(e.anchor);e.anchor.kind=spec.anchorKind;e.anchor.valid=1;
            e.anchor.source=Source(f,keys[i],1,f.sourceGeneration,save);e.anchor.world=Anchor(f.spheres[i].worldCenter);
            e.probe.structSize=sizeof(e.probe);e.probe.valid=1;
            e.probe.source=Source(f,i==0?"native-release-palm":"native-release-hand",f.physicalHand,f.handGeneration,save);
            const auto* point=i==0?f.palmPosition:f.handPosition;std::copy(point,point+3,e.probe.world);
        }
        // sampleId is HIGGS's native-phase sequence, not the older published
        // post-VRIK hand observation and not an invented controller input.
        const RegionEvaluationFrame frame{sizeof(frame),0,f.epoch,save,f.sampleId,double(f.observedAtNs)/1e9};
        RegionsView view{sizeof(view),kRegionsApiVersion};
        if(detail::PublishProviderRegions(h.writer,frame,evaluations.data(),count,observations_.data(),kMaxRegionObservations,&view)!=Status::Ok)
            return HIGGS_HAND_NOT_READY;
        std::uint32_t mask=0;
        for(unsigned j=0;j<count;++j) {
            const auto i=indices[j];
            const auto& e=evaluations[j];
            const auto end=observations_.begin()+view.count;
            const auto row=std::find_if(observations_.begin(),end,[&](const auto& o){
                return o.regionId==e.regionId&&o.regionRevision==e.expectedRegionRevision&&
                    o.evaluated.epoch==f.epoch&&o.evaluated.saveGeneration==save&&o.evaluated.frame==frame.frame&&
                    o.probe.source.sampleId==f.sampleId&&o.probe.source.identity==f.physicalHand;
            });
            if(row==end)return handled(0);
            // Regions have closed surfaces. This native behavior's strict action
            // boundary uses the same pure distance kernel, not native fallback.
            if(row->state==static_cast<std::uint32_t>(RegionSpatialState::Ready)&&
               detail::ZonePointSignedDistance(h.regions[i].spec.shape,e.anchor.world,e.probe.world)<0)
                mask|=1u<<i;
        }
        return handled(mask);
    }catch(...){return HIGGS_HAND_INTERNAL_ERROR;}
}
} // namespace body_slots::game
