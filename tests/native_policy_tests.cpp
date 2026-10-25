#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "body_slots_native_policy_api.h"
#include "body_slots_configuration_api.h"
#include "runtime_token.h"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

using namespace body_slots;
namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

NativePolicySnapshot Snapshot() {
    NativePolicySnapshot value{sizeof(value), kNativePolicyApiVersion, 0, {}};
    for (unsigned slot = 0; slot < kNativePlacementCount; ++slot)
        for (unsigned field = 0; field < kNativePolicyFields; ++field)
            value.policies[slot].values[field] = double(slot * kNativePolicyFields + field) / 8.0 - 3.0;
    const std::uint64_t representations[]{0x7ff8000000000123ULL, 0xfff8000000000456ULL,
        0x8000000000000000ULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL, 1ULL};
    for (unsigned field = 0; field < std::size(representations); ++field)
        std::memcpy(&value.policies[2].values[field], &representations[field], sizeof(double));
    return value;
}
bool Same(const NativePolicySnapshot& left, const NativePolicySnapshot& right) {
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}
void Wave(NativePolicySnapshot& snapshot, unsigned step) {
    for (unsigned slot = 0; slot < kNativePlacementCount; ++slot)
        for (unsigned field = 0; field < kNativePolicyFields; ++field)
            snapshot.policies[slot].values[field] = double(step) * 1024.0 + double(slot * kNativePolicyFields + field);
}
void Predicates(const NativePolicy& copied) {
    auto policy = copied;
    const double values[]{0.0, 0.5, 0.5000000000000001, 1.0, -1.0,
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()};
    const bool visible[]{false, false, true, true, false, true, false, false};
    const bool allowed[]{false, true, true, true, false, true, false, true};
    for (unsigned sample = 0; sample < std::size(values); ++sample) {
        for (auto& value : policy.values) value = values[sample];
        CHECK(NativePolicyVisible(policy) == visible[sample]);
        CHECK(NativePolicyAllowsWeapon(policy, NativeWeaponClass::None));
        for (unsigned category = 1; category <= 6; ++category)
            CHECK(NativePolicyAllowsWeapon(policy, static_cast<NativeWeaponClass>(category)) == allowed[sample]);
        CHECK(!NativePolicyAllowsWeapon(policy, static_cast<NativeWeaponClass>(7)));
        CHECK(!NativePolicyAllowsWeapon(policy, static_cast<NativeWeaponClass>(0xffffffffu)));
    }
    // Every weapon category reads its own value, independently of visibility/hand.
    for (unsigned selected = 1; selected <= 6; ++selected) {
        for (auto& value : policy.values) value = 0.0;
        policy.values[selected + 1] = 0.5;
        policy.values[static_cast<unsigned>(NativePolicyField::Hand)] = 2.0;
        CHECK(!NativePolicyVisible(policy));
        for (unsigned category = 1; category <= 6; ++category)
            CHECK(NativePolicyAllowsWeapon(policy, static_cast<NativeWeaponClass>(category)) == (category == selected));
    }
}
void TokenBoundaries() {
    using detail::NextRuntimeToken;
    using detail::RuntimeDomain;
    constexpr std::uint64_t maximum = 0x00ffffffffffffffULL;
    const std::array<RuntimeDomain, 3> domains{RuntimeDomain::Registry, RuntimeDomain::NativePlacement, RuntimeDomain::NativePolicy};
    const std::uint64_t firstTokens[]{1ULL, 0x0100000000000001ULL, 0x0200000000000001ULL};
    const std::uint64_t lastTokens[]{0x00ffffffffffffffULL, 0x01ffffffffffffffULL, 0x02ffffffffffffffULL};
    CHECK(detail::kRuntimeCounterLimit == maximum);
    for (unsigned index = 0; index < domains.size(); ++index) {
        const auto first = NextRuntimeToken(1, domains[index]);
        CHECK(first.status == Status::Ok && first.token == firstTokens[index] && first.nextCounter == 2);
        const auto last = NextRuntimeToken(maximum, domains[index]);
        CHECK(last.status == Status::Ok && last.token == lastTokens[index] && last.nextCounter == maximum + 1);
        for (const auto counter : {0ULL, maximum + 1, std::numeric_limits<std::uint64_t>::max()}) {
            const auto rejected = NextRuntimeToken(counter, domains[index]);
            CHECK(rejected.status == Status::LimitExceeded && rejected.token == 0 && rejected.nextCounter == 0);
        }
    }
    for (const auto domain : {static_cast<RuntimeDomain>(3), static_cast<RuntimeDomain>(255)}) {
        const auto rejected = NextRuntimeToken(1, domain);
        CHECK(rejected.status == Status::InvalidArgument && rejected.token == 0 && rejected.nextCounter == 0);
    }
}
}
int main(int argc, char** argv) {
    try {
        TokenBoundaries();
        CHECK(argc == 2);
        const auto module = LoadLibraryA(argv[1]); CHECK(module);
        const auto query = reinterpret_cast<QueryNativePolicyInterface>(GetProcAddress(module, "BodySlots_QueryNativePolicyInterface"));
        CHECK(query);
        CHECK(!query(0, sizeof(NativePolicyApi)));
        CHECK(!query(kNativePolicyApiVersion, sizeof(NativePolicyApi) - 1));
        CHECK(!query(kNativePolicyApiVersion, sizeof(NativePolicyApi) + 1));
        const auto* api = query(kNativePolicyApiVersion, sizeof(NativePolicyApi));
        CHECK(api && api->structSize == 48 && api->version == 1 && api->capabilities == 2);
        CHECK(kNativePolicyFields == 9 && static_cast<unsigned>(NativePolicyField::HoverHaptics) == 8 &&
            sizeof(NativePolicy) == 72 && sizeof(NativePolicySnapshot) == 1024);
        CHECK(api->AcquireOwner && api->ReleaseOwner && api->CopySnapshot && api->ReplaceSnapshot);
        auto initial = Snapshot(), current = Snapshot();
        current.revision = 777;
        auto sentinel = current;
        CHECK(api->CopySnapshot(0, &current) == Status::Unavailable && Same(current, sentinel));
        CHECK(api->CopySnapshot(0, nullptr) == Status::InvalidArgument);
        CHECK(api->CopySnapshot(999, &current) == Status::Unauthorized && Same(current, sentinel));
        std::uint64_t owner = 777;
        const auto invalidAcquire = [&](const char* identity, const NativePolicySnapshot* input, Status expected) {
            current = sentinel;
            CHECK(api->AcquireOwner(identity, input, &owner, &current) == expected);
            CHECK(owner == 777 && Same(current, sentinel));
        };
        invalidAcquire(nullptr, &initial, Status::InvalidArgument);
        invalidAcquire("", &initial, Status::InvalidArgument);
        invalidAcquire("bad:name", &initial, Status::InvalidArgument);
        const std::string longIdentity(64, 'x');
        invalidAcquire(longIdentity.c_str(), &initial, Status::InvalidArgument);
        invalidAcquire("fixture", nullptr, Status::InvalidArgument);
        auto bad = initial; bad.revision = 1;
        invalidAcquire("fixture", &bad, Status::InvalidArgument);
        bad = initial; --bad.structSize;
        invalidAcquire("fixture", &bad, Status::UnsupportedVersion);
        bad = initial; ++bad.version;
        invalidAcquire("fixture", &bad, Status::UnsupportedVersion);
        CHECK(api->AcquireOwner("fixture", &initial, nullptr, &current) == Status::InvalidArgument && Same(current, sentinel));
        CHECK(api->AcquireOwner("fixture", &initial, &owner, nullptr) == Status::InvalidArgument && owner == 777);
        current = sentinel; --current.structSize; const auto invalidOutput = current;
        CHECK(api->AcquireOwner("fixture", &initial, &owner, &current) == Status::UnsupportedVersion);
        CHECK(owner == 777 && Same(current, invalidOutput));
        CHECK(api->CopySnapshot(0, &current) == Status::UnsupportedVersion && Same(current, invalidOutput));
        current = Snapshot();
        const auto original = initial;
        CHECK(api->AcquireOwner("fixture.native14.policy", &initial, &owner, &current) == Status::Ok);
        CHECK(owner && owner != 777 && current.revision);
        CHECK(Same(initial, original));
        CHECK(std::memcmp(current.policies, initial.policies, sizeof(initial.policies)) == 0);
        Predicates(current.policies[2]);
        auto publicCopy = Snapshot();
        CHECK(api->CopySnapshot(0, &publicCopy) == Status::Ok && Same(publicCopy, current));
        publicCopy.policies[0].values[0] = 999;
        CHECK(api->CopySnapshot(owner, &publicCopy) == Status::Ok && Same(publicCopy, current));
        std::uint64_t other = 888;
        sentinel = current;
        CHECK(api->AcquireOwner("other", &initial, &other, &publicCopy) == Status::Conflict);
        CHECK(other == 888 && Same(publicCopy, sentinel));
        CHECK(api->CopySnapshot(owner + 1, &publicCopy) == Status::Unauthorized && Same(publicCopy, sentinel));
        CHECK(api->ReplaceSnapshot(0, &current, &publicCopy) == Status::Unauthorized && Same(publicCopy, sentinel));
        CHECK(api->ReplaceSnapshot(owner + 1, &current, &publicCopy) == Status::Unauthorized && Same(publicCopy, sentinel));
        CHECK(api->ReplaceSnapshot(owner, nullptr, &publicCopy) == Status::InvalidArgument && Same(publicCopy, sentinel));
        CHECK(api->ReplaceSnapshot(owner, &current, nullptr) == Status::InvalidArgument);
        bad = current; bad.revision = 0;
        CHECK(api->ReplaceSnapshot(owner, &bad, &publicCopy) == Status::Conflict && Same(publicCopy, sentinel));
        bad = current; --bad.structSize;
        CHECK(api->ReplaceSnapshot(owner, &bad, &publicCopy) == Status::UnsupportedVersion && Same(publicCopy, sentinel));
        bad = current; ++bad.version;
        CHECK(api->ReplaceSnapshot(owner, &bad, &publicCopy) == Status::UnsupportedVersion && Same(publicCopy, sentinel));
        publicCopy = current; ++publicCopy.version; const auto badOutput = publicCopy;
        CHECK(api->ReplaceSnapshot(owner, &current, &publicCopy) == Status::UnsupportedVersion && Same(publicCopy, badOutput));
        CHECK(api->CopySnapshot(owner, &publicCopy) == Status::UnsupportedVersion && Same(publicCopy, badOutput));
        const auto firstRevision = current.revision;
        CHECK(api->ReplaceSnapshot(owner, &current, &current) == Status::Ok && current.revision == firstRevision);
        auto stale = current;
        // Changing just the sign of zero, then a NaN payload, is a real bit change.
        const std::uint64_t positiveZero = 0, secondNan = 0x7ff8000000009876ULL;
        auto candidate = current;
        std::memcpy(&candidate.policies[2].values[2], &positiveZero, sizeof(double));
        CHECK(api->ReplaceSnapshot(owner, &candidate, &current) == Status::Ok && current.revision > firstRevision);
        sentinel = current; publicCopy = current;
        CHECK(api->ReplaceSnapshot(owner, &stale, &publicCopy) == Status::Conflict && Same(publicCopy, sentinel));
        candidate = current;
        std::memcpy(&candidate.policies[2].values[0], &secondNan, sizeof(double));
        CHECK(api->ReplaceSnapshot(owner, &candidate, &current) == Status::Ok && current.revision > sentinel.revision);
        CHECK(std::memcmp(candidate.policies, current.policies, sizeof(candidate.policies)) == 0);
        const auto nanRevision = current.revision;
        CHECK(api->ReplaceSnapshot(owner, &current, &current) == Status::Ok && current.revision == nanRevision);

        // The same DLL hosts independent policy and placement stores.
        const auto placementQuery = reinterpret_cast<QueryNativePlacementInterface>(GetProcAddress(module, "BodySlots_QueryNativePlacementInterface"));
        CHECK(placementQuery);
        const auto* placement = placementQuery(kNativePlacementApiVersion, sizeof(NativePlacementApi)); CHECK(placement && placement->capabilities == 1);
        NativePlacementSnapshot pose{sizeof(pose), kNativePlacementApiVersion, 0, {}};
        pose.placements[13].position[0] = 123; pose.placements[0].rotation[0] = 2;
        std::uint64_t poseOwner = 0;
        CHECK(placement->AcquireOwner("fixture.native14.placement", &pose, &poseOwner, &pose) == Status::Ok);
        sentinel = current;
        pose.placements[13].position[0] = -45;
        CHECK(placement->ReplaceSnapshot(poseOwner, &pose, &pose) == Status::Ok);
        CHECK(api->CopySnapshot(0, &publicCopy) == Status::Ok && Same(publicCopy, sentinel));
        const auto unchangedPose = pose;

        // All live handle domains coexist in this DLL. A valid handle for another
        // service must neither authorize a native operation nor consume its lease.
        const auto registryQuery = reinterpret_cast<QueryInterface>(GetProcAddress(module, "BodySlots_QueryInterface"));
        const auto configurationQuery = reinterpret_cast<QueryConfigurationInterface>(GetProcAddress(module, "BodySlots_QueryConfigurationInterface"));
        CHECK(registryQuery && configurationQuery);
        const auto* registry = registryQuery(kApiVersion, sizeof(Api));
        const auto* configuration = configurationQuery(kConfigurationApiVersion, sizeof(ConfigurationApi));
        CHECK(registry && configuration);
        std::uint64_t writer = 0, observer = 0, customizer = 0;
        CHECK(registry->RegisterClient("fixture.cross-service", &writer) == Status::Ok);
        CHECK(registry->RegisterObserver(&observer) == Status::Ok);
        CHECK(configuration->RegisterCustomizer("fixture.cross-service", &customizer) == Status::Ok);
        CHECK((writer >> 56) == 0 && (observer >> 56) == 0 && (customizer >> 56) == 0);
        CHECK(writer != observer && writer != customizer && observer != customizer);
        CHECK((poseOwner >> 56) == 1 && (owner >> 56) == 2);
        const auto policyBeforeForeign = current;
        const auto banksUnchanged = [&] {
            auto copiedPolicy = Snapshot();
            CHECK(api->CopySnapshot(0, &copiedPolicy) == Status::Ok && Same(copiedPolicy, policyBeforeForeign));
            CHECK(api->CopySnapshot(owner, &copiedPolicy) == Status::Ok && Same(copiedPolicy, policyBeforeForeign));
            auto copiedPose = unchangedPose;
            CHECK(placement->CopySnapshot(poseOwner, &copiedPose) == Status::Ok);
            CHECK(std::memcmp(&copiedPose, &unchangedPose, sizeof(copiedPose)) == 0);
        };
        auto changedPolicy = policyBeforeForeign; changedPolicy.policies[13].values[7] = -321;
        for (const auto foreign : {writer, observer, customizer, poseOwner}) {
            auto rejectedPolicy = policyBeforeForeign;
            CHECK(api->CopySnapshot(foreign, &rejectedPolicy) == Status::Unauthorized && Same(rejectedPolicy, policyBeforeForeign));
            CHECK(api->ReplaceSnapshot(foreign, &changedPolicy, &rejectedPolicy) == Status::Unauthorized && Same(rejectedPolicy, policyBeforeForeign));
            CHECK(api->ReleaseOwner(foreign) == Status::Unauthorized);
            banksUnchanged();
        }
        auto changedPose = unchangedPose; changedPose.placements[13].position[0] = 987;
        for (const auto foreign : {writer, observer, customizer, owner}) {
            auto rejectedPose = unchangedPose;
            CHECK(placement->CopySnapshot(foreign, &rejectedPose) == Status::Unauthorized);
            CHECK(std::memcmp(&rejectedPose, &unchangedPose, sizeof(rejectedPose)) == 0);
            CHECK(placement->ReplaceSnapshot(foreign, &changedPose, &rejectedPose) == Status::Unauthorized);
            CHECK(std::memcmp(&rejectedPose, &unchangedPose, sizeof(rejectedPose)) == 0);
            CHECK(placement->ReleaseOwner(foreign) == Status::Unauthorized);
            banksUnchanged();
        }
        std::uint32_t definitionCount = 999; std::uint64_t registryRevision = 0, configurationRevision = 0;
        CHECK(registry->CopyDefinitions(writer, 0, nullptr, 0, &definitionCount, &registryRevision) == Status::Ok && definitionCount == 0);
        CHECK(registry->CopyDefinitions(observer, registryRevision, nullptr, 0, &definitionCount, &registryRevision) == Status::Ok && definitionCount == 0);
        CHECK(configuration->CopyEffectiveDefinitions(customizer, 0, nullptr, 0, &definitionCount, &configurationRevision) == Status::Ok && definitionCount == 0);
        CHECK(registry->ReleaseClient(owner) == Status::Unauthorized);
        CHECK(registry->ReleaseClient(poseOwner) == Status::Unauthorized);
        CHECK(configuration->ReleaseCustomizer(owner) == Status::Unauthorized);
        CHECK(configuration->ReleaseCustomizer(poseOwner) == Status::Unauthorized);
        CHECK(configuration->ReleaseCustomizer(customizer) == Status::Ok);
        CHECK(registry->ReleaseClient(observer) == Status::Ok);
        CHECK(registry->ReleaseClient(writer) == Status::Ok);
        banksUnchanged();

        Wave(current, 0);
        CHECK(api->ReplaceSnapshot(owner, &current, &current) == Status::Ok);
        std::atomic<bool> start{false}, running{true};
        std::atomic<unsigned> ready{0}, errors{0};
        std::array<std::atomic<unsigned>, 3> reads{};
        std::array<std::thread, 3> readers;
        for (unsigned index = 0; index < readers.size(); ++index) readers[index] = std::thread([&, index] {
            auto copy = Snapshot(); std::uint64_t previous = 0;
            ++ready; while (!start.load()) std::this_thread::yield();
            do {
                if (api->CopySnapshot(index ? owner : 0, &copy) != Status::Ok || copy.revision < previous) { ++errors; break; }
                previous = copy.revision;
                const auto stamp = copy.policies[0].values[0];
                for (unsigned slot = 0; slot < kNativePlacementCount; ++slot)
                    for (unsigned field = 0; field < kNativePolicyFields; ++field)
                        if (copy.policies[slot].values[field] != stamp + double(slot * kNativePolicyFields + field)) ++errors;
                ++reads[index];
            } while (running.load());
        });
        while (ready.load() != readers.size()) std::this_thread::yield();
        start = true;
        for (unsigned step = 1; step <= 4000; ++step) {
            Wave(current, step);
            if (api->ReplaceSnapshot(owner, &current, &current) != Status::Ok) { ++errors; break; }
        }
        running = false;
        for (auto& reader : readers) reader.join();
        CHECK(errors == 0);
        for (const auto& count : reads) CHECK(count > 0);
        CHECK(api->CopySnapshot(0, &publicCopy) == Status::Ok && Same(publicCopy, current));
        CHECK(current.policies[kNativePlacementCount - 1].values[kNativePolicyFields - 1] ==
            4000.0 * 1024.0 + double(kNativePlacementCount * kNativePolicyFields - 1));
        CHECK(placement->CopySnapshot(poseOwner, &pose) == Status::Ok);
        CHECK(std::memcmp(&pose, &unchangedPose, sizeof(pose)) == 0);
        const auto lastRevision = current.revision;
        sentinel = current;
        CHECK(api->ReleaseOwner(owner + 1) == Status::Unauthorized);
        CHECK(api->CopySnapshot(0, &publicCopy) == Status::Ok && Same(publicCopy, sentinel));
        CHECK(api->ReleaseOwner(owner) == Status::Ok);
        CHECK(api->CopySnapshot(0, &publicCopy) == Status::Unavailable && Same(publicCopy, sentinel));
        CHECK(api->CopySnapshot(owner, &publicCopy) == Status::Unauthorized && Same(publicCopy, sentinel));
        CHECK(api->ReleaseOwner(owner) == Status::Unauthorized);
        auto aliased = original;
        CHECK(api->AcquireOwner(std::string(63, 'x').c_str(), &aliased, &other, &aliased) == Status::Ok);
        CHECK(other > owner && aliased.revision > lastRevision);
        CHECK(std::memcmp(aliased.policies, original.policies, sizeof(original.policies)) == 0);
        sentinel = aliased; publicCopy = aliased;
        CHECK(api->ReplaceSnapshot(owner, &aliased, &publicCopy) == Status::Unauthorized && Same(publicCopy, sentinel));
        CHECK(api->CopySnapshot(0, &publicCopy) == Status::Ok && Same(publicCopy, aliased));
        CHECK(api->ReleaseOwner(other) == Status::Ok);
        CHECK(api->CopySnapshot(0, &publicCopy) == Status::Unavailable && Same(publicCopy, sentinel));
        CHECK(placement->CopySnapshot(poseOwner, &pose) == Status::Ok && std::memcmp(&pose, &unchangedPose, sizeof(pose)) == 0);
        CHECK(placement->ReleaseOwner(poseOwner) == Status::Ok);
        CHECK(FreeLibrary(module));
        std::cout << checks << " native policy ABI checks passed; 4000 whole-bank writes, three concurrent readers, legacy predicates, independent native stores and token-domain rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
