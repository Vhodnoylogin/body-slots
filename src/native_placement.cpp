#include "native_placement.h"
#include "validation.h"
#include <cstring>
#include <limits>

namespace body_slots::detail {
namespace {
bool CopyIdentity(const char* input, char (&output)[64]) noexcept {
    if (!input) return false;
    std::size_t length = 0;
    for (; length < sizeof(output); ++length) {
        if (!input[length]) break;
        output[length] = input[length];
    }
    return length < sizeof(output) && ValidString(output, sizeof(output), true);
}
bool ValidHeader(const NativePlacementSnapshot& snapshot) noexcept {
    return snapshot.structSize == sizeof(snapshot) && snapshot.version == kNativePlacementApiVersion;
}
template<class F> Status Guard(F&& operation) noexcept {
    try { return operation(); } catch (...) { return Status::InternalError; }
}
}

NativePlacementStore::State NativePlacementStore::Acquired(const State& previous,
    const char (&identity)[64], const NativePlacementSnapshot& initial) noexcept {
    State next = previous;
    next.owner = next.nextOwner++;
    std::memcpy(next.identity, identity, sizeof(identity));
    std::memcpy(next.snapshot.placements, initial.placements, sizeof(initial.placements));
    ++next.snapshot.revision;
    return next;
}
NativePlacementStore::State NativePlacementStore::Released(const State& previous) noexcept {
    State next = previous;
    next.owner = 0;
    std::memset(next.identity, 0, sizeof(next.identity));
    // Clear inaccessible data while retaining monotonic revisions/owner identities.
    std::memset(next.snapshot.placements, 0, sizeof(next.snapshot.placements));
    return next;
}
NativePlacementStore::State NativePlacementStore::Replaced(const State& previous,
    const NativePlacementSnapshot& candidate) noexcept {
    State next = previous;
    std::memcpy(next.snapshot.placements, candidate.placements, sizeof(candidate.placements));
    ++next.snapshot.revision;
    return next;
}
void NativePlacementStore::Commit(const State& candidate) noexcept { state_ = candidate; }

Status NativePlacementStore::AcquireOwner(const char* identity, const NativePlacementSnapshot* initial,
    std::uint64_t* owner, NativePlacementSnapshot* committed) noexcept {
    return Guard([&] {
        char name[64]{};
        if (!initial || !owner || !committed || !CopyIdentity(identity, name)) return Status::InvalidArgument;
        const NativePlacementSnapshot copied = *initial;
        if (!ValidHeader(copied) || !ValidHeader(*committed)) return Status::UnsupportedVersion;
        if (copied.revision != 0) return Status::InvalidArgument;
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.owner) return Status::Conflict;
        const auto max = std::numeric_limits<std::uint64_t>::max();
        if (state_.nextOwner == max || state_.snapshot.revision == max) return Status::LimitExceeded;
        const State candidate = Acquired(state_, name, copied);
        Commit(candidate);
        *owner = state_.owner;
        *committed = state_.snapshot;
        return Status::Ok;
    });
}
Status NativePlacementStore::ReleaseOwner(std::uint64_t owner) noexcept {
    return Guard([&] {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owner || owner != state_.owner) return Status::Unauthorized;
        const State candidate = Released(state_);
        Commit(candidate);
        return Status::Ok;
    });
}
Status NativePlacementStore::CopySnapshot(std::uint64_t owner, NativePlacementSnapshot* snapshot) const noexcept {
    return Guard([&] {
        if (!snapshot) return Status::InvalidArgument;
        if (!ValidHeader(*snapshot)) return Status::UnsupportedVersion;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owner || owner != state_.owner) return Status::Unauthorized;
        *snapshot = state_.snapshot;
        return Status::Ok;
    });
}
Status NativePlacementStore::ReplaceSnapshot(std::uint64_t owner,
    const NativePlacementSnapshot* input, NativePlacementSnapshot* committed) noexcept {
    return Guard([&] {
        if (!input || !committed) return Status::InvalidArgument;
        const NativePlacementSnapshot copied = *input;
        if (!ValidHeader(copied) || !ValidHeader(*committed)) return Status::UnsupportedVersion;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owner || owner != state_.owner) return Status::Unauthorized;
        if (!copied.revision || copied.revision != state_.snapshot.revision) return Status::Conflict;
        if (std::memcmp(copied.placements, state_.snapshot.placements, sizeof(copied.placements)) != 0) {
            if (state_.snapshot.revision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
            const State candidate = Replaced(state_, copied);
            Commit(candidate);
        }
        *committed = state_.snapshot;
        return Status::Ok;
    });
}

namespace {
NativePlacementStore& Store() { static NativePlacementStore store; return store; }
Status BS_CALL Acquire(const char* identity, const NativePlacementSnapshot* initial,
    std::uint64_t* owner, NativePlacementSnapshot* committed) noexcept {
    return Store().AcquireOwner(identity, initial, owner, committed);
}
Status BS_CALL Release(std::uint64_t owner) noexcept { return Store().ReleaseOwner(owner); }
Status BS_CALL Copy(std::uint64_t owner, NativePlacementSnapshot* snapshot) noexcept {
    return Store().CopySnapshot(owner, snapshot);
}
Status BS_CALL Replace(std::uint64_t owner, const NativePlacementSnapshot* candidate,
    NativePlacementSnapshot* committed) noexcept { return Store().ReplaceSnapshot(owner, candidate, committed); }
const NativePlacementApi kApi{sizeof(NativePlacementApi), kNativePlacementApiVersion,
    kNativePlacementCapability, Acquire, Release, Copy, Replace};
}
} // namespace body_slots::detail

extern "C" __declspec(dllexport) const body_slots::NativePlacementApi* BS_CALL
BodySlots_QueryNativePlacementInterface(std::uint32_t version, std::uint32_t size) noexcept {
    return version == body_slots::kNativePlacementApiVersion && size == sizeof(body_slots::NativePlacementApi)
        ? &body_slots::detail::kApi : nullptr;
}
