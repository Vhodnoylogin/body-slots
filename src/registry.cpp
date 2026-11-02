#include "validation.h"
#include "configuration.h"
#include "runtime_token.h"
#include "instances.h"
#include "instance_configuration.h"
#include "runtime_saved.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>

namespace body_slots::detail {
constexpr std::size_t kMaxWriters = 64, kMaxObservers = 64, kMaxClients = kMaxWriters + kMaxObservers;
struct Client { std::uint64_t token = 0; char provider[64]{}; bool releasing = false; };
std::atomic<RuntimeReleaseHook> runtimeReleaseHook{nullptr};
void SetRuntimeReleaseHook(RuntimeReleaseHook hook) noexcept { runtimeReleaseHook.store(hook,std::memory_order_release); }
struct Customizer { std::uint64_t token = 0; char identity[64]{}; };
struct Registry {
    std::mutex mutex;
    std::array<Client, kMaxClients> clients{};
    std::array<SlotRecord, kMaxDefinitions> records{};
    std::array<Customizer, 32> customizers{};
    CustomizationStore customizations{};
    InstanceStore instances{};
    InstanceCustomizationStore instanceCustomizations{};
    RuntimeCustomizationData runtimeCustomizations{};
    std::uint32_t count = 0;
    std::uint64_t revision = 1, nextToken = 1, nextSlot = 1, nextDefinitionRevision = 1;
    std::uint64_t configurationRevision = 1;
    Client* FindClient(std::uint64_t token) noexcept {
        if (token) for (auto& c : clients) if (c.token == token && !c.releasing) return &c;
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
        const auto allocation = NextRuntimeToken(s.nextToken, RuntimeDomain::Registry);
        if (allocation.status != Status::Ok) return allocation.status;
        for (std::size_t i = 0; i < kMaxWriters; ++i) {
            auto& c = s.clients[i]; if (c.token) continue;
            std::memcpy(c.provider, name, sizeof(name)); c.token = allocation.token; s.nextToken = allocation.nextCounter;
            *client = c.token; return Status::Ok;
        }
        return Status::LimitExceeded;
    });
}
Status BS_CALL RegisterObserver(std::uint64_t* client) noexcept {
    return Guard([&] {
        if (!client) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        const auto allocation = NextRuntimeToken(s.nextToken, RuntimeDomain::Registry);
        if (allocation.status != Status::Ok) return allocation.status;
        for (std::size_t i = kMaxWriters; i < kMaxClients; ++i) if (!s.clients[i].token) {
            s.clients[i].token = allocation.token; s.nextToken = allocation.nextCounter;
            *client = s.clients[i].token; return Status::Ok;
        }
        return Status::LimitExceeded;
    });
}
Status BS_CALL ReleaseClient(std::uint64_t token) noexcept {
    return Guard([&] {
        auto transition = std::make_unique<InstanceStore::Candidate>();
        // Revoke registration/invocation first, then drain callback code outside
        // the registry mutex. A callback cannot release its own live stack.
        auto& state=State();
        { std::lock_guard<std::mutex> lock(state.mutex);
            auto* client=state.FindClient(token); if(!client)return Status::Unauthorized;
            client->releasing=true;
        }
        const auto hook=runtimeReleaseHook.load(std::memory_order_acquire);
        const auto drained=hook?hook(token):Status::Ok;
        if(drained!=Status::Ok) {
            std::lock_guard<std::mutex> lock(state.mutex);
            for(auto& client:state.clients)if(client.token==token)client.releasing=false;
            return drained;
        }
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        Client* c=nullptr;for(auto& client:s.clients)if(client.token==token)c=&client;
        if(!c)return Status::Unauthorized;
        std::uint32_t out = 0;
        bool owned = false;
        for (std::uint32_t i = 0; i < s.count; ++i) if (!std::strcmp(s.records[i].definition.provider, c->provider)) owned = true;
        if (owned && !s.CanAdvance(0, 0)) { c->releasing=false;return Status::LimitExceeded; }
        const auto status = s.instances.PrepareRelease(token, owned, *transition);
        if (status != Status::Ok) { c->releasing=false;return status; }
        for (std::uint32_t i = 0; i < s.count; ++i)
            if (std::strcmp(s.records[i].definition.provider, c->provider)) s.records[out++] = s.records[i];
        if (owned) { s.count = out; ++s.revision; ++s.configurationRevision; }
        s.instances.Apply(*transition);
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
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto instanceStatus = s.instances.PrepareInvalidation(*transition);
        if (instanceStatus != Status::Ok) return instanceStatus;
        SlotRecord record{d, s.nextDefinitionRevision++, create ? s.nextSlot++ : s.records[index].slotId};
        s.records[index] = record; if (create) ++s.count;
        Sort(s.records, s.count); ++s.revision; ++s.configurationRevision;
        s.instances.Apply(*transition); *output = record; return Status::Ok;
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
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto instanceStatus = s.instances.PrepareInvalidation(*transition);
        if (instanceStatus != Status::Ok) return instanceStatus;
        for (auto i = index + 1; i < s.count; ++i) s.records[i - 1] = s.records[i];
        --s.count; ++s.revision; ++s.configurationRevision; s.instances.Apply(*transition); return Status::Ok;
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
            auto transition = std::make_unique<InstanceStore::Candidate>();
            const auto instanceStatus = s.instances.PrepareInvalidation(*transition);
            if (instanceStatus != Status::Ok) return instanceStatus;
            s.records = candidate; s.count = total; s.nextSlot = nextSlot;
            s.nextDefinitionRevision = nextRevision; ++s.revision; ++s.configurationRevision;
            s.instances.Apply(*transition);
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
InstanceRecord EffectiveInstance(const Registry& state, const InstanceStore::Entry& entry) noexcept {
    InstanceRecord out{}; out.structSize = sizeof(out); out.flags = entry.spec.flags;
    std::memcpy(out.key.ownerProvider, entry.provider, sizeof(entry.provider));
    std::memcpy(out.key.equipmentKey, entry.spec.equipmentKey, sizeof(out.key.equipmentKey));
    std::memcpy(out.key.slotKey, entry.spec.slotKey, sizeof(out.key.slotKey));
    std::memcpy(out.definitionProvider, entry.spec.definitionProvider, sizeof(out.definitionProvider));
    std::memcpy(out.definitionKey, entry.spec.definitionKey, sizeof(out.definitionKey));
    out.instanceHandle = entry.handle; out.instanceRevision = entry.revision;
    out.pinnedDefinitionRevision = entry.spec.definitionRevision; out.binding = entry.spec.binding;
    out.templateState = static_cast<std::uint32_t>(InstanceTemplateState::Missing);
    const auto index = state.FindKey(entry.spec.definitionProvider, entry.spec.definitionKey);
    if (index == state.count) return out;
    out.currentDefinitionRevision = state.records[index].definitionRevision;
    out.templateState = static_cast<std::uint32_t>(InstanceTemplateState::Changed);
    if (out.currentDefinitionRevision != out.pinnedDefinitionRevision) return out;
    const auto effective = Effective(state, state.records[index]);
    out.templateState = static_cast<std::uint32_t>(InstanceTemplateState::Current);
    out.effective = effective.slot.definition; out.customizationMask = effective.customizationMask;
    return out;
}
Status BS_CALL UpsertInstance(std::uint64_t writer, const InstanceSpec* spec,
    std::uint64_t scope, std::uint64_t expected, InstanceRecord* committed) noexcept {
    return Guard([&] {
        if (!spec || !committed) return Status::InvalidArgument;
        const auto input = *spec;
        const auto validation = ValidateInstanceSpec(input); if (validation != Status::Ok) return validation;
        if (committed->structSize != sizeof(*committed)) return Status::UnsupportedVersion;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        const auto* client = s.FindClient(writer);
        if (!client || !client->provider[0]) return Status::Unauthorized;
        if (!scope || scope != s.instances.Generation()) return Status::Conflict;
        const auto index = s.FindKey(input.definitionProvider, input.definitionKey);
        if (index == s.count) return Status::NotFound;
        if (input.definitionRevision != s.records[index].definitionRevision) return Status::Conflict;
        auto transition = std::make_unique<InstanceStore::Candidate>(); std::uint64_t handle = 0;
        const auto status = s.instances.PrepareUpsert(writer, client->provider, input, scope, expected, *transition, handle);
        if (status != Status::Ok) return status;
        s.instances.Apply(*transition);
        *committed = EffectiveInstance(s, *s.instances.Find(handle)); return Status::Ok;
    });
}
Status BS_CALL RemoveInstance(std::uint64_t writer, std::uint64_t handle,
    std::uint64_t scope, std::uint64_t expected) noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        const auto* client = s.FindClient(writer);
        if (!client || !client->provider[0]) return Status::Unauthorized;
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto status = s.instances.PrepareRemove(writer, handle, scope, expected, *transition);
        if (status != Status::Ok) return status;
        s.instances.Apply(*transition); return Status::Ok;
    });
}
Status BS_CALL CopyEffectiveInstances(std::uint64_t reader, std::uint64_t scope, std::uint64_t expected,
    InstanceRecord* records, std::uint32_t capacity, InstanceView* view) noexcept {
    return Guard([&] {
        if (!view || (capacity && !records) || capacity > kMaxInstances) return Status::InvalidArgument;
        if (view->structSize != sizeof(*view) || view->version != kInstanceApiVersion || view->reserved) return Status::UnsupportedVersion;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.CanReadConfiguration(reader)) return Status::Unauthorized;
        const InstanceView header{sizeof(InstanceView), kInstanceApiVersion, s.instances.Generation(),
            s.instances.ViewRevision(), s.revision, s.configurationRevision, s.instances.Count(), 0};
        if ((scope && scope != header.saveGeneration) || (expected && expected != header.viewRevision)) {
            *view = header; return Status::Conflict;
        }
        if (capacity < header.count) { *view = header; return Status::BufferTooSmall; }
        for (std::uint32_t index = 0; index < header.count; ++index) records[index] = EffectiveInstance(s, s.instances.At(index));
        *view = header; return Status::Ok;
    });
}
Status BS_CALL EvaluateInstanceEligibility(std::uint64_t reader, std::uint64_t handle,
    std::uint64_t scope, std::uint64_t expected, std::uint32_t hand, std::uint32_t content,
    std::uint32_t* allowed) noexcept {
    return Guard([&] {
        if (!allowed || (hand != Left && hand != Right) || !content || (content & (content - 1)) || (content & ~AllContent)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.CanReadConfiguration(reader)) return Status::Unauthorized;
        if (!scope || scope != s.instances.Generation() || !expected || expected != s.instances.ViewRevision()) return Status::Conflict;
        const auto* entry = s.instances.Find(handle); if (!entry) return Status::NotFound;
        const auto record = EffectiveInstance(s, *entry); const auto& definition = record.effective;
        *allowed = record.templateState == static_cast<std::uint32_t>(InstanceTemplateState::Current)
            && (record.flags & kInstanceActive) && (definition.flags & kEnabled)
            && (definition.handMask & hand) && (definition.contentMask & content) ? 1u : 0u;
        return Status::Ok;
    });
}
InstanceCustomizationKey CustomizationKey(const InstanceStore::Entry& entry) noexcept {
    InstanceCustomizationKey key{};
    std::memcpy(key.instance.ownerProvider, entry.provider, 64);
    std::memcpy(key.instance.equipmentKey, entry.spec.equipmentKey, 64);
    std::memcpy(key.instance.slotKey, entry.spec.slotKey, 64);
    std::memcpy(key.definitionProvider, entry.spec.definitionProvider, 64);
    std::memcpy(key.definitionKey, entry.spec.definitionKey, 64);
    return key;
}
const InstanceStore::Entry* FindCustomizationInstance(const Registry& state, const InstanceCustomizationKey& key) noexcept {
    for (std::uint32_t index = 0; index < state.instances.Count(); ++index) {
        const auto& entry = state.instances.At(index);
        if (SameInstanceCustomizationKey(CustomizationKey(entry), key)) return &entry;
    }
    return nullptr;
}
ConfiguredInstanceRecord ConfiguredInstance(const Registry& state, const InstanceStore::Entry& entry) noexcept {
    ConfiguredInstanceRecord out{}; out.instance = EffectiveInstance(state, entry);
    if (out.instance.templateState != static_cast<std::uint32_t>(InstanceTemplateState::Current)) return out;
    const auto* custom = state.instanceCustomizations.Find(CustomizationKey(entry)); if (!custom) return out;
    auto& definition = out.instance.effective; out.instanceCustomizationMask = custom->fieldMask;
    if (custom->fieldMask & CustomizePosition) std::memcpy(definition.position, custom->position, sizeof(custom->position));
    if (custom->fieldMask & CustomizeRotation) std::memcpy(definition.rotation, custom->rotation, sizeof(custom->rotation));
    if (custom->fieldMask & CustomizeAccessRegion) {
        definition.radius = custom->radius; std::memcpy(definition.regionCenter, custom->regionCenter, sizeof(custom->regionCenter));
    }
    if (custom->fieldMask & CustomizeEnabled) definition.flags = custom->enabled ? kEnabled : 0;
    return out;
}
InstanceView InstanceHeader(const Registry& state, std::uint32_t count) noexcept {
    return {sizeof(InstanceView), kInstanceApiVersion, state.instances.Generation(), state.instances.ViewRevision(),
        state.revision, state.configurationRevision, count, 0};
}
bool CurrentInstanceView(const Registry& state, std::uint64_t scope, std::uint64_t view) noexcept {
    return scope && view && scope == state.instances.Generation() && view == state.instances.ViewRevision();
}
Status BS_CALL ApplyActivationChanges(std::uint64_t writer, const ActivationChange* changes, std::uint32_t count,
    std::uint64_t scope, std::uint64_t view, InstanceView* output) noexcept {
    return Guard([&] {
        if (!changes || !output || !count || count > kMaxActivationChanges) return Status::InvalidArgument;
        if (output->structSize != sizeof(*output) || output->version != kInstanceApiVersion || output->reserved) return Status::UnsupportedVersion;
        auto copied = std::make_unique<std::array<ActivationChange, kMaxActivationChanges>>();
        std::copy(changes, changes + count, copied->begin());
        const auto validation = ValidateActivationChanges(copied->data(), count); if (validation != Status::Ok) return validation;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        const auto* client = s.FindClient(writer); if (!client || !client->provider[0]) return Status::Unauthorized;
        if (!CurrentInstanceView(s, scope, view)) return Status::Conflict;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& change = (*copied)[i]; const auto* entry = s.instances.Find(change.instanceHandle);
            if (!entry) return Status::NotFound;
            if (entry->owner != writer) return Status::Unauthorized;
            if (!change.expectedInstanceRevision || change.expectedInstanceRevision != entry->revision) return Status::Conflict;
            if (change.flags & kInstanceActive) {
                const auto index = s.FindKey(entry->spec.definitionProvider, entry->spec.definitionKey);
                if (index == s.count || s.records[index].definitionRevision != entry->spec.definitionRevision) return Status::Unavailable;
            }
        }
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto status = s.instances.PrepareActivation(writer, copied->data(), count, scope, view, *transition);
        if (status != Status::Ok) return status;
        s.instances.Apply(*transition); *output = InstanceHeader(s, s.instances.Count()); return Status::Ok;
    });
}
Status BS_CALL ApplyInstanceCustomization(std::uint64_t lease, const InstanceCustomization* customization,
    std::uint64_t scope, std::uint64_t view, ConfiguredInstanceRecord* output) noexcept {
    return Guard([&] {
        if (!customization || !output) return Status::InvalidArgument;
        const auto custom = *customization;
        const auto validation = ValidateInstanceCustomization(custom); if (validation != Status::Ok) return validation;
        if (output->instance.structSize != sizeof(InstanceRecord)) return Status::UnsupportedVersion;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.FindCustomizer(lease)) return Status::Unauthorized;
        if (!CurrentInstanceView(s, scope, view)) return Status::Conflict;
        const auto* entry = FindCustomizationInstance(s, custom.key); if (!entry) return Status::NotFound;
        if (EffectiveInstance(s, *entry).templateState != static_cast<std::uint32_t>(InstanceTemplateState::Current)) return Status::Unavailable;
        auto customizationTransition = std::make_unique<InstanceCustomizationStore::Candidate>(); bool changed = false;
        const auto status = s.instanceCustomizations.PrepareApply(custom, *customizationTransition, changed);
        if (status != Status::Ok) return status;
        if (changed) {
            auto transition = std::make_unique<InstanceStore::Candidate>();
            const auto instanceStatus = s.instances.PrepareInvalidation(*transition); if (instanceStatus != Status::Ok) return instanceStatus;
            s.instanceCustomizations.Apply(*customizationTransition); s.instances.Apply(*transition);
        }
        *output = ConfiguredInstance(s, *entry); return Status::Ok;
    });
}
Status BS_CALL ClearInstanceCustomization(std::uint64_t lease, const InstanceCustomizationKey* key,
    std::uint64_t scope, std::uint64_t view) noexcept {
    return Guard([&] {
        if (!key) return Status::InvalidArgument;
        const auto copied = *key;
        const auto validation = ValidateInstanceCustomizationKey(copied); if (validation != Status::Ok) return validation;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.FindCustomizer(lease)) return Status::Unauthorized;
        if (!CurrentInstanceView(s, scope, view)) return Status::Conflict;
        auto customizationTransition = std::make_unique<InstanceCustomizationStore::Candidate>();
        const auto status = s.instanceCustomizations.PrepareClear(copied, *customizationTransition); if (status != Status::Ok) return status;
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto instanceStatus = s.instances.PrepareInvalidation(*transition); if (instanceStatus != Status::Ok) return instanceStatus;
        s.instanceCustomizations.Apply(*customizationTransition); s.instances.Apply(*transition); return Status::Ok;
    });
}
template<class Record, class CopyRecord>
Status CopyInstanceConfiguration(std::uint64_t reader, std::uint64_t scope, std::uint64_t expected,
    Record* records, std::uint32_t capacity, InstanceView* view, bool saved, CopyRecord copy) noexcept {
    return Guard([&] {
        if (!view || (capacity && !records) || capacity > kMaxInstances) return Status::InvalidArgument;
        if (view->structSize != sizeof(*view) || view->version != kInstanceApiVersion || view->reserved) return Status::UnsupportedVersion;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.CanReadConfiguration(reader)) return Status::Unauthorized;
        const auto header = InstanceHeader(s, saved ? s.instanceCustomizations.Count() : s.instances.Count());
        if ((scope && scope != header.saveGeneration) || (expected && expected != header.viewRevision)) {
            *view = header; return Status::Conflict;
        }
        if (capacity < header.count) { *view = header; return Status::BufferTooSmall; }
        for (std::uint32_t index = 0; index < header.count; ++index) records[index] = copy(s, index);
        *view = header; return Status::Ok;
    });
}
Status BS_CALL CopyConfiguredInstances(std::uint64_t reader, std::uint64_t scope, std::uint64_t expected,
    ConfiguredInstanceRecord* records, std::uint32_t capacity, InstanceView* view) noexcept {
    return CopyInstanceConfiguration(reader, scope, expected, records, capacity, view, false,
        [](const Registry& state, std::uint32_t index) { return ConfiguredInstance(state, state.instances.At(index)); });
}
Status BS_CALL CopyInstanceCustomizations(std::uint64_t reader, std::uint64_t scope, std::uint64_t expected,
    InstanceCustomizationRecord* records, std::uint32_t capacity, InstanceView* view) noexcept {
    return CopyInstanceConfiguration(reader, scope, expected, records, capacity, view, true,
        [](const Registry& state, std::uint32_t index) {
            InstanceCustomizationRecord out{state.instanceCustomizations.At(index), 0, 0};
            const auto* entry = FindCustomizationInstance(state, out.customization.key);
            out.available = entry && EffectiveInstance(state, *entry).templateState == static_cast<std::uint32_t>(InstanceTemplateState::Current) ? 1u : 0u;
            return out;
        });
}
Status BS_CALL EvaluateConfiguredInstanceEligibility(std::uint64_t reader, std::uint64_t handle,
    std::uint64_t scope, std::uint64_t expected, std::uint32_t hand, std::uint32_t content, std::uint32_t* allowed) noexcept {
    return Guard([&] {
        if (!allowed || (hand != Left && hand != Right) || !content || (content & (content - 1)) || (content & ~AllContent)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.CanReadConfiguration(reader)) return Status::Unauthorized;
        if (!CurrentInstanceView(s, scope, expected)) return Status::Conflict;
        const auto* entry = s.instances.Find(handle); if (!entry) return Status::NotFound;
        const auto record = ConfiguredInstance(s, *entry); const auto& definition = record.instance.effective;
        *allowed = record.instance.templateState == static_cast<std::uint32_t>(InstanceTemplateState::Current)
            && (record.instance.flags & kInstanceActive) && (definition.flags & kEnabled)
            && (definition.handMask & hand) && (definition.contentMask & content) ? 1u : 0u;
        return Status::Ok;
    });
}
Status BS_CALL RegisterCustomizer(const char* identity, std::uint64_t* lease) noexcept {
    return Guard([&] {
        char name[64]{}; if (!lease || !CopyIdentity(identity, name)) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        for (const auto& c : s.customizers) if (c.token && !std::strcmp(c.identity, name)) return Status::Conflict;
        const auto allocation = NextRuntimeToken(s.nextToken, RuntimeDomain::Registry);
        if (allocation.status != Status::Ok) return allocation.status;
        for (auto& c : s.customizers) if (!c.token) {
            std::memcpy(c.identity, name, sizeof(name)); c.token = allocation.token; s.nextToken = allocation.nextCounter;
            *lease = c.token; return Status::Ok;
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
            auto transition = std::make_unique<InstanceStore::Candidate>();
            const auto instanceStatus = s.instances.PrepareInvalidation(*transition);
            if (instanceStatus != Status::Ok) return instanceStatus;
            s.customizations.entries[index] = c; if (index == s.customizations.count) ++s.customizations.count;
            std::sort(s.customizations.entries.begin(), s.customizations.entries.begin() + s.customizations.count,
                [](const Customization& a, const Customization& b) {
                    const int provider = std::strcmp(a.provider, b.provider);
                    return provider < 0 || (provider == 0 && std::strcmp(a.key, b.key) < 0);
                });
            ++s.configurationRevision;
            s.instances.Apply(*transition);
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
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto instanceStatus = s.instances.PrepareInvalidation(*transition);
        if (instanceStatus != Status::Ok) return instanceStatus;
        for (auto i = index + 1; i < s.customizations.count; ++i) s.customizations.entries[i - 1] = s.customizations.entries[i];
        s.customizations.entries[--s.customizations.count] = {}; ++s.configurationRevision;
        s.instances.Apply(*transition); return Status::Ok;
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
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto instanceStatus = s.instances.PrepareInvalidation(*transition);
        if (instanceStatus != Status::Ok) return instanceStatus;
        s.customizations = *candidate; ++s.configurationRevision; s.instances.Apply(*transition); return Status::Ok;
    });
}
Status ResetCustomizations() noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (s.configurationRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto instanceStatus = s.instances.PrepareInvalidation(*transition);
        if (instanceStatus != Status::Ok) return instanceStatus;
        s.customizations = {}; ++s.configurationRevision; s.instances.Apply(*transition); return Status::Ok;
    });
}
Status ResetSaveScope(InstanceView* committed) noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (s.configurationRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        auto transition = std::make_unique<InstanceStore::Candidate>();
        const auto status = s.instances.PrepareReset(*transition); if (status != Status::Ok) return status;
        auto customizationTransition = std::make_unique<InstanceCustomizationStore::Candidate>();
        s.instanceCustomizations.PrepareReset(*customizationTransition);
        s.customizations = {}; ++s.configurationRevision;
        s.runtimeCustomizations = {};
        s.instanceCustomizations.Apply(*customizationTransition); s.instances.Apply(*transition);
        if (committed) *committed = InstanceHeader(s, 0);
        return Status::Ok;
    });
}
Status ResetSaveScope() noexcept { return ResetSaveScope(nullptr); }
Status SaveConfigurationBatch(std::vector<std::uint8_t>& definitions, std::vector<std::uint8_t>& instances, bool& hasInstances) noexcept {
    std::vector<std::uint8_t> runtime;bool hasRuntime=false;
    return SaveConfigurationBatch(definitions,instances,hasInstances,runtime,hasRuntime);
}
Status SaveConfigurationBatch(std::vector<std::uint8_t>& definitions, std::vector<std::uint8_t>& instances, bool& hasInstances,
    std::vector<std::uint8_t>& runtime,bool& hasRuntime) noexcept {
    return Guard([&] {
        auto definitionData = std::make_unique<CustomizationStore>();
        auto instanceData = std::make_unique<InstanceCustomizationData>();
        auto runtimeData = std::make_unique<RuntimeCustomizationData>();
        { auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
            *definitionData = s.customizations; s.instanceCustomizations.CopyTo(*instanceData); *runtimeData=s.runtimeCustomizations; }
        std::vector<std::uint8_t> definitionBytes, instanceBytes,runtimeBytes;
        auto status = EncodeCustomizations(*definitionData, definitionBytes); if (status != Status::Ok) return status;
        status = EncodeInstanceCustomizations(*instanceData, instanceBytes); if (status != Status::Ok) return status;
        status = EncodeRuntimeCustomizations(*runtimeData,runtimeBytes);if(status!=Status::Ok)return status;
        definitions.swap(definitionBytes); instances.swap(instanceBytes); hasInstances = instanceData->count != 0;
        runtime.swap(runtimeBytes);hasRuntime=runtimeData->count!=0;
        return Status::Ok;
    });
}
Status LoadConfigurationBatch(const std::vector<std::uint8_t>* definitions, const std::vector<std::uint8_t>* instances,
    std::uint64_t scope, std::uint64_t view) noexcept {
    return LoadConfigurationBatch(definitions,instances,nullptr,scope,view);
}
Status LoadConfigurationBatch(const std::vector<std::uint8_t>* definitions,const std::vector<std::uint8_t>* instances,
    const std::vector<std::uint8_t>* runtime,std::uint64_t scope,std::uint64_t view) noexcept {
    return Guard([&] {
        if ((instances||runtime) && !definitions) return Status::InvalidArgument;
        if ((definitions && definitions->size() > kMaxCustomizationSaveBytes) ||
            (instances && instances->size() > kMaxInstanceCustomizationSaveBytes)) return Status::InvalidArgument;
        auto definitionData = std::make_unique<CustomizationStore>();
        auto instanceData = std::make_unique<InstanceCustomizationData>();
        auto runtimeData = std::make_unique<RuntimeCustomizationData>();
        if (definitions) {
            const auto status = DecodeCustomizations(definitions->data(), static_cast<std::uint32_t>(definitions->size()), *definitionData);
            if (status != Status::Ok) return status;
        }
        if (instances) {
            const auto status = DecodeInstanceCustomizations(instances->data(), static_cast<std::uint32_t>(instances->size()), *instanceData);
            if (status != Status::Ok) return status;
        }
        if(runtime){if(runtime->size()>kMaxRuntimeCustomizationSaveBytes)return Status::InvalidArgument;const auto status=DecodeRuntimeCustomizations(runtime->data(),static_cast<std::uint32_t>(runtime->size()),*runtimeData);if(status!=Status::Ok)return status;}
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!CurrentInstanceView(s, scope, view)) return Status::Conflict;
        if (s.configurationRevision == std::numeric_limits<std::uint64_t>::max()) return Status::LimitExceeded;
        auto transition = std::make_unique<InstanceStore::Candidate>();
        auto status = s.instances.PrepareInvalidation(*transition); if (status != Status::Ok) return status;
        auto customizationTransition = std::make_unique<InstanceCustomizationStore::Candidate>();
        status = s.instanceCustomizations.PrepareReplace(*instanceData, *customizationTransition); if (status != Status::Ok) return status;
        s.customizations = *definitionData; ++s.configurationRevision;
        s.runtimeCustomizations=*runtimeData;
        s.instanceCustomizations.Apply(*customizationTransition); s.instances.Apply(*transition); return Status::Ok;
    });
}
const InstanceConfigurationApi kInstanceConfigurationApi{sizeof(InstanceConfigurationApi), kInstanceConfigurationApiVersion,
    kConfiguredInstancesCapability | kSavedInstanceCustomizationCapability, ApplyInstanceCustomization, ClearInstanceCustomization,
    CopyConfiguredInstances, CopyInstanceCustomizations, EvaluateConfiguredInstanceEligibility};
