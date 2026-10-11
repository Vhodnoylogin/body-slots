#include "body_slots_configuration_api.h"
#include "configuration.h"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace body_slots;
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t, std::uint32_t) noexcept;
extern "C" const ConfigurationApi* BS_CALL BodySlots_QueryConfigurationInterface(std::uint32_t, std::uint32_t) noexcept;
static unsigned checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #c); } while (false)
Definition Make(const char* provider = "test.author", const char* key = "pouch") {
    Definition d{}; d.structSize = sizeof(d); d.flags = kEnabled;
    std::strcpy(d.provider, provider); std::strcpy(d.key, key); std::strcpy(d.anchor, "NPC Pelvis [Pelv]");
    std::strcpy(d.referenceRig, "skyrimvr-humanoid-v1"); std::strcpy(d.kind, "potion-pouch");
    d.rotation[0] = d.rotation[4] = d.rotation[8] = 1; d.radius = 8; d.handMask = Left; d.contentMask = Potion;
    return d;
}
Customization Edit(const char* provider = "test.author", const char* key = "pouch") {
    Customization c{}; c.structSize = sizeof(c); c.fieldMask = CustomizePosition | CustomizeEnabled;
    std::strcpy(c.provider, provider); std::strcpy(c.key, key); c.position[0] = 42; c.enabled = 0;
    return c;
}
struct Snapshot { std::array<EffectiveSlotRecord, kMaxDefinitions> entries{}; std::uint32_t count = 0; std::uint64_t revision = 0; };
Snapshot Copy(const ConfigurationApi& api, std::uint64_t reader) {
    Snapshot s{}; CHECK(api.CopyEffectiveDefinitions(reader, 0, s.entries.data(), kMaxDefinitions, &s.count, &s.revision) == Status::Ok); return s;
}
void Same(const Snapshot& a, const Snapshot& b) {
    CHECK(a.count == b.count && a.revision == b.revision);
    CHECK(!std::memcmp(a.entries.data(), b.entries.data(), a.count * sizeof(EffectiveSlotRecord)));
}
void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
int main() {
    try {
        CHECK(sizeof(Definition) == 408 && sizeof(SlotRecord) == 424 && sizeof(Api) == 80);
        CHECK(!BodySlots_QueryConfigurationInterface(0, sizeof(ConfigurationApi)));
        CHECK(!BodySlots_QueryConfigurationInterface(1, sizeof(ConfigurationApi) - 1));
        const auto& v1 = *BodySlots_QueryInterface(1, sizeof(Api));
        const auto& api = *BodySlots_QueryConfigurationInterface(1, sizeof(ConfigurationApi));
        CHECK(api.capabilities == 3 && api.structSize == 72);
        std::uint64_t author = 0, observer = 0, editor = 0, sentinel = 999;
        CHECK(v1.RegisterClient("test.author", &author) == Status::Ok);
        CHECK(v1.RegisterObserver(&observer) == Status::Ok);
        CHECK(api.RegisterCustomizer("test.editor", &editor) == Status::Ok && editor != author && editor != observer);
        CHECK(api.RegisterCustomizer("test.editor", &sentinel) == Status::Conflict && sentinel == 999);
        CHECK(api.RegisterCustomizer("bad:name", &sentinel) == Status::InvalidArgument && sentinel == 999);
        CHECK(api.RegisterCustomizer("test.null", nullptr) == Status::InvalidArgument);
        CHECK(api.ReleaseCustomizer(observer) == Status::Unauthorized);
        CHECK(v1.ReleaseClient(editor) == Status::Unauthorized);
        auto definition = Make(); SlotRecord authored{};
        CHECK(v1.UpsertDefinition(author, &definition, 0, &authored) == Status::Ok);
        auto before = Copy(api, observer); CHECK(before.count == 1 && before.entries[0].configurationRevision == before.revision);
        EffectiveSlotRecord committed{}; auto customization = Edit();
        CHECK(api.ApplyCustomization(observer, &customization, before.revision, &committed) == Status::Unauthorized);
        CHECK(api.ApplyCustomization(author, &customization, before.revision, &committed) == Status::Unauthorized);
        CHECK(api.ApplyCustomization(editor, &customization, 0, &committed) == Status::Conflict);
        CHECK(api.ApplyCustomization(editor, &customization, before.revision, &committed) == Status::Ok);
        CHECK(committed.configurationRevision > before.revision && committed.slot.definitionRevision == authored.definitionRevision);
        CHECK(committed.slot.definition.position[0] == 42 && committed.slot.definition.flags == 0);
        CHECK(committed.slot.definition.contentMask == Potion && committed.slot.definition.handMask == Left);
        std::array<SlotRecord, kMaxDefinitions> originals{}; std::uint32_t count = 0; std::uint64_t revision = 0;
        CHECK(v1.CopyDefinitions(observer, 0, originals.data(), kMaxDefinitions, &count, &revision) == Status::Ok);
        CHECK(count == 1 && !std::memcmp(&originals[0], &authored, sizeof(authored)));
        CHECK(v1.UpsertDefinition(editor, &definition, authored.definitionRevision, &authored) == Status::Unauthorized);
        CHECK(v1.RemoveDefinition(editor, "pouch", authored.definitionRevision) == Status::Unauthorized);
        CHECK(v1.ReplaceDefinitions(editor, &definition, 1, 0) == Status::Unauthorized);
        std::uint32_t allowed = 99;
        CHECK(v1.EvaluateEligibility(observer, authored.slotId, authored.definitionRevision, Left, Potion, &allowed) == Status::Ok && allowed == 1);
        CHECK(api.EvaluateEffectiveEligibility(observer, authored.slotId, committed.configurationRevision, Left, Potion, &allowed) == Status::Ok && allowed == 0);
        customization.enabled = 1;
        CHECK(api.ApplyCustomization(editor, &customization, committed.configurationRevision, &committed) == Status::Ok);
        CHECK(api.EvaluateEffectiveEligibility(editor, authored.slotId, committed.configurationRevision, Left, Weapon, &allowed) == Status::Ok && allowed == 0);
        CHECK(api.EvaluateEffectiveEligibility(editor, authored.slotId, committed.configurationRevision, Right, Potion, &allowed) == Status::Ok && allowed == 0);
        before = Copy(api, editor);
        CHECK(api.ApplyCustomization(editor, &customization, before.revision, &committed) == Status::Ok && committed.configurationRevision == before.revision);
        const auto invalid = [&] {
            std::vector<Customization> cases; auto add = [&](auto mutation) { auto c = customization; mutation(c); cases.push_back(c); };
            add([](auto& c) { c.structSize = 1; }); add([](auto& c) { c.fieldMask = 0; }); add([](auto& c) { c.fieldMask = 16; });
            add([](auto& c) { c.reserved = 1; }); add([](auto& c) { c.enabled = 2; }); add([](auto& c) { c.key[63] = 'x'; });
            add([](auto& c) { c.position[0] = std::numeric_limits<float>::quiet_NaN(); });
            add([](auto& c) { c.rotation[0] = 1; }); add([](auto& c) { c.fieldMask |= CustomizeRotation; c.rotation[0] = 2; });
            add([](auto& c) { c.fieldMask |= CustomizeAccessRegion; c.radius = -1; });
            add([](auto& c) { c.fieldMask = CustomizeEnabled; }); return cases;
        }();
        for (const auto& c : invalid) { CHECK(api.ApplyCustomization(editor, &c, before.revision, &committed) == Status::InvalidArgument); Same(before, Copy(api, observer)); }
        auto missing = Edit("absent", "not-authored");
        CHECK(api.ApplyCustomization(editor, &missing, before.revision, &committed) == Status::NotFound); Same(before, Copy(api, observer));
        auto complete = customization; complete.fieldMask = AllCustomizationFields;
        complete.rotation[1] = -1; complete.rotation[3] = 1; complete.rotation[8] = 1;
        complete.radius = 12; complete.regionCenter[0] = -3; complete.regionCenter[2] = 2;
        CHECK(api.ApplyCustomization(editor, &complete, before.revision, &committed) == Status::Ok);
        CHECK(committed.slot.definition.rotation[1] == -1 && committed.slot.definition.rotation[3] == 1);
        CHECK(committed.slot.definition.radius == 12 && committed.slot.definition.regionCenter[0] == -3);
        CHECK(!std::strcmp(committed.slot.definition.anchor, definition.anchor) && committed.slot.definition.contentMask == Potion);
        CHECK(api.ApplyCustomization(editor, &customization, committed.configurationRevision, &committed) == Status::Ok);
        CHECK(committed.slot.definition.rotation[0] == 1 && committed.slot.definition.radius == definition.radius && committed.customizationMask == customization.fieldMask);
        before = Copy(api, observer);
        definition.radius = 9;
        CHECK(v1.UpsertDefinition(author, &definition, authored.definitionRevision, &authored) == Status::Ok);
        auto changed = Copy(api, observer); CHECK(changed.revision > before.revision && changed.entries[0].slot.definition.position[0] == 42);
        CHECK(changed.entries[0].slot.definition.radius == 9 && changed.entries[0].slot.definitionRevision == authored.definitionRevision);
        CHECK(api.ApplyCustomization(editor, &customization, before.revision, &committed) == Status::Conflict);
        CHECK(v1.RemoveDefinition(author, "pouch", authored.definitionRevision) == Status::Ok);
        CustomizationRecord dormant{}; CHECK(api.CopyCustomizations(observer, 0, &dormant, 1, &count, &revision) == Status::Ok && count == 1 && dormant.available == 0);
        CHECK(v1.ReleaseClient(author) == Status::Ok);
        CHECK(v1.RegisterClient("test.author", &author) == Status::Ok);
        const auto oldSlotId = authored.slotId;
        CHECK(v1.UpsertDefinition(author, &definition, 0, &authored) == Status::Ok && authored.slotId != oldSlotId);
        CHECK(Copy(api, observer).entries[0].slot.definition.position[0] == 42);
        CHECK(api.CopyCustomizations(observer, 0, &dormant, 1, &count, &revision) == Status::Ok && dormant.available == 1);
        auto second = Make("test.author", "second"); SlotRecord secondRecord{};
        CHECK(v1.UpsertDefinition(author, &second, 0, &secondRecord) == Status::Ok);
        before = Copy(api, observer); CHECK(before.count == 2 && before.entries[0].configurationRevision == before.entries[1].configurationRevision);
        Definition defaults[]{definition, second};
        CHECK(v1.ReplaceDefinitions(author, defaults, 2, 0) == Status::Ok); Same(before, Copy(api, observer));
        definition.position[0] = 5; defaults[0] = definition;
        CHECK(v1.ReplaceDefinitions(author, defaults, 2, 0) == Status::Ok);
        auto replaced = Copy(api, observer); CHECK(replaced.revision > before.revision && replaced.entries[0].slot.definition.position[0] == 42);
        before = replaced;
        EffectiveSlotRecord untouched{}; std::memset(&untouched, 0xa5, sizeof(untouched)); auto untouchedBefore = untouched;
        CHECK(api.CopyEffectiveDefinitions(observer, 0, &untouched, 1, &count, &revision) == Status::BufferTooSmall && count == 2);
        CHECK(!std::memcmp(&untouched, &untouchedBefore, sizeof(untouched)));
        CHECK(api.CopyEffectiveDefinitions(observer, before.revision - 1, &untouched, 1, &count, &revision) == Status::Conflict);
        CHECK(!std::memcmp(&untouched, &untouchedBefore, sizeof(untouched)));
        CHECK(api.ClearCustomization(observer, "test.author", "pouch", before.revision) == Status::Unauthorized);
        CHECK(api.ClearCustomization(editor, "test.author", "pouch", 0) == Status::Conflict);
        std::vector<std::uint8_t> saved; CHECK(detail::SaveCustomizations(saved) == Status::Ok && saved.size() == 224);
        auto rejected = [&](std::vector<std::uint8_t> bytes) {
            CHECK(detail::LoadCustomizations(bytes.data(), static_cast<std::uint32_t>(bytes.size())) != Status::Ok);
            Same(before, Copy(api, observer));
        };
        rejected({}); rejected(std::vector<std::uint8_t>(saved.begin(), saved.end() - 1));
        auto corrupt = saved; corrupt.push_back(0); rejected(corrupt);
        for (const auto offset : {0u, 4u, 8u, 12u, 16u, 20u, 24u, 24u + 63u, 220u}) { corrupt = saved; corrupt[offset] ^= 0x80; rejected(corrupt); }
        corrupt = saved; Put(corrupt, 16 + 136, 0x7fc00000); rejected(corrupt);
        corrupt = saved; corrupt.insert(corrupt.end(), saved.begin() + 16, saved.end()); Put(corrupt, 12, 2); rejected(corrupt);
        corrupt[16 + 208 + 72] = 'x'; Put(corrupt, 16 + 208 + 204, 1); rejected(corrupt);
        detail::CustomizationStore unknown{}; unknown.count = 1; unknown.entries[0] = missing;
        std::vector<std::uint8_t> missingSave; CHECK(detail::EncodeCustomizations(unknown, missingSave) == Status::Ok);
        CHECK(detail::ResetCustomizations() == Status::Ok);
        CHECK(Copy(api, observer).entries[0].slot.definition.position[0] == definition.position[0]);
        CHECK(detail::LoadCustomizations(missingSave.data(), static_cast<std::uint32_t>(missingSave.size())) == Status::Ok);
        CHECK(api.CopyCustomizations(observer, 0, &dormant, 1, &count, &revision) == Status::Ok && dormant.available == 0);
        std::uint64_t missingAuthor = 0; CHECK(v1.RegisterClient("absent", &missingAuthor) == Status::Ok);
        auto reattachedDefault = Make("absent", "not-authored"); SlotRecord reattached{};
        CHECK(v1.UpsertDefinition(missingAuthor, &reattachedDefault, 0, &reattached) == Status::Ok);
        auto attached = Copy(api, observer); CHECK(attached.count == 3 && attached.entries[0].slot.definition.position[0] == 42);
        CHECK(attached.entries[0].slot.slotId == reattached.slotId && attached.entries[0].slot.definition.contentMask == Potion);
        CHECK(api.CopyCustomizations(observer, 0, &dormant, 1, &count, &revision) == Status::Ok && dormant.available == 1);
        const auto attachedRevision = revision;
        CHECK(v1.ReleaseClient(missingAuthor) == Status::Ok);
        CHECK(api.CopyCustomizations(observer, 0, &dormant, 1, &count, &revision) == Status::Ok && dormant.available == 0 && revision > attachedRevision);
        std::vector<std::uint8_t> dormantAgain; CHECK(detail::SaveCustomizations(dormantAgain) == Status::Ok && dormantAgain == missingSave);
        CHECK(api.ClearCustomization(editor, "absent", "not-authored", revision) == Status::Ok);
        CHECK(detail::LoadCustomizations(saved.data(), static_cast<std::uint32_t>(saved.size())) == Status::Ok);
        CHECK(Copy(api, observer).entries[0].slot.definition.position[0] == 42);
        // Concurrent default publication/customization cannot mix snapshot versions or policy.
        std::atomic<bool> done{false}, publisherDone{false}, failed{false};
        std::thread publisher([&] {
            for (unsigned i = 0; i < 200; ++i) {
                second.position[1] = static_cast<float>(i);
                if (v1.UpsertDefinition(author, &second, secondRecord.definitionRevision, &secondRecord) != Status::Ok) failed = true;
            }
            publisherDone = true;
        });
        std::thread writer([&] {
            for (unsigned i = 0; i < 200; ++i) {
                customization.position[0] = (i & 1) ? 21.f : 42.f;
                std::uint32_t n = 0; std::uint64_t r = 0;
                if (api.CopyEffectiveDefinitions(editor, 0, nullptr, 0, &n, &r) != Status::BufferTooSmall) failed = true;
                const auto status = api.ApplyCustomization(editor, &customization, r, &committed);
                if (status != Status::Ok && status != Status::Conflict) failed = true;
            }
            done = true;
        });
        while (!done || !publisherDone) {
            auto coherent = Copy(api, observer);
            for (std::uint32_t i = 0; i < coherent.count; ++i) {
                if (coherent.entries[i].configurationRevision != coherent.revision || coherent.entries[i].slot.definition.contentMask != Potion) failed = true;
            }
        }
        writer.join(); publisher.join(); CHECK(!failed);
        CHECK(detail::ResetCustomizations() == Status::Ok);
        detail::CustomizationStore full{}; full.count = kMaxCustomizations;
        for (std::uint32_t i = 0; i < full.count; ++i) full.entries[i] = Edit("dormant", ("key" + std::to_string(i)).c_str());
        std::vector<std::uint8_t> fullSave; CHECK(detail::EncodeCustomizations(full, fullSave) == Status::Ok && fullSave.size() == detail::kMaxCustomizationSaveBytes);
        CHECK(detail::LoadCustomizations(fullSave.data(), static_cast<std::uint32_t>(fullSave.size())) == Status::Ok);
        before = Copy(api, observer); CHECK(api.ApplyCustomization(editor, &customization, before.revision, &committed) == Status::LimitExceeded);
        CHECK(api.ClearCustomization(editor, "dormant", "key0", before.revision) == Status::Ok);
        before = Copy(api, observer); CHECK(api.ApplyCustomization(editor, &customization, before.revision, &committed) == Status::Ok);
        std::array<std::uint64_t, 31> extra{};
        for (std::size_t i = 0; i < extra.size(); ++i) CHECK(api.RegisterCustomizer(("editor" + std::to_string(i)).c_str(), &extra[i]) == Status::Ok);
        CHECK(api.RegisterCustomizer("overflow", &sentinel) == Status::LimitExceeded && sentinel == 999);
        for (auto lease : extra) CHECK(api.ReleaseCustomizer(lease) == Status::Ok);
        CHECK(api.ReleaseCustomizer(editor) == Status::Ok);
        CHECK(api.ClearCustomization(editor, "test.author", "pouch", committed.configurationRevision) == Status::Unauthorized);
        CHECK(v1.ReleaseClient(author) == Status::Ok && v1.ReleaseClient(observer) == Status::Ok);
        CHECK(detail::ResetCustomizations() == Status::Ok);
        std::cout << "ALL CONFIGURATION PASS: " << checks << " checks; authored ABI, authority, coherent effective snapshots, stale edits, dormant reattachment, bounded atomic save decoding\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
