#pragma once
#include "body_slots_configuration_api.h"
#include <array>
#include <vector>
namespace body_slots::detail {
constexpr std::uint32_t kCustomizationSaveVersion = 1;
constexpr std::uint32_t kCustomizationSaveHeaderBytes = 16;
constexpr std::uint32_t kCustomizationSaveRecordBytes = 208;
constexpr std::uint32_t kMaxCustomizationSaveBytes = kCustomizationSaveHeaderBytes + kMaxCustomizations * kCustomizationSaveRecordBytes;
struct CustomizationStore {
    std::uint32_t count = 0;
    std::array<Customization, kMaxCustomizations> entries{};
};
Status ValidateCustomization(const Customization&) noexcept;
// Internal helpers. Neither parsing nor encoding mutates the registry or outputs on failure.
Status EncodeCustomizations(const CustomizationStore&, std::vector<std::uint8_t>& output) noexcept;
Status DecodeCustomizations(const std::uint8_t*, std::uint32_t bytes, CustomizationStore& output) noexcept;
Status SaveCustomizations(std::vector<std::uint8_t>& output) noexcept;
Status LoadCustomizations(const std::uint8_t*, std::uint32_t bytes) noexcept;
Status ResetCustomizations() noexcept;
} // namespace body_slots::detail
