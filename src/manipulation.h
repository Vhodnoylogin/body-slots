#pragma once
#include "body_slots_manipulation_api.h"
namespace body_slots::detail {
struct DragStart {
    std::uint64_t requestId,epoch,frame,editingRevision,saveGeneration,viewRevision;
    std::uint64_t bodyGeneration,anchorGeneration;
    std::uint32_t physicalHand,reserved;
    RuntimeRecord slot;
    RuntimeTransform handWorld,anchorWorld;
};
// Trusted game adapter only. The HIGGS grant is checked by that adapter before
// begin/every frame/commit. No public route can manufacture a granted drag.
bool ReadEditingMode(SlotEditingSnapshot&,std::uint64_t* owner=nullptr) noexcept;
Status BeginAuthorizedDrag(const DragStart&) noexcept;
Status AdvanceAuthorizedDrag(std::uint64_t requestId,std::uint64_t frame,
    const RuntimeTransform& handWorld,const RuntimeTransform& anchorWorld) noexcept;
Status FinishAuthorizedDrag(std::uint64_t requestId,bool commit) noexcept;
bool ApplyDragPreview(std::uint64_t instanceHandle,RuntimeTransform&) noexcept;
bool IsSlotBeingDragged(std::uint64_t instanceHandle) noexcept;
void InvalidateDrag() noexcept;
bool ComputeDraggedPlacement(const RuntimeTransform& initialSlot,const RuntimeTransform& initialHand,
    const RuntimeTransform& currentHand,const RuntimeTransform& currentAnchor,
    RuntimeTransform& preview,float localPosition[3],float localRotation[9]) noexcept;
const ManipulationApi& GetManipulationApi() noexcept;
Status RuntimeCustomizerIdentity(std::uint64_t) noexcept;
Status RuntimeConfigurationReader(std::uint64_t) noexcept;
Status ApplyDraggedPlacement(std::uint64_t customizer,std::uint64_t instanceHandle,
    std::uint64_t saveGeneration,std::uint64_t viewRevision,const float position[3],const float rotation[9]) noexcept;
} // namespace body_slots::detail
