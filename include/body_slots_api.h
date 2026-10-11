#pragma once
// Initial, experimental ABI. Fixed copied records; no Skyrim objects or allocator ownership.
#include <cstdint>
#include <type_traits>
#if defined(_WIN32)
#define BS_CALL __cdecl
#else
#define BS_CALL
#endif

namespace body_slots {
constexpr std::uint32_t kApiVersion = 1;
constexpr std::uint32_t kDefinitionVersion = 1;
constexpr std::uint32_t kMaxDefinitions = 256;
constexpr std::uint64_t kRegistryCapability = 1;
constexpr std::uint64_t kSnapshotCapability = 2;
constexpr std::uint64_t kContentFilterCapability = 4;

enum class Status : std::uint32_t {
    Ok = 0, InvalidArgument = 1, UnsupportedVersion = 2, NotFound = 3,
    Conflict = 4, BufferTooSmall = 5, Unauthorized = 6, LimitExceeded = 7,
    Unavailable = 8, InternalError = 9
};
enum Hand : std::uint32_t { Left = 1, Right = 2, Both = 3 };
enum Content : std::uint32_t {
    Weapon = 1, Shield = 2, Torch = 4, Potion = 8, Food = 16, Tool = 32, Other = 64,
    AllContent = 127
};
enum ScaleMode : std::uint32_t { AnchorScale = 0 };
constexpr std::uint32_t kEnabled = 1;

// Names/keys are bounded NUL-terminated ASCII. provider:key is the stable identity.
// v1 supports skyrimvr-humanoid-v1, Skyrim engine units, Z up and right-handed axes.
// A spherical access region is explicit; it does not derive from item type/model bounds.
struct Definition {
    std::uint32_t structSize;
    std::uint32_t flags;
    char provider[64];
    char key[64];
    char anchor[96];
    char referenceRig[64];
    char kind[32];
    float position[3];
    float rotation[9]; // row-major orthonormal matrix, determinant +1
    float radius;
    float regionCenter[3]; // slot-local, transformed by position/rotation/anchor scale
    std::uint32_t handMask;
    std::uint32_t contentMask;
    std::uint32_t scaleMode;
    std::uint32_t reserved;
};
struct SlotRecord {
    Definition definition;
    std::uint64_t definitionRevision;
    std::uint64_t slotId; // process-local, monotonically allocated, never a persistent key
};
static_assert(sizeof(Definition) == 408, "Definition ABI changed");
static_assert(sizeof(SlotRecord) == 424, "Snapshot ABI changed");
static_assert(std::is_trivially_copyable<SlotRecord>::value, "Copied ABI only");

struct Api {
    std::uint32_t structSize;
    std::uint32_t version;
    std::uint64_t capabilities;
    Status (BS_CALL *RegisterClient)(const char* provider, std::uint64_t* client);
    Status (BS_CALL *ReleaseClient)(std::uint64_t client);
    // expectedRevision=0 creates only; updates require the exact previous revision.
    Status (BS_CALL *UpsertDefinition)(std::uint64_t client, const Definition*, std::uint64_t expectedRevision, SlotRecord*);
    Status (BS_CALL *RemoveDefinition)(std::uint64_t client, const char* key, std::uint64_t expectedRevision);
    // Atomic copied snapshot. expectedRegistryRevision=0 takes latest. On conflict or
    // insufficient capacity no partial records are written; count/revision identify retry.
    Status (BS_CALL *CopyDefinitions)(std::uint64_t client, std::uint64_t expectedRegistryRevision,
        SlotRecord* records, std::uint32_t capacity, std::uint32_t* count, std::uint64_t* registryRevision);
    // Exactly one physical hand and one content category; no engine/inventory mutation.
    Status (BS_CALL *EvaluateEligibility)(std::uint64_t client, std::uint64_t slotId,
        std::uint64_t expectedRevision, std::uint32_t physicalHand, std::uint32_t contentCategory, std::uint32_t* allowed);
    // Replace this client's authored definitions atomically. Saved-player customization
    // belongs to a separately negotiated table; this operation only changes author defaults.
    Status (BS_CALL *ReplaceDefinitions)(std::uint64_t client, const Definition* definitions,
        std::uint32_t count, std::uint64_t expectedRegistryRevision);
    // Separate read-only lease pool; no namespace/definition ownership. Observers may
    // copy all providers and evaluate categories, but all mutations are Unauthorized.
    Status (BS_CALL *RegisterObserver)(std::uint64_t* client);
};
using QueryInterface = const Api* (BS_CALL *)(std::uint32_t version, std::uint32_t callerSize);
// Optional Windows authoring/package publication export, shared by native fixtures.
// Creates a writer lease for a whole validated provider package. ReleaseClient owns
// its lifetime. A duplicate provider fails; output is untouched on failure.
using LoadPackage = Status (BS_CALL *)(const wchar_t* path, std::uint64_t* client);
} // namespace body_slots

// Export: BodySlots_QueryInterface(version, sizeof(Api)). SKSE and native fixture use
// the same table. Registry contains no engine-bound static initialization.
// Export: BodySlots_LoadPackage(const wchar_t* path, uint64_t* client).
