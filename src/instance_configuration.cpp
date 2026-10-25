#include "instance_configuration.h"
#include "configuration.h"
#include "validation.h"
#include <algorithm>
#include <cstring>
#include <memory>

namespace body_slots::detail {
bool SameInstanceCustomizationKey(const InstanceCustomizationKey& a, const InstanceCustomizationKey& b) noexcept {
    return !std::memcmp(&a, &b, sizeof(a)); // all canonical string arrays, no padding
}
Status ValidateInstanceCustomizationKey(const InstanceCustomizationKey& key) noexcept {
    return ValidString(key.instance.ownerProvider, 64, true) && ValidString(key.instance.equipmentKey, 64, true)
        && ValidString(key.instance.slotKey, 64, true) && ValidString(key.definitionProvider, 64, true)
        && ValidString(key.definitionKey, 64, true) ? Status::Ok : Status::InvalidArgument;
}
Status ValidateInstanceCustomization(const InstanceCustomization& value) noexcept {
    if (value.structSize != sizeof(value)) return Status::UnsupportedVersion;
    const auto keyStatus = ValidateInstanceCustomizationKey(value.key); if (keyStatus != Status::Ok) return keyStatus;
    Customization geometry{}; geometry.structSize = sizeof(geometry); geometry.fieldMask = value.fieldMask;
    std::memcpy(geometry.provider, value.key.definitionProvider, 64); std::memcpy(geometry.key, value.key.definitionKey, 64);
    std::memcpy(geometry.position, value.position, sizeof(value.position));
    std::memcpy(geometry.rotation, value.rotation, sizeof(value.rotation));
    geometry.radius = value.radius; std::memcpy(geometry.regionCenter, value.regionCenter, sizeof(value.regionCenter));
    geometry.enabled = value.enabled; geometry.reserved = value.reserved;
    return ValidateCustomization(geometry);
}
namespace {
void Sort(InstanceCustomizationData& data) noexcept {
    std::sort(data.entries.begin(), data.entries.begin() + data.count, [](const auto& a, const auto& b) {
        return std::memcmp(&a.key, &b.key, sizeof(a.key)) < 0;
    });
}
Status ValidateData(const InstanceCustomizationData& data) noexcept {
    if (data.count > kMaxInstanceCustomizations) return Status::LimitExceeded;
    for (std::uint32_t index = 0; index < data.count; ++index) {
        const auto status = ValidateInstanceCustomization(data.entries[index]); if (status != Status::Ok) return status;
        for (std::uint32_t prior = 0; prior < index; ++prior)
            if (SameInstanceCustomizationKey(data.entries[index].key, data.entries[prior].key)) return Status::Conflict;
    }
    return Status::Ok;
}
void U32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
}
void Floats(std::vector<std::uint8_t>& bytes, const float* values, unsigned count) {
    for (unsigned index = 0; index < count; ++index) { std::uint32_t bits; std::memcpy(&bits, values + index, 4); U32(bytes, bits); }
}
struct Reader {
    const std::uint8_t* cursor;
    std::uint32_t U32() noexcept { std::uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(*cursor++) << (8 * i); return value; }
    void Chars(char* output, unsigned count) noexcept { std::memcpy(output, cursor, count); cursor += count; }
    void Floats(float* output, unsigned count) noexcept { for (unsigned i = 0; i < count; ++i) { auto bits = U32(); std::memcpy(output + i, &bits, 4); } }
};
constexpr std::uint32_t kMagic = 0x49435342; // BSCI
}
const InstanceCustomization* InstanceCustomizationStore::Find(const InstanceCustomizationKey& key) const noexcept {
    for (std::uint32_t index = 0; index < data_.count; ++index)
        if (SameInstanceCustomizationKey(data_.entries[index].key, key)) return &data_.entries[index];
    return nullptr;
}
Status InstanceCustomizationStore::PrepareApply(const InstanceCustomization& value, Candidate& candidate, bool& changed) const noexcept {
    const auto validation = ValidateInstanceCustomization(value); if (validation != Status::Ok) return validation;
    const auto* previous = Find(value.key);
    changed = !previous || std::memcmp(previous, &value, sizeof(value));
    if (!changed) { candidate.changed_ = false; return Status::Ok; }
    if (!previous && data_.count == kMaxInstanceCustomizations) return Status::LimitExceeded;
    candidate.data_ = data_; candidate.changed_ = true;
    auto& next = candidate.data_;
    const auto index = previous ? static_cast<std::uint32_t>(previous - data_.entries.data()) : next.count++;
    next.entries[index] = value; Sort(next); return Status::Ok;
}
Status InstanceCustomizationStore::PrepareClear(const InstanceCustomizationKey& key, Candidate& candidate) const noexcept {
    const auto* previous = Find(key); if (!previous) return Status::NotFound;
    candidate.data_ = data_; candidate.changed_ = true;
    auto& next = candidate.data_; const auto index = static_cast<std::uint32_t>(previous - data_.entries.data());
    for (auto i = index + 1; i < next.count; ++i) next.entries[i - 1] = next.entries[i];
    next.entries[--next.count] = {}; return Status::Ok;
}
Status InstanceCustomizationStore::PrepareReplace(const InstanceCustomizationData& data, Candidate& candidate) const noexcept {
    const auto status = ValidateData(data); if (status != Status::Ok) return status;
    candidate.data_ = {}; candidate.data_.count = data.count;
    for (std::uint32_t index = 0; index < data.count; ++index) candidate.data_.entries[index] = data.entries[index];
    Sort(candidate.data_); candidate.changed_ = true; return Status::Ok;
}
void InstanceCustomizationStore::PrepareReset(Candidate& candidate) const noexcept {
    candidate.data_ = {}; candidate.changed_ = true;
}
Status EncodeInstanceCustomizations(const InstanceCustomizationData& data, std::vector<std::uint8_t>& output) noexcept {
    try {
        const auto status = ValidateData(data); if (status != Status::Ok) return status;
        auto sorted = std::make_unique<InstanceCustomizationData>(); *sorted = data; Sort(*sorted);
        std::vector<std::uint8_t> bytes; bytes.reserve(16 + data.count * 400);
        U32(bytes, kMagic); U32(bytes, 1); U32(bytes, 400); U32(bytes, data.count);
        for (std::uint32_t index = 0; index < sorted->count; ++index) {
            const auto& value = sorted->entries[index]; U32(bytes, value.structSize); U32(bytes, value.fieldMask);
            for (const auto* text : {value.key.instance.ownerProvider, value.key.instance.equipmentKey, value.key.instance.slotKey,
                value.key.definitionProvider, value.key.definitionKey}) bytes.insert(bytes.end(), text, text + 64);
            Floats(bytes, value.position, 3); Floats(bytes, value.rotation, 9); Floats(bytes, &value.radius, 1); Floats(bytes, value.regionCenter, 3);
            U32(bytes, value.enabled); U32(bytes, value.reserved);
        }
        output.swap(bytes); return Status::Ok;
    } catch (...) { return Status::InternalError; }
}
Status DecodeInstanceCustomizations(const std::uint8_t* bytes, std::uint32_t size, InstanceCustomizationData& output) noexcept {
    try {
        if (!bytes || size < 16 || size > kMaxInstanceCustomizationSaveBytes) return Status::InvalidArgument;
        Reader reader{bytes}; if (reader.U32() != kMagic) return Status::InvalidArgument;
        if (reader.U32() != 1 || reader.U32() != 400) return Status::UnsupportedVersion;
        const auto count = reader.U32(); if (count > kMaxInstanceCustomizations) return Status::LimitExceeded;
        if (size != 16 + count * 400) return Status::InvalidArgument;
        auto candidate = std::make_unique<InstanceCustomizationData>(); candidate->count = count;
        for (std::uint32_t index = 0; index < count; ++index) {
            auto& value = candidate->entries[index]; value.structSize = reader.U32(); value.fieldMask = reader.U32();
            for (auto* text : {value.key.instance.ownerProvider, value.key.instance.equipmentKey, value.key.instance.slotKey,
                value.key.definitionProvider, value.key.definitionKey}) reader.Chars(text, 64);
            reader.Floats(value.position, 3); reader.Floats(value.rotation, 9); reader.Floats(&value.radius, 1); reader.Floats(value.regionCenter, 3);
            value.enabled = reader.U32(); value.reserved = reader.U32();
        }
        const auto status = ValidateData(*candidate); if (status != Status::Ok) return status;
        Sort(*candidate); output = *candidate; return Status::Ok;
    } catch (...) { return Status::InternalError; }
}
} // namespace body_slots::detail
