// Actual registry/runtime/manipulation cores. No SDK, engine or runtime doubles.
#include "runtime.h"
#include "instances.h"
#include "manipulation.h"
#include "runtime_saved.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace body_slots;
using namespace body_slots::detail;
extern "C" const Api* BS_CALL BodySlots_QueryInterface(std::uint32_t, std::uint32_t) noexcept;
extern "C" const InstanceApi* BS_CALL BodySlots_QueryInstanceInterface(std::uint32_t, std::uint32_t) noexcept;
extern "C" const ConfigurationApi* BS_CALL BodySlots_QueryConfigurationInterface(std::uint32_t, std::uint32_t) noexcept;
extern "C" const ContentObservationApi* BS_CALL BodySlots_QueryContentObservationInterface(std::uint32_t, std::uint32_t) noexcept;

namespace {
std::atomic<unsigned> checks{0};
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #c); } while (false)
static_assert(sizeof(ContentObservation) == 56 && sizeof(ContentObservationApi) == 24, "new copied ABI");
static_assert(sizeof(Api) == 80 && sizeof(InstanceApi) == 48 && sizeof(RuntimeApi) == 104, "old tables stay unchanged");
static_assert(std::is_standard_layout<ContentObservation>::value && std::is_trivially_copyable<ContentObservationApi>::value, "C++17 copied POD");
constexpr std::uint64_t kUntouched = 0xfeedbeef;
const RuntimeApi& runtime = GetRuntimeApi();
const ContentObservationApi& projection = GetContentObservationApi();

RuntimeTransform Identity() {
    RuntimeTransform result{}; result.scale = 1; result.rotation[0] = result.rotation[4] = result.rotation[8] = 1; return result;
}
struct Snapshot {
    std::vector<RuntimeRecord> rows = std::vector<RuntimeRecord>(kMaxInstances);
    RuntimeView view{sizeof(RuntimeView), kRuntimeApiVersion};
};
struct Fixture {
    const Api* base = BodySlots_QueryInterface(1, sizeof(Api));
    const InstanceApi* instances = BodySlots_QueryInstanceInterface(1, sizeof(InstanceApi));
    const ConfigurationApi* configuration = BodySlots_QueryConfigurationInterface(1, sizeof(ConfigurationApi));
    std::uint64_t owner = 0, templateOwner = 0, other = 0, reader = 0, customizer = 0;
    std::uint64_t handle = 0, epoch = 71, frame = 0, behavior = 0, executeCalls = 0;
    bool failExecution = false;
    InstanceSpec spec{};
    ActionRequest last{};

