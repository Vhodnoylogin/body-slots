#pragma once
// Additive, copied-POD logical interaction geometry. No engine objects or grants.
#include "body_slots_zone_api.h"

namespace body_slots {
constexpr std::uint32_t kRegionsApiVersion = 1;
constexpr std::uint32_t kMaxInteractionRegions = 128, kMaxRegionObservations = 256;
enum class RegionAnchorKind : std::uint32_t { Player = 1, Npc = 2, World = 3, Transient = 4 };
enum class RegionUnits : std::uint32_t { SkyrimWorld = 1 };
enum class RegionSpatialState : std::uint32_t { Unavailable = 0, Outside = 1, Inside = 2, Dwelling = 3, Ready = 4 };
enum class RegionUnavailableReason : std::uint32_t {
    None = 0, Missing = 1, InvalidSnapshot = 2, ScopeMismatch = 3,
    Incoherent = 4, Stale = 5, RepeatedSample = 6
};
struct InteractionRegionSpec {
    std::uint32_t structSize, anchorKind;
    char provider[64], key[64]; // stable logical identity, owned by the writer lease
    ZoneShape shape; // region-local units, including Cylinder=4 (flat endcaps)
    float enterInset, exitOutset; // nonnegative local distance; closed thresholds
    double dwellSeconds, maxSampleAgeSeconds, maxSampleSkewSeconds, maxContinuityGapSeconds;
};
struct InteractionRegionRecord {
    InteractionRegionSpec spec;
    std::uint64_t regionId, revision; // transient process ids, never persistent handles
};
// Declared source provenance is copied evidence, not authentication of an external
// service. identity names the target/probe within provider/source. generation is
// changed on replacement/rebinding; sampleId advances for every new sample.
struct RegionSampleSource {
    char provider[64], source[64];
    std::uint64_t identity, generation, sampleId, epoch, saveGeneration, frame;
    double timeSeconds; // same supplied monotonic clock as RegionEvaluationFrame
    std::uint32_t units, reserved;
};
struct RegionAnchorSnapshot {
    std::uint32_t structSize, kind, valid, reserved;
    RegionSampleSource source;
    RuntimeTransform world; // row-major proper rigid rotation, positive uniform scale
};
struct RegionPointSnapshot {
    std::uint32_t structSize, valid, reserved[2];
    RegionSampleSource source;
    float world[3];
    std::uint32_t reservedTail;
};
struct RegionEvaluation {
    std::uint32_t structSize, reserved;
    std::uint64_t regionId, expectedRegionRevision;
    RegionAnchorSnapshot anchor;
    RegionPointSnapshot probe;
};
struct RegionEvaluationFrame {
    std::uint32_t structSize, reserved;
    std::uint64_t epoch, saveGeneration, frame;
    double timeSeconds;
};
struct RegionObservation {
    std::uint64_t regionId, regionRevision, sequence;
    RegionEvaluationFrame evaluated;
    RegionAnchorSnapshot anchor;
    RegionPointSnapshot probe;
    std::uint32_t state, unavailableReason;
    double enteredAtSeconds, dwellElapsedSeconds;
};
struct RegionsView {
    std::uint32_t structSize, version;
    std::uint64_t epoch, saveGeneration, revision, sequence;
    std::uint32_t count, reserved;
};
struct RegionsApi {
    std::uint32_t structSize, version;
    std::uint64_t capabilities; // bit0: owned primitives, bit1: copied synchronous point observations
    Status (BS_CALL *UpsertRegion)(std::uint64_t writer, const InteractionRegionSpec*,
        std::uint64_t expectedRevision, InteractionRegionRecord*);
    Status (BS_CALL *RemoveRegion)(std::uint64_t writer, std::uint64_t regionId, std::uint64_t expectedRevision);
    Status (BS_CALL *CopyRegions)(std::uint64_t reader, std::uint64_t expectedViewRevision,
        InteractionRegionRecord*, std::uint32_t capacity, RegionsView*);
    // Complete per-writer set. Omitted pairs are removed and their dwell is lost.
    // Exact region revisions; all snapshots copied during this call. count=0 clears
    // this writer's observations. Invalid/missing/stale samples yield Unavailable.
    Status (BS_CALL *Publish)(std::uint64_t writer, const RegionEvaluationFrame*,
        const RegionEvaluation*, std::uint32_t count, RegionsView*);
    // Last evaluated evidence only: does not advance time or promise present validity.
    Status (BS_CALL *CopyObservations)(std::uint64_t reader, std::uint64_t expectedViewRevision,
        RegionObservation*, std::uint32_t capacity, RegionsView*);
};
using QueryRegionsInterface = const RegionsApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
static_assert(sizeof(InteractionRegionSpec) == 232, "region spec ABI");
static_assert(sizeof(InteractionRegionRecord) == 248, "region record ABI");
static_assert(sizeof(RegionSampleSource) == 192, "source ABI");
static_assert(sizeof(RegionAnchorSnapshot) == 264, "anchor ABI");
static_assert(sizeof(RegionPointSnapshot) == 224, "point ABI");
static_assert(sizeof(RegionEvaluation) == 512, "evaluation ABI");
static_assert(sizeof(RegionEvaluationFrame) == 40, "frame ABI");
static_assert(sizeof(RegionObservation) == 576, "observation ABI");
static_assert(sizeof(RegionsView) == 48 && sizeof(RegionsApi) == 56, "regions table ABI (x64)");
static_assert(std::is_standard_layout<RegionObservation>::value && std::is_trivially_copyable<RegionObservation>::value, "copied ABI only");
} // namespace body_slots
// Export: BodySlots_QueryRegionsInterface(version, sizeof(RegionsApi)).
