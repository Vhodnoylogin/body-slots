#pragma once
#include "body_slots_regions_api.h"
#include "higgsnativeregions.h"
#include <array>
#include <mutex>

namespace body_slots::game {
// One owner of copied provider state. No input polling, inventory, grants or
// engine objects. HIGGS invokes this synchronously from its native hand phase.
class HiggsNativeRegions final {
public:
    explicit HiggsNativeRegions(const Api& registry) : registry_(registry) {}
    ~HiggsNativeRegions();
    HiggsNativeRegions(const HiggsNativeRegions&) = delete;
    HiggsNativeRegions& operator=(const HiggsNativeRegions&) = delete;
    HIGGS_HandStatus Evaluate(const HIGGS_NativeRegionFrame&, HIGGS_NativeRegionResult&) noexcept;
    void Reset() noexcept;
    static HIGGS_HandStatus HIGGS_HAND_CALL Detect(const HIGGS_NativeRegionFrame*,
        HIGGS_NativeRegionResult*, void*) noexcept;
private:
    struct HandState {
        std::uint64_t writer{}, sample{}, time{}, source{}, hand{};
        std::array<InteractionRegionRecord,3> regions{};
    };
    const Api& registry_;
    std::mutex mutex_;
    std::array<HandState,2> hands_{};
    std::array<RegionObservation,kMaxRegionObservations> observations_{};
    std::uint64_t epoch_{}, barredEpoch_{};
};
// Thin Windows runtime negotiation. Absence/incompatibility is optional.
void ConnectHiggsRegions(void* module) noexcept;
void ResetHiggsRegions() noexcept;
} // namespace body_slots::game
