#pragma once
#include "body_slots_api.h"

namespace body_slots::detail {
enum class RuntimeDomain : std::uint8_t { Registry = 0, NativePlacement = 1, NativePolicy = 2, Instance = 3 };
constexpr std::uint64_t kRuntimeCounterLimit = 0x00ffffffffffffffULL;
struct RuntimeTokenCandidate { Status status; std::uint64_t token; std::uint64_t nextCounter; };

// A pure allocation decision prevents accidental cross-service handle aliases.
// It is not authentication: native plugins still cooperate in one address space.
constexpr RuntimeTokenCandidate NextRuntimeToken(std::uint64_t counter, RuntimeDomain domain) noexcept {
    const auto tag = static_cast<std::uint8_t>(domain);
    if (tag > static_cast<std::uint8_t>(RuntimeDomain::Instance)) return {Status::InvalidArgument, 0, 0};
    if (!counter || counter > kRuntimeCounterLimit) return {Status::LimitExceeded, 0, 0};
    return {Status::Ok, (std::uint64_t(tag) << 56) | counter, counter + 1};
}
} // namespace body_slots::detail
