#pragma once
#include "body_slots_instance_configuration_api.h"
#include <array>
#include <vector>

namespace body_slots::detail {
constexpr std::uint32_t kInstanceCustomizationSaveVersion = 1;
constexpr std::uint32_t kInstanceCustomizationSaveHeaderBytes = 16;
constexpr std::uint32_t kInstanceCustomizationSaveRecordBytes = 400;
constexpr std::uint32_t kMaxInstanceCustomizationSaveBytes = 16 + kMaxInstanceCustomizations * 400;
struct InstanceCustomizationData {
    std::uint32_t count = 0;
    std::array<InstanceCustomization, kMaxInstanceCustomizations> entries{};
};
Status ValidateInstanceCustomizationKey(const InstanceCustomizationKey&) noexcept;
Status ValidateInstanceCustomization(const InstanceCustomization&) noexcept;
bool SameInstanceCustomizationKey(const InstanceCustomizationKey&, const InstanceCustomizationKey&) noexcept;
// All access is under Registry's one mutex. No runtime handles, separate lock or
// definition copies live in this saved store. Only private Commit publishes state.
class InstanceCustomizationStore final {
public:
    class Candidate {
        friend class InstanceCustomizationStore;
        InstanceCustomizationData data_{};
        bool changed_ = false;
    };
    std::uint32_t Count() const noexcept { return data_.count; }
    const InstanceCustomization& At(std::uint32_t index) const noexcept { return data_.entries[index]; }
    const InstanceCustomization* Find(const InstanceCustomizationKey&) const noexcept;
    void CopyTo(InstanceCustomizationData& output) const noexcept { output = data_; }
    Status PrepareApply(const InstanceCustomization&, Candidate&, bool& changed) const noexcept;
    Status PrepareClear(const InstanceCustomizationKey&, Candidate&) const noexcept;
    Status PrepareReplace(const InstanceCustomizationData&, Candidate&) const noexcept;
    void PrepareReset(Candidate& candidate) const noexcept;
    void Apply(const Candidate& candidate) noexcept { if (candidate.changed_) Commit(candidate.data_); }
private:
    void Commit(const InstanceCustomizationData& candidate) noexcept { data_ = candidate; }
    InstanceCustomizationData data_{};
};
// Portable codecs and batch bridge leave outputs unchanged on failure.
Status EncodeInstanceCustomizations(const InstanceCustomizationData&, std::vector<std::uint8_t>&) noexcept;
Status DecodeInstanceCustomizations(const std::uint8_t*, std::uint32_t, InstanceCustomizationData&) noexcept;
Status SaveConfigurationBatch(std::vector<std::uint8_t>& definitions,
    std::vector<std::uint8_t>& instances, bool& hasInstances) noexcept;
Status LoadConfigurationBatch(const std::vector<std::uint8_t>* definitions,
    const std::vector<std::uint8_t>* instances, std::uint64_t expectedScope,
    std::uint64_t expectedView) noexcept;
} // namespace body_slots::detail
