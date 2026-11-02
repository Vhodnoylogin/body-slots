#pragma once
#include "body_slots_instance_api.h"
#include "body_slots_instance_activation_api.h"
#include <array>

namespace body_slots { struct ConfiguredInstanceRecord; }

namespace body_slots::detail {
Status ValidateInstanceSpec(const InstanceSpec&) noexcept;
Status ValidateActivationChanges(const ActivationChange*, std::uint32_t count) noexcept;
// The enclosing Registry mutex guards every call. No independent lock, definition
// copy, engine binding or persistence lives here. Candidates are prepared before
// any coupled registry/configuration publication; only Commit changes live state.
class InstanceStore final {
public:
    struct Entry {
        std::uint64_t owner = 0;
        char provider[64]{};
        InstanceSpec spec{};
        std::uint64_t handle = 0, revision = 0;
    };
private:
    struct State {
        std::array<Entry, kMaxInstances> entries{};
        std::uint32_t count = 0;
        std::uint64_t generation = 1, view = 1, nextHandle = 1, nextRevision = 1;
    };
public:
    class Candidate {
        friend class InstanceStore;
        State state_{};
        bool changed_ = false;
    };
    std::uint32_t Count() const noexcept { return state_.count; }
    std::uint64_t Generation() const noexcept { return state_.generation; }
    std::uint64_t ViewRevision() const noexcept { return state_.view; }
    const Entry& At(std::uint32_t index) const noexcept { return state_.entries[index]; }
    const Entry* Find(std::uint64_t handle) const noexcept;
    Status PrepareUpsert(std::uint64_t owner, const char* provider, const InstanceSpec&,
        std::uint64_t expectedGeneration, std::uint64_t expectedRevision,
        Candidate&, std::uint64_t& handle) const noexcept;
    Status PrepareRemove(std::uint64_t owner, std::uint64_t handle,
        std::uint64_t expectedGeneration, std::uint64_t expectedRevision, Candidate&) const noexcept;
    Status PrepareActivation(std::uint64_t owner, const ActivationChange*, std::uint32_t count,
        std::uint64_t expectedGeneration, std::uint64_t expectedView, Candidate&) const noexcept;
    Status PrepareRelease(std::uint64_t owner, bool definitionsChanged, Candidate&) const noexcept;
    Status PrepareInvalidation(Candidate&) const noexcept;
    Status PrepareReset(Candidate&) const noexcept;
    void Apply(const Candidate& candidate) noexcept { if (candidate.changed_) Commit(candidate.state_); }
private:
    static bool SameKey(const Entry&, const char* provider, const InstanceSpec&) noexcept;
    static void Sort(State&) noexcept;
    void Commit(const State& candidate) noexcept { state_ = candidate; }
    State state_{};
};
// Save-scope invalidation is separate from a standalone configuration reset.
Status ResetSaveScope() noexcept;
Status ResetSaveScope(InstanceView* committed) noexcept;
// Runtime reads a coherent registry snapshot and derives ownership from live
// registry leases. These are internal helpers, never client-supplied identities.
Status RuntimeClientIdentity(std::uint64_t token, bool writer, char provider[64]) noexcept;
Status RuntimeOwnsInstance(std::uint64_t writer, std::uint64_t handle) noexcept;
Status CopyRuntimeConfiguration(ConfiguredInstanceRecord*, std::uint32_t capacity, InstanceView*) noexcept;
using RuntimeReleaseHook = Status (*)(std::uint64_t) noexcept;
void SetRuntimeReleaseHook(RuntimeReleaseHook) noexcept;
} // namespace body_slots::detail
