#include "package.h"
#include "validation.h"
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace body_slots::detail {
static_assert(sizeof(float) == 4, "Package requires IEEE-754 binary32");
static_assert(offsetof(Definition, provider) == 8 && offsetof(Definition, position) == 328 &&
    offsetof(Definition, radius) == 376 && offsetof(Definition, handMask) == 392, "Package field offsets changed");
std::uint32_t U32(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
float Float(const unsigned char* bytes) noexcept {
    const auto bits = U32(bytes); float result; std::memcpy(&result, &bits, 4); return result;
}
void Decode(const unsigned char* p, Definition& d) noexcept {
    d = {}; d.structSize = U32(p); d.flags = U32(p + 4);
    std::memcpy(d.provider, p + 8, 64); std::memcpy(d.key, p + 72, 64);
    std::memcpy(d.anchor, p + 136, 96); std::memcpy(d.referenceRig, p + 232, 64); std::memcpy(d.kind, p + 296, 32);
    for (int i = 0; i < 3; ++i) d.position[i] = Float(p + 328 + i * 4);
    for (int i = 0; i < 9; ++i) d.rotation[i] = Float(p + 340 + i * 4);
    d.radius = Float(p + 376);
    for (int i = 0; i < 3; ++i) d.regionCenter[i] = Float(p + 380 + i * 4);
    d.handMask = U32(p + 392); d.contentMask = U32(p + 396); d.scaleMode = U32(p + 400); d.reserved = U32(p + 404);
}
Status ReadPackage(const std::filesystem::path& path, Package& output, std::string& diagnostic) noexcept {
    try {
        auto fail = [&](Status status, const char* reason) { diagnostic = reason; return status; };
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return fail(Status::Unavailable, "cannot open package");
        const auto end = stream.tellg();
        if (end < kPackageHeaderSize || end > kMaxPackageBytes) return fail(Status::InvalidArgument, "package size out of bounds");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(end)); stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return fail(Status::InvalidArgument, "truncated package");
        // Reject a file that grew between sizing and reading, too.
        if (stream.peek() != std::ifstream::traits_type::eof()) return fail(Status::InvalidArgument, "trailing bytes");
        const auto* p = bytes.data();
        const unsigned char magic[8]{'B','S','L','O','T','S','\r','\n'};
        if (std::memcmp(p, magic, 8)) return fail(Status::InvalidArgument, "wrong magic");
        if (U32(p + 8) != kPackageVersion) return fail(Status::UnsupportedVersion, "unsupported package version");
        const auto count = U32(p + 20);
        if (U32(p + 12) != kPackageHeaderSize || U32(p + 16) != sizeof(Definition) || U32(p + 28))
            return fail(Status::InvalidArgument, "invalid sizes or flags");
        if (count > kMaxDefinitions) return fail(Status::LimitExceeded, "too many definitions");
        const auto payload = count * static_cast<std::uint32_t>(sizeof(Definition));
        if (U32(p + 24) != payload || bytes.size() != kPackageHeaderSize + payload)
            return fail(Status::InvalidArgument, "payload length mismatch");
        for (std::uint32_t i = 96; i < kPackageHeaderSize; ++i) if (p[i]) return fail(Status::InvalidArgument, "reserved header bytes nonzero");
        auto storage = std::make_unique<Package>(); auto& candidate = *storage;
        candidate.count = count; std::memcpy(candidate.provider, p + 32, 64);
        if (!ValidString(candidate.provider, 64, true)) return fail(Status::InvalidArgument, "invalid provider namespace");
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& d = candidate.definitions[i]; Decode(p + kPackageHeaderSize + i * sizeof(Definition), d);
            const auto status = ValidateDefinition(d);
            if (status != Status::Ok) return fail(status, "invalid definition");
            if (std::strcmp(d.provider, candidate.provider)) return fail(Status::Unauthorized, "record provider differs from package");
            for (std::uint32_t j = 0; j < i; ++j)
                if (!std::strcmp(d.key, candidate.definitions[j].key)) return fail(Status::Conflict, "duplicate slot key");
        }
        output = candidate; diagnostic = "validated"; return Status::Ok;
    } catch (...) { return Status::InternalError; }
}
Status PublishPackage(const Api& api, const Package& package, std::uint64_t* output) noexcept {
    try {
        if (!output || !api.RegisterClient || !api.ReleaseClient || !api.ReplaceDefinitions ||
            !ValidString(package.provider, 64, true)) return Status::InvalidArgument;
        if (package.count > kMaxDefinitions) return Status::LimitExceeded;
        // Validate before registration, including caller-constructed Package instances.
        for (std::uint32_t i = 0; i < package.count; ++i) {
            const auto status = ValidateDefinition(package.definitions[i]); if (status != Status::Ok) return status;
            if (std::strcmp(package.provider, package.definitions[i].provider)) return Status::Unauthorized;
            for (std::uint32_t j = 0; j < i; ++j)
                if (!std::strcmp(package.definitions[j].key, package.definitions[i].key)) return Status::Conflict;
        }
        std::uint64_t token = 0; const auto registered = api.RegisterClient(package.provider, &token);
        if (registered != Status::Ok) return registered;
        const auto published = api.ReplaceDefinitions(token, package.definitions.data(), package.count, 0);
        if (published != Status::Ok) { api.ReleaseClient(token); return published; }
        *output = token; return Status::Ok;
    } catch (...) { return Status::InternalError; }
}
} // namespace body_slots::detail