Status RuntimeClientIdentity(std::uint64_t token, bool writer, char provider[64]) noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        const auto* c = s.FindClient(token);
        if (!c || (writer && !c->provider[0])) return Status::Unauthorized;
        if (provider) std::memcpy(provider, c->provider, 64);
        return Status::Ok;
    });
}
Status RuntimeOwnsInstance(std::uint64_t token, std::uint64_t handle) noexcept {
    return Guard([&] {
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        const auto* c = s.FindClient(token);
        if (!c || !c->provider[0]) return Status::Unauthorized;
        const auto* entry = s.instances.Find(handle);
        if (!entry) return Status::NotFound;
        return entry->owner == token ? Status::Ok : Status::Unauthorized;
    });
}
Status CopyRuntimeConfiguration(ConfiguredInstanceRecord* records, std::uint32_t capacity, InstanceView* view) noexcept {
    return CopyRuntimeConfigurationWithCustomizations(records,capacity,view,nullptr);
}
Status CopyRuntimeConfigurationWithCustomizations(ConfiguredInstanceRecord* records,std::uint32_t capacity,
    InstanceView* view,RuntimeCustomizationData* customizations) noexcept {
    return Guard([&] {
        if (!records || !view || capacity < kMaxInstances) return Status::InvalidArgument;
        auto& s = State(); std::lock_guard<std::mutex> lock(s.mutex);
        for (std::uint32_t i = 0; i < s.instances.Count(); ++i) records[i] = ConfiguredInstance(s, s.instances.At(i));
        if(customizations)*customizations=s.runtimeCustomizations;
        *view = InstanceHeader(s, s.instances.Count()); return Status::Ok;
    });
}
Status SetSavedRuntimeCustomization(std::uint64_t writer,std::uint64_t handle,const RuntimeBindingSpec* binding,
    const SlotVisualSpec* visual) noexcept {
    return Guard([&] {
        if(!binding&&!visual)return Status::InvalidArgument;
        if(binding){const auto status=ValidateRuntimeBinding(*binding);if(status!=Status::Ok)return status;}
        if(visual){const auto status=ValidateRuntimeVisual(*visual);if(status!=Status::Ok)return status;}
        auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);const auto* client=s.FindClient(writer);
        if(!client||!client->provider[0])return Status::Unauthorized;
        const auto* instance=s.instances.Find(handle);if(!instance)return Status::NotFound;if(instance->owner!=writer)return Status::Unauthorized;
        const auto configured=ConfiguredInstance(s,*instance);RuntimeCustomization value{};
        value.key.instance=configured.instance.key;std::memcpy(value.key.definitionProvider,configured.instance.definitionProvider,64);std::memcpy(value.key.definitionKey,configured.instance.definitionKey,64);
        auto index=s.runtimeCustomizations.count;for(std::uint32_t i=0;i<s.runtimeCustomizations.count;++i)if(SameInstanceCustomizationKey(s.runtimeCustomizations.entries[i].key,value.key)){index=i;value=s.runtimeCustomizations.entries[i];break;}
        if(index==s.runtimeCustomizations.count&&index==kMaxInstances)return Status::LimitExceeded;
        if(binding){value.binding=*binding;value.fieldMask|=kSavedRuntimeBinding;}if(visual){value.visual=*visual;value.fieldMask|=kSavedRuntimeVisual;}
        if(index<s.runtimeCustomizations.count&&!std::memcmp(&value,&s.runtimeCustomizations.entries[index],sizeof(value)))return Status::Ok;
        auto transition=std::make_unique<InstanceStore::Candidate>();const auto status=s.instances.PrepareInvalidation(*transition);if(status!=Status::Ok)return status;
        s.runtimeCustomizations.entries[index]=value;if(index==s.runtimeCustomizations.count)++s.runtimeCustomizations.count;s.instances.Apply(*transition);return Status::Ok;
    });
}
Status CopySavedRuntimeCustomizations(std::uint64_t reader,RuntimeCustomizationRecord* records,std::uint32_t capacity,InstanceView* view) noexcept {
    return Guard([&]{
        if(!view||capacity>kMaxInstances||(capacity&&!records))return Status::InvalidArgument;
        if(view->structSize!=sizeof(InstanceView)||view->version!=kInstanceApiVersion||view->reserved)return Status::UnsupportedVersion;
        auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);if(!s.FindClient(reader))return Status::Unauthorized;
        *view=InstanceHeader(s,s.runtimeCustomizations.count);if(capacity<s.runtimeCustomizations.count)return Status::BufferTooSmall;
        std::copy(s.runtimeCustomizations.entries.begin(),s.runtimeCustomizations.entries.begin()+s.runtimeCustomizations.count,records);return Status::Ok;
    });
}
Status ClearSavedRuntimeCustomization(std::uint64_t writer,const InstanceCustomizationKey* key,std::uint32_t mask,std::uint64_t scope,std::uint64_t view) noexcept {
    return Guard([&]{
        if(!key||ValidateInstanceCustomizationKey(*key)!=Status::Ok||!mask||(mask&~3u))return Status::InvalidArgument;
        auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);const auto* client=s.FindClient(writer);if(!client||!client->provider[0]||std::strcmp(client->provider,key->instance.ownerProvider))return Status::Unauthorized;
        if(!CurrentInstanceView(s,scope,view))return Status::Conflict;
        std::uint32_t index=0;for(;index<s.runtimeCustomizations.count;++index)if(SameInstanceCustomizationKey(s.runtimeCustomizations.entries[index].key,*key))break;
        if(index==s.runtimeCustomizations.count)return Status::NotFound;
        auto transition=std::make_unique<InstanceStore::Candidate>();auto status=s.instances.PrepareInvalidation(*transition);if(status!=Status::Ok)return status;
        auto& record=s.runtimeCustomizations.entries[index];record.fieldMask&=~mask;if(mask&kSavedRuntimeBinding)record.binding={};if(mask&kSavedRuntimeVisual)record.visual={};
        if(!record.fieldMask){for(auto i=index+1;i<s.runtimeCustomizations.count;++i)s.runtimeCustomizations.entries[i-1]=s.runtimeCustomizations.entries[i];s.runtimeCustomizations.entries[--s.runtimeCustomizations.count]={};}
        s.instances.Apply(*transition);return Status::Ok;
    });
}
const InstanceApi kInstanceApi{sizeof(InstanceApi), kInstanceApiVersion,
    kRuntimeInstancesCapability | kBodyAnchorInstancesCapability, UpsertInstance, RemoveInstance,
    CopyEffectiveInstances, EvaluateInstanceEligibility};