    Snapshot Copy() const {
        Snapshot result;
        CHECK(runtime.CopyRuntime(reader, result.rows.data(), kMaxInstances, &result.view) == Status::Ok);
        return result;
    }
    RuntimeRecord Row() const {
        const auto snapshot = Copy();
        for (std::uint32_t i = 0; i < snapshot.view.count; ++i)
            if (snapshot.rows[i].configured.instance.instanceHandle == handle) return snapshot.rows[i];
        throw std::runtime_error("fixture instance missing");
    }
    ContentObservation Observation(std::uint64_t identity, std::uint64_t quantity) const {
        const auto snapshot = Copy();
        return {sizeof(ContentObservation), 0, epoch, snapshot.view.saveGeneration, handle,
                Row().contentVersion, identity, quantity};
    }
    void Frame() {
        AnchorSample anchor{handle, 1, 1, 0, Identity()};
        RuntimeFrame update{}; update.epoch = epoch; update.frame = ++frame;
        update.bodyGeneration = 1; update.timeSeconds = double(frame);
        update.hands[0] = {Left, 1, frame, {0, 0, 0}};
        update.hands[1] = {Right, 1, frame, {0, 0, 0}};
        update.anchors = &anchor; update.anchorCount = 1;
        CHECK(UpdateRuntime(update) == Status::Ok);
    }
    void Create() {
        const auto snapshot = Copy();
        InstanceRecord created{}; created.structSize = sizeof(created);
        CHECK(instances->UpsertInstance(owner, &spec, snapshot.view.saveGeneration, 0, &created) == Status::Ok);
        handle = created.instanceHandle;
    }
    void Start() {
        CHECK(base && instances && configuration);
        CHECK(base->RegisterClient("projection-owner", &owner) == Status::Ok);
        CHECK(base->RegisterClient("projection-template", &templateOwner) == Status::Ok);
        CHECK(base->RegisterClient("projection-other", &other) == Status::Ok);
        CHECK(base->RegisterObserver(&reader) == Status::Ok);
        CHECK(configuration->RegisterCustomizer("projection-editor", &customizer) == Status::Ok);
        Definition definition{}; definition.structSize = sizeof(definition); definition.flags = kEnabled;
        std::strcpy(definition.provider, "projection-template"); std::strcpy(definition.key, "opening");
        std::strcpy(definition.anchor, "Pelvis"); std::strcpy(definition.referenceRig, "skyrimvr-humanoid-v1");
        std::strcpy(definition.kind, "pouch"); definition.rotation[0] = definition.rotation[4] = definition.rotation[8] = 1;
        definition.radius = 3; definition.handMask = Both; definition.contentMask = Potion;
        SlotRecord authored{}; CHECK(base->UpsertDefinition(templateOwner, &definition, 0, &authored) == Status::Ok);
        spec.structSize = sizeof(spec); spec.flags = kInstanceActive;
        std::strcpy(spec.equipmentKey, "provider-bag"); std::strcpy(spec.slotKey, "opening");
        std::strcpy(spec.definitionProvider, "projection-template"); std::strcpy(spec.definitionKey, "opening");
        spec.definitionRevision = authored.definitionRevision; spec.binding = 1;
        Create();
    }
    void RegisterBehavior() {
        BehaviorSpec record{}; record.structSize = sizeof(record); record.instanceHandle = handle;
        record.context = this; record.higgsClient = 123; record.higgsExecutor = 456;
        record.offers = [](void*, const ActionContext*, ActionProposal* out, std::uint32_t capacity, std::uint32_t* count) {
            if (!capacity) return Status::BufferTooSmall;
            out[0] = {1, 1, 5, 0, 17}; *count = 1; return Status::Ok;
        };
        record.execute = [](void* context, const ActionRequest* request) {
            auto& self = *static_cast<Fixture*>(context); self.last = *request; ++self.executeCalls;
            return self.failExecution ? Status::Unavailable : Status::Ok;
        };
        CHECK(runtime.RegisterBehavior(owner, &record, &behavior) == Status::Ok);
    }
    std::vector<ActionOffer> Offers(std::uint64_t sequence) const {
        RuntimeInput input{}; input.structSize = sizeof(input); input.physicalHand = Left;
        input.contentCategory = Potion; input.available = 1; input.button = input.edge = 1;
        input.epoch = epoch; input.frame = frame; input.inputSequence = sequence; input.poseSample = frame;
        input.handStateRevision = 8;
        std::vector<ActionOffer> offers(kMaxActionOffers); std::uint32_t count = 0;
        CHECK(CollectOffers(input, offers.data(), kMaxActionOffers, &count) == Status::Ok);
        offers.resize(count); return offers;
    }
    ActionGrant Grant(const ActionOffer& offer) const {
        return {epoch, 1000 + offer.context.input.inputSequence, 2000 + offer.context.input.inputSequence,
                offer.offerId, offer.policyRevision, offer.context.input.inputSequence, Left, 0};
    }
    void Publish(std::uint64_t identity, std::uint64_t quantity, std::uint64_t expectedVersion) const {
        auto observation = Observation(identity, quantity); std::uint64_t result = kUntouched;
        CHECK(projection.Publish(owner, &observation, &result) == Status::Ok && result == expectedVersion);
    }
    void Close() {
        CHECK(configuration->ReleaseCustomizer(customizer) == Status::Ok);
        CHECK(base->ReleaseClient(owner) == Status::Ok);
        CHECK(base->ReleaseClient(templateOwner) == Status::Ok);
        CHECK(base->ReleaseClient(other) == Status::Ok);
        CHECK(base->ReleaseClient(reader) == Status::Ok);
    }
};

void CheckRejected(Fixture& fixture, const ContentObservation& observation, std::uint64_t writer, Status expected) {
    const auto before = fixture.Row(); std::uint64_t result = kUntouched;
    CHECK(projection.Publish(writer, &observation, &result) == expected && result == kUntouched);
    const auto after = fixture.Row();
    CHECK(after.contentVersion == before.contentVersion && after.contentIdentity == before.contentIdentity &&
          after.quantity == before.quantity && after.runtimeRevision == before.runtimeRevision);
}

struct Notifications {
    Fixture* fixture = nullptr;
    std::uint64_t subscription = 0, calls = 0;
    Status reentrantRead = Status::InternalError, reentrantNoop = Status::InternalError, selfRemoval = Status::InternalError;
    std::uint64_t observedVersion = 0;
};
void BS_CALL OnContent(void* context, const RuntimeEvent* event) {
    if (event->kind != static_cast<std::uint32_t>(RuntimeEventKind::ContentChanged)) return;
    auto& state = *static_cast<Notifications*>(context); ++state.calls;
    Snapshot snapshot;
    state.reentrantRead = runtime.CopyRuntime(state.fixture->reader, snapshot.rows.data(), kMaxInstances, &snapshot.view);
    const auto& row = snapshot.rows[0];
    ContentObservation current{sizeof(ContentObservation), 0, snapshot.view.epoch, snapshot.view.saveGeneration,
                               row.configured.instance.instanceHandle, row.contentVersion, row.contentIdentity, row.quantity};
    state.reentrantNoop = projection.Publish(state.fixture->owner, &current, &state.observedVersion);
    state.selfRemoval = runtime.Unsubscribe(state.fixture->reader, state.subscription);
}
struct BlockingNotification {
    std::atomic<bool> entered{false}, unblock{false};
};
void BS_CALL BlockContent(void* context, const RuntimeEvent* event) {
    if (event->kind != static_cast<std::uint32_t>(RuntimeEventKind::ContentChanged)) return;
    auto& state = *static_cast<BlockingNotification*>(context); state.entered = true;
    while (!state.unblock) std::this_thread::yield();
}

void ValidationAndInitialProjection(Fixture& fixture) {
    CHECK(BodySlots_QueryContentObservationInterface(1, 24) == &projection);
    CHECK(!BodySlots_QueryContentObservationInterface(0, 24) && !BodySlots_QueryContentObservationInterface(2, 24));
    CHECK(!BodySlots_QueryContentObservationInterface(1, 23) && !BodySlots_QueryContentObservationInterface(1, 25));
    CHECK(projection.structSize == 24 && projection.version == 1 && projection.capabilities == 1 && projection.Publish);
    auto observation = fixture.Observation(0x1234, 5); std::uint64_t result = kUntouched;
    CHECK(projection.Publish(fixture.owner, nullptr, &result) == Status::InvalidArgument && result == kUntouched);
    CHECK(projection.Publish(fixture.owner, &observation, nullptr) == Status::InvalidArgument);
    CheckRejected(fixture, observation, fixture.owner, Status::Conflict); // No observed runtime epoch yet.
    fixture.Frame();
    CheckRejected(fixture, observation, fixture.templateOwner, Status::Unauthorized); // Template authors do not own instances.
    CheckRejected(fixture, observation, fixture.other, Status::Unauthorized);
    CheckRejected(fixture, observation, fixture.reader, Status::Unauthorized);
    CheckRejected(fixture, observation, fixture.customizer, Status::Unauthorized);
    CheckRejected(fixture, observation, 0, Status::Unauthorized);
    for (unsigned field = 0; field < 8; ++field) {
        auto invalid = observation;
        switch (field) {
        case 0: --invalid.structSize; break;
        case 1: invalid.reserved = 1; break;
        case 2: invalid.epoch = 0; break;
        case 3: invalid.saveGeneration = 0; break;
        case 4: invalid.instanceHandle = 0; break;
        case 5: invalid.contentIdentity = 0; break;
        case 6: invalid.quantity = 0; break;
        case 7: ++invalid.structSize; break;
        }
        CheckRejected(fixture, invalid, fixture.owner, Status::InvalidArgument);
    }
    auto stale = observation; ++stale.epoch; CheckRejected(fixture, stale, fixture.owner, Status::Conflict);
    stale = observation; ++stale.saveGeneration; CheckRejected(fixture, stale, fixture.owner, Status::Conflict);
    stale = observation; ++stale.expectedContentVersion; CheckRejected(fixture, stale, fixture.owner, Status::Conflict);
    stale = observation; stale.instanceHandle ^= 0x100000; CheckRejected(fixture, stale, fixture.owner, Status::NotFound);

    // No content-commit validator or action exists: initial provider stock can drive a preview.
    SetContentCommitValidator(nullptr);
    CHECK(projection.Publish(fixture.owner, &observation, &result) == Status::Ok && result == 1);
    auto row = fixture.Row();
    CHECK(row.contentVersion == 1 && row.contentIdentity == 0x1234 && row.quantity == 5);
    CHECK(row.presentationState == static_cast<std::uint32_t>(PresentationState::Pending) && row.displayedContentVersion == 0);
    std::uint32_t count = 99;
    CHECK(CopyActionProgress(nullptr, 0, &count) == Status::Ok && count == 0 && fixture.executeCalls == 0);
    SlotVisualSpec preview{}; preview.structSize = sizeof(preview); preview.contentVersion = row.contentVersion;
    std::strcpy(preview.previewModel, "body-slots/projected-preview.nif");
    CHECK(runtime.SetVisual(fixture.owner, fixture.handle, &preview) == Status::Ok);
    row = fixture.Row(); CHECK(ReportPresentation(fixture.handle, row.visualRevision, 1, PresentationState::Ready) == Status::Ok);
    CHECK(fixture.Row().displayedContentVersion == 1);
    CheckRejected(fixture, observation, fixture.owner, Status::Conflict); // A delayed initial value must not be rebased.
}

void VersionsOffersAndNotifications(Fixture& fixture) {
    fixture.Frame(); fixture.RegisterBehavior();
    Notifications notifications; notifications.fixture = &fixture;
    CHECK(runtime.Subscribe(fixture.reader, OnContent, &notifications, &notifications.subscription) == Status::Ok);
    auto offers = fixture.Offers(1); CHECK(offers.size() == 1);
    const auto ready = fixture.Row(); const auto revision = fixture.Copy().view.runtimeRevision;
    fixture.Publish(0x1234, 5, 1);
    const auto noop = fixture.Row();
    CHECK(noop.runtimeRevision == ready.runtimeRevision && fixture.Copy().view.runtimeRevision == revision &&
          noop.presentationState == ready.presentationState && noop.displayedContentVersion == 1 && notifications.calls == 0);
    std::uint64_t request = kUntouched;
    CHECK(DispatchSelected(offers[0], fixture.Grant(offers[0]), &request) == Status::Ok && fixture.executeCalls == 1);
    CHECK(ApplyHandResult(fixture.last.grant.requestId, fixture.epoch, CommitStage::Cancelled) == Status::Ok);

    offers = fixture.Offers(2); CHECK(offers.size() == 1); const auto old = fixture.Row();
    const auto oldViewRevision = fixture.Copy().view.viewRevision;
    std::vector<std::uint8_t> beforeDefinitions, beforeInstances, beforeRuntime;
    bool hadInstances = false, hadRuntime = false;
    CHECK(SaveConfigurationBatch(beforeDefinitions, beforeInstances, hadInstances, beforeRuntime, hadRuntime) == Status::Ok);
    fixture.Publish(0x1234, 4, 2); // Quantity alone advances the version.
    CHECK(notifications.calls == 1 && notifications.reentrantRead == Status::Ok && notifications.reentrantNoop == Status::Ok &&
          notifications.observedVersion == 2 && notifications.selfRemoval == Status::Conflict);
    CHECK(fixture.Row().presentationState == static_cast<std::uint32_t>(PresentationState::Pending) && fixture.Row().displayedContentVersion == 0);
    CHECK(ReportPresentation(fixture.handle, old.visualRevision, old.contentVersion, PresentationState::Ready) == Status::Conflict);
    CHECK(runtime.SetVisual(fixture.owner, fixture.handle, &old.visual) == Status::Conflict);
    CHECK(fixture.Copy().view.viewRevision == oldViewRevision);
    std::vector<std::uint8_t> afterDefinitions, afterInstances, afterRuntime;
    bool hasInstances = false, hasRuntime = false;
    CHECK(SaveConfigurationBatch(afterDefinitions, afterInstances, hasInstances, afterRuntime, hasRuntime) == Status::Ok &&
          beforeDefinitions == afterDefinitions && beforeInstances == afterInstances && beforeRuntime == afterRuntime &&
          hadInstances == hasInstances && hadRuntime == hasRuntime); // No assignment or stock enters saved configuration.
    request = kUntouched;
    CHECK(DispatchSelected(offers[0], fixture.Grant(offers[0]), &request) == Status::Conflict && request == kUntouched && fixture.executeCalls == 1);
    auto stale = fixture.Observation(0x1234, 4); --stale.expectedContentVersion;
    CheckRejected(fixture, stale, fixture.owner, Status::Conflict); // Even equal values require the current CAS.
    CHECK(runtime.Unsubscribe(fixture.reader, notifications.subscription) == Status::Ok);
    fixture.Publish(0x5678, 4, 3); CHECK(notifications.calls == 1); // Identity alone also advances.
    fixture.Publish(0, 0, 4); CHECK(fixture.Row().contentIdentity == 0 && fixture.Row().quantity == 0);
    fixture.Publish(0, 0, 4);

    // Removing an observer drains an in-flight content event without holding runtime locks.
    BlockingNotification blocked; std::uint64_t subscription = 0;
    CHECK(runtime.Subscribe(fixture.reader, BlockContent, &blocked, &subscription) == Status::Ok);
    auto observation = fixture.Observation(0x99, 2); std::uint64_t result = kUntouched;
    Status published = Status::InternalError, removed = Status::InternalError;
    std::atomic<bool> publishDone{false}, removeStarted{false}, removeDone{false};
    std::thread publisher([&] { published = projection.Publish(fixture.owner, &observation, &result); publishDone = true; });
    while (!blocked.entered && !publishDone) std::this_thread::yield();
    if (!blocked.entered) { publisher.join(); CHECK(blocked.entered); }
    std::thread remover([&] { removeStarted = true; removed = runtime.Unsubscribe(fixture.reader, subscription); removeDone = true; });
    while (!removeStarted) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    const bool drainedTooSoon = removeDone; const auto whileBlocked = fixture.Row();
    blocked.unblock = true; publisher.join(); remover.join();
    CHECK(!drainedTooSoon && published == Status::Ok && result == 5 && removed == Status::Ok && whileBlocked.contentVersion == 5);
    CHECK(runtime.Unsubscribe(fixture.reader, subscription) == Status::NotFound);
}

void PendingRecoveryAndDrag(Fixture& fixture) {
    auto offers = fixture.Offers(3); CHECK(offers.size() == 1); std::uint64_t request = 0;
    CHECK(DispatchSelected(offers[0], fixture.Grant(offers[0]), &request) == Status::Ok);
    auto changed = fixture.Observation(0x88, 3);
    CheckRejected(fixture, changed, fixture.owner, Status::Conflict);
    fixture.Publish(0x99, 2, 5); // Current-value no-op remains legal during an action.
    ContentCommit unsupported{}; unsupported.structSize = sizeof(unsupported); unsupported.stage = 2;
    unsupported.requestId = request; unsupported.instanceHandle = fixture.handle;
    CHECK(runtime.ReportContentCommit(fixture.owner, &unsupported) == Status::Unavailable); // Projection installed no receipt authority.
    CHECK(ApplyHandResult(fixture.last.grant.requestId, fixture.epoch, CommitStage::Cancelled) == Status::Ok);
    fixture.Publish(0x88, 3, 6); // A confirmed noncommit releases the reservation.

    offers = fixture.Offers(4); CHECK(offers.size() == 1);
    CHECK(DispatchSelected(offers[0], fixture.Grant(offers[0]), &request) == Status::Ok);
    CHECK(ApplyHandResult(fixture.last.grant.requestId, fixture.epoch, CommitStage::Committed) == Status::Ok);
    CHECK(runtime.RemoveBehavior(fixture.owner, fixture.behavior) == Status::Ok);
    ActionProgress recovery{};
    CHECK(runtime.CopyActionStatus(fixture.owner, request, &recovery) == Status::Ok && recovery.handCommitted && !recovery.committed &&
          recovery.stage == static_cast<std::uint32_t>(CommitStage::Cancelled));
    changed = fixture.Observation(0x77, 4);
    CheckRejected(fixture, changed, fixture.owner, Status::Conflict); // Terminal local stage cannot hide unresolved HIGGS commitment.
    fixture.Publish(0x88, 3, 6);
    ++fixture.epoch; fixture.Frame();
    CheckRejected(fixture, changed, fixture.owner, Status::Conflict); // A delayed prior-epoch result stays stale.
    fixture.Publish(0x77, 4, 7);

    SlotEditingSnapshot mode{}; SlotEditingMode enabled{sizeof(SlotEditingMode), 1, Both, 1};
    CHECK(GetManipulationApi().SetEditingMode(fixture.customizer, &enabled, 1, &mode) == Status::Ok);
    const auto snapshot = fixture.Copy(); const auto row = fixture.Row();
    DragStart drag{9000, fixture.epoch, fixture.frame, mode.revision, snapshot.view.saveGeneration, snapshot.view.viewRevision,
                   row.bodyGeneration, row.anchorGeneration, Left, 0, row, Identity(), Identity()};
    CHECK(BeginAuthorizedDrag(drag) == Status::Ok && IsSlotBeingDragged(fixture.handle));
    changed = fixture.Observation(0x66, 5); CheckRejected(fixture, changed, fixture.owner, Status::Conflict);
    fixture.Publish(0x77, 4, 7);
    CHECK(FinishAuthorizedDrag(drag.requestId, false) == Status::Ok && !IsSlotBeingDragged(fixture.handle));
    fixture.Publish(0x66, 5, 8);
}

void InstanceAndSaveLifetime(Fixture& fixture) {
    const auto oldObservation = fixture.Observation(0x55, 1); const auto oldHandle = fixture.handle;
    const auto old = fixture.Row().configured.instance; const auto snapshot = fixture.Copy();
    CHECK(fixture.instances->RemoveInstance(fixture.owner, oldHandle, snapshot.view.saveGeneration, old.instanceRevision) == Status::Ok);
    fixture.Create(); fixture.Frame();
    CHECK(fixture.handle != oldHandle && fixture.Row().contentVersion == 0 && fixture.Row().contentIdentity == 0 && fixture.Row().quantity == 0);
    CheckRejected(fixture, oldObservation, fixture.owner, Status::NotFound);
    fixture.Publish(0x55, 1, 1);
    const auto priorSave = fixture.Observation(0x44, 1);
    std::vector<std::uint8_t> definitions, instances, runtimeBytes; bool hasInstances = false, hasRuntime = false;
    CHECK(SaveConfigurationBatch(definitions, instances, hasInstances, runtimeBytes, hasRuntime) == Status::Ok && hasRuntime);
    InstanceView reset{}; CHECK(ResetSaveScope(&reset) == Status::Ok);
    CHECK(fixture.Copy().view.count == 0);
    CHECK(LoadConfigurationBatch(&definitions, &instances, &runtimeBytes, reset.saveGeneration, reset.viewRevision) == Status::Ok);
    fixture.Create(); fixture.Frame();
    CHECK(fixture.Row().contentVersion == 0 && fixture.Row().contentIdentity == 0 && fixture.Row().quantity == 0);
    CHECK(!std::strcmp(fixture.Row().visual.previewModel, "body-slots/projected-preview.nif")); // Visual configuration survives, content does not.
    CheckRejected(fixture, priorSave, fixture.owner, Status::NotFound);
    auto wrongSave = fixture.Observation(0x44, 1); wrongSave.saveGeneration = priorSave.saveGeneration;
    CheckRejected(fixture, wrongSave, fixture.owner, Status::Conflict);
    fixture.Publish(0x44, 1, 1);
    auto revoked = fixture.Observation(0x33, 1); std::uint64_t released = 0;
    CHECK(fixture.base->RegisterClient("projection-revoked", &released) == Status::Ok);
    CHECK(fixture.base->ReleaseClient(released) == Status::Ok);
    CheckRejected(fixture, revoked, released, Status::Unauthorized);
}
Status AllowReservationReceipt(const ContentCommit&,const ActionRequest&) noexcept { return Status::Ok; }
ContentCommit ReservationReceipt(const ActionRequest& request,CommitStage stage) {
    const auto& row=request.offer.context.slot;
    return {sizeof(ContentCommit),static_cast<std::uint32_t>(stage),request.requestId,
        request.grant.epoch,request.grant.requestId,request.grant.grantId,
        row.configured.instance.instanceHandle,row.contentVersion,row.contentVersion+1,0x9876,2};
}
void UnsettledReservations(Fixture& fixture) {
    // A callback error and generic HIGGS cancellation are both compatible with
    // an executor whose mutation outcome has not arrived yet.
    ++fixture.epoch;fixture.Frame();fixture.RegisterBehavior();fixture.failExecution=true;
    const auto first=fixture.Offers(1001),cached=fixture.Offers(1002);
    CHECK(first.size()==1&&cached.size()==1);
    std::uint64_t request=0;
    CHECK(DispatchSelected(first[0],fixture.Grant(first[0]),&request)==Status::Unavailable);
    const auto hand=fixture.last.grant.requestId;
    fixture.failExecution=false;
    CHECK(fixture.Offers(1003).empty());
    std::uint64_t untouched=kUntouched;
    CHECK(DispatchSelected(cached[0],fixture.Grant(cached[0]),&untouched)==Status::Conflict&&untouched==kUntouched);
    CheckRejected(fixture,fixture.Observation(0xaaaa,3),fixture.owner,Status::Conflict);
    const auto row=fixture.Row();
    CHECK(ReserveDragSlot(fixture.handle,3001,fixture.epoch,row.runtimeRevision)==Status::Conflict);
    std::array<PendingHandAction,kMaxPendingActions> awaiting{};std::uint32_t count=0;
    CHECK(CopyPendingHandActions(awaiting.data(),awaiting.size(),&count)==Status::Ok&&count==1&&
          awaiting[0].grant.requestId==hand&&awaiting[0].executor==456);
    CHECK(ApplyHandResult(hand,fixture.epoch,CommitStage::Cancelled,false,false)==Status::Ok);
    CheckRejected(fixture,fixture.Observation(0xaaaa,3),fixture.owner,Status::Conflict);
    CHECK(CopyPendingHandActions(awaiting.data(),awaiting.size(),&count)==Status::Ok&&count==1);
    CHECK(ApplyHandResult(hand,fixture.epoch,CommitStage::Committed)==Status::Ok);
    ActionProgress progress{};
    CHECK(runtime.CopyActionStatus(fixture.owner,request,&progress)==Status::Ok&&progress.handCommitted&&!progress.committed);
    CHECK(ApplyHandResult(hand,fixture.epoch,CommitStage::Cancelled)==Status::Ok);
    CheckRejected(fixture,fixture.Observation(0xaaaa,3),fixture.owner,Status::Conflict);
    CHECK(ApplyHandResult(hand,fixture.epoch,CommitStage::Cancelled,true,false)==Status::Ok);
    CHECK(ApplyHandResult(hand,fixture.epoch,CommitStage::Committed)==Status::Ok); // Delayed pre-compensation copy cannot reopen it.
    CHECK(runtime.CopyActionStatus(fixture.owner,request,&progress)==Status::Ok&&progress.handCommitted);
    fixture.Publish(0xaaaa,3,row.contentVersion+1);
    CHECK(CopyPendingHandActions(awaiting.data(),awaiting.size(),&count)==Status::Ok&&!count);
    CHECK(runtime.RemoveBehavior(fixture.owner,fixture.behavior)==Status::Ok);

    // A validated late provider mirror may settle a locally cancelled action.
    // It does not invoke the removed executor again.
    ++fixture.epoch;fixture.Frame();fixture.RegisterBehavior();
    auto offers=fixture.Offers(2001);CHECK(offers.size()==1);
    CHECK(DispatchSelected(offers[0],fixture.Grant(offers[0]),&request)==Status::Ok);
    const auto late=fixture.last;const auto calls=fixture.executeCalls;
    CHECK(runtime.RemoveBehavior(fixture.owner,fixture.behavior)==Status::Ok);
    CheckRejected(fixture,fixture.Observation(0xbbbb,4),fixture.owner,Status::Conflict);
    CHECK(ApplyHandResult(late.grant.requestId,fixture.epoch,CommitStage::Committed)==Status::Ok);
    CHECK(runtime.CopyActionStatus(fixture.owner,request,&progress)==Status::Ok&&progress.handCommitted);
    SetContentCommitValidator(AllowReservationReceipt);
    auto receipt=ReservationReceipt(late,CommitStage::Committed);
    CHECK(runtime.ReportContentCommit(fixture.owner,&receipt)==Status::Ok);
    receipt.stage=static_cast<std::uint32_t>(CommitStage::Completed);
    CHECK(runtime.ReportContentCommit(fixture.owner,&receipt)==Status::Ok&&fixture.executeCalls==calls);
    fixture.Publish(0xbbbb,4,receipt.contentVersion+1);
    CHECK(CopyPendingHandActions(awaiting.data(),awaiting.size(),&count)==Status::Ok&&!count);
    SetContentCommitValidator(nullptr);

    // Terminal history pressure on a different slot cannot evict a committed
    // action still awaiting its logical content mirror.
    ++fixture.epoch;fixture.Frame();fixture.RegisterBehavior();
    offers=fixture.Offers(3001);CHECK(offers.size()==1);
    CHECK(DispatchSelected(offers[0],fixture.Grant(offers[0]),&request)==Status::Ok);
    const auto unresolved=fixture.last;
    CHECK(ApplyHandResult(unresolved.grant.requestId,fixture.epoch,CommitStage::Committed)==Status::Ok);
    CHECK(runtime.RemoveBehavior(fixture.owner,fixture.behavior)==Status::Ok);
    const auto target=fixture.handle;const auto originalSpec=fixture.spec;
    std::strcpy(fixture.spec.equipmentKey,"projection-pressure");fixture.Create();fixture.Frame();fixture.RegisterBehavior();
    for(std::uint64_t i=0;i<kMaxPendingActions+2;++i){
        offers=fixture.Offers(4000+i);CHECK(offers.size()==1);std::uint64_t other=0;
        CHECK(DispatchSelected(offers[0],fixture.Grant(offers[0]),&other)==Status::Ok);
        CHECK(ApplyHandResult(fixture.last.grant.requestId,fixture.epoch,CommitStage::Cancelled)==Status::Ok);
    }
    CHECK(runtime.CopyActionStatus(fixture.owner,request,&progress)==Status::Ok&&progress.handCommitted&&!progress.committed);
    CHECK(CopyPendingHandActions(awaiting.data(),awaiting.size(),&count)==Status::Ok&&count==1&&
          awaiting[0].grant.requestId==unresolved.grant.requestId);
    CHECK(runtime.RemoveBehavior(fixture.owner,fixture.behavior)==Status::Ok);
    fixture.handle=target;fixture.spec=originalSpec;fixture.Frame();fixture.RegisterBehavior();
    CHECK(fixture.Offers(5001).empty());
    CheckRejected(fixture,fixture.Observation(0xcccc,5),fixture.owner,Status::Conflict);
    CHECK(ApplyHandResult(unresolved.grant.requestId,fixture.epoch,CommitStage::Cancelled,true,false)==Status::Ok);
    CHECK(fixture.Offers(5002).size()==1);
    fixture.Publish(0xcccc,5,fixture.Row().contentVersion+1);
    CHECK(runtime.RemoveBehavior(fixture.owner,fixture.behavior)==Status::Ok);
}
} // namespace

int main() {
    try {
        Fixture fixture; fixture.Start();
        ValidationAndInitialProjection(fixture);
        VersionsOffersAndNotifications(fixture);
        PendingRecoveryAndDrag(fixture);
        UnsettledReservations(fixture);
        InstanceAndSaveLifetime(fixture);
        fixture.Close();
        std::cout << "content observation: " << checks << " checks passed; actual production core, no inventory mutation or receipt validator\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
