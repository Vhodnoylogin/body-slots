#include "interaction_regions.h"
#include "instances.h"
#include "validation.h"
#include "zone_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace body_slots::detail {
namespace {
constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
struct RegionEntry { std::uint64_t owner = 0; InteractionRegionRecord record{}; };
struct ObservationEntry { std::uint64_t owner = 0; RegionObservation record{}; };
struct WriterFrame { std::uint64_t owner = 0, frame = 0; double time = 0; };
struct RegionsState {
    std::mutex mutex;
    std::array<RegionEntry, kMaxInteractionRegions> regions{};
    std::array<ObservationEntry, kMaxRegionObservations> observations{};
    std::array<WriterFrame, 64> writers{};
    std::uint32_t regionCount = 0, observationCount = 0;
    std::uint64_t epoch = 0, save = 0, revision = 1, sequence = 0, nextId = 1, nextRegionRevision = 1;
};
RegionsState& State() { static RegionsState state; return state; }
template<class F> Status Guard(F&& f) noexcept { try { return f(); } catch (...) { return Status::InternalError; } }
bool CanAdvance(const RegionsState& s) { return s.revision < kMax && s.sequence < kMax; }
void Advance(RegionsState& s) { ++s.revision; ++s.sequence; }
void ClearObservations(RegionsState& s) {
    s.observations={}; s.observationCount=0; s.writers={};
}
void SyncScope(RegionsState& s) {
    if(s.save && s.save!=CurrentSaveGeneration()) {
        ClearObservations(s); s.epoch=0; s.save=0;
        if(CanAdvance(s)) Advance(s);
    }
}
bool SameIdentity(const RegionSampleSource& a, const RegionSampleSource& b) {
    return a.identity == b.identity && !std::strcmp(a.provider,b.provider) && !std::strcmp(a.source,b.source);
}
bool SamePair(const RegionObservation& a, const RegionEvaluation& b) {
    return a.regionId == b.regionId && SameIdentity(a.anchor.source,b.anchor.source) && SameIdentity(a.probe.source,b.probe.source);
}
bool SamePair(const RegionEvaluation& a, const RegionEvaluation& b) {
    return a.regionId == b.regionId && SameIdentity(a.anchor.source,b.anchor.source) && SameIdentity(a.probe.source,b.probe.source);
}
bool ValidIdentity(const RegionSampleSource& s) {
    return s.identity && ValidString(s.provider,sizeof(s.provider),true) && ValidString(s.source,sizeof(s.source),true);
}
bool ValidSource(const RegionSampleSource& s) {
    return ValidIdentity(s) && s.generation && s.sampleId && s.epoch && s.saveGeneration && s.frame &&
        std::isfinite(s.timeSeconds) && s.timeSeconds >= 0 && s.units == static_cast<std::uint32_t>(RegionUnits::SkyrimWorld) && !s.reserved;
}
bool ValidView(const RegionsView* view) {
    return view && view->structSize == sizeof(*view) && view->version == kRegionsApiVersion && !view->reserved;
}
RegionsView View(const RegionsState& s, std::uint32_t count) {
    return {sizeof(RegionsView),kRegionsApiVersion,s.epoch,s.save,s.revision,s.sequence,count,0};
}
RegionEntry* Find(RegionsState& s, std::uint64_t id) {
    for(std::uint32_t i=0;i<s.regionCount;++i) if(s.regions[i].record.regionId==id) return &s.regions[i];
    return nullptr;
}
void RemoveObservations(RegionsState& s, std::uint64_t owner, std::uint64_t region = 0) {
    std::uint32_t n=0;
    for(std::uint32_t i=0;i<s.observationCount;++i)
        if((owner && s.observations[i].owner != owner) || (!owner && s.observations[i].record.regionId != region))
            s.observations[n++]=s.observations[i];
    for(auto i=n;i<s.observationCount;++i) s.observations[i]={};
    s.observationCount=n;
}
bool RemoveOwner(RegionsState& s, std::uint64_t owner) {
    bool changed=false; std::uint32_t n=0;
    for(std::uint32_t i=0;i<s.regionCount;++i) {
        if(s.regions[i].owner==owner) changed=true;
        else s.regions[n++]=s.regions[i];
    }
    for(auto i=n;i<s.regionCount;++i) s.regions[i]={};
    s.regionCount=n;
    const auto before=s.observationCount; RemoveObservations(s,owner); changed |= before!=s.observationCount;
    for(auto& w:s.writers) if(w.owner==owner) { w={}; changed=true; }
    return changed;
}
void Prune(RegionsState& s) {
    SyncScope(s);
    std::array<std::uint64_t,64> checked{}; std::uint32_t count=0;
    auto check=[&](std::uint64_t owner) {
        if(!owner || std::find(checked.begin(),checked.begin()+count,owner)!=checked.begin()+count) return;
        if(count<checked.size()) checked[count++]=owner;
        if(!RuntimeWriterLeaseExists(owner) && RemoveOwner(s,owner) && CanAdvance(s)) Advance(s);
    };
    // Removal compacts the arrays. Revisit the same offset after pruning a dead owner.
    for(std::uint32_t i=0;i<s.regionCount;) { const auto owner=s.regions[i].owner; check(owner); if(i<s.regionCount && s.regions[i].owner==owner) ++i; }
    for(std::size_t i=0;i<s.writers.size();++i) check(s.writers[i].owner);
}
RegionUnavailableReason SampleReason(const InteractionRegionSpec& spec, const RegionEvaluationFrame& frame, const RegionEvaluation& e, bool independentFrames=false) {
    if(!e.anchor.valid || !e.probe.valid) return RegionUnavailableReason::Missing;
    if(e.anchor.valid!=1 || e.probe.valid!=1 || e.anchor.reserved || e.probe.reserved[0] || e.probe.reserved[1] || e.probe.reservedTail ||
        !ValidSource(e.anchor.source) || !ValidSource(e.probe.source) || e.anchor.kind!=spec.anchorKind)
        return RegionUnavailableReason::InvalidSnapshot;
    const auto& a=e.anchor.source; const auto& p=e.probe.source;
    if(a.epoch!=frame.epoch || p.epoch!=frame.epoch || a.saveGeneration!=frame.saveGeneration || p.saveGeneration!=frame.saveGeneration)
        return RegionUnavailableReason::ScopeMismatch;
    if((!independentFrames && (a.frame!=p.frame || a.frame>frame.frame)) || p.frame>frame.frame || a.timeSeconds>frame.timeSeconds || p.timeSeconds>frame.timeSeconds ||
        std::abs(a.timeSeconds-p.timeSeconds)>spec.maxSampleSkewSeconds)
        return RegionUnavailableReason::Incoherent;
    if(frame.timeSeconds-a.timeSeconds>spec.maxSampleAgeSeconds || frame.timeSeconds-p.timeSeconds>spec.maxSampleAgeSeconds)
        return RegionUnavailableReason::Stale;
    return RegionUnavailableReason::None;
}
bool PriorInside(const RegionObservation& o) {
    return o.state==static_cast<std::uint32_t>(RegionSpatialState::Inside) || o.state==static_cast<std::uint32_t>(RegionSpatialState::Dwelling) ||
        o.state==static_cast<std::uint32_t>(RegionSpatialState::Ready);
}
bool SameGeneration(const RegionObservation& previous, const RegionEvaluation& current) {
    return previous.anchor.source.generation==current.anchor.source.generation && previous.probe.source.generation==current.probe.source.generation;
}
RegionObservation Evaluate(const InteractionRegionRecord& region, const RegionEvaluationFrame& frame, const RegionEvaluation& evaluation,
    const RegionObservation* previous, std::uint64_t sequence, bool independentFrames=false) {
    RegionObservation out{}; out.regionId=region.regionId; out.regionRevision=region.revision; out.sequence=sequence;
    out.evaluated=frame; out.anchor=evaluation.anchor; out.probe=evaluation.probe;
    auto unavailable=SampleReason(region.spec,frame,evaluation,independentFrames);
    const double distance=ZonePointSignedDistance(region.spec.shape,evaluation.anchor.world,evaluation.probe.world);
    if(unavailable==RegionUnavailableReason::None && !std::isfinite(distance)) unavailable=RegionUnavailableReason::InvalidSnapshot;
    const bool sameGeneration=previous && SameGeneration(*previous,evaluation);
    const bool sameProbe=independentFrames && sameGeneration && SameIdentity(previous->probe.source,evaluation.probe.source) &&
        evaluation.probe.source.sampleId==previous->probe.source.sampleId && evaluation.probe.source.frame==previous->probe.source.frame &&
        evaluation.probe.source.timeSeconds==previous->probe.source.timeSeconds &&
        std::equal(evaluation.probe.world,evaluation.probe.world+3,previous->probe.world);
    if(unavailable==RegionUnavailableReason::None && sameGeneration &&
        ((independentFrames ? evaluation.anchor.source.sampleId<previous->anchor.source.sampleId : evaluation.anchor.source.sampleId<=previous->anchor.source.sampleId) || (!sameProbe && evaluation.probe.source.sampleId<=previous->probe.source.sampleId) ||
         (independentFrames ? evaluation.anchor.source.frame<previous->anchor.source.frame : evaluation.anchor.source.frame<=previous->anchor.source.frame) || (!sameProbe && evaluation.probe.source.frame<=previous->probe.source.frame) ||
         evaluation.anchor.source.timeSeconds<previous->anchor.source.timeSeconds || evaluation.probe.source.timeSeconds<previous->probe.source.timeSeconds))
        unavailable=RegionUnavailableReason::RepeatedSample;
    out.unavailableReason=static_cast<std::uint32_t>(unavailable);
    if(unavailable!=RegionUnavailableReason::None) return out;
    const bool continuous=previous && sameGeneration && previous->regionRevision==region.revision && PriorInside(*previous) &&
        previous->evaluated.frame<kMax && (sameProbe ? frame.frame==previous->evaluated.frame : frame.frame==previous->evaluated.frame+1) && frame.timeSeconds>=previous->evaluated.timeSeconds &&
        frame.timeSeconds-previous->evaluated.timeSeconds<=region.spec.maxContinuityGapSeconds &&
        (independentFrames ? evaluation.anchor.source.frame>=previous->anchor.source.frame : evaluation.anchor.source.frame==previous->anchor.source.frame+1) &&
        (sameProbe || evaluation.probe.source.frame==previous->probe.source.frame+1);
    const bool inside=distance <= (continuous ? double(region.spec.exitOutset) : -double(region.spec.enterInset));
    if(!inside) { out.state=static_cast<std::uint32_t>(RegionSpatialState::Outside); return out; }
    out.enteredAtSeconds=continuous ? previous->enteredAtSeconds : frame.timeSeconds;
    out.dwellElapsedSeconds=sameProbe && continuous ? previous->dwellElapsedSeconds : frame.timeSeconds-out.enteredAtSeconds;
    out.state=static_cast<std::uint32_t>(out.dwellElapsedSeconds>=region.spec.dwellSeconds ? RegionSpatialState::Ready :
        (continuous ? RegionSpatialState::Dwelling : RegionSpatialState::Inside));
    return out;
}
Status BS_CALL Upsert(std::uint64_t writer, const InteractionRegionSpec* spec, std::uint64_t expected, InteractionRegionRecord* output) noexcept {
    return Guard([&] {
        if(!spec || !output) return Status::InvalidArgument;
        const auto copied=*spec; auto status=ValidateInteractionRegion(copied); if(status!=Status::Ok) return status;
        char provider[64]{}; status=RuntimeClientIdentity(writer,true,provider); if(status!=Status::Ok) return status;
        if(std::strcmp(provider,copied.provider)) return Status::Unauthorized;
        auto& s=State(); std::lock_guard lock(s.mutex); Prune(s);
        if(RuntimeClientIdentity(writer,true,nullptr)!=Status::Ok) return Status::Unauthorized;
        RegionEntry* existing=nullptr;
        for(std::uint32_t i=0;i<s.regionCount;++i)
            if(!std::strcmp(s.regions[i].record.spec.provider,copied.provider) && !std::strcmp(s.regions[i].record.spec.key,copied.key)) existing=&s.regions[i];
        if(existing && existing->owner!=writer) return Status::Unauthorized;
        if(existing && (!expected || expected!=existing->record.revision)) return Status::Conflict;
        if(!existing && expected) return Status::NotFound;
        if(!existing && s.regionCount==kMaxInteractionRegions) return Status::LimitExceeded;
        if(!CanAdvance(s) || s.nextRegionRevision==kMax || (!existing && s.nextId==kMax)) return Status::LimitExceeded;
        if(!existing) { existing=&s.regions[s.regionCount++]; existing->owner=writer; existing->record.regionId=s.nextId++; }
        else RemoveObservations(s,0,existing->record.regionId);
        existing->record.spec=copied; existing->record.revision=s.nextRegionRevision++; Advance(s); *output=existing->record; return Status::Ok;
    });
}
Status BS_CALL Remove(std::uint64_t writer, std::uint64_t id, std::uint64_t expected) noexcept {
    return Guard([&] {
        if(!id || !expected) return Status::InvalidArgument;
        auto status=RuntimeClientIdentity(writer,true,nullptr); if(status!=Status::Ok) return status;
        auto& s=State(); std::lock_guard lock(s.mutex); Prune(s); auto* entry=Find(s,id);
        if(!entry) return Status::NotFound;
        if(entry->owner!=writer || RuntimeClientIdentity(writer,true,nullptr)!=Status::Ok) return Status::Unauthorized;
        if(entry->record.revision!=expected) return Status::Conflict;
        if(!CanAdvance(s)) return Status::LimitExceeded;
        const auto index=static_cast<std::uint32_t>(entry-s.regions.data());
        for(auto i=index+1;i<s.regionCount;++i) s.regions[i-1]=s.regions[i];
        s.regions[--s.regionCount]={}; RemoveObservations(s,0,id); Advance(s); return Status::Ok;
    });
}
template<class Record, class Entries> Status Copy(std::uint64_t reader, std::uint64_t expected, Record* records,
    std::uint32_t capacity, RegionsView* view, Entries entries, std::uint32_t limit) {
    if(!ValidView(view) || capacity>limit || (capacity && !records)) return Status::InvalidArgument;
    auto status=RuntimeClientIdentity(reader,false,nullptr); if(status!=Status::Ok) return status;
    auto& s=State(); std::lock_guard lock(s.mutex); Prune(s);
    if(RuntimeClientIdentity(reader,false,nullptr)!=Status::Ok) return Status::Unauthorized;
    const auto count=entries(s,nullptr); *view=View(s,count);
    if(expected && expected!=s.revision) return Status::Conflict;
    if(capacity<count) return Status::BufferTooSmall;
    entries(s,records); return Status::Ok;
}
Status BS_CALL CopyRegions(std::uint64_t reader, std::uint64_t expected, InteractionRegionRecord* records, std::uint32_t capacity, RegionsView* view) noexcept {
    return Guard([&] { return Copy(reader,expected,records,capacity,view,[](const RegionsState& s, InteractionRegionRecord* output) {
        if(output) for(std::uint32_t i=0;i<s.regionCount;++i) output[i]=s.regions[i].record; return s.regionCount;
    },kMaxInteractionRegions); });
}
Status BS_CALL CopyObservations(std::uint64_t reader, std::uint64_t expected, RegionObservation* records, std::uint32_t capacity, RegionsView* view) noexcept {
    return Guard([&] { return Copy(reader,expected,records,capacity,view,[](const RegionsState& s, RegionObservation* output) {
        if(output) for(std::uint32_t i=0;i<s.observationCount;++i) output[i]=s.observations[i].record; return s.observationCount;
    },kMaxRegionObservations); });
}
Status BS_CALL Publish(std::uint64_t writer, const RegionEvaluationFrame* frame, const RegionEvaluation* evaluations, std::uint32_t count, RegionsView* view) noexcept {
    return Guard([&] {
        if(!frame || !ValidView(view) || count>kMaxRegionObservations || (count && !evaluations)) return Status::InvalidArgument;
        const auto f=*frame;
        if(f.structSize!=sizeof(f) || f.reserved || !f.epoch || !f.saveGeneration || !f.frame || !std::isfinite(f.timeSeconds) || f.timeSeconds<0) return Status::InvalidArgument;
        auto status=RuntimeClientIdentity(writer,true,nullptr); if(status!=Status::Ok) return status;
        // Copy caller storage before taking the service lock; no retained pointers.
        std::array<RegionEvaluation,kMaxRegionObservations> input{};
        for(std::uint32_t i=0;i<count;++i) {
            input[i]=evaluations[i]; const auto& e=input[i];
            if(e.structSize!=sizeof(e) || e.reserved || !e.regionId || !e.expectedRegionRevision || e.anchor.structSize!=sizeof(e.anchor) ||
                e.probe.structSize!=sizeof(e.probe) || !ValidIdentity(e.anchor.source) || !ValidIdentity(e.probe.source)) return Status::InvalidArgument;
            for(std::uint32_t j=0;j<i;++j) if(SamePair(e,input[j])) return Status::InvalidArgument;
        }
        auto& s=State(); std::lock_guard lock(s.mutex); Prune(s);
        if(RuntimeClientIdentity(writer,true,nullptr)!=Status::Ok) return Status::Unauthorized;
        if(!s.epoch || !s.save) return Status::Unavailable;
        if(f.epoch!=s.epoch || f.saveGeneration!=s.save) return Status::Conflict;
        std::uint32_t preserved=0;
        for(std::uint32_t i=0;i<s.observationCount;++i) if(s.observations[i].owner!=writer) ++preserved;
        if(count>kMaxRegionObservations-preserved) return Status::LimitExceeded;
        for(std::uint32_t i=0;i<count;++i) {
            auto* region=Find(s,input[i].regionId); if(!region) return Status::NotFound;
            if(region->owner!=writer) return Status::Unauthorized;
            if(region->record.revision!=input[i].expectedRegionRevision) return Status::Conflict;
        }
        WriterFrame* previousFrame=nullptr; WriterFrame* empty=nullptr;
        for(auto& w:s.writers) { if(w.owner==writer) previousFrame=&w; if(!w.owner && !empty) empty=&w; }
        if(!previousFrame && !empty) return Status::LimitExceeded;
        if(!CanAdvance(s)) return Status::LimitExceeded;
        if(previousFrame && (f.frame<=previousFrame->frame || f.timeSeconds<previousFrame->time)) {
            // A clock/frame rollback is a safety invalidation, even though the bad
            // publication fails. Keep the high-water mark; never rebase old time.
            RemoveObservations(s,writer); Advance(s); return Status::Conflict;
        }
        std::array<ObservationEntry,kMaxRegionObservations> next{}; std::uint32_t n=0;
        for(std::uint32_t i=0;i<s.observationCount;++i) if(s.observations[i].owner!=writer) next[n++]=s.observations[i];
        for(std::uint32_t i=0;i<count;++i) {
            const RegionObservation* previous=nullptr;
            for(std::uint32_t j=0;j<s.observationCount;++j)
                if(s.observations[j].owner==writer && SamePair(s.observations[j].record,input[i])) { previous=&s.observations[j].record; break; }
            next[n++]={writer,Evaluate(Find(s,input[i].regionId)->record,f,input[i],previous,s.sequence+1)};
        }
        s.observations=next; s.observationCount=n;
        *(previousFrame ? previousFrame : empty)={writer,f.frame,f.timeSeconds}; Advance(s); *view=View(s,n); return Status::Ok;
    });
}
const RegionsApi api{sizeof(RegionsApi),kRegionsApiVersion,3,Upsert,Remove,CopyRegions,Publish,CopyObservations};
} // namespace
Status ValidateInteractionRegion(const InteractionRegionSpec& s) noexcept {
    if(s.structSize!=sizeof(s) || s.anchorKind<1 || s.anchorKind>4 || !ValidString(s.provider,sizeof(s.provider),true) ||
        !ValidString(s.key,sizeof(s.key),true) || !std::isfinite(s.enterInset) || !std::isfinite(s.exitOutset) ||
        s.enterInset<0 || s.exitOutset<0 || s.enterInset>1000000 || s.exitOutset>1000000 ||
        !std::isfinite(s.dwellSeconds) || s.dwellSeconds<0 || s.dwellSeconds>3600 ||
        !std::isfinite(s.maxSampleAgeSeconds) || s.maxSampleAgeSeconds<=0 || s.maxSampleAgeSeconds>10 ||
        !std::isfinite(s.maxSampleSkewSeconds) || s.maxSampleSkewSeconds<0 || s.maxSampleSkewSeconds>s.maxSampleAgeSeconds ||
        !std::isfinite(s.maxContinuityGapSeconds) || s.maxContinuityGapSeconds<=0 || s.maxContinuityGapSeconds>10)
        return Status::InvalidArgument;
    return ValidateZoneShape(s.shape);
}
const RegionsApi& GetRegionsApi() noexcept { return api; }
Status CopyOwnedInteractionRegion(std::uint64_t writer, std::uint64_t id, std::uint64_t expected,
    InteractionRegionRecord& output) noexcept {
    return Guard([&] {
        auto& s=State(); std::lock_guard lock(s.mutex); Prune(s);
        if(RuntimeClientIdentity(writer,true,nullptr)!=Status::Ok) return Status::Unauthorized;
        auto* entry=Find(s,id); if(!entry) return Status::NotFound;
        if(entry->owner!=writer) return Status::Unauthorized;
        if(entry->record.revision!=expected) return Status::Conflict;
        output=entry->record; return Status::Ok;
    });
}
RegionObservation EvaluateInteractionRegion(const InteractionRegionRecord& region, const RegionEvaluationFrame& frame,
    const RegionEvaluation& evaluation, const RegionObservation* previous, std::uint64_t sequence, bool independentFrames) noexcept {
    return Evaluate(region,frame,evaluation,previous,sequence,independentFrames);
}
void SetInteractionRegionsScope(std::uint64_t epoch, std::uint64_t save) noexcept {
    try { auto& s=State(); std::lock_guard lock(s.mutex);
        SyncScope(s);
        if(epoch && save && save!=CurrentSaveGeneration()) return;
        if(s.epoch==epoch && s.save==save) return;
        ClearObservations(s); s.epoch=epoch && save ? epoch : 0; s.save=epoch && save ? save : 0;
        if(CanAdvance(s)) Advance(s);
    } catch(...) {}
}
void InvalidateInteractionRegions() noexcept { SetInteractionRegionsScope(0,0); }
void ReleaseInteractionRegionsClient(std::uint64_t client) noexcept {
    try { if(!client) return; auto& s=State(); std::lock_guard lock(s.mutex); if(RemoveOwner(s,client) && CanAdvance(s)) Advance(s); } catch(...) {}
}
} // namespace body_slots::detail