const InstanceActivationApi kInstanceActivationApi{sizeof(InstanceActivationApi), kInstanceActivationApiVersion,
    kAtomicInstanceActivationCapability, ApplyActivationChanges};
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
extern "C" __declspec(dllexport) const body_slots::InstanceApi* BS_CALL BodySlots_QueryInstanceInterface(std::uint32_t version, std::uint32_t size) noexcept {
    if (version != body_slots::kInstanceApiVersion || size != sizeof(body_slots::InstanceApi)) return nullptr;
    return &body_slots::detail::kInstanceApi;
}
extern "C" __declspec(dllexport) const body_slots::InstanceActivationApi* BS_CALL BodySlots_QueryInstanceActivationInterface(std::uint32_t version, std::uint32_t size) noexcept {
    if (version != body_slots::kInstanceActivationApiVersion || size != sizeof(body_slots::InstanceActivationApi)) return nullptr;
    return &body_slots::detail::kInstanceActivationApi;
}
extern "C" __declspec(dllexport) const body_slots::InstanceConfigurationApi* BS_CALL BodySlots_QueryInstanceConfigurationInterface(std::uint32_t version, std::uint32_t size) noexcept {
    if (version != body_slots::kInstanceConfigurationApiVersion || size != sizeof(body_slots::InstanceConfigurationApi)) return nullptr;
    return &body_slots::detail::kInstanceConfigurationApi;
}
