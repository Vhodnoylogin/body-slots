#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "instance_configuration.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using namespace body_slots;
namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)
struct View {
    InstanceView header{sizeof(InstanceView), 1, 0, 0, 0, 0, 0, 0};
    std::array<ConfiguredInstanceRecord, kMaxInstances> records{};
};
View Copy(const InstanceConfigurationApi& api, std::uint64_t reader) {
    View out; CHECK(api.CopyConfiguredInstances(reader, 0, 0, out.records.data(), kMaxInstances, &out.header) == Status::Ok); return out;
}
InstanceView Saved(const InstanceConfigurationApi& api, std::uint64_t reader, InstanceCustomizationRecord* rows, std::uint32_t capacity) {
    InstanceView view{sizeof(view), 1, 0, 0, 0, 0, 0, 0}; CHECK(api.CopyCustomizations(reader, 0, 0, rows, capacity, &view) == Status::Ok); return view;
}
Definition Make() {
    Definition d{}; d.structSize = sizeof(d); d.flags = kEnabled;
    std::strcpy(d.provider, "templates"); std::strcpy(d.key, "belt"); std::strcpy(d.anchor, "NPC Pelvis [Pelv]");
    std::strcpy(d.referenceRig, "skyrimvr-humanoid-v1"); std::strcpy(d.kind, "pouch");
    d.rotation[0] = d.rotation[4] = d.rotation[8] = 1; d.radius = 5; d.position[0] = 2; d.handMask = Left; d.contentMask = Potion; return d;
}
InstanceSpec Spec(const SlotRecord& definition, const char* bag) {
    InstanceSpec spec{}; spec.structSize = sizeof(spec); spec.flags = kInstanceActive;
    std::strcpy(spec.equipmentKey, bag); std::strcpy(spec.slotKey, "opening"); std::strcpy(spec.definitionProvider, "templates");
    std::strcpy(spec.definitionKey, "belt"); spec.definitionRevision = definition.definitionRevision; spec.binding = 1; return spec;
}
InstanceCustomization Patch(const InstanceRecord& record, float position) {
    InstanceCustomization value{}; value.structSize = sizeof(value); value.fieldMask = CustomizePosition;
    value.key.instance = record.key; std::memcpy(value.key.definitionProvider, record.definitionProvider, 64);
    std::memcpy(value.key.definitionKey, record.definitionKey, 64); value.position[0] = position; return value;
}
ConfiguredInstanceRecord Output() { ConfiguredInstanceRecord out{}; out.instance.structSize = sizeof(InstanceRecord); return out; }
std::uint32_t U32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t out = 0; for (unsigned i = 0; i < 4; ++i) out |= std::uint32_t(bytes[offset + i]) << (8 * i); return out;
}
void Codec(const InstanceCustomization& source) {
    using namespace body_slots::detail;
    auto data = std::make_unique<InstanceCustomizationData>(); data->count = 2;
    data->entries[0] = source; data->entries[1] = source;
    std::strcpy(data->entries[0].key.instance.equipmentKey, "z-bag");
    std::strcpy(data->entries[1].key.instance.equipmentKey, "a-bag");
    std::vector<std::uint8_t> bytes;
    CHECK(EncodeInstanceCustomizations(*data, bytes) == Status::Ok && bytes.size() == 816);
    CHECK(U32(bytes, 0) == 0x49435342 && U32(bytes, 4) == 1 && U32(bytes, 8) == 400 && U32(bytes, 12) == 2);
    auto reverse = std::make_unique<InstanceCustomizationData>(); *reverse = *data;
    std::swap(reverse->entries[0], reverse->entries[1]); std::vector<std::uint8_t> ordered;
    CHECK(EncodeInstanceCustomizations(*reverse, ordered) == Status::Ok && ordered == bytes);
    auto decoded = std::make_unique<InstanceCustomizationData>();
    CHECK(DecodeInstanceCustomizations(bytes.data(), static_cast<std::uint32_t>(bytes.size()), *decoded) == Status::Ok);
    CHECK(decoded->count == 2 && !std::strcmp(decoded->entries[0].key.instance.equipmentKey, "a-bag"));
    CHECK(decoded->entries[0].position[0] == source.position[0]);
    const auto original = std::make_unique<InstanceCustomizationData>(*decoded);
    auto reject = [&](std::vector<std::uint8_t> bad) {
        CHECK(DecodeInstanceCustomizations(bad.data(), static_cast<std::uint32_t>(bad.size()), *decoded) != Status::Ok);
        CHECK(std::memcmp(decoded.get(), original.get(), sizeof(*decoded)) == 0);
    };
    for (const auto offset : {0u, 4u, 8u, 12u, 16u, 20u, 24u, 16u + 399u}) {
        auto bad = bytes; bad[offset] ^= 0x80; reject(std::move(bad));
    }
    auto duplicate = bytes; std::copy(duplicate.begin() + 16, duplicate.begin() + 416, duplicate.begin() + 416); reject(std::move(duplicate));
    auto nan = bytes; nan[16 + 328] = 0; nan[16 + 329] = 0; nan[16 + 330] = 0xc0; nan[16 + 331] = 0x7f; reject(std::move(nan));
    auto truncated = bytes; truncated.pop_back(); reject(std::move(truncated));
    auto trailing = bytes; trailing.push_back(0); reject(std::move(trailing));
    reject(std::vector<std::uint8_t>(kMaxInstanceCustomizationSaveBytes + 1));
    data->count = kMaxInstanceCustomizations;
    for (unsigned i = 0; i < data->count; ++i) {
        data->entries[i] = source; std::memset(data->entries[i].key.instance.equipmentKey, 0, 64);
        std::strcpy(data->entries[i].key.instance.equipmentKey, std::to_string(i).c_str());
    }
    CHECK(EncodeInstanceCustomizations(*data, bytes) == Status::Ok && bytes.size() == 102416);
    CHECK(DecodeInstanceCustomizations(bytes.data(), static_cast<std::uint32_t>(bytes.size()), *decoded) == Status::Ok && decoded->count == 256);
    data->count = 257; ordered = {1, 2, 3};
    CHECK(EncodeInstanceCustomizations(*data, ordered) == Status::LimitExceeded && ordered == std::vector<std::uint8_t>({1, 2, 3}));
}
}
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2); const auto dll = LoadLibraryA(argv[1]); CHECK(dll);
        const auto query = reinterpret_cast<QueryInstanceConfigurationInterface>(GetProcAddress(dll, "BodySlots_QueryInstanceConfigurationInterface"));
        const auto baseQuery = reinterpret_cast<QueryInterface>(GetProcAddress(dll, "BodySlots_QueryInterface"));
        const auto configurationQuery = reinterpret_cast<QueryConfigurationInterface>(GetProcAddress(dll, "BodySlots_QueryConfigurationInterface"));
        const auto instanceQuery = reinterpret_cast<QueryInstanceInterface>(GetProcAddress(dll, "BodySlots_QueryInstanceInterface"));
        CHECK(query && baseQuery && configurationQuery && instanceQuery);
        CHECK(!query(0, sizeof(InstanceConfigurationApi)) && !query(1, sizeof(InstanceConfigurationApi) - 1) && !query(1, sizeof(InstanceConfigurationApi) + 1));
        const auto* api = query(1, sizeof(InstanceConfigurationApi)); const auto* base = baseQuery(1, sizeof(Api));
        const auto* definitions = configurationQuery(1, sizeof(ConfigurationApi)); const auto* instances = instanceQuery(1, sizeof(InstanceApi));
        CHECK(api && base && definitions && instances && api->structSize == 56 && api->version == 1 && api->capabilities == 3);
        CHECK(sizeof(InstanceCustomizationKey) == 320 && sizeof(InstanceCustomization) == 400 && sizeof(InstanceCustomizationRecord) == 408 && sizeof(ConfiguredInstanceRecord) == 792);
        std::uint64_t author = 0, equipment = 0, observer = 0, editor = 0;
        CHECK(base->RegisterClient("templates", &author) == Status::Ok); CHECK(base->RegisterClient("equipment", &equipment) == Status::Ok);
        CHECK(base->RegisterObserver(&observer) == Status::Ok); CHECK(definitions->RegisterCustomizer("instance-editor", &editor) == Status::Ok);
        auto definition = Make(); SlotRecord templateRecord{}; CHECK(base->UpsertDefinition(author, &definition, 0, &templateRecord) == Status::Ok);
        auto view = Copy(*api, observer); auto aSpec = Spec(templateRecord, "bag-A"), bSpec = Spec(templateRecord, "bag-B");
        InstanceRecord a{}, b{}; a.structSize = b.structSize = sizeof(a);
        CHECK(instances->UpsertInstance(equipment, &aSpec, view.header.saveGeneration, 0, &a) == Status::Ok);
        CHECK(instances->UpsertInstance(equipment, &bSpec, view.header.saveGeneration, 0, &b) == Status::Ok);
        view = Copy(*api, observer);
        Customization common{}; common.structSize = sizeof(common); common.fieldMask = CustomizePosition;
        std::strcpy(common.provider, "templates"); std::strcpy(common.key, "belt"); common.position[0] = 12; EffectiveSlotRecord commonResult{};
        CHECK(definitions->ApplyCustomization(editor, &common, view.header.configurationRevision, &commonResult) == Status::Ok);
        view = Copy(*api, observer); const auto inheritedView = view;
        auto patch = Patch(a, 42); patch.fieldMask |= CustomizeEnabled; patch.enabled = 0;
        auto output = Output(); CHECK(api->ApplyCustomization(editor, &patch, view.header.saveGeneration, view.header.viewRevision, &output) == Status::Ok);
        CHECK(output.instance.effective.position[0] == 42 && output.instance.effective.flags == 0);
        CHECK(output.instance.customizationMask == CustomizePosition && output.instanceCustomizationMask == (CustomizePosition | CustomizeEnabled));
        view = Copy(*api, observer); CHECK(view.header.viewRevision > inheritedView.header.viewRevision && view.header.configurationRevision == inheritedView.header.configurationRevision);
        CHECK(view.records[1].instance.effective.position[0] == 12 && view.records[1].instanceCustomizationMask == 0);
        std::array<InstanceRecord, 2> inherited{}; auto header = view.header;
        CHECK(instances->CopyEffectiveInstances(observer, 0, 0, inherited.data(), 2, &header) == Status::Ok);
        CHECK(std::memcmp(&inherited[0], &inheritedView.records[0].instance, sizeof(a)) == 0);
        CHECK(std::memcmp(&inherited[1], &inheritedView.records[1].instance, sizeof(b)) == 0);
        std::uint32_t allowed = 99;
        CHECK(instances->EvaluateInstanceEligibility(observer, a.instanceHandle, header.saveGeneration, header.viewRevision, Left, Potion, &allowed) == Status::Ok && allowed == 1);
        CHECK(api->EvaluateConfiguredEligibility(observer, a.instanceHandle, header.saveGeneration, header.viewRevision, Left, Potion, &allowed) == Status::Ok && allowed == 0);
        CHECK(api->EvaluateConfiguredEligibility(observer, b.instanceHandle, header.saveGeneration, header.viewRevision, Left, Potion, &allowed) == Status::Ok && allowed == 1);
        const auto unchanged = view; CHECK(api->ApplyCustomization(editor, &patch, header.saveGeneration, header.viewRevision, &output) == Status::Ok);
        view = Copy(*api, observer); CHECK(std::memcmp(&view.header, &unchanged.header, sizeof(header)) == 0);
        auto sentinel = Output(); sentinel.instance.flags = 0xa5a5a5a5; auto rejected = sentinel;
        CHECK(api->ApplyCustomization(equipment, &patch, header.saveGeneration, header.viewRevision, &rejected) == Status::Unauthorized);
        CHECK(api->ApplyCustomization(observer, &patch, header.saveGeneration, header.viewRevision, &rejected) == Status::Unauthorized);
        CHECK(api->ApplyCustomization(editor, &patch, 0, header.viewRevision, &rejected) == Status::Conflict);
        CHECK(api->ApplyCustomization(editor, &patch, header.saveGeneration, 0, &rejected) == Status::Conflict);
        CHECK(api->ApplyCustomization(editor, &patch, header.saveGeneration + 1, header.viewRevision, &rejected) == Status::Conflict);
        CHECK(std::memcmp(&rejected, &sentinel, sizeof(rejected)) == 0);
        for (unsigned mutation = 0; mutation < 8; ++mutation) {
            auto bad = patch;
            if (mutation == 0) bad.fieldMask = 0;
            if (mutation == 1) bad.fieldMask = 16;
            if (mutation == 2) bad.enabled = 2;
            if (mutation == 3) bad.reserved = 1;
            if (mutation == 4) bad.position[0] = std::numeric_limits<float>::quiet_NaN();
            if (mutation == 5) bad.rotation[0] = 1; // unselected fields must be zero
            if (mutation == 6) bad.key.instance.equipmentKey[63] = 'x';
            if (mutation == 7) --bad.structSize;
            CHECK(api->ApplyCustomization(editor, &bad, header.saveGeneration, header.viewRevision, &rejected) == (mutation == 7 ? Status::UnsupportedVersion : Status::InvalidArgument));
            CHECK(std::memcmp(&rejected, &sentinel, sizeof(rejected)) == 0);
        }
        auto records = std::make_unique<std::array<InstanceCustomizationRecord, 256>>();
        auto savedView = Saved(*api, observer, records->data(), 256); CHECK(savedView.count == 1 && (*records)[0].available == 1);
        CHECK(std::memcmp(&(*records)[0].customization, &patch, sizeof(patch)) == 0);
        Codec(patch);
        auto replacement = Patch(a, 0); replacement.fieldMask = CustomizeAccessRegion; replacement.radius = 7; replacement.regionCenter[0] = 1;
        CHECK(api->ApplyCustomization(editor, &replacement, header.saveGeneration, header.viewRevision, &output) == Status::Ok);
        CHECK(output.instance.effective.position[0] == 12 && output.instance.effective.flags == kEnabled && output.instance.effective.radius == 7);
        view = Copy(*api, observer); patch = Patch(a, 12);
        CHECK(api->ApplyCustomization(editor, &patch, view.header.saveGeneration, view.header.viewRevision, &output) == Status::Ok);
        view = Copy(*api, observer); common.position[0] = 14;
        CHECK(definitions->ApplyCustomization(editor, &common, view.header.configurationRevision, &commonResult) == Status::Ok);
        view = Copy(*api, observer); CHECK(view.records[0].instance.effective.position[0] == 12 && view.records[1].instance.effective.position[0] == 14);
        auto untouched = Output(); untouched.instance.flags = 123; const auto oldUntouched = untouched; auto conflictHeader = view.header;
        CHECK(api->CopyConfiguredInstances(observer, 0, inheritedView.header.viewRevision, &untouched, 1, &conflictHeader) == Status::Conflict);
        CHECK(std::memcmp(&untouched, &oldUntouched, sizeof(untouched)) == 0 && conflictHeader.count == 2);
        CHECK(api->CopyConfiguredInstances(observer, 0, 0, &untouched, 1, &conflictHeader) == Status::BufferTooSmall);
        CHECK(std::memcmp(&untouched, &oldUntouched, sizeof(untouched)) == 0);
        CHECK(instances->RemoveInstance(equipment, a.instanceHandle, view.header.saveGeneration, a.instanceRevision) == Status::Ok);
        savedView = Saved(*api, editor, records->data(), 256); CHECK(savedView.count == 1 && (*records)[0].available == 0);
        CHECK(api->ApplyCustomization(editor, &patch, savedView.saveGeneration, savedView.viewRevision, &rejected) == Status::NotFound);
        CHECK(instances->UpsertInstance(equipment, &aSpec, savedView.saveGeneration, 0, &a) == Status::Ok);
        view = Copy(*api, observer); CHECK(view.records[0].instance.effective.position[0] == 12);
        CHECK(base->ReleaseClient(equipment) == Status::Ok);
        savedView = Saved(*api, observer, records->data(), 256); CHECK(savedView.count == 1 && (*records)[0].available == 0);
        CHECK(definitions->ReleaseCustomizer(editor) == Status::Ok);
        CHECK(definitions->RegisterCustomizer("instance-editor", &editor) == Status::Ok);
        CHECK(base->RegisterClient("equipment", &equipment) == Status::Ok);
        CHECK(instances->UpsertInstance(equipment, &aSpec, savedView.saveGeneration, 0, &a) == Status::Ok);
        CHECK(base->RemoveDefinition(author, definition.key, templateRecord.definitionRevision) == Status::Ok);
        view = Copy(*api, observer); CHECK(view.records[0].instance.templateState == 1 && view.records[0].instanceCustomizationMask == 0);
        CHECK(api->ApplyCustomization(editor, &patch, view.header.saveGeneration, view.header.viewRevision, &rejected) == Status::Unavailable);
        CHECK(base->UpsertDefinition(author, &definition, 0, &templateRecord) == Status::Ok);
        view = Copy(*api, observer); CHECK(view.records[0].instance.templateState == 2 && view.records[0].instanceCustomizationMask == 0);
        aSpec.definitionRevision = templateRecord.definitionRevision;
        CHECK(instances->UpsertInstance(equipment, &aSpec, view.header.saveGeneration, a.instanceRevision, &a) == Status::Ok);
        view = Copy(*api, observer); CHECK(view.records[0].instance.effective.position[0] == 12);
        // Fill all saved slots through actual runtime instances; removing activation
        // does not free saved capacity until an explicit dormant clear.
        for (unsigned index = 1; index < kMaxInstances; ++index) {
            const auto bag = std::string("bag/") + std::to_string(index); auto spec = Spec(templateRecord, bag.c_str());
            InstanceRecord instance{}; instance.structSize = sizeof(instance);
            CHECK(instances->UpsertInstance(equipment, &spec, view.header.saveGeneration, 0, &instance) == Status::Ok);
            auto value = Patch(instance, float(index)); view = Copy(*api, observer);
            CHECK(api->ApplyCustomization(editor, &value, view.header.saveGeneration, view.header.viewRevision, &output) == Status::Ok);
        }
        view = Copy(*api, observer); CHECK(instances->RemoveInstance(equipment, a.instanceHandle, view.header.saveGeneration, a.instanceRevision) == Status::Ok);
        auto extraSpec = Spec(templateRecord, "extra"); InstanceRecord extra{}; extra.structSize = sizeof(extra);
        CHECK(instances->UpsertInstance(equipment, &extraSpec, view.header.saveGeneration, 0, &extra) == Status::Ok);
        auto extraPatch = Patch(extra, 123); view = Copy(*api, observer); rejected = sentinel;
        CHECK(api->ApplyCustomization(editor, &extraPatch, view.header.saveGeneration, view.header.viewRevision, &rejected) == Status::LimitExceeded);
        CHECK(std::memcmp(&rejected, &sentinel, sizeof(rejected)) == 0);
        CHECK(api->ClearCustomization(editor, &patch.key, view.header.saveGeneration, view.header.viewRevision) == Status::Ok);
        view = Copy(*api, observer);
        CHECK(api->ApplyCustomization(editor, &extraPatch, view.header.saveGeneration, view.header.viewRevision, &output) == Status::Ok);
        savedView = Saved(*api, observer, records->data(), 256); CHECK(savedView.count == 256);
        CHECK(base->ReleaseClient(equipment) == Status::Ok); CHECK(base->ReleaseClient(author) == Status::Ok);
        savedView = Saved(*api, observer, records->data(), 256); CHECK(savedView.count == 256);
        for (unsigned index = 0; index < savedView.count; ++index) CHECK((*records)[index].available == 0);
        CHECK(definitions->ReleaseCustomizer(editor) == Status::Ok); CHECK(base->ReleaseClient(observer) == Status::Ok);
        CHECK(FreeLibrary(dll));
        std::cout << checks << " instance configuration/codec checks passed; separate masks, old-view immutability, independent saved placement, dormant records and bounded portable BSCI\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
