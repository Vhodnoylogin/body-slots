#include "instances.h"
#include "runtime_token.h"
#include "validation.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace body_slots::detail {
Status ValidateInstanceSpec(const InstanceSpec& spec) noexcept {
    if (spec.structSize != sizeof(spec)) return Status::UnsupportedVersion;
    if ((spec.flags & ~kInstanceActive) || spec.reserved || !spec.definitionRevision ||
        !ValidString(spec.equipmentKey, sizeof(spec.equipmentKey), true) ||
        !ValidString(spec.slotKey, sizeof(spec.slotKey), true) ||
        !ValidString(spec.definitionProvider, sizeof(spec.definitionProvider), true) ||
        !ValidString(spec.definitionKey, sizeof(spec.definitionKey), true)) return Status::InvalidArgument;
    if (spec.binding != static_cast<std::uint32_t>(InstanceBinding::BodyAnchor)) return Status::UnsupportedVersion;
    return Status::Ok;
}
bool InstanceStore::SameKey(const Entry& entry, const char* provider, const InstanceSpec& spec) noexcept {
    return !std::strcmp(entry.provider, provider) && !std::strcmp(entry.spec.equipmentKey, spec.equipmentKey)
        && !std::strcmp(entry.spec.slotKey, spec.slotKey);
}
void InstanceStore::Sort(State& state) noexcept {
    std::sort(state.entries.begin(), state.entries.begin() + state.count, [](const Entry& a, const Entry& b) {
        const auto owner = std::strcmp(a.provider, b.provider);
        if (owner) return owner < 0;
        const auto equipment = std::strcmp(a.spec.equipmentKey, b.spec.equipmentKey);
        return equipment ? equipment < 0 : std::strcmp(a.spec.slotKey, b.spec.slotKey) < 0;
    });
}
const InstanceStore::Entry* InstanceStore::Find(std::uint64_t handle) const noexcept {
    for (std::uint32_t index = 0; index < state_.count; ++index)
        if (state_.entries[index].handle == handle) return &state_.entries[index];
    return nullptr;
}
Status InstanceStore::PrepareInvalidation(Candidate& candidate) const noexcept {
    if (state_.view == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
    candidate.state_ = state_; ++candidate.state_.view; candidate.changed_ = true;
    return Status::Ok;
}
Status InstanceStore::PrepareUpsert(std::uint64_t owner, const char* provider, const InstanceSpec& spec,
    std::uint64_t expectedGeneration, std::uint64_t expectedRevision,
    Candidate& candidate, std::uint64_t& handle) const noexcept {
    if (!expectedGeneration || expectedGeneration != state_.generation) return Status::Conflict;
    std::uint32_t index = 0;
    while (index < state_.count && !SameKey(state_.entries[index], provider, spec)) ++index;
    const bool create = index == state_.count;
    if (create && expectedRevision) return Status::NotFound;
    if (!create) {
        const auto& old = state_.entries[index];
        if (old.owner != owner) return Status::Unauthorized;
        if (!expectedRevision || old.revision != expectedRevision) return Status::Conflict;
        if (std::strcmp(old.spec.definitionProvider, spec.definitionProvider) ||
            std::strcmp(old.spec.definitionKey, spec.definitionKey) || old.spec.binding != spec.binding) return Status::Conflict;
        if (!std::memcmp(&old.spec, &spec, sizeof(spec))) {
            candidate.changed_ = false; handle = old.handle; return Status::Ok;
        }
    }
    if (create && state_.count == kMaxInstances) return Status::LimitExceeded;
    if (state_.nextRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
    const auto allocation = NextRuntimeToken(state_.nextHandle, RuntimeDomain::Instance);
    if (create && allocation.status != Status::Ok) return allocation.status;
    const auto status = PrepareInvalidation(candidate); if (status != Status::Ok) return status;
    auto& next = candidate.state_;
    Entry entry{}; entry.owner = owner; std::memcpy(entry.provider, provider, sizeof(entry.provider)); entry.spec = spec;
    entry.handle = create ? allocation.token : state_.entries[index].handle;
    entry.revision = next.nextRevision++;
    next.entries[index] = entry;
    if (create) { ++next.count; next.nextHandle = allocation.nextCounter; }
    Sort(next); handle = entry.handle;
    return Status::Ok;
}
Status InstanceStore::PrepareRemove(std::uint64_t owner, std::uint64_t handle,
    std::uint64_t expectedGeneration, std::uint64_t expectedRevision, Candidate& candidate) const noexcept {
    if (!expectedGeneration || expectedGeneration != state_.generation) return Status::Conflict;
    const auto* entry = Find(handle); if (!entry) return Status::NotFound;
    if (entry->owner != owner) return Status::Unauthorized;
    if (!expectedRevision || entry->revision != expectedRevision) return Status::Conflict;
    const auto status = PrepareInvalidation(candidate); if (status != Status::Ok) return status;
    auto& next = candidate.state_;
    const auto index = static_cast<std::uint32_t>(entry - state_.entries.data());
    for (auto i = index + 1; i < next.count; ++i) next.entries[i - 1] = next.entries[i];
    next.entries[--next.count] = {};
    return Status::Ok;
}
Status InstanceStore::PrepareRelease(std::uint64_t owner, bool definitionsChanged, Candidate& candidate) const noexcept {
    bool owns = false;
    for (std::uint32_t index = 0; index < state_.count; ++index) if (state_.entries[index].owner == owner) owns = true;
    if (!owns && !definitionsChanged) { candidate.changed_ = false; return Status::Ok; }
    const auto status = PrepareInvalidation(candidate); if (status != Status::Ok) return status;
    auto& next = candidate.state_; std::uint32_t out = 0;
    for (std::uint32_t index = 0; index < state_.count; ++index)
        if (state_.entries[index].owner != owner) next.entries[out++] = state_.entries[index];
    for (auto index = out; index < next.count; ++index) next.entries[index] = {};
    next.count = out;
    return Status::Ok;
}
Status InstanceStore::PrepareReset(Candidate& candidate) const noexcept {
    if (state_.generation == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
    const auto status = PrepareInvalidation(candidate); if (status != Status::Ok) return status;
    candidate.state_.entries = {}; candidate.state_.count = 0; ++candidate.state_.generation;
    return Status::Ok;
}
} // namespace body_slots::detail
