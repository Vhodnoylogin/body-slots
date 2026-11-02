#pragma once
#include "body_slots_runtime_api.h"
namespace body_slots {
constexpr std::uint32_t kManipulationApiVersion=1;
struct SlotEditingMode {
    std::uint32_t structSize, enabled, physicalHands, button; // HIGGS grip=1 or trigger=2
};
struct SlotEditingSnapshot {
    SlotEditingMode mode;
    std::uint64_t revision;
};
enum class SlotDragStage : std::uint32_t { None=0, Preview=1, Committed=2, Cancelled=3, Rejected=4 };
struct SlotDragSnapshot {
    std::uint32_t structSize, stage, physicalHand, failure;
    std::uint64_t dragId, instanceHandle, epoch, frame, editingRevision;
    RuntimeTransform provisionalWorld; // copy only; not saved until committed
};
struct ManipulationApi {
    std::uint32_t structSize,version;
    std::uint64_t capabilities; // bit0 explicit editing mode, bit1 grant-authorized rigid dragging
    // A live dedicated customizer token, exact nonzero revision (initial1), one
    // global edit mode. Changing/disabling it cancels a provisional drag.
    Status (BS_CALL *SetEditingMode)(std::uint64_t customizer,const SlotEditingMode*,std::uint64_t expectedRevision,SlotEditingSnapshot*);

    Status (BS_CALL *CopyEditingMode)(std::uint64_t reader,SlotEditingSnapshot*);

    Status (BS_CALL *CopyDrag)(std::uint64_t reader,SlotDragSnapshot*);

    Status (BS_CALL *CancelDrag)(std::uint64_t customizer,std::uint64_t dragId);
};
using QueryManipulationInterface=const ManipulationApi* (BS_CALL *)(std::uint32_t,std::uint32_t);
static_assert(sizeof(ManipulationApi)==48,"ManipulationApi ABI changed (x64)");
static_assert(std::is_trivially_copyable<SlotDragSnapshot>::value,"Copied ABI only");
} // namespace body_slots
// Export: BodySlots_QueryManipulationInterface(version,sizeof(ManipulationApi)).
