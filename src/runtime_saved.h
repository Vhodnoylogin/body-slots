#pragma once
#include "body_slots_zone_api.h"
#include "body_slots_content_policy_api.h"
#include <array>
#include <vector>
namespace body_slots::detail {
constexpr std::uint32_t kSavedRuntimeBinding=1, kSavedRuntimeVisual=2, kSavedRuntimeShape=4, kSavedRuntimePolicy=8;
constexpr std::uint32_t kRuntimeCustomizationSaveVersion=3;
constexpr std::uint32_t kRuntimeCustomizationSaveRecordBytes=1148;
constexpr std::uint32_t kMaxRuntimeCustomizationSaveBytes=16+kMaxInstances*kRuntimeCustomizationSaveRecordBytes;
using RuntimeCustomization=RuntimeCustomizationRecord;
struct RuntimeCustomizationData {
    std::uint32_t count=0;
    std::array<RuntimeCustomization,kMaxInstances> entries{};
    std::array<ZoneShape,kMaxInstances> shapes{};
    std::array<SlotContentPolicy,kMaxInstances> policies{};
};
Status ValidateRuntimeBinding(const RuntimeBindingSpec&) noexcept;
Status ValidateRuntimeVisual(const SlotVisualSpec&) noexcept;
Status EncodeRuntimeCustomizations(const RuntimeCustomizationData&,std::vector<std::uint8_t>&) noexcept;
Status DecodeRuntimeCustomizations(const std::uint8_t*,std::uint32_t,RuntimeCustomizationData&) noexcept;
using RuntimeFormResolver=bool (*)(std::uint32_t oldId,std::uint32_t* resolved,void* context);
// Failed resolution leaves that binding explicitly dormant (kind=3), while
// unrelated geometry/visual overrides remain loadable. Output is all-or-none.
Status RemapRuntimeCustomizationForms(std::vector<std::uint8_t>&,RuntimeFormResolver,void*) noexcept;
// All data below is guarded by Registry's mutex, never the runtime mutex.
Status SetSavedRuntimeCustomization(std::uint64_t writer,std::uint64_t handle,
    const RuntimeBindingSpec*,const SlotVisualSpec*) noexcept;
Status CopyRuntimeConfigurationWithCustomizations(ConfiguredInstanceRecord*,std::uint32_t,InstanceView*,RuntimeCustomizationData*) noexcept;
Status CopySavedRuntimeCustomizations(std::uint64_t,RuntimeCustomizationRecord*,std::uint32_t,InstanceView*) noexcept;
Status ClearSavedRuntimeCustomization(std::uint64_t,const InstanceCustomizationKey*,std::uint32_t,std::uint64_t,std::uint64_t) noexcept;
Status SetSavedZoneShape(std::uint64_t,std::uint64_t,const ZoneShape*,std::uint64_t,std::uint64_t) noexcept;
Status ClearSavedZoneShape(std::uint64_t,const InstanceCustomizationKey*,std::uint64_t,std::uint64_t) noexcept;
Status CopySavedZoneShapes(std::uint64_t,SavedZoneShape*,std::uint32_t,InstanceView*) noexcept;
Status SaveConfigurationBatch(std::vector<std::uint8_t>& definitions,std::vector<std::uint8_t>& instances,
    bool& hasInstances,std::vector<std::uint8_t>& runtime,bool& hasRuntime) noexcept;
Status LoadConfigurationBatch(const std::vector<std::uint8_t>* definitions,const std::vector<std::uint8_t>* instances,
    const std::vector<std::uint8_t>* runtime,std::uint64_t scope,std::uint64_t view) noexcept;
} // namespace body_slots::detail
