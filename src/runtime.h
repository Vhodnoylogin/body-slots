#pragma once
#include "body_slots_runtime_api.h"
#include "body_slots_behavior_api.h"
#include "body_slots_content_observation_api.h"
#include "body_slots_regions_api.h"
namespace body_slots::detail {
struct HandProbe {
    std::uint32_t physicalHand, valid;
    std::uint64_t sampleId;
    float world[3];
};
struct AnchorSample {
    std::uint64_t instanceHandle, anchorGeneration;
    std::uint32_t valid, reserved;
    RuntimeTransform world;
};
struct RuntimeFrame {
    std::uint64_t epoch, frame, bodyGeneration;
    double timeSeconds;
    HandProbe hands[2];
    const AnchorSample* anchors;
    std::uint32_t anchorCount;
};
// Adapter calls only; input/grant authority remains HIGGS. All frame observations
// are synchronous, world-space, finite and from one declared update boundary.
Status UpdateRuntime(const RuntimeFrame&) noexcept;
Status CollectOffers(const RuntimeInput&, ActionOffer*, std::uint32_t capacity, std::uint32_t* count) noexcept;
bool RevalidateOfferContent(const ActionOffer&,std::uint64_t currentHandObservationId) noexcept;
const BehaviorApi& GetBehaviorApi() noexcept;
const ContentObservationApi& GetContentObservationApi() noexcept;
Status DispatchSelected(const ActionOffer&, const ActionGrant&, std::uint64_t* requestId) noexcept;
Status CopyActionProgress(ActionProgress*, std::uint32_t capacity, std::uint32_t* count) noexcept;
// Private adapter observations; local callback cancellation does not settle the
// assigned HIGGS action. Keep observing it without changing any public record.
struct PendingHandAction { ActionGrant grant; std::uint64_t executor; };
Status CopyPendingHandActions(PendingHandAction*, std::uint32_t capacity, std::uint32_t* count) noexcept;
using ContentCommitValidator=Status (*)(const ContentCommit&,const ActionRequest&) noexcept;
// Installed by the HIGGS adapter. Missing authority makes receipts unavailable.
// The hook runs outside runtime/registry locks and must validate the assigned
// HIGGS executor's matching confirmed result, or current grant for Accepted.
void SetContentCommitValidator(ContentCommitValidator) noexcept;
// A terminal noncommit is factual HIGGS evidence, never a missing history row or
// a local callback failure. Compensation must be explicitly confirmed upstream.
Status ApplyHandResult(std::uint64_t higgsRequestId,std::uint64_t epoch,CommitStage,
                       bool compensated=false,bool noncommitConfirmed=true) noexcept;
void InvalidateRuntime() noexcept;
// Trusted native-provider adapter only. Scope acceptance, publication and copy
// share the runtime invalidation fence; callers cannot select a save generation.
Status PublishProviderRegions(std::uint64_t writer, const RegionEvaluationFrame&,
    const RegionEvaluation*, std::uint32_t count, RegionObservation*,
    std::uint32_t capacity, RegionsView*) noexcept;
Status ReserveDragSlot(std::uint64_t instanceHandle,std::uint64_t requestId,std::uint64_t epoch,std::uint64_t runtimeRevision) noexcept;
void ReleaseDragSlot(std::uint64_t requestId) noexcept;
Status ReportPresentation(std::uint64_t instanceHandle, std::uint64_t visualRevision,
    std::uint64_t contentVersion, PresentationState) noexcept;
bool ComposeSlotTransform(const Definition&, const RuntimeTransform&, RuntimeTransform&, float center[3], float& radius) noexcept;
bool ContainsPoint(const float center[3], float radius, const float point[3]) noexcept;
const RuntimeApi& GetRuntimeApi() noexcept;
} // namespace body_slots::detail
