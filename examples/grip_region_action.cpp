#include "grip_region_action.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace body_slots::examples {
namespace {
bool ValidName(const char (&name)[64]) {
    return name[0] && std::memchr(name, 0, sizeof(name));
}
bool ValidSource(const RegionSampleSource& source) {
    return source.identity && source.generation && ValidName(source.provider) && ValidName(source.source);
}
bool SameSource(const RegionSampleSource& actual, const RegionSampleSource& expected,
                const RegionEvaluationFrame& frame) {
    return actual.identity == expected.identity && actual.generation == expected.generation &&
           std::strncmp(actual.provider, expected.provider, sizeof(actual.provider)) == 0 &&
           std::strncmp(actual.source, expected.source, sizeof(actual.source)) == 0 &&
           actual.epoch == frame.epoch && actual.saveGeneration == frame.saveGeneration &&
           actual.sampleId && actual.frame && actual.units == static_cast<std::uint32_t>(RegionUnits::SkyrimWorld) &&
           std::isfinite(actual.timeSeconds) && actual.timeSeconds >= 0 && actual.timeSeconds <= frame.timeSeconds;
}
}

GripRegionAction::GripRegionAction(GripRegionTarget target, Clock clock) noexcept
    : target_(target), clock_(clock) {}

GripRegionAction::~GripRegionAction() { Disconnect(); }

std::uint32_t GripRegionAction::Connect(const HiggsGripActions001* api, const char* providerId,
                                        bool fullRouteActive) {
    if (token_ || fullRouteActive) return HIGGS_GRIP_CONFLICT;
    if (!api || !api->Register || !api->Remove || !clock_ || !target_.regionId || !target_.regionRevision ||
        !target_.epoch || !target_.saveGeneration || (target_.hand != Left && target_.hand != Right) ||
        !ValidSource(target_.anchor) || !ValidSource(target_.probe) ||
        !std::isfinite(target_.maxAgeSeconds) || target_.maxAgeSeconds <= 0)
        return HIGGS_GRIP_INVALID;
    const auto status = api->Register(providerId, Offer, Selected, this, &token_);
    if (status == HIGGS_GRIP_OK) api_ = api;
    return status;
}

void GripRegionAction::Invalidate() noexcept { window_.store(0); }

void GripRegionAction::Disconnect() {
    Invalidate();
    if (!token_) return;
    while (api_->Remove(token_) == HIGGS_GRIP_BUSY) std::this_thread::yield();
    token_ = 0;
    api_ = nullptr;
}

bool GripRegionAction::Matches(const RegionObservation& row) const noexcept {
    const auto& frame = row.evaluated;
    return std::isfinite(frame.timeSeconds) && std::isfinite(row.enteredAtSeconds) &&
           0 <= row.enteredAtSeconds && row.enteredAtSeconds <= frame.timeSeconds &&
           row.state == static_cast<std::uint32_t>(RegionSpatialState::Ready) &&
           row.unavailableReason == static_cast<std::uint32_t>(RegionUnavailableReason::None) &&
           row.anchor.valid == 1 && row.probe.valid == 1 &&
           SameSource(row.anchor.source, target_.anchor, frame) && SameSource(row.probe.source, target_.probe, frame);
}

bool GripRegionAction::Update(Evaluate evaluate, void* context) {
    if (!token_ || !evaluate) { Invalidate(); return false; }
    const auto initial = window_.load();
    const bool pending = (initial & 1) != 0;
    const auto previousExpiry = expires_.load();
    if (pending) Invalidate(); // claim before reading the game, never replay a selection

    RegionObservation row{};
    const bool eligible = evaluate(context, row);
    if (row.regionId != target_.regionId || row.regionRevision != target_.regionRevision ||
        row.evaluated.epoch != target_.epoch || row.evaluated.saveGeneration != target_.saveGeneration ||
        row.sequence <= lastSequence_ || row.evaluated.frame <= lastFrame_) {
        Invalidate();
        return false;
    }
    lastSequence_ = row.sequence;
    lastFrame_ = row.evaluated.frame;
    if (!eligible || !Matches(row)) { Invalidate(); return false; }
    const double now = clock_();
    const double deadline = std::min(row.anchor.source.timeSeconds, row.probe.source.timeSeconds) + target_.maxAgeSeconds;
    if (!std::isfinite(now) || !std::isfinite(deadline) || row.evaluated.timeSeconds > now || now > deadline) {
        Invalidate();
        return false;
    }
    const bool continuous = now <= previousExpiry && row.enteredAtSeconds == enteredAt_;
    if (!continuous) Invalidate(); // exit/re-entry or a later sample cannot revive an old press
    enteredAt_ = row.enteredAtSeconds;
    expires_.store(deadline);
    if (!window_.load() && nextTicket_) {
        window_.store(nextTicket_);
        nextTicket_ += 2;
    }
    return pending && continuous;
}

std::uint64_t GripRegionAction::Offer(std::uint32_t isLeft, void* context) noexcept {
    const auto& self = *static_cast<GripRegionAction*>(context);
    if (isLeft != static_cast<std::uint32_t>(self.target_.hand == Left)) return 0;
    const auto ticket = self.window_.load();
    if (!ticket || (ticket & 1) || !(self.clock_() <= self.expires_.load())) return 0;
    return ticket == self.window_.load() ? ticket : 0;
}

void GripRegionAction::Selected(std::uint32_t isLeft, std::uint64_t ticket, void* context) noexcept {
    auto& self = *static_cast<GripRegionAction*>(context);
    if (!ticket || (ticket & 1) || isLeft != static_cast<std::uint32_t>(self.target_.hand == Left) ||
        !(self.clock_() <= self.expires_.load())) return;
    const auto pending = ticket | 1;
    self.window_.compare_exchange_strong(ticket, pending);
}
} // namespace body_slots::examples
