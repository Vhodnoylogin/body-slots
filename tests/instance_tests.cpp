#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "body_slots_instance_api.h"
#include "body_slots_configuration_api.h"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace body_slots;
namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)
struct Snapshot { InstanceView view{sizeof(InstanceView), kInstanceApiVersion, 0, 0, 0, 0, 0, 0}; std::array<InstanceRecord, kMaxInstances> rows{}; };
Snapshot Copy(const InstanceApi& api, std::uint64_t reader) {
    Snapshot out; CHECK(api.CopyEffectiveInstances(reader, 0, 0, out.rows.data(), kMaxInstances, &out.view) == Status::Ok); return out;
}
void Same(const Snapshot& a, const Snapshot& b) {
    CHECK(std::memcmp(&a.view, &b.view, sizeof(a.view)) == 0);
    CHECK(std::memcmp(a.rows.data(), b.rows.data(), a.view.count * sizeof(InstanceRecord)) == 0);
}
Definition Template(const char* key = "belt") {
    Definition d{}; d.structSize = sizeof(d); d.flags = kEnabled;
    std::strcpy(d.provider, "template"); std::strcpy(d.key, key); std::strcpy(d.anchor, "NPC Pelvis [Pelv]");
    std::strcpy(d.referenceRig, "skyrimvr-humanoid-v1"); std::strcpy(d.kind, "pouch");
    d.rotation[0] = d.rotation[4] = d.rotation[8] = 1; d.radius = 5; d.position[0] = 2;
    d.handMask = Left; d.contentMask = Potion; return d;
}
InstanceSpec Spec(const SlotRecord& definition, const char* equipment, std::uint32_t flags = kInstanceActive) {
    InstanceSpec out{}; out.structSize = sizeof(out); out.flags = flags;
    std::strcpy(out.equipmentKey, equipment); std::strcpy(out.slotKey, "opening");
    std::strcpy(out.definitionProvider, definition.definition.provider); std::strcpy(out.definitionKey, definition.definition.key);
    out.definitionRevision = definition.definitionRevision; out.binding = static_cast<std::uint32_t>(InstanceBinding::BodyAnchor); return out;
}
InstanceRecord Output() { InstanceRecord out{}; out.structSize = sizeof(out); return out; }
}
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2); auto dll = LoadLibraryA(argv[1]); CHECK(dll);
        const auto query = reinterpret_cast<QueryInstanceInterface>(GetProcAddress(dll, "BodySlots_QueryInstanceInterface"));
        const auto baseQuery = reinterpret_cast<QueryInterface>(GetProcAddress(dll, "BodySlots_QueryInterface"));
        const auto configQuery = reinterpret_cast<QueryConfigurationInterface>(GetProcAddress(dll, "BodySlots_QueryConfigurationInterface"));
        CHECK(query && baseQuery && configQuery);
        CHECK(!query(0, sizeof(InstanceApi)) && !query(1, sizeof(InstanceApi) - 1) && !query(1, sizeof(InstanceApi) + 1));
        const auto* api = query(1, sizeof(InstanceApi)); const auto* base = baseQuery(1, sizeof(Api)); const auto* config = configQuery(1, sizeof(ConfigurationApi));
        CHECK(api && base && config && api->structSize == 48 && api->version == 1 && api->capabilities == 3);
        CHECK(sizeof(InstanceSpec) == 280 && sizeof(InstanceRecord) == 784 && sizeof(InstanceView) == 48);
        std::uint64_t author = 0, owner = 0, other = 0, observer = 0, editor = 0;
        CHECK(base->RegisterClient("template", &author) == Status::Ok);
        CHECK(base->RegisterClient("equipment", &owner) == Status::Ok);
        CHECK(base->RegisterClient("other", &other) == Status::Ok);
        CHECK(base->RegisterObserver(&observer) == Status::Ok);
        CHECK(config->RegisterCustomizer("instance-editor", &editor) == Status::Ok);
        auto empty = Copy(*api, observer); CHECK(empty.view.count == 0 && empty.view.saveGeneration && empty.view.viewRevision);
        auto d = Template(); SlotRecord definition{};
        CHECK(base->UpsertDefinition(author, &d, 0, &definition) == Status::Ok);
        auto before = Copy(*api, observer); CHECK(before.view.viewRevision > empty.view.viewRevision);
        auto spec = Spec(definition, "bag-A"); auto first = Output();
        CHECK(api->UpsertInstance(owner, &spec, before.view.saveGeneration, 0, &first) == Status::Ok);
        CHECK((first.instanceHandle >> 56) == 3 && first.instanceRevision && !std::strcmp(first.key.ownerProvider, "equipment"));
        auto secondSpec = Spec(definition, "bag-B", 0); auto second = Output();
        CHECK(api->UpsertInstance(owner, &secondSpec, before.view.saveGeneration, 0, &second) == Status::Ok);
        CHECK(second.instanceHandle != first.instanceHandle && second.pinnedDefinitionRevision == first.pinnedDefinitionRevision);
        before = Copy(*api, observer); CHECK(before.view.count == 2);
        Same(before, Copy(*api, owner)); Same(before, Copy(*api, editor));
        std::uint32_t count = 0; std::uint64_t revision = 0;
        CHECK(base->CopyDefinitions(observer, 0, &definition, 1, &count, &revision) == Status::Ok && count == 1);
        auto committed = Output();
        CHECK(api->UpsertInstance(owner, &spec, before.view.saveGeneration, first.instanceRevision, &committed) == Status::Ok);
        CHECK(std::memcmp(&committed, &first, sizeof(first)) == 0); Same(before, Copy(*api, observer));
        auto sentinel = Output(); sentinel.flags = 0xa5a5a5a5; auto rejected = sentinel;
        CHECK(api->UpsertInstance(owner, &spec, 0, first.instanceRevision, &rejected) == Status::Conflict);
        CHECK(api->UpsertInstance(owner, &spec, before.view.saveGeneration + 1, first.instanceRevision, &rejected) == Status::Conflict);
        CHECK(api->UpsertInstance(owner, &spec, before.view.saveGeneration, 0, &rejected) == Status::Conflict);
        CHECK(api->UpsertInstance(observer, &spec, before.view.saveGeneration, 0, &rejected) == Status::Unauthorized);
        CHECK(api->UpsertInstance(editor, &spec, before.view.saveGeneration, 0, &rejected) == Status::Unauthorized);
        CHECK(api->RemoveInstance(other, first.instanceHandle, before.view.saveGeneration, first.instanceRevision) == Status::Unauthorized);
        CHECK(api->RemoveInstance(owner, first.instanceHandle, before.view.saveGeneration, first.instanceRevision + 1) == Status::Conflict);
        CHECK(api->RemoveInstance(owner, definition.slotId, before.view.saveGeneration, first.instanceRevision) == Status::NotFound);
        CHECK(std::memcmp(&rejected, &sentinel, sizeof(sentinel)) == 0); Same(before, Copy(*api, observer));
        for (unsigned mutation = 0; mutation < 7; ++mutation) {
            auto invalid = spec;
            if (mutation == 0) invalid.flags = 2;
            if (mutation == 1) invalid.reserved = 1;
            if (mutation == 2) invalid.equipmentKey[63] = 'x';
            if (mutation == 3) invalid.slotKey[0] = ':';
            if (mutation == 4) invalid.definitionRevision = 0;
            if (mutation == 5) invalid.binding = 2;
            if (mutation == 6) --invalid.structSize;
            const auto status = api->UpsertInstance(owner, &invalid, before.view.saveGeneration, first.instanceRevision, &rejected);
            CHECK(status == (mutation >= 5 ? Status::UnsupportedVersion : Status::InvalidArgument));
            CHECK(std::memcmp(&rejected, &sentinel, sizeof(sentinel)) == 0); Same(before, Copy(*api, observer));
        }
        std::array<InstanceRecord, 2> untouched{}; std::memset(untouched.data(), 0xa5, sizeof(untouched)); const auto untouchedBefore = untouched;
        auto header = before.view;
        CHECK(api->CopyEffectiveInstances(observer, header.saveGeneration, header.viewRevision - 1, untouched.data(), 2, &header) == Status::Conflict);
        CHECK(header.viewRevision == before.view.viewRevision && std::memcmp(untouched.data(), untouchedBefore.data(), sizeof(untouched)) == 0);
        CHECK(api->CopyEffectiveInstances(observer, 0, 0, untouched.data(), 1, &header) == Status::BufferTooSmall && header.count == 2);
        CHECK(std::memcmp(untouched.data(), untouchedBefore.data(), sizeof(untouched)) == 0);
        header.version = 99; const auto badHeader = header;
        CHECK(api->CopyEffectiveInstances(observer, 0, 0, untouched.data(), 2, &header) == Status::UnsupportedVersion);
        CHECK(std::memcmp(&header, &badHeader, sizeof(header)) == 0);
        header = before.view;
        CHECK(api->CopyEffectiveInstances(first.instanceHandle, 0, 0, untouched.data(), 2, &header) == Status::Unauthorized);
        CHECK(std::memcmp(&header, &before.view, sizeof(header)) == 0);
        std::uint32_t allowed = 99;
        CHECK(api->EvaluateInstanceEligibility(observer, first.instanceHandle, header.saveGeneration, header.viewRevision, Left, Potion, &allowed) == Status::Ok && allowed == 1);
        CHECK(api->EvaluateInstanceEligibility(editor, first.instanceHandle, header.saveGeneration, header.viewRevision, Right, Potion, &allowed) == Status::Ok && allowed == 0);
        CHECK(api->EvaluateInstanceEligibility(observer, second.instanceHandle, header.saveGeneration, header.viewRevision, Left, Potion, &allowed) == Status::Ok && allowed == 0);
        allowed = 99;
        CHECK(api->EvaluateInstanceEligibility(observer, first.instanceHandle, header.saveGeneration, header.viewRevision, Both, Potion, &allowed) == Status::InvalidArgument && allowed == 99);
        auto third = Output(); CHECK(api->UpsertInstance(other, &spec, header.saveGeneration, 0, &third) == Status::Ok);
        CHECK(third.instanceHandle != first.instanceHandle && !std::strcmp(third.key.ownerProvider, "other"));
        auto withOther = Copy(*api, observer); CHECK(withOther.view.count == 3);
        CHECK(base->ReleaseClient(other) == Status::Ok); // no authored definitions owned by this writer
        before = Copy(*api, observer); CHECK(before.view.count == 2 && before.view.viewRevision > withOther.view.viewRevision);
        CHECK(api->EvaluateInstanceEligibility(observer, first.instanceHandle, header.saveGeneration, header.viewRevision, Left, Potion, &allowed) == Status::Conflict && allowed == 99);

        d.position[0] = 9; CHECK(base->UpsertDefinition(author, &d, definition.definitionRevision, &definition) == Status::Ok);
        auto changed = Copy(*api, observer);
        CHECK(changed.rows[0].templateState == static_cast<unsigned>(InstanceTemplateState::Changed));
        Definition zero{}; CHECK(std::memcmp(&changed.rows[0].effective, &zero, sizeof(zero)) == 0);
        CHECK(changed.rows[0].instanceRevision == first.instanceRevision);
        CHECK(api->UpsertInstance(owner, &spec, changed.view.saveGeneration, first.instanceRevision, &rejected) == Status::Conflict);
        spec.definitionRevision = definition.definitionRevision;
        CHECK(api->UpsertInstance(owner, &spec, changed.view.saveGeneration, first.instanceRevision, &first) == Status::Ok);
        CHECK(first.effective.position[0] == 9 && first.templateState == 0);
        CHECK(base->RemoveDefinition(author, d.key, definition.definitionRevision) == Status::Ok);
        changed = Copy(*api, observer); CHECK(changed.rows[0].templateState == 1 && changed.rows[1].templateState == 1 && changed.rows[0].currentDefinitionRevision == 0);
        CHECK(base->UpsertDefinition(author, &d, 0, &definition) == Status::Ok);
        changed = Copy(*api, observer); CHECK(changed.rows[0].templateState == 2 && changed.rows[1].templateState == 2);
        spec.definitionRevision = secondSpec.definitionRevision = definition.definitionRevision; secondSpec.flags = kInstanceActive;
        CHECK(api->UpsertInstance(owner, &spec, changed.view.saveGeneration, first.instanceRevision, &first) == Status::Ok);
        CHECK(api->UpsertInstance(owner, &secondSpec, changed.view.saveGeneration, second.instanceRevision, &second) == Status::Ok);
        auto otherTemplate = Template("other-template"); SlotRecord otherDefinition{};
        CHECK(base->UpsertDefinition(author, &otherTemplate, 0, &otherDefinition) == Status::Ok);
        auto retarget = spec; std::strcpy(retarget.definitionKey, otherTemplate.key); retarget.definitionRevision = otherDefinition.definitionRevision;
        CHECK(api->UpsertInstance(owner, &retarget, changed.view.saveGeneration, first.instanceRevision, &rejected) == Status::Conflict);
        before = Copy(*api, observer);
        Customization custom{}; custom.structSize = sizeof(custom); custom.fieldMask = CustomizePosition;
        std::strcpy(custom.provider, "template"); std::strcpy(custom.key, "belt"); custom.position[0] = 42;
        EffectiveSlotRecord effective{};
        CHECK(config->ApplyCustomization(editor, &custom, before.view.configurationRevision, &effective) == Status::Ok);
        changed = Copy(*api, observer);
        CHECK(changed.view.viewRevision > before.view.viewRevision && changed.view.configurationRevision > before.view.configurationRevision);
        CHECK(changed.rows[0].effective.position[0] == 42 && changed.rows[1].effective.position[0] == 42);
        CHECK(changed.rows[0].pinnedDefinitionRevision == definition.definitionRevision && changed.rows[0].instanceRevision == first.instanceRevision);
        CHECK(config->ApplyCustomization(editor, &custom, changed.view.configurationRevision, &effective) == Status::Ok); Same(changed, Copy(*api, observer));
        CHECK(config->ClearCustomization(editor, "template", "belt", changed.view.configurationRevision) == Status::Ok);

        std::atomic<bool> done{false}, failed{false};
        std::thread publisher([&] {
            std::uint32_t n = 0; std::uint64_t currentRevision = 0;
            std::array<EffectiveSlotRecord, kMaxDefinitions> records{};
            if (config->CopyEffectiveDefinitions(observer, 0, records.data(), kMaxDefinitions, &n, &currentRevision) != Status::Ok) failed = true;
            for (unsigned tick = 0; tick < 1000 && !failed; ++tick) {
                auto patch = custom; patch.position[0] = float(tick % 2);
                EffectiveSlotRecord result{};
                if (config->ApplyCustomization(editor, &patch, currentRevision, &result) != Status::Ok) { failed = true; break; }
                currentRevision = result.configurationRevision;
            }
            done = true;
        });
        unsigned samples = 0;
        do {
            auto current = Copy(*api, observer); ++samples;
            CHECK(current.view.count == 2 && current.view.saveGeneration == before.view.saveGeneration);
            CHECK(current.rows[0].templateState == 0 && current.rows[1].templateState == 0);
            CHECK(std::memcmp(&current.rows[0].effective, &current.rows[1].effective, sizeof(Definition)) == 0);
        } while (!done);
        publisher.join(); CHECK(!failed && samples > 0);
        before = Copy(*api, observer);
        CHECK(api->RemoveInstance(owner, first.instanceHandle, before.view.saveGeneration, first.instanceRevision) == Status::Ok);
        auto recreated = Output(); CHECK(api->UpsertInstance(owner, &spec, before.view.saveGeneration, 0, &recreated) == Status::Ok);
        CHECK(recreated.instanceHandle > first.instanceHandle && recreated.instanceRevision > first.instanceRevision);
        before = Copy(*api, observer);
        CHECK(api->RemoveInstance(owner, first.instanceHandle, before.view.saveGeneration, first.instanceRevision) == Status::NotFound);
        CHECK(base->ReleaseClient(owner) == Status::Ok); CHECK(Copy(*api, observer).view.count == 0);
        CHECK(base->RegisterClient("capacity", &owner) == Status::Ok);
        const auto capacityScope = Copy(*api, observer).view.saveGeneration;
        for (unsigned index = 0; index < kMaxInstances; ++index) {
            const auto key = std::string("bag/") + std::to_string(index); auto input = Spec(definition, key.c_str());
            auto output = Output(); CHECK(api->UpsertInstance(owner, &input, capacityScope, 0, &output) == Status::Ok);
        }
        before = Copy(*api, observer); CHECK(before.view.count == kMaxInstances);
        auto tooMany = Spec(definition, "one-too-many"); rejected = sentinel;
        CHECK(api->UpsertInstance(owner, &tooMany, capacityScope, 0, &rejected) == Status::LimitExceeded);
        CHECK(std::memcmp(&rejected, &sentinel, sizeof(rejected)) == 0); Same(before, Copy(*api, observer));
        CHECK(base->ReleaseClient(author) == Status::Ok);
        changed = Copy(*api, observer); CHECK(changed.view.count == kMaxInstances && changed.rows[0].templateState == 1);
        CHECK(base->ReleaseClient(owner) == Status::Ok); CHECK(Copy(*api, observer).view.count == 0);
        CHECK(config->ReleaseCustomizer(editor) == Status::Ok); CHECK(base->ReleaseClient(observer) == Status::Ok);
        CHECK(FreeLibrary(dll));
        std::cout << checks << " instance ABI checks passed; independent equipment identities, revision pinning, coherent inherited configuration, release without definitions and transient ABA rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
