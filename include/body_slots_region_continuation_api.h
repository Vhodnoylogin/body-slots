#pragma once
#include "body_slots_api.h"

namespace body_slots {
enum class RegionInputEnd : std::uint32_t { Released = 1, Cancelled = 2, ObservationLost = 3 };
// Main-phase notification to the original winner. No field is a mutation permit.
struct RegionInputTerminal {
    std::uint32_t structSize, end, hand, button;
    std::uint64_t binding, epoch, pressInput, terminalInput, request, action, correlation;
    std::uint32_t providerReason, reserved;
};
using RegionInputEnded = void(BS_CALL*)(void* bindingContext, const RegionInputTerminal*);
struct RegionContinuationApi {
    std::uint32_t structSize, version;
    // Uses the binding's registered context. Set once, before the first press.
    // RemoveBinding / ReleaseClient also drain in-flight notifications.
    Status(BS_CALL* Subscribe)(std::uint64_t writer, std::uint64_t binding, RegionInputEnded);
};
using QueryRegionContinuationInterface = const RegionContinuationApi*(BS_CALL*)(std::uint32_t, std::uint32_t);
static_assert(sizeof(RegionInputTerminal) == 80 && sizeof(RegionContinuationApi) == 16);
} // namespace body_slots
