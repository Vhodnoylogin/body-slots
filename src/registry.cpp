#include "validation.h"
#include "configuration.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>

namespace body_slots::detail {
constexpr std::size_t kMaxWriters = 64, kMaxObservers = 64, kMaxClients = kMaxWriters + kMaxObservers;
struct Client { std::uint64_t token = 0; char provider[64]{}; };
struct Customizer { std::uint64_t token = 0; char identity[64]{}; };
struct Registry {
    std::mutex mutex;
    std::array<Client, kMaxClients> clients{};
    std::array<SlotRecord, kMaxDefinitions> records{};
    std::array<Customizer, 32> customizers{};
    CustomizationStore customizations{};
    std::uint32_t count = 0;
    std::uint64_t revision = 1, nextToken = 1, nextSlot = 1, nextDefinitionRevision = 1;
    std::uint64_t configurationRevision = 1;
    Client* FindClient(std::uint64_t token) noexcept {
        if (token) for (auto& c : clients) if (c.token == token) return &c;
        return nullptr;
    }
    Customizer* FindCustomizer(std::uint64_t token) noexcept {
        if (token) for (auto& c : customizers) if (c.token == token) return &c;
        return nullptr;
    }
    bool CanReadConfiguration(std::uint64_t token) noexcept { return FindClient(token) || FindCustomizer(token); }
    std::uint32_t FindCustomization(const char* provider, const char* key) const noexcept {
        for (std::uint32_t i = 0; i < customizations.count; ++i)
            if (!std::strcmp(customizations.entries[i].provider, provider) && !std::strcmp(customizations.entries[i].key, key)) return i;
        return customizations.count;
    }
    std::uint32_t FindKey(const char* provider, const char* key) const noexcept {
        for (std::uint32_t i = 0; i < count; ++i)
            if (!std::strcmp(records[i].definition.provider, provider) && !std::strcmp(records[i].definition.key, key)) return i;
        return count;
    }
    bool CanAdvance(std::uint64_t definitions, std::uint64_t slots) const noexcept {
        const auto max = std::numeric_limits<std::uint64_t>::max();
        return revision < max && configurationRevision < max && definitions <= max - nextDefinitionRevision && slots <= max - nextSlot;
    }
};
Registry& State() { static Registry registry; return registry; }
void Sort(std::array<SlotRecord, kMaxDefinitions>& records, std::uint32_t count) noexcept {
    std::sort(records.begin(), records.begin() + count, [](const SlotRecord& a, const SlotRecord& b) {
        const int provider = std::strcmp(a.definition.provider, b.definition.provider);
        return provider < 0 || (provider == 0 && std::strcmp(a.definition.key, b.definition.key) < 0);
    });
}
// Every ABI entry catches C++ exceptions. Callers must supply accessible, correctly sized
// memory; this contract cannot safely validate arbitrary unmapped native pointers.
template<class F> Status Guard(F&& function) noexcept {
    try { return function(); } catch (...) { return Status::InternalError; }
}
Status BS_CALL RegisterClient(const char* provider, std::uint64_t* client) noexcept {
    return Guard([&] {
        if (!provider || !client) return Status::InvalidArgument;
        // This API takes a C string, not a 64-byte record. Read only through its NUL.
        char name[64]{};
        std::size_t n = 0;
        for (; n < sizeof(name); ++n) { if (!provider[n]) break; name[n] = provider[n]; }
        if (n == sizeof(name) || !ValidString(name, sizeof(name), true)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        for (auto& c : s.clients) if (c.token && !std::strcmp(c.provider, name)) return Status::Conflict;
        if (s.nextToken == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        for (std::size_t i = 0; i < kMaxWriters; ++i) {
            auto& c = s.clients[i]; if (c.token) continue;
            std::memcpy(c.provider, name, sizeof(name)); c.token = s.nextToken++;
            *client = c.token; return Status::Ok;
        }
        return Status::LimitExceeded;
    });
}
Status BS_CALL RegisterObserver(std::uint64_t* client) noexcept {
    return Guard([&] {
        if (!client) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (s.nextToken == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        for (std::size_t i = kMaxWriters; i < kMaxClients; ++i) if (!s.clients[i].token) {
            s.clients[i].token = s.nextToken++; *client = s.clients[i].token; return Status::Ok;
        }
        return Status::LimitExceeded;
    });
}
Status BS_CALL ReleaseClient(std::uint64_t token) noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        auto* c = s.FindClient(token); if (!c) return Status::Unauthorized;
        std::uint32_t out = 0;
        bool owned = false;
        for (std::uint32_t i = 0; i < s.count; ++i) if (!std::strcmp(s.records[i].definition.provider, c->provider)) owned = true;
        if (owned && !s.CanAdvance(0, 0)) return Status::LimitExceeded;
        for (std::uint32_t i = 0; i < s.count; ++i)
            if (std::strcmp(s.records[i].definition.provider, c->provider)) s.records[out++] = s.records[i];
        if (owned) { s.count = out; ++s.revision; ++s.configurationRevision; }
        *c = {}; return Status::Ok;
    });
}
Status BS_CALL UpsertDefinition(std::uint64_t token, const Definition* definition, std::uint64_t expected, SlotRecord* output) noexcept {
    return Guard([&] {
        if (!definition || !output) return Status::InvalidArgument;
        Definition d = *definition;
        const auto validation = ValidateDefinition(d); if (validation != Status::Ok) return validation;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        auto* c = s.FindClient(token); if (!c || !c->provider[0]) return Status::Unauthorized;
        if (std::strcmp(c->provider, d.provider)) return Status::Unauthorized;
        const auto index = s.FindKey(c->provider, d.key); const bool create = index == s.count;
        if (create && expected) return Status::NotFound;
        if (!create && (!expected || expected != s.records[index].definitionRevision)) return Status::Conflict;
        if (create && s.count == kMaxDefinitions) return Status::LimitExceeded;
        if (!s.CanAdvance(1, create ? 1 : 0)) return Status::LimitExceeded;
        SlotRecord record{d, s.nextDefinitionRevision++, create ? s.nextSlot++ : s.records[index].slotId};
        s.records[index] = record; if (create) ++s.count;
        Sort(s.records, s.count); ++s.revision; ++s.configurationRevision; *output = record; return Status::Ok;
    });
}
Status BS_CALL RemoveDefinition(std::uint64_t token, const char* key, std::uint64_t expected) noexcept {
    return Guard([&] {
        if (!key) return Status::InvalidArgument;
        char name[64]{}; std::size_t n = 0;
        for (; n < sizeof(name); ++n) { if (!key[n]) break; name[n] = key[n]; }
        if (n == sizeof(name) || !ValidString(name, sizeof(name), true)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        auto* c = s.FindClient(token); if (!c || !c->provider[0]) return Status::Unauthorized;
        const auto index = s.FindKey(c->provider, name); if (index == s.count) return Status::NotFound;
        if (!expected || expected != s.records[index].definitionRevision) return Status::Conflict;
        if (!s.CanAdvance(0, 0)) return Status::LimitExceeded;
        for (auto i = index + 1; i < s.count; ++i) s.records[i - 1] = s.records[i];
        --s.count; ++s.revision; ++s.configurationRevision; return Status::Ok;
    });
}
Status BS_CALL CopyDefinitions(std::uint64_t token, std::uint64_t expected, SlotRecord* records, std::uint32_t capacity,
    std::uint32_t* count, std::uint64_t* revision) noexcept {
    return Guard([&] {
        if (!count || !revision || (capacity && !records) || capacity > kMaxDefinitions) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.FindClient(token)) return Status::Unauthorized;
        *count = s.count; *revision = s.revision;
        if (expected && expected != s.revision) return Status::Conflict;
        if (capacity < s.count) return Status::BufferTooSmall;
        if (s.count) std::memcpy(records, s.records.data(), s.count * sizeof(SlotRecord));
        return Status::Ok;
    });
}
Status BS_CALL EvaluateEligibility(std::uint64_t token, std::uint64_t slotId, std::uint64_t expected,
    std::uint32_t hand, std::uint32_t content, std::uint32_t* allowed) noexcept {
    return Guard([&] {
        if (!allowed || (hand != Left && hand != Right) || !content || (content & (content - 1)) || (content & ~AllContent)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.FindClient(token)) return Status::Unauthorized;
        for (std::uint32_t i = 0; i < s.count; ++i) if (s.records[i].slotId == slotId) {
            const auto& r = s.records[i]; if (!expected || expected != r.definitionRevision) return Status::Conflict;
            *allowed = (r.definition.flags & kEnabled) && (r.definition.handMask & hand) && (r.definition.contentMask & content) ? 1u : 0u;
            return Status::Ok;
        }
        return Status::NotFound;
    });
}
Status BS_CALL ReplaceDefinitions(std::uint64_t token, const Definition* definitions, std::uint32_t count, std::uint64_t expected) noexcept {
    return Guard([&] {
        if (count > kMaxDefinitions) return Status::LimitExceeded;
        if (count && !definitions) return Status::InvalidArgument;
        auto inputStorage = std::make_unique<std::array<Definition, kMaxDefinitions>>();
        auto& input = *inputStorage;
        for (std::uint32_t i = 0; i < count; ++i) {
            input[i] = definitions[i]; const auto validation = ValidateDefinition(input[i]);
            if (validation != Status::Ok) return validation;
            for (std::uint32_t j = 0; j < i; ++j)
                if (!std::strcmp(input[i].provider, input[j].provider) && !std::strcmp(input[i].key, input[j].key)) return Status::Conflict;
        }
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        const auto* c = s.FindClient(token); if (!c || !c->provider[0]) return Status::Unauthorized;
        if (expected && expected != s.revision) return Status::Conflict;
        for (std::uint32_t i = 0; i < count; ++i) if (std::strcmp(input[i].provider, c->provider)) return Status::Unauthorized;
        auto candidateStorage = std::make_unique<std::array<SlotRecord, kMaxDefinitions>>();
        auto& candidate = *candidateStorage; std::uint32_t total = 0;
        for (std::uint32_t i = 0; i < s.count; ++i) if (std::strcmp(s.records[i].definition.provider, c->provider)) candidate[total++] = s.records[i];
        if (count > kMaxDefinitions - total) return Status::LimitExceeded;
        std::uint64_t changes = 0, newSlots = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto old = s.FindKey(c->provider, input[i].key);
            if (old == s.count) { ++newSlots; ++changes; candidate[total++] = {input[i], 0, 0}; }
            else if (std::memcmp(&input[i], &s.records[old].definition, sizeof(Definition))) { ++changes; candidate[total++] = {input[i], 0, s.records[old].slotId}; }
            else candidate[total++] = s.records[old];
        }
        if (!s.CanAdvance(changes, newSlots)) return Status::LimitExceeded;
        auto nextRevision = s.nextDefinitionRevision, nextSlot = s.nextSlot;
        for (std::uint32_t i = 0; i < total; ++i) {
            if (!candidate[i].definitionRevision) candidate[i].definitionRevision = nextRevision++;
            if (!candidate[i].slotId) candidate[i].slotId = nextSlot++;
        }
        Sort(candidate, total);
        bool changed = total != s.count;
        for (std::uint32_t i = 0; !changed && i < total; ++i)
            changed = std::memcmp(&candidate[i], &s.records[i], sizeof(SlotRecord)) != 0;
        if (changed) {
            s.records = candidate; s.count = total; s.nextSlot = nextSlot;
            s.nextDefinitionRevision = nextRevision; ++s.revision; ++s.configurationRevision;
        }
        return Status::Ok;
    });
}
bool CopyIdentity(const char* input, char (&output)[64]) noexcept {
    if (!input) return false;
    std::size_t n = 0; for (; n < sizeof(output); ++n) { if (!input[n]) break; output[n] = input[n]; }
    return n != sizeof(output) && ValidString(output, sizeof(output), true);
}
EffectiveSlotRecord Effective(const Registry& state, const SlotRecord& authored) noexcept {
    EffectiveSlotRecord out{authored, state.configurationRevision, 0, 0};
    const auto index = state.FindCustomization(authored.definition.provider, authored.definition.key);
    if (index == state.customizations.count) return out;
    const auto& c = state.customizations.entries[index]; auto& d = out.slot.definition;
    out.customizationMask = c.fieldMask;
    if (c.fieldMask & CustomizePosition) std::memcpy(d.position, c.position, sizeof(c.position));
    if (c.fieldMask & CustomizeRotation) std::memcpy(d.rotation, c.rotation, sizeof(c.rotation));
    if (c.fieldMask & CustomizeAccessRegion) { d.radius = c.radius; std::memcpy(d.regionCenter, c.regionCenter, sizeof(c.regionCenter)); }
    if (c.fieldMask & CustomizeEnabled) d.flags = c.enabled ? kEnabled : 0;
    return out;
}
Status BS_CALL RegisterCustomizer(const char* identity, std::uint64_t* lease) noexcept {
    return Guard([&] {
        char name[64]{}; if (!lease || !CopyIdentity(identity, name)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        for (const auto& c : s.customizers) if (c.token && !std::strcmp(c.identity, name)) return Status::Conflict;
        if (s.nextToken == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        for (auto& c : s.customizers) if (!c.token) {
            std::memcpy(c.identity, name, sizeof(name)); c.token = s.nextToken++; *lease = c.token; return Status::Ok;
        }
        return Status::LimitExceeded;
    });
}
Status BS_CALL ReleaseCustomizer(std::uint64_t lease) noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        auto* c = s.FindCustomizer(lease); if (!c) return Status::Unauthorized;
        *c = {}; return Status::Ok; // saved overrides belong to the save, not this temporary caller
    });
}
Status BS_CALL ApplyCustomization(std::uint64_t lease, const Customization* customization,
    std::uint64_t expected, EffectiveSlotRecord* output) noexcept {
    return Guard([&] {
        if (!customization || !output) return Status::InvalidArgument;
        const Customization c = *customization; const auto status = ValidateCustomization(c);
        if (status != Status::Ok) return status;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.FindCustomizer(lease)) return Status::Unauthorized;
        if (!expected || expected != s.configurationRevision) return Status::Conflict;
        const auto authored = s.FindKey(c.provider, c.key); if (authored == s.count) return Status::NotFound;
        const auto index = s.FindCustomization(c.provider, c.key);
        if (index == s.customizations.count && s.customizations.count == kMaxCustomizations) return Status::LimitExceeded;
        const bool changed = index == s.customizations.count || std::memcmp(&c, &s.customizations.entries[index], sizeof(c));
        if (changed) {
            if (s.configurationRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
            s.customizations.entries[index] = c; if (index == s.customizations.count) ++s.customizations.count;
            std::sort(s.customizations.entries.begin(), s.customizations.entries.begin() + s.customizations.count,
                [](const Customization& a, const Customization& b) {
                    const int provider = std::strcmp(a.provider, b.provider);
                    return provider < 0 || (provider == 0 && std::strcmp(a.key, b.key) < 0);
                });
            ++s.configurationRevision;
        }
        *output = Effective(s, s.records[authored]); return Status::Ok;
    });
}
Status BS_CALL ClearCustomization(std::uint64_t lease, const char* provider, const char* key, std::uint64_t expected) noexcept {
    return Guard([&] {
        char name[64]{}, slotKey[64]{};
        if (!CopyIdentity(provider, name) || !CopyIdentity(key, slotKey)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.FindCustomizer(lease)) return Status::Unauthorized;
        if (!expected || expected != s.configurationRevision) return Status::Conflict;
        const auto index = s.FindCustomization(name, slotKey); if (index == s.customizations.count) return Status::NotFound;
        if (s.configurationRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        for (auto i = index + 1; i < s.customizations.count; ++i) s.customizations.entries[i - 1] = s.customizations.entries[i];
        s.customizations.entries[--s.customizations.count] = {}; ++s.configurationRevision; return Status::Ok;
    });
}
Status BS_CALL CopyEffectiveDefinitions(std::uint64_t reader, std::uint64_t expected, EffectiveSlotRecord* records,
    std::uint32_t capacity, std::uint32_t* count, std::uint64_t* revision) noexcept {
    return Guard([&] {
        if (!count || !revision || (capacity && !records) || capacity > kMaxDefinitions) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.CanReadConfiguration(reader)) return Status::Unauthorized;
        *count = s.count; *revision = s.configurationRevision;
        if (expected && expected != s.configurationRevision) return Status::Conflict;
        if (capacity < s.count) return Status::BufferTooSmall;
        for (std::uint32_t i = 0; i < s.count; ++i) records[i] = Effective(s, s.records[i]);
        return Status::Ok;
    });
}
Status BS_CALL CopyCustomizations(std::uint64_t reader, std::uint64_t expected, CustomizationRecord* records,
    std::uint32_t capacity, std::uint32_t* count, std::uint64_t* revision) noexcept {
    return Guard([&] {
        if (!count || !revision || (capacity && !records) || capacity > kMaxCustomizations) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.CanReadConfiguration(reader)) return Status::Unauthorized;
        *count = s.customizations.count; *revision = s.configurationRevision;
        if (expected && expected != s.configurationRevision) return Status::Conflict;
        if (capacity < s.customizations.count) return Status::BufferTooSmall;
        for (std::uint32_t i = 0; i < s.customizations.count; ++i) {
            const auto& c = s.customizations.entries[i];
            records[i] = {c, s.configurationRevision, s.FindKey(c.provider, c.key) != s.count ? 1u : 0u, 0};
        }
        return Status::Ok;
    });
}
Status BS_CALL EvaluateEffectiveEligibility(std::uint64_t reader, std::uint64_t slotId, std::uint64_t expected,
    std::uint32_t hand, std::uint32_t content, std::uint32_t* allowed) noexcept {
    return Guard([&] {
        if (!allowed || (hand != Left && hand != Right) || !content || (content & (content - 1)) || (content & ~AllContent)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.CanReadConfiguration(reader)) return Status::Unauthorized;
        if (!expected || expected != s.configurationRevision) return Status::Conflict;
        for (std::uint32_t i = 0; i < s.count; ++i) if (s.records[i].slotId == slotId) {
            const auto effective = Effective(s, s.records[i]); const auto& d = effective.slot.definition;
            *allowed = (d.flags & kEnabled) && (d.handMask & hand) && (d.contentMask & content) ? 1u : 0u;
            return Status::Ok;
        }
        return Status::NotFound;
    });
}
Status SaveCustomizations(std::vector<std::uint8_t>& output) noexcept {
    return Guard([&] {
        auto store = std::make_unique<CustomizationStore>();
        { auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex); *store = s.customizations; }
        return EncodeCustomizations(*store, output);
    });
}
Status LoadCustomizations(const std::uint8_t* bytes, std::uint32_t size) noexcept {
    return Guard([&] {
        auto candidate = std::make_unique<CustomizationStore>();
        auto status = DecodeCustomizations(bytes, size, *candidate); if (status != Status::Ok) return status;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (s.configurationRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        s.customizations = *candidate; ++s.configurationRevision; return Status::Ok;
    });
}
Status ResetCustomizations() noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (s.configurationRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        s.customizations = {}; ++s.configurationRevision; return Status::Ok;
    });
}
const ConfigurationApi kConfigurationApi{sizeof(ConfigurationApi), kConfigurationApiVersion,
    kEffectiveConfigurationCapability | kSavedCustomizationCapability, RegisterCustomizer, ReleaseCustomizer,
    ApplyCustomization, ClearCustomization, CopyEffectiveDefinitions, CopyCustomizations, EvaluateEffectiveEligibility};
const Api kApi{sizeof(Api), kApiVersion, kRegistryCapability | kSnapshotCapability | kContentFilterCapability,
    RegisterClient, ReleaseClient, UpsertDefinition, RemoveDefinition, CopyDefinitions, EvaluateEligibility, ReplaceDefinitions, RegisterObserver};
} // namespace body_slots::detail

extern "C" __declspec(dllexport) const body_slots::Api* BS_CALL BodySlots_QueryInterface(std::uint32_t version, std::uint32_t size) noexcept {
    if (version != body_slots::kApiVersion || size != sizeof(body_slots::Api)) return nullptr;
    return &body_slots::detail::kApi;
}

extern "C" __declspec(dllexport) const body_slots::ConfigurationApi* BS_CALL BodySlots_QueryConfigurationInterface(std::uint32_t version, std::uint32_t size) noexcept {
    if (version != body_slots::kConfigurationApiVersion || size != sizeof(body_slots::ConfigurationApi)) return nullptr;
    return &body_slots::detail::kConfigurationApi;
}
