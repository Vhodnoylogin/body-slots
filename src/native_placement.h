#pragma once
#include "body_slots_native_placement_api.h"
#include <mutex>

namespace body_slots::detail {
// This class is the sole owner of the compatibility bank. ABI adapters cannot
// access its state. Every transition builds a candidate, then Commit publishes it.
class NativePlacementStore final {
public:
    Status AcquireOwner(const char* identity, const NativePlacementSnapshot* initial,
        std::uint64_t* owner, NativePlacementSnapshot* committed) noexcept;
    Status ReleaseOwner(std::uint64_t owner) noexcept;
    Status CopySnapshot(std::uint64_t owner, NativePlacementSnapshot* snapshot) const noexcept;
    Status ReplaceSnapshot(std::uint64_t owner, const NativePlacementSnapshot* candidate,
        NativePlacementSnapshot* committed) noexcept;

private:
    struct State {
        std::uint64_t owner = 0;
        std::uint64_t nextOwner = 1;
        char identity[64]{};
        NativePlacementSnapshot snapshot{sizeof(NativePlacementSnapshot), kNativePlacementApiVersion, 0, {}};
    };
    // Pure transition builders never alter the source state or caller buffers.
    static State Acquired(const State&, const char (&identity)[64], const NativePlacementSnapshot&) noexcept;
    static State Released(const State&) noexcept;
    static State Replaced(const State&, const NativePlacementSnapshot&) noexcept;
    void Commit(const State& candidate) noexcept;

    mutable std::mutex mutex_;
    State state_;
};
} // namespace body_slots::detail
