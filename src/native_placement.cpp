#include "native_placement.h"

namespace body_slots::detail {
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
