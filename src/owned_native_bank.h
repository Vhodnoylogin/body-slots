#pragma once
#include "validation.h"
#include "runtime_token.h"
#include <cstring>
#include <limits>
#include <mutex>

namespace body_slots::detail {
// Shared owner/revision implementation for the two native compatibility banks.
template<class Snapshot, std::uint32_t Version, RuntimeDomain Domain>
class OwnedNativeBank final {
public:
    Status AcquireOwner(const char* identity, const Snapshot* initial,
        std::uint64_t* owner, Snapshot* committed) noexcept {
        return Guard([&] {
            char name[64]{};
            if (!initial || !owner || !committed || !CopyIdentity(identity, name)) return Status::InvalidArgument;
            const Snapshot copied = *initial;
            if (!Valid(copied) || !Valid(*committed)) return Status::UnsupportedVersion;
            if (copied.revision) return Status::InvalidArgument;
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_.owner) return Status::Conflict;
            const auto max = std::numeric_limits<std::uint64_t>::max();
            const auto allocation = NextRuntimeToken(state_.nextOwner, Domain);
            if (allocation.status != Status::Ok) return allocation.status;
            if (state_.snapshot.revision == max) return Status::LimitExceeded;
            const State candidate = Acquired(state_, name, copied, allocation);
            Commit(candidate);
            *owner = state_.owner;
            *committed = state_.snapshot;
            return Status::Ok;
        });
    }
    Status ReleaseOwner(std::uint64_t owner) noexcept {
        return Guard([&] {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!owner || owner != state_.owner) return Status::Unauthorized;
            const State candidate = Released(state_);
            Commit(candidate);
            return Status::Ok;
        });
    }
    Status CopySnapshot(std::uint64_t owner, Snapshot* output) const noexcept { return Copy(owner, true, output); }
    // Read-only introspection retains no caller code, state, lease or resource.
    Status CopyPublicSnapshot(Snapshot* output) const noexcept { return Copy(0, false, output); }
    Status ReplaceSnapshot(std::uint64_t owner, const Snapshot* input, Snapshot* committed) noexcept {
        return Guard([&] {
            if (!input || !committed) return Status::InvalidArgument;
            const Snapshot copied = *input;
            if (!Valid(copied) || !Valid(*committed)) return Status::UnsupportedVersion;
            std::lock_guard<std::mutex> lock(mutex_);
            if (!owner || owner != state_.owner) return Status::Unauthorized;
            if (!copied.revision || copied.revision != state_.snapshot.revision) return Status::Conflict;
            if (std::memcmp(&copied, &state_.snapshot, sizeof(copied))) {
                if (state_.snapshot.revision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
                const State candidate = Replaced(state_, copied);
                Commit(candidate);
            }
            *committed = state_.snapshot;
            return Status::Ok;
        });
    }
private:
    struct State {
        std::uint64_t owner = 0;
        std::uint64_t nextOwner = 1;
        char identity[64]{};
        Snapshot snapshot{sizeof(Snapshot), Version, 0, {}};
    };
    static bool CopyIdentity(const char* input, char (&output)[64]) noexcept {
        if (!input) return false;
        std::size_t length = 0;
        for (; length < sizeof(output); ++length) { if (!input[length]) break; output[length] = input[length]; }
        return length < sizeof(output) && ValidString(output, sizeof(output), true);
    }
    static bool Valid(const Snapshot& snapshot) noexcept { return snapshot.structSize == sizeof(snapshot) && snapshot.version == Version; }
    template<class F> static Status Guard(F&& operation) noexcept {
        try { return operation(); } catch (...) { return Status::InternalError; }
    }
    // Pure candidate builders never change the previous state or caller inputs.
    static State Acquired(const State& previous, const char (&identity)[64], const Snapshot& initial,
        const RuntimeTokenCandidate& allocation) noexcept {
        State next = previous;
        next.owner = allocation.token;
        next.nextOwner = allocation.nextCounter;
        std::memcpy(next.identity, identity, sizeof(identity));
        next.snapshot = initial;
        next.snapshot.revision = previous.snapshot.revision + 1;
        return next;
    }
    static State Released(const State& previous) noexcept {
        State next = previous;
        next.owner = 0;
        std::memset(next.identity, 0, sizeof(next.identity));
        next.snapshot = Snapshot{sizeof(Snapshot), Version, previous.snapshot.revision, {}};
        return next;
    }
    static State Replaced(const State& previous, const Snapshot& payload) noexcept {
        State next = previous;
        next.snapshot = payload;
        ++next.snapshot.revision;
        return next;
    }
    void Commit(const State& candidate) noexcept { state_ = candidate; }
    Status Copy(std::uint64_t owner, bool requireOwner, Snapshot* output) const noexcept {
        return Guard([&] {
            if (!output) return Status::InvalidArgument;
            if (!Valid(*output)) return Status::UnsupportedVersion;
            std::lock_guard<std::mutex> lock(mutex_);
            if (requireOwner && (!owner || owner != state_.owner)) return Status::Unauthorized;
            if (!state_.owner) return Status::Unavailable;
            *output = state_.snapshot;
            return Status::Ok;
        });
    }
    mutable std::mutex mutex_;
    State state_;
};
} // namespace body_slots::detail
