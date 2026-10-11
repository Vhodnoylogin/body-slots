#include "configuration.h"
#include "validation.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace body_slots::detail {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "Save floats require IEEE binary32");
Status ValidateCustomization(const Customization& c) noexcept {
    if (c.structSize != sizeof(Customization) || !c.fieldMask || (c.fieldMask & ~AllCustomizationFields) ||
        c.reserved || c.enabled > 1 || !ValidString(c.provider, sizeof(c.provider), true) ||
        !ValidString(c.key, sizeof(c.key), true)) return Status::InvalidArgument;
    const auto zero = [](const float* values, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) if (values[i] != 0.f) return false;
        return true;
    };
    if (!(c.fieldMask & CustomizePosition) && !zero(c.position, 3)) return Status::InvalidArgument;
    if (!(c.fieldMask & CustomizeRotation) && !zero(c.rotation, 9)) return Status::InvalidArgument;
    if (!(c.fieldMask & CustomizeAccessRegion) && (c.radius != 0.f || !zero(c.regionCenter, 3))) return Status::InvalidArgument;
    if (!(c.fieldMask & CustomizeEnabled) && c.enabled) return Status::InvalidArgument;
    Definition d{}; d.structSize = sizeof(d); d.flags = kEnabled;
    std::memcpy(d.provider, c.provider, sizeof(c.provider)); std::memcpy(d.key, c.key, sizeof(c.key));
    std::strcpy(d.anchor, "NPC Pelvis [Pelv]"); std::strcpy(d.referenceRig, "skyrimvr-humanoid-v1");
    std::strcpy(d.kind, "customization"); d.rotation[0] = d.rotation[4] = d.rotation[8] = 1;
    d.radius = 1; d.handMask = Both; d.contentMask = AllContent;
    if (c.fieldMask & CustomizePosition) std::memcpy(d.position, c.position, sizeof(c.position));
    if (c.fieldMask & CustomizeRotation) std::memcpy(d.rotation, c.rotation, sizeof(c.rotation));
    if (c.fieldMask & CustomizeAccessRegion) { d.radius = c.radius; std::memcpy(d.regionCenter, c.regionCenter, sizeof(c.regionCenter)); }
    return ValidateDefinition(d);
}
namespace {
constexpr std::uint32_t kSaveMagic = 0x46435342; // BSCF, explicit little-endian encoding
void U32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void Floats(std::vector<std::uint8_t>& bytes, const float* values, unsigned count) {
    for (unsigned i = 0; i < count; ++i) { std::uint32_t bits; std::memcpy(&bits, &values[i], 4); U32(bytes, bits); }
}
struct Reader {
    const std::uint8_t* cursor;
    std::uint32_t U32() noexcept { std::uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(*cursor++) << (8 * i); return value; }
    void Chars(char* out, unsigned count) noexcept { std::memcpy(out, cursor, count); cursor += count; }
    void Floats(float* out, unsigned count) noexcept { for (unsigned i = 0; i < count; ++i) { auto bits = U32(); std::memcpy(&out[i], &bits, 4); } }
};
bool Duplicate(const CustomizationStore& store, std::uint32_t index) noexcept {
    for (std::uint32_t j = 0; j < index; ++j)
        if (!std::strcmp(store.entries[j].provider, store.entries[index].provider) &&
            !std::strcmp(store.entries[j].key, store.entries[index].key)) return true;
    return false;
}
}
Status EncodeCustomizations(const CustomizationStore& store, std::vector<std::uint8_t>& output) noexcept {
    try {
        if (store.count > kMaxCustomizations) return Status::LimitExceeded;
        for (std::uint32_t i = 0; i < store.count; ++i) {
            auto status = ValidateCustomization(store.entries[i]); if (status != Status::Ok) return status;
            if (Duplicate(store, i)) return Status::Conflict;
        }
        std::vector<std::uint8_t> bytes; bytes.reserve(kCustomizationSaveHeaderBytes + store.count * kCustomizationSaveRecordBytes);
        U32(bytes, kSaveMagic); U32(bytes, kCustomizationSaveVersion); U32(bytes, kCustomizationSaveRecordBytes); U32(bytes, store.count);
        for (std::uint32_t i = 0; i < store.count; ++i) {
            const auto& c = store.entries[i]; U32(bytes, c.structSize); U32(bytes, c.fieldMask);
            bytes.insert(bytes.end(), c.provider, c.provider + sizeof(c.provider)); bytes.insert(bytes.end(), c.key, c.key + sizeof(c.key));
            Floats(bytes, c.position, 3); Floats(bytes, c.rotation, 9); Floats(bytes, &c.radius, 1); Floats(bytes, c.regionCenter, 3);
            U32(bytes, c.enabled); U32(bytes, c.reserved);
        }
        output.swap(bytes); return Status::Ok;
    } catch (...) { return Status::InternalError; }
}
Status DecodeCustomizations(const std::uint8_t* bytes, std::uint32_t size, CustomizationStore& output) noexcept {
    try {
        if (!bytes || size < kCustomizationSaveHeaderBytes || size > kMaxCustomizationSaveBytes) return Status::InvalidArgument;
        Reader reader{bytes}; if (reader.U32() != kSaveMagic) return Status::InvalidArgument;
        if (reader.U32() != kCustomizationSaveVersion || reader.U32() != kCustomizationSaveRecordBytes) return Status::UnsupportedVersion;
        const auto count = reader.U32(); if (count > kMaxCustomizations) return Status::LimitExceeded;
        if (size != kCustomizationSaveHeaderBytes + count * kCustomizationSaveRecordBytes) return Status::InvalidArgument;
        CustomizationStore candidate{}; candidate.count = count;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& c = candidate.entries[i]; c.structSize = reader.U32(); c.fieldMask = reader.U32();
            reader.Chars(c.provider, sizeof(c.provider)); reader.Chars(c.key, sizeof(c.key));
            reader.Floats(c.position, 3); reader.Floats(c.rotation, 9); reader.Floats(&c.radius, 1); reader.Floats(c.regionCenter, 3);
            c.enabled = reader.U32(); c.reserved = reader.U32();
            auto status = ValidateCustomization(c); if (status != Status::Ok) return status;
            if (Duplicate(candidate, i)) return Status::Conflict;
        }
        output = candidate; return Status::Ok;
    } catch (...) { return Status::InternalError; }
}
} // namespace body_slots::detail
