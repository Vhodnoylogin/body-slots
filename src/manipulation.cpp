#include "manipulation.h"
#include "runtime.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
namespace body_slots::detail {
namespace {
bool Valid(const RuntimeTransform& t) {
    for(float x:t.position)if(!std::isfinite(x))return false;
    if(!std::isfinite(t.scale)||t.scale<=0||t.scale>1000000)return false;
    for(float x:t.rotation)if(!std::isfinite(x))return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=i;j<3;++j){double dot=0;for(unsigned k=0;k<3;++k)dot+=double(t.rotation[i*3+k])*t.rotation[j*3+k];if(std::abs(dot-(i==j?1.:0.))>.001)return false;}
    const auto* r=t.rotation;return std::abs(double(r[0])*(double(r[4])*r[8]-double(r[5])*r[7])-double(r[1])*(double(r[3])*r[8]-double(r[5])*r[6])+double(r[2])*(double(r[3])*r[7]-double(r[4])*r[6])-1)<.001;
}
struct State {
    std::mutex mutex;
    SlotEditingSnapshot editing{{sizeof(SlotEditingMode),0,Both,1},1};
    std::uint64_t owner{};
    SlotDragSnapshot drag{sizeof(SlotDragSnapshot)};
    DragStart start{};
    float position[3]{},rotation[9]{};
};
State& Store(){static State s;return s;}
template<class F>Status Guard(F&& f) noexcept{try{return f();}catch(...){return Status::InternalError;}}
void Cancel(State& s){if(s.drag.stage==1){ReleaseDragSlot(s.drag.dragId);s.drag.stage=static_cast<std::uint32_t>(SlotDragStage::Cancelled);}}
Status BS_CALL SetMode(std::uint64_t owner,const SlotEditingMode* mode,std::uint64_t expected,SlotEditingSnapshot* out) noexcept {
    return Guard([&]{
        if(!mode||!out||mode->structSize!=sizeof(*mode)||mode->enabled>1||!mode->physicalHands||(mode->physicalHands&~Both)||(mode->button!=1&&mode->button!=2))return Status::InvalidArgument;
        if(RuntimeCustomizerIdentity(owner)!=Status::Ok)return Status::Unauthorized;
        auto& s=Store();std::lock_guard lock(s.mutex);if(!expected||expected!=s.editing.revision)return Status::Conflict;
        if(s.owner&&s.owner!=owner&&RuntimeCustomizerIdentity(s.owner)==Status::Ok)return Status::Conflict;
        if(s.editing.revision==std::numeric_limits<std::uint64_t>::max())return Status::LimitExceeded;
        Cancel(s);s.owner=mode->enabled?owner:0;s.editing.mode=*mode;++s.editing.revision;*out=s.editing;return Status::Ok;
    });
}
Status BS_CALL CopyMode(std::uint64_t reader,SlotEditingSnapshot* out) noexcept {
    return Guard([&]{if(!out)return Status::InvalidArgument;if(RuntimeConfigurationReader(reader)!=Status::Ok)return Status::Unauthorized;auto& s=Store();std::lock_guard lock(s.mutex);*out=s.editing;return Status::Ok;});
}
Status BS_CALL CopyDrag(std::uint64_t reader,SlotDragSnapshot* out) noexcept {
    return Guard([&]{if(!out||out->structSize!=sizeof(*out))return Status::InvalidArgument;if(RuntimeConfigurationReader(reader)!=Status::Ok)return Status::Unauthorized;auto& s=Store();std::lock_guard lock(s.mutex);*out=s.drag;return Status::Ok;});
}
Status BS_CALL CancelDrag(std::uint64_t owner,std::uint64_t drag) noexcept {
    return Guard([&]{if(RuntimeCustomizerIdentity(owner)!=Status::Ok)return Status::Unauthorized;auto& s=Store();std::lock_guard lock(s.mutex);if(s.owner!=owner)return Status::Unauthorized;if(!drag||s.drag.dragId!=drag)return Status::NotFound;Cancel(s);return Status::Ok;});
}
const ManipulationApi api{sizeof(ManipulationApi),kManipulationApiVersion,3,SetMode,CopyMode,CopyDrag,CancelDrag};
}
bool ComputeDraggedPlacement(const RuntimeTransform& slot,const RuntimeTransform& initial,const RuntimeTransform& hand,const RuntimeTransform& anchor,RuntimeTransform& preview,float position[3],float rotation[9]) noexcept {
    if(!position||!rotation||!Valid(slot)||!Valid(initial)||!Valid(hand)||!Valid(anchor))return false;
    double delta[9]{};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)delta[i*3+j]+=double(hand.rotation[i*3+k])*initial.rotation[j*3+k];
    RuntimeTransform candidate{};candidate.scale=anchor.scale;
    for(unsigned i=0;i<3;++i){double value=hand.position[i];for(unsigned j=0;j<3;++j)value+=delta[i*3+j]*(double(slot.position[j])-initial.position[j]);candidate.position[i]=static_cast<float>(value);for(unsigned j=0;j<3;++j){double r=0;for(unsigned k=0;k<3;++k)r+=delta[i*3+k]*slot.rotation[k*3+j];candidate.rotation[i*3+j]=static_cast<float>(r);}}
    if(!Valid(candidate))return false;float local[3]{},localR[9]{};
    for(unsigned i=0;i<3;++i){double value=0;for(unsigned j=0;j<3;++j)value+=(double(candidate.position[j])-anchor.position[j])*anchor.rotation[j*3+i]/anchor.scale;local[i]=static_cast<float>(value);if(!std::isfinite(local[i])||std::abs(local[i])>1000000)return false;for(unsigned j=0;j<3;++j){double r=0;for(unsigned k=0;k<3;++k)r+=double(anchor.rotation[k*3+i])*candidate.rotation[k*3+j];localR[i*3+j]=static_cast<float>(r);}}
    preview=candidate;std::copy(local,local+3,position);std::copy(localR,localR+9,rotation);return true;
}
bool ReadEditingMode(SlotEditingSnapshot& out,std::uint64_t* owner) noexcept {
    try{auto& s=Store();std::lock_guard lock(s.mutex);if(!s.owner||!s.editing.mode.enabled||RuntimeCustomizerIdentity(s.owner)!=Status::Ok)return false;out=s.editing;if(owner)*owner=s.owner;return true;}catch(...){return false;}
}
Status BeginAuthorizedDrag(const DragStart& start) noexcept {
    return Guard([&]{auto& s=Store();std::lock_guard lock(s.mutex);
        if(!start.requestId||!start.epoch||!start.frame||!start.saveGeneration||!start.viewRevision||!start.bodyGeneration||!start.anchorGeneration||start.reserved||(start.physicalHand!=Left&&start.physicalHand!=Right)||start.slot.availability||!(start.slot.insideHands&start.physicalHand))return Status::InvalidArgument;
        if(!s.owner||RuntimeCustomizerIdentity(s.owner)!=Status::Ok)return Status::Unauthorized;
        if(!s.editing.mode.enabled||s.editing.revision!=start.editingRevision||!(s.editing.mode.physicalHands&start.physicalHand)||s.drag.stage==1)return Status::Conflict;
        RuntimeTransform preview{};float position[3],rotation[9];if(!ComputeDraggedPlacement(start.slot.world,start.handWorld,start.handWorld,start.anchorWorld,preview,position,rotation))return Status::InvalidArgument;
        const auto reserved=ReserveDragSlot(start.slot.configured.instance.instanceHandle,start.requestId,start.epoch,start.slot.runtimeRevision);if(reserved!=Status::Ok)return reserved;
        s.start=start;std::copy(position,position+3,s.position);std::copy(rotation,rotation+9,s.rotation);s.drag={sizeof(SlotDragSnapshot),1,start.physicalHand,0,start.requestId,start.slot.configured.instance.instanceHandle,start.epoch,start.frame,start.editingRevision,preview};return Status::Ok;
    });
}
Status AdvanceAuthorizedDrag(std::uint64_t request,std::uint64_t frame,const RuntimeTransform& hand,const RuntimeTransform& anchor) noexcept {
    return Guard([&]{auto& s=Store();std::lock_guard lock(s.mutex);if(s.drag.stage!=1||s.drag.dragId!=request||frame<s.drag.frame)return Status::Conflict;if(!s.owner||RuntimeCustomizerIdentity(s.owner)!=Status::Ok){Cancel(s);return Status::Unauthorized;}RuntimeTransform preview{};float position[3],rotation[9];if(!ComputeDraggedPlacement(s.start.slot.world,s.start.handWorld,hand,anchor,preview,position,rotation)){Cancel(s);return Status::InvalidArgument;}s.drag.frame=frame;s.drag.provisionalWorld=preview;std::copy(position,position+3,s.position);std::copy(rotation,rotation+9,s.rotation);return Status::Ok;});
}
Status FinishAuthorizedDrag(std::uint64_t request,bool commit) noexcept {
    return Guard([&]{auto& s=Store();std::lock_guard lock(s.mutex);if(s.drag.dragId!=request||s.drag.stage!=1)return Status::Conflict;if(!commit){Cancel(s);return Status::Ok;}const auto status=ApplyDraggedPlacement(s.owner,s.drag.instanceHandle,s.start.saveGeneration,s.start.viewRevision,s.position,s.rotation);ReleaseDragSlot(request);s.drag.stage=static_cast<std::uint32_t>(status==Status::Ok?SlotDragStage::Committed:SlotDragStage::Rejected);s.drag.failure=static_cast<std::uint32_t>(status);return status;});
}
bool ApplyDragPreview(std::uint64_t handle,RuntimeTransform& out) noexcept {
    try{auto& s=Store();std::lock_guard lock(s.mutex);if(s.drag.stage!=1||s.drag.instanceHandle!=handle)return false;out=s.drag.provisionalWorld;return true;}catch(...){return false;}
}
bool IsSlotBeingDragged(std::uint64_t handle) noexcept {try{auto& s=Store();std::lock_guard lock(s.mutex);return s.drag.stage==1&&s.drag.instanceHandle==handle;}catch(...){return true;}}
void InvalidateDrag() noexcept {try{auto& s=Store();std::lock_guard lock(s.mutex);Cancel(s);}catch(...) {}}
const ManipulationApi& GetManipulationApi() noexcept{return api;}
} // namespace body_slots::detail
extern "C" __declspec(dllexport) const body_slots::ManipulationApi* BS_CALL BodySlots_QueryManipulationInterface(std::uint32_t version,std::uint32_t size) noexcept {
    return version==body_slots::kManipulationApiVersion&&size==sizeof(body_slots::ManipulationApi)?&body_slots::detail::GetManipulationApi():nullptr;
}
