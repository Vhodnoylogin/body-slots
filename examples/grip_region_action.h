#pragma once

#include "body_slots_regions_api.h"
#include "higgsgripactions.h"
#include <atomic>

namespace body_slots::examples {

// Bind one physical hand to one registered region in the current save.
// The two expected sources pin provider, source, identity and generation.
struct GripRegionTarget {
    std::uint64_t regionId, regionRevision, epoch, saveGeneration;
    RegionSampleSource anchor, probe;
    Hand hand;
    double maxAgeSeconds;
};

class GripRegionAction {
public:
    // Clock is monotonic, thread-safe and in the RegionEvaluationFrame time domain.
    using Clock = double (*)() noexcept;
    // On the main thread, evaluate the region again, copy its row and check the
    // consumer's current gameplay eligibility. False withdraws the offer.
    using Evaluate = bool (*)(void* context, RegionObservation& out) noexcept;

    GripRegionAction(GripRegionTarget target, Clock clock) noexcept;
    ~GripRegionAction();
    GripRegionAction(const GripRegionAction&) = delete;
    GripRegionAction& operator=(const GripRegionAction&) = delete;

    // Call lifecycle methods and Update on the main thread. Pass true if this
    // mechanic already uses the full region-action route.
    std::uint32_t Connect(const HiggsGripActions001* api, const char* providerId,
                          bool fullRouteActive);
    void Disconnect();
    void Invalidate() noexcept;

    // True once for a selected press that still qualifies. Perform the local
    // gameplay action immediately on this thread.
    bool Update(Evaluate evaluate, void* context);

private:
    static std::uint64_t Offer(std::uint32_t isLeft, void* context) noexcept;
    static void Selected(std::uint32_t isLeft, std::uint64_t ticket, void* context) noexcept;
    bool Matches(const RegionObservation& observation) const noexcept;

    const GripRegionTarget target_;
    const Clock clock_;
    const HiggsGripActions001* api_{};
    std::uint64_t token_{}, nextTicket_{2}, lastSequence_{}, lastFrame_{};
    double enteredAt_{};
    std::atomic<std::uint64_t> window_{}; // even = offered, low bit = one selected press
    std::atomic<double> expires_{};
};

} // namespace body_slots::examples
