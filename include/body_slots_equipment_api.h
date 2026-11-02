#pragma once
#include "body_slots_runtime_api.h"
namespace body_slots {
constexpr std::uint32_t kEquipmentApiVersion=1,kMaxEquipmentSetSlots=32;
struct EquipmentSlotEntry {
    InstanceSpec instance;
    RuntimeBindingSpec binding; // actual worn-form gate required
    std::uint64_t expectedInstanceRevision; // zero creates only
};
struct EquipmentApi {
    std::uint32_t structSize,version;
    std::uint64_t capabilities; // bit0 atomic binding/create, bit1 activation/removal by equipment key
    // Every entry names the SAME provider-validated durable equipment key and worn
    // base form. Templates/keys/bindings are validated before one publication.
    // Bindings already customized under a stable key are preserved on restoration.
    Status (BS_CALL *BindSlotSet)(std::uint64_t writer,const EquipmentSlotEntry*,std::uint32_t count,
        std::uint64_t expectedSaveGeneration,std::uint64_t expectedViewRevision,
        InstanceRecord* committed,std::uint32_t capacity,InstanceView*);

    Status (BS_CALL *SetSlotSetActive)(std::uint64_t writer,const char* equipmentKey,std::uint32_t active,
        std::uint64_t expectedSaveGeneration,std::uint64_t expectedViewRevision,InstanceView*);

    // Runtime removal leaves saved binding/geometry/visual customizations dormant.
    Status (BS_CALL *RemoveSlotSet)(std::uint64_t writer,const char* equipmentKey,
        std::uint64_t expectedSaveGeneration,std::uint64_t expectedViewRevision,InstanceView*);
};
using QueryEquipmentInterface=const EquipmentApi* (BS_CALL *)(std::uint32_t,std::uint32_t);
static_assert(sizeof(EquipmentApi)==40,"EquipmentApi ABI changed (x64)");
} // namespace body_slots
// Export: BodySlots_QueryEquipmentInterface(version,sizeof(EquipmentApi)).
