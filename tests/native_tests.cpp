#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "body_slots_api.h"
#include "package.h"
#include "validation.h"
#include "common/ITypes.h"
#include "skse64/PluginAPI.h"
#include "skse64_common/skse_version.h"
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace body_slots;
namespace fs = std::filesystem;
static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)
Definition Make(const char* provider, const char* key) {
    Definition d{}; d.structSize = sizeof(d); d.flags = kEnabled; std::strcpy(d.provider, provider); std::strcpy(d.key, key);
    std::strcpy(d.anchor, "NPC Pelvis [Pelv]"); std::strcpy(d.referenceRig, "skyrimvr-humanoid-v1"); std::strcpy(d.kind, "potion-pouch");
    d.position[0] = 6; d.position[1] = 14; d.regionCenter[2] = 2;
    d.rotation[0] = d.rotation[4] = d.rotation[8] = 1; d.radius = 10; d.handMask = Left; d.contentMask = Potion;
    return d;
}
struct Snapshot { std::array<SlotRecord, kMaxDefinitions> records{}; std::uint32_t count = 0; std::uint64_t revision = 0; };
Snapshot Copy(const Api& api, std::uint64_t client) {
    Snapshot s{}; CHECK(api.CopyDefinitions(client, 0, s.records.data(), kMaxDefinitions, &s.count, &s.revision) == Status::Ok); return s;
}
void Same(const Snapshot& a, const Snapshot& b) {
    CHECK(a.count == b.count && a.revision == b.revision);
    CHECK(std::memcmp(a.records.data(), b.records.data(), a.count * sizeof(SlotRecord)) == 0);
}
void Core(const Api& api, const fs::path& dllDirectory) {
    std::uint64_t owner = 0, other = 0, sentinel = 12345;
    CHECK(api.RegisterClient("test.owner", &owner) == Status::Ok);
    CHECK(api.RegisterClient("test.observer", &other) == Status::Ok && other != owner);
    CHECK(api.RegisterClient("test.owner", &sentinel) == Status::Conflict && sentinel == 12345);
    CHECK(api.RegisterClient("bad:name", &sentinel) == Status::InvalidArgument && sentinel == 12345);
    CHECK(api.RegisterClient("", &sentinel) == Status::InvalidArgument);
    std::string longName(64, 'x'); CHECK(api.RegisterClient(longName.c_str(), &sentinel) == Status::InvalidArgument);
    CHECK(api.RegisterClient("ok", nullptr) == Status::InvalidArgument);
    Definition d = Make("test.owner", "new-potion"); SlotRecord first{};
    CHECK(api.UpsertDefinition(owner, &d, 0, &first) == Status::Ok && first.slotId && first.definitionRevision);
    auto before = Copy(api, other); CHECK(before.count == 1 && before.records[0].slotId == first.slotId);
    SlotRecord output{}; std::memset(&output, 0xA5, sizeof(output)); const auto saved = output;
    std::uint64_t observerLease = 0; CHECK(api.RegisterObserver(&observerLease) == Status::Ok && observerLease != owner);
    Same(before, Copy(api, observerLease));
    CHECK(api.UpsertDefinition(observerLease, &d, first.definitionRevision, &output) == Status::Unauthorized);
    CHECK(api.RemoveDefinition(observerLease, d.key, first.definitionRevision) == Status::Unauthorized);
    CHECK(api.ReplaceDefinitions(observerLease, &d, 1, before.revision) == Status::Unauthorized);
    CHECK(api.ReplaceDefinitions(observerLease, nullptr, 0, before.revision) == Status::Unauthorized);
    CHECK(api.ReleaseClient(observerLease) == Status::Ok); Same(before, Copy(api, other));
    CHECK(api.CopyDefinitions(observerLease, 0, &output, 1, &before.count, &before.revision) == Status::Unauthorized);
    CHECK(api.UpsertDefinition(other, &d, first.definitionRevision, &output) == Status::Unauthorized);
    CHECK(std::memcmp(&output, &saved, sizeof(output)) == 0);
    CHECK(api.RemoveDefinition(other, d.key, first.definitionRevision) == Status::NotFound);
    CHECK(api.UpsertDefinition(owner, &d, 0, &output) == Status::Conflict);
    CHECK(api.RemoveDefinition(owner, d.key, first.definitionRevision + 1) == Status::Conflict);
    CHECK(api.UpsertDefinition(owner, &d, first.definitionRevision + 1, &output) == Status::Conflict);
    Same(before, Copy(api, other));
    std::uint32_t count = 999; std::uint64_t revision = 999;
    CHECK(api.CopyDefinitions(other, 0, nullptr, 0, &count, &revision) == Status::BufferTooSmall && count == 1 && revision == before.revision);
    CHECK(api.CopyDefinitions(other, 0, &output, 0, &count, &revision) == Status::BufferTooSmall);
    CHECK(std::memcmp(&output, &saved, sizeof(output)) == 0);
    CHECK(api.CopyDefinitions(other, before.revision + 1, &output, 1, &count, &revision) == Status::Conflict && count == 1 && revision == before.revision);
    CHECK(std::memcmp(&output, &saved, sizeof(output)) == 0);
    CHECK(api.CopyDefinitions(other, 0, nullptr, 1, &count, &revision) == Status::InvalidArgument);
    CHECK(api.CopyDefinitions(other, 0, &output, 257, &count, &revision) == Status::InvalidArgument);
    std::uint32_t allowed = 99;
    CHECK(api.EvaluateEligibility(other, first.slotId, first.definitionRevision, Left, Potion, &allowed) == Status::Ok && allowed == 1);
    CHECK(api.EvaluateEligibility(other, first.slotId, first.definitionRevision, Right, Potion, &allowed) == Status::Ok && allowed == 0);
    CHECK(api.EvaluateEligibility(other, first.slotId, first.definitionRevision, Left, Weapon, &allowed) == Status::Ok && allowed == 0);
    allowed = 99;
    CHECK(api.EvaluateEligibility(other, first.slotId, first.definitionRevision, Both, Potion, &allowed) == Status::InvalidArgument && allowed == 99);
    CHECK(api.EvaluateEligibility(other, first.slotId, first.definitionRevision, Left, Potion | Food, &allowed) == Status::InvalidArgument && allowed == 99);
    CHECK(api.EvaluateEligibility(other, first.slotId, 0, Left, Potion, &allowed) == Status::Conflict && allowed == 99);
    CHECK(api.EvaluateEligibility(other, 999999, first.definitionRevision, Left, Potion, &allowed) == Status::NotFound);
    const std::vector<Definition> invalid = [&] {
        std::vector<Definition> cases; auto add = [&](auto change) { auto bad = d; change(bad); cases.push_back(bad); };
        add([](auto& x) { x.structSize = 384; }); add([](auto& x) { x.flags = 2; }); add([](auto& x) { x.reserved = 1; });
        add([](auto& x) { std::memset(x.key, 'a', sizeof(x.key)); }); add([](auto& x) { x.key[0] = static_cast<char>(128); });
        add([](auto& x) { x.key[63] = 'a'; }); add([](auto& x) { x.provider[0] = ':'; }); add([](auto& x) { x.anchor[0] = 0; });
        add([](auto& x) { x.position[0] = std::numeric_limits<float>::quiet_NaN(); });
        add([](auto& x) { x.regionCenter[0] = std::numeric_limits<float>::infinity(); }); add([](auto& x) { x.position[0] = 1000001; });
        add([](auto& x) { x.rotation[0] = 2; }); add([](auto& x) { x.rotation[0] = -1; }); add([](auto& x) { x.rotation[3] = 0.1f; });
        add([](auto& x) { x.rotation[0] = std::numeric_limits<float>::infinity(); });
        add([](auto& x) { x.radius = 0; }); add([](auto& x) { x.radius = -1; }); add([](auto& x) { x.radius = std::numeric_limits<float>::quiet_NaN(); });
        add([](auto& x) { x.handMask = 0; }); add([](auto& x) { x.handMask = 4; }); add([](auto& x) { x.contentMask = 0; });
        add([](auto& x) { x.contentMask = 128; }); add([](auto& x) { x.scaleMode = 1; }); return cases;
    }();
    for (const auto& bad : invalid) {
        CHECK(api.UpsertDefinition(owner, &bad, first.definitionRevision, &output) == Status::InvalidArgument);
        CHECK(std::memcmp(&output, &saved, sizeof(output)) == 0);
    }
    auto badRig = d; std::memset(badRig.referenceRig, 0, sizeof(badRig.referenceRig)); std::strcpy(badRig.referenceRig, "unknown-rig");
    CHECK(api.UpsertDefinition(owner, &badRig, first.definitionRevision, &output) == Status::UnsupportedVersion);
    CHECK(api.UpsertDefinition(owner, nullptr, 0, &output) == Status::InvalidArgument);
    Same(before, Copy(api, other));
    d.position[0] = 7; SlotRecord updated{};
    CHECK(api.UpsertDefinition(owner, &d, first.definitionRevision, &updated) == Status::Ok && updated.slotId == first.slotId && updated.definitionRevision > first.definitionRevision);
    CHECK(api.EvaluateEligibility(other, first.slotId, first.definitionRevision, Left, Potion, &allowed) == Status::Conflict);
    before = Copy(api, other);
    auto newSlot = Make("test.owner", "second"); Definition replacement[]{d, newSlot};
    CHECK(api.ReplaceDefinitions(owner, replacement, 2, before.revision - 1) == Status::Conflict); Same(before, Copy(api, other));
    Definition duplicate[]{d, d}; CHECK(api.ReplaceDefinitions(owner, duplicate, 2, before.revision) == Status::Conflict); Same(before, Copy(api, other));
    Definition mixed[]{d, Make("foreign", "third")}; CHECK(api.ReplaceDefinitions(owner, mixed, 2, before.revision) == Status::Unauthorized); Same(before, Copy(api, other));
    mixed[1] = newSlot; mixed[1].radius = 0;
    CHECK(api.ReplaceDefinitions(owner, mixed, 2, before.revision) == Status::InvalidArgument); Same(before, Copy(api, other));
    CHECK(api.ReplaceDefinitions(owner, replacement, 257, before.revision) == Status::LimitExceeded); Same(before, Copy(api, other));
    CHECK(api.ReplaceDefinitions(owner, replacement, 2, before.revision) == Status::Ok);
    before = Copy(api, other); CHECK(before.count == 2);
    CHECK(api.ReplaceDefinitions(owner, replacement, 2, before.revision) == Status::Ok); Same(before, Copy(api, other));
    CHECK(api.ReplaceDefinitions(owner, &newSlot, 1, before.revision) == Status::Ok);
    CHECK(api.EvaluateEligibility(other, first.slotId, updated.definitionRevision, Left, Potion, &allowed) == Status::NotFound);
    SlotRecord recreated{}; CHECK(api.UpsertDefinition(owner, &d, 0, &recreated) == Status::Ok && recreated.slotId > first.slotId && recreated.definitionRevision > updated.definitionRevision);
    // The same short key may be owned by a different provider.
    auto otherDef = Make("test.observer", "new-potion"); SlotRecord otherRecord{};
    CHECK(api.UpsertDefinition(other, &otherDef, 0, &otherRecord) == Status::Ok && otherRecord.slotId != recreated.slotId);
    auto consumer = LoadLibraryW((dllDirectory / L"consumer_fixture.dll").c_str()); CHECK(consumer != nullptr);
    auto consume = reinterpret_cast<int(BS_CALL*)(const Api*, std::uint64_t)>(GetProcAddress(consumer, "BodySlotsConsumer_Run"));
    CHECK(consume && consume(&api, recreated.slotId) == 0); FreeLibrary(consumer);
    auto disabled = d; disabled.flags = 0;
    CHECK(api.UpsertDefinition(owner, &disabled, recreated.definitionRevision, &updated) == Status::Ok);
    CHECK(api.EvaluateEligibility(other, updated.slotId, updated.definitionRevision, Left, Potion, &allowed) == Status::Ok && allowed == 0);
    before = Copy(api, other); CHECK(api.ReleaseClient(owner) == Status::Ok);
    auto after = Copy(api, other); CHECK(after.count == 1 && after.revision > before.revision);
    count = 777; revision = 777;
    CHECK(api.CopyDefinitions(owner, 0, &output, 1, &count, &revision) == Status::Unauthorized && count == 777 && revision == 777);
    CHECK(api.UpsertDefinition(owner, &d, 0, &output) == Status::Unauthorized);
    CHECK(api.ReleaseClient(owner) == Status::Unauthorized);
    std::uint64_t again = 0; CHECK(api.RegisterClient("test.owner", &again) == Status::Ok && again != owner);
    CHECK(api.UpsertDefinition(again, &d, 0, &updated) == Status::Ok && updated.slotId > recreated.slotId);
    CHECK(api.RemoveDefinition(again, d.key, updated.definitionRevision) == Status::Ok);
    CHECK(api.ReleaseClient(again) == Status::Ok); CHECK(api.ReleaseClient(other) == Status::Ok);
    std::cout << "PASS registry, ownership, revisions, atomicity, snapshot outputs, hand/content filters, two DLLs\n";
}
void Limits(const Api& api) {
    std::uint64_t owner = 0, other = 0; CHECK(api.RegisterClient("test.capacity", &owner) == Status::Ok);
    CHECK(api.RegisterClient("test.capacity-other", &other) == Status::Ok);
    std::array<Definition, kMaxDefinitions> defs{};
    for (unsigned i = 0; i < kMaxDefinitions; ++i) defs[i] = Make("test.capacity", std::to_string(i).c_str());
    CHECK(api.ReplaceDefinitions(owner, defs.data(), kMaxDefinitions, 0) == Status::Ok);
    auto before = Copy(api, other); CHECK(before.count == 256);
    auto d = Make("test.capacity-other", "new"); SlotRecord r{};
    CHECK(api.UpsertDefinition(other, &d, 0, &r) == Status::LimitExceeded);
    CHECK(api.ReplaceDefinitions(other, &d, 1, before.revision) == Status::LimitExceeded); Same(before, Copy(api, other));
    CHECK(api.ReleaseClient(owner) == Status::Ok); CHECK(api.ReleaseClient(other) == Status::Ok);
    std::array<std::uint64_t, 64> clients{};
    for (unsigned i = 0; i < clients.size(); ++i) CHECK(api.RegisterClient(("test.clients/" + std::to_string(i)).c_str(), &clients[i]) == Status::Ok);
    std::uint64_t out = 123; CHECK(api.RegisterClient("test.one-too-many", &out) == Status::LimitExceeded && out == 123);
    std::array<std::uint64_t, 64> observers{};
    for (auto& observer : observers) CHECK(api.RegisterObserver(&observer) == Status::Ok);
    CHECK(api.RegisterObserver(&out) == Status::LimitExceeded && out == 123);
    CHECK(api.RegisterObserver(nullptr) == Status::InvalidArgument);
    auto snapshot = Copy(api, observers[0]); CHECK(snapshot.count == 0);
    CHECK(api.ReleaseClient(observers[1]) == Status::Ok); Same(snapshot, Copy(api, observers[0]));
    CHECK(api.RegisterObserver(&observers[1]) == Status::Ok);
    for (auto c : observers) CHECK(api.ReleaseClient(c) == Status::Ok);
    for (auto c : clients) CHECK(api.ReleaseClient(c) == Status::Ok);
    std::cout << "PASS bounded registry and client limits\n";
}
void Parallel(const Api& api) {
    std::uint64_t observer = 0; CHECK(api.RegisterObserver(&observer) == Status::Ok);
    std::array<std::uint64_t, 4> clients{};
    for (unsigned i = 0; i < clients.size(); ++i) CHECK(api.RegisterClient(("test.parallel/" + std::to_string(i)).c_str(), &clients[i]) == Status::Ok);
    std::atomic<bool> failed{false}; std::atomic<unsigned> finished{0}; std::vector<std::thread> workers;
    for (unsigned i = 0; i < clients.size(); ++i) workers.emplace_back([&, i] {
        auto d = Make(("test.parallel/" + std::to_string(i)).c_str(), "slot"); SlotRecord result{}; std::uint64_t expected = 0;
        for (unsigned j = 0; j < 1500; ++j) {
            d.position[0] = static_cast<float>(j);
            if (api.UpsertDefinition(clients[i], &d, expected, &result) != Status::Ok) { failed = true; break; }
            expected = result.definitionRevision;
        }
        ++finished;
    });
    std::uint64_t previous = 0; unsigned snapshots = 0;
    do {
        Snapshot s{};
        if (api.CopyDefinitions(observer, 0, s.records.data(), kMaxDefinitions, &s.count, &s.revision) != Status::Ok || s.revision < previous || s.count > 4) failed = true;
        previous = s.revision;
        for (unsigned i = 0; i < s.count; ++i) {
            if (detail::ValidateDefinition(s.records[i].definition) != Status::Ok || !s.records[i].slotId || !s.records[i].definitionRevision) failed = true;
            for (unsigned j = 0; j < i; ++j) if (s.records[i].slotId == s.records[j].slotId) failed = true;
        }
        ++snapshots;
    } while (finished.load() < 4);
    for (auto& worker : workers) worker.join(); CHECK(!failed && snapshots > 0);
    for (auto c : clients) CHECK(api.ReleaseClient(c) == Status::Ok);
    CHECK(api.ReleaseClient(observer) == Status::Ok);
    std::cout << "PASS parallel 6000 mutations with " << snapshots << " coherent snapshots\n";
}
void Packages(const Api& api, HMODULE dll, const fs::path& source, const fs::path& outputDir) {
    std::uint64_t observer = 0; CHECK(api.RegisterObserver(&observer) == Status::Ok);
    detail::Package package{}; std::string diagnostic;
    CHECK(detail::ReadPackage(source, package, diagnostic) == Status::Ok && package.count == 1);
    CHECK(!std::strcmp(package.provider, "example.potion-belt"));
    const auto expected = Make("example.potion-belt", "potion/front-left");
    CHECK(!std::memcmp(&expected, &package.definitions[0], sizeof(Definition)));
    auto before = Copy(api, observer); std::uint64_t token = 999;
    auto bad = package; bad.definitions[0].radius = 0;
    CHECK(detail::PublishPackage(api, bad, &token) == Status::InvalidArgument && token == 999); Same(before, Copy(api, observer));
    CHECK(detail::PublishPackage(api, package, &token) == Status::Ok && token != 999);
    before = Copy(api, observer); std::uint64_t duplicate = 777;
    CHECK(detail::PublishPackage(api, package, &duplicate) == Status::Conflict && duplicate == 777); Same(before, Copy(api, observer));
    const auto loadPackage = reinterpret_cast<LoadPackage>(GetProcAddress(dll, "BodySlots_LoadPackage")); CHECK(loadPackage);
    CHECK(loadPackage(nullptr, &duplicate) == Status::InvalidArgument && duplicate == 777);
    CHECK(loadPackage(source.c_str(), &duplicate) == Status::Conflict && duplicate == 777); Same(before, Copy(api, observer));
    CHECK(api.ReleaseClient(token) == Status::Ok);
    CHECK(loadPackage(source.c_str(), &token) == Status::Ok);
    before = Copy(api, observer); CHECK(before.count == 1);
    std::ifstream stream(source, std::ios::binary); const std::vector<unsigned char> original{std::istreambuf_iterator<char>(stream), {}};
    auto testBytes = [&](const std::vector<unsigned char>& bytes) {
        const auto path = outputDir / "corrupt.bslots";
        { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
        auto unchanged = package; auto parsed = unchanged;
        CHECK(detail::ReadPackage(path, parsed, diagnostic) != Status::Ok);
        CHECK(!std::memcmp(&parsed, &unchanged, sizeof(detail::Package))); Same(before, Copy(api, observer));
        CHECK(loadPackage(path.c_str(), &duplicate) != Status::Ok && duplicate == 777); Same(before, Copy(api, observer));
    };
    for (std::size_t length : {std::size_t(0), std::size_t(7), std::size_t(111), std::size_t(112), original.size() - 1})
        testBytes(std::vector<unsigned char>(original.begin(), original.begin() + length));
    for (const auto offset : {0u, 8u, 12u, 16u, 20u, 24u, 28u, 32u, 95u, 96u, 112u, 116u, 120u, 112u + 392u, 112u + 396u, 112u + 400u, 112u + 404u}) {
        auto bytes = original; bytes[offset] ^= 0x80; testBytes(bytes);
    }
    auto nonfinite = original; nonfinite[112 + 376] = 0; nonfinite[112 + 377] = 0; nonfinite[112 + 378] = 0x80; nonfinite[112 + 379] = 0x7f; testBytes(nonfinite);
    auto trailing = original; trailing.push_back(0); testBytes(trailing);
    auto oversized = original; oversized.resize(detail::kMaxPackageBytes + 1); testBytes(oversized);
    // Build structurally valid two-record bytes with an invalid second record, then duplicate keys.
    auto two = original; two.insert(two.end(), original.begin() + detail::kPackageHeaderSize, original.end());
    auto u32 = [&](unsigned offset, unsigned value) { for (unsigned i = 0; i < 4; ++i) two[offset + i] = static_cast<unsigned char>(value >> (8 * i)); };
    u32(20, 2); u32(24, 816); testBytes(two);
    two[112 + 408 + 72] = 'x'; u32(112 + 408 + 404, 1); testBytes(two);
    CHECK(api.ReleaseClient(token) == Status::Ok); CHECK(api.ReleaseClient(observer) == Status::Ok);
    std::cout << "PASS independent Python/C++ package layout, atomic publication, malformed/truncated/corrupted packages\n";
}
static SKSEMessagingInterface::EventCallback listener = nullptr;
static bool listenerResult = true;
static SKSEMessagingInterface messaging{};
static void* Interface(UInt32 id) { return id == kInterface_Messaging ? &messaging : nullptr; }
static PluginHandle Handle() { return 42; }
static bool Listen(PluginHandle handle, const char* sender, SKSEMessagingInterface::EventCallback callback) {
    if (handle != 42 || std::strcmp(sender, "SKSE")) return false;
    if (listenerResult) listener = callback; return listenerResult;
}
void Skse(const Api& api, HMODULE dll, bool fakeHost) {
    auto query = reinterpret_cast<_SKSEPlugin_Query>(GetProcAddress(dll, "SKSEPlugin_Query"));
    auto load = reinterpret_cast<_SKSEPlugin_Load>(GetProcAddress(dll, "SKSEPlugin_Load")); CHECK(query && load);
    SKSEInterface skse{}; skse.runtimeVersion = RUNTIME_VR_VERSION_1_4_15; skse.skseVersion = PACKED_SKSE_VERSION;
    skse.QueryInterface = Interface; skse.GetPluginHandle = Handle; PluginInfo info{};
    CHECK(!query(nullptr, &info) && !load(nullptr)); CHECK(!query(&skse, nullptr));
    CHECK(query(&skse, &info) == fakeHost && !std::strcmp(info.name, "BodySlots"));
    if (!fakeHost) { CHECK(!load(&skse)); std::cout << "PASS native DLL safety and non-game SKSE host rejection\n"; return; }
    auto bad = skse; bad.runtimeVersion = RUNTIME_VERSION_1_5_80; CHECK(!query(&bad, &info) && !load(&bad));
    bad = skse; bad.isEditor = 1; CHECK(!query(&bad, &info) && !load(&bad));
    bad = skse; bad.skseVersion = 0; CHECK(!query(&bad, &info) && !load(&bad));
    CHECK(!load(&skse)); messaging.interfaceVersion = 1; messaging.RegisterListener = Listen; CHECK(!load(&skse));
    messaging.interfaceVersion = SKSEMessagingInterface::kInterfaceVersion; listenerResult = false; CHECK(!load(&skse));
    listenerResult = true; CHECK(load(&skse) && listener); CHECK(!load(&skse));
    std::uint64_t observer = 0; CHECK(api.RegisterObserver(&observer) == Status::Ok);
    CHECK(Copy(api, observer).count == 0);
    SKSEMessagingInterface::Message message{"other", SKSEMessagingInterface::kMessage_PostLoad, 0, nullptr};
    listener(&message); CHECK(Copy(api, observer).count == 0);
    message.sender = "SKSE"; message.type = SKSEMessagingInterface::kMessage_PostPostLoad; listener(&message);
    CHECK(Copy(api, observer).count == 0);
    message.type = SKSEMessagingInterface::kMessage_PostLoad; listener(&message);
    auto snapshot = Copy(api, observer); CHECK(snapshot.count == 1 && !std::strcmp(snapshot.records[0].definition.provider, "example.potion-belt"));
    listener(&message); Same(snapshot, Copy(api, observer));
    CHECK(api.ReleaseClient(observer) == Status::Ok);
    std::cout << "PASS simulated public SKSE messaging, guards, PostLoad-only package load, corrupt package isolation\n";
}
int wmain(int argc, wchar_t** argv) {
    try {
        const bool skseMode = argc > 1 && !std::wcscmp(argv[1], L"--skse");
        CHECK(argc == (skseMode ? 4 : 4));
        const fs::path dllPath = argv[skseMode ? 2 : 1], packagePath = argv[skseMode ? 3 : 2];
        HMODULE dll = LoadLibraryW(dllPath.c_str()); CHECK(dll != nullptr);
        const auto query = reinterpret_cast<QueryInterface>(GetProcAddress(dll, "BodySlots_QueryInterface")); CHECK(query);
        CHECK(query(0, sizeof(Api)) == nullptr && query(kApiVersion, sizeof(Api) - 1) == nullptr);
        const auto* api = query(kApiVersion, sizeof(Api)); CHECK(api && api->structSize == sizeof(Api) && api->capabilities == 7);
        if (skseMode) Skse(*api, dll, true);
        else { Skse(*api, dll, false); Core(*api, dllPath.parent_path()); Limits(*api); Parallel(*api); Packages(*api, dll, packagePath, argv[3]); }
        FreeLibrary(dll); std::cout << "ALL PASS: " << checks << " checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
