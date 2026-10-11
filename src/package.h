#pragma once
#include "body_slots_api.h"
#include <array>
#include <filesystem>
#include <string>

namespace body_slots::detail {
constexpr std::uint32_t kPackageVersion = 1;
constexpr std::uint32_t kPackageHeaderSize = 112;
constexpr std::uint32_t kMaxPackageBytes = kPackageHeaderSize + kMaxDefinitions * sizeof(Definition);
struct Package {
    char provider[64]{};
    std::uint32_t count = 0;
    std::array<Definition, kMaxDefinitions> definitions{};
};
// Internal C++ helper, not an extension of the public ABI. File contents are untrusted.
Status ReadPackage(const std::filesystem::path& path, Package& output, std::string& diagnostic) noexcept;
// A new package owns a new provider token. Duplicate namespaces fail, rather than
// replacing another file/API caller's definitions. Failure leaves token/output untouched.
Status PublishPackage(const Api& api, const Package& package, std::uint64_t* token) noexcept;
} // namespace body_slots::detail
