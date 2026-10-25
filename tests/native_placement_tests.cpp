#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "body_slots_native_placement_api.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
unsigned checks = 0;
bool Check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) std::cerr << "line " << line << ": " << expression << '\n';
    return value;
}
#define CHECK(expression) do { if (!Check((expression), #expression, __LINE__)) return 1; } while (false)
body_slots::NativePlacementSnapshot Snapshot() {
    body_slots::NativePlacementSnapshot value{sizeof(value), body_slots::kNativePlacementApiVersion, 0, {}};
    for (unsigned i = 0; i < body_slots::kNativePlacementCount; ++i) {
        value.placements[i].position[0] = float(i + 30); // intentionally outside some legacy INI clamps
        value.placements[i].position[1] = -float(i);
        value.placements[i].rotation[0] = 2; // intentionally nonrigid: compatibility is a raw bank
        value.placements[i].rotation[4] = value.placements[i].rotation[8] = 1;
    }
    return value;
}
}
int main(int argc, char** argv) {
    using namespace body_slots;
    CHECK(argc == 2);
    const auto module = LoadLibraryA(argv[1]);
    CHECK(module != nullptr);
    const auto query = reinterpret_cast<QueryNativePlacementInterface>(GetProcAddress(module, "BodySlots_QueryNativePlacementInterface"));
    CHECK(query != nullptr);
    CHECK(query(0, sizeof(NativePlacementApi)) == nullptr);
    CHECK(query(kNativePlacementApiVersion, sizeof(NativePlacementApi) - 1) == nullptr);
    const auto* api = query(kNativePlacementApiVersion, sizeof(NativePlacementApi));
    CHECK(api && api->structSize == sizeof(*api) && api->version == kNativePlacementApiVersion);
    CHECK(api->capabilities == kNativePlacementCapability);
    auto initial = Snapshot();
    // Preserve even a legacy floating-point representation that strict definitions reject.
    const std::uint32_t nanBits = 0x7fc00123;
    std::memcpy(&initial.placements[2].rotation[3], &nanBits, sizeof(nanBits));
    auto current = Snapshot();
    std::uint64_t owner = 0;
    CHECK(api->AcquireOwner(nullptr, &initial, &owner, &current) == Status::InvalidArgument);
    CHECK(owner == 0);
    CHECK(api->AcquireOwner("vrik.native14", &initial, &owner, &current) == Status::Ok);
    CHECK(owner != 0 && current.revision != 0);
    CHECK(std::memcmp(initial.placements, current.placements, sizeof(initial.placements)) == 0);
    auto sentinel = current;
    auto rejected = sentinel;
    std::uint64_t other = 777;
    CHECK(api->AcquireOwner("other", &initial, &other, &rejected) == Status::Conflict);
    CHECK(other == 777 && std::memcmp(&rejected, &sentinel, sizeof(sentinel)) == 0);
    CHECK(api->CopySnapshot(owner + 1, &rejected) == Status::Unauthorized);
    CHECK(std::memcmp(&rejected, &sentinel, sizeof(sentinel)) == 0);
    CHECK(api->CopySnapshot(owner, &current) == Status::Ok);
    const auto firstRevision = current.revision;
    CHECK(api->ReplaceSnapshot(owner, &current, &current) == Status::Ok);
    CHECK(current.revision == firstRevision); // unchanged candidates do not invent a revision
    auto changed = current;
    changed.placements[0].position[0] = 57;
    changed.placements[0].rotation[0] = -2;
    CHECK(api->ReplaceSnapshot(owner, &changed, &current) == Status::Ok);
    CHECK(current.revision > firstRevision);
    CHECK(current.placements[0].position[0] == 57 && current.placements[0].rotation[0] == -2);
    sentinel = current;
    rejected = current;
    CHECK(api->ReplaceSnapshot(owner, &changed, &rejected) == Status::Conflict);
    CHECK(std::memcmp(&rejected, &sentinel, sizeof(sentinel)) == 0);
    CHECK(api->ReplaceSnapshot(owner + 1, &current, &rejected) == Status::Unauthorized);
    CHECK(std::memcmp(&rejected, &sentinel, sizeof(sentinel)) == 0);
    auto invalid = current;
    invalid.version += 1;
    CHECK(api->ReplaceSnapshot(owner, &invalid, &rejected) == Status::UnsupportedVersion);
    CHECK(std::memcmp(&rejected, &sentinel, sizeof(sentinel)) == 0);
    auto invalidOutput = current;
    invalidOutput.structSize = 4;
    const auto badSentinel = invalidOutput;
    CHECK(api->ReplaceSnapshot(owner, &current, &invalidOutput) == Status::UnsupportedVersion);
    CHECK(std::memcmp(&invalidOutput, &badSentinel, sizeof(badSentinel)) == 0);
    CHECK(api->CopySnapshot(owner, &invalidOutput) == Status::UnsupportedVersion);
    CHECK(std::memcmp(&invalidOutput, &badSentinel, sizeof(badSentinel)) == 0);

    for (auto& p : current.placements) { p.position[0] = 0; p.rotation[0] = 0; }
    CHECK(api->ReplaceSnapshot(owner, &current, &current) == Status::Ok);
    std::atomic<bool> running{true};
    std::atomic<unsigned> errors{0};
    std::thread reader([&] {
        auto copy = Snapshot();
        while (running.load()) {
            if (api->CopySnapshot(owner, &copy) != Status::Ok) { ++errors; break; }
            const auto value = copy.placements[0].position[0];
            for (const auto& p : copy.placements)
                if (p.position[0] != value || p.rotation[0] != value) { ++errors; break; }
        }
    });
    for (unsigned step = 1; step <= 4000; ++step) {
        auto next = Snapshot();
        if (api->CopySnapshot(owner, &next) != Status::Ok) { ++errors; break; }
        for (auto& p : next.placements) { p.position[0] = float(step); p.rotation[0] = float(step); }
        if (api->ReplaceSnapshot(owner, &next, &next) != Status::Ok) { ++errors; break; }
    }
    running = false;
    reader.join();
    CHECK(errors == 0);
    CHECK(api->CopySnapshot(owner, &current) == Status::Ok);
    CHECK(current.placements[13].position[0] == 4000 && current.placements[13].rotation[0] == 4000);
    const auto lastRevision = current.revision;
    CHECK(api->ReleaseOwner(owner + 1) == Status::Unauthorized);
    CHECK(api->ReleaseOwner(owner) == Status::Ok);
    CHECK(api->ReleaseOwner(owner) == Status::Unauthorized);
    CHECK(api->CopySnapshot(owner, &current) == Status::Unauthorized);
    CHECK(api->AcquireOwner("next", &initial, &other, &current) == Status::Ok);
    CHECK(other != owner && current.revision > lastRevision);
    CHECK(api->ReplaceSnapshot(owner, &current, &current) == Status::Unauthorized);
    CHECK(api->ReleaseOwner(other) == Status::Ok);
    CHECK(FreeLibrary(module));
    std::cout << checks << " native placement ABI checks passed; concurrent whole-bank publication exercised\n";
}
