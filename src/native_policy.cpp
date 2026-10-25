#include "body_slots_native_policy_api.h"
#include "owned_native_bank.h"

namespace body_slots::detail {
namespace {
using NativePolicyStore = OwnedNativeBank<NativePolicySnapshot, kNativePolicyApiVersion, RuntimeDomain::NativePolicy>;
NativePolicyStore& Store() { static NativePolicyStore store; return store; }
Status BS_CALL Acquire(const char* identity, const NativePolicySnapshot* initial,
    std::uint64_t* owner, NativePolicySnapshot* committed) noexcept {
    return Store().AcquireOwner(identity, initial, owner, committed);
}
Status BS_CALL Release(std::uint64_t owner) noexcept { return Store().ReleaseOwner(owner); }
Status BS_CALL Copy(std::uint64_t owner, NativePolicySnapshot* snapshot) noexcept {
    return owner ? Store().CopySnapshot(owner, snapshot) : Store().CopyPublicSnapshot(snapshot);
}
Status BS_CALL Replace(std::uint64_t owner, const NativePolicySnapshot* candidate,
    NativePolicySnapshot* committed) noexcept { return Store().ReplaceSnapshot(owner, candidate, committed); }
const NativePolicyApi kApi{sizeof(NativePolicyApi), kNativePolicyApiVersion,
    kNativePolicyCapability, Acquire, Release, Copy, Replace};
}
} // namespace body_slots::detail

extern "C" __declspec(dllexport) const body_slots::NativePolicyApi* BS_CALL
BodySlots_QueryNativePolicyInterface(std::uint32_t version, std::uint32_t size) noexcept {
    return version == body_slots::kNativePolicyApiVersion && size == sizeof(body_slots::NativePolicyApi)
        ? &body_slots::detail::kApi : nullptr;
}
