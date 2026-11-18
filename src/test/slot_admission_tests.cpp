// The production admission rules with the actual HIGGS shared-window broker.
// Scene-node observations are not simulated here; SKSE/gameplay is separate.
#include "interaction_validation.h"
#include "hand_broker.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

using namespace body_slots;
using namespace body_slots::detail;
namespace {
unsigned checks{};
#define CHECK(expr) do { ++checks; if (!(expr)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); std::abort(); } } while (false)
struct SharedSlots {
    HiggsHands::Broker broker;
    std::uint64_t recognizer{}, provider{}, executor{}, registration{}, epoch{}, input{}, createdAt{};
    std::array<InstanceKey, 2> keys{};
    std::array<std::uint64_t, 2> requests{};
    HIGGS_ActionRequest selected{};
    unsigned notifications{};
    bool reverse{}, ordered{};

    SharedSlots(bool reversed, bool withTarget) : reverse(reversed), ordered(withTarget) {
        HIGGS_HandClientInfo info{sizeof(info)};
        std::strcpy(info.name, "slot recognizer");
        CHECK(broker.Register(info, recognizer) == HIGGS_HAND_OK);
        std::strcpy(info.name, "one slot provider");
        CHECK(broker.Register(info, provider) == HIGGS_HAND_OK);
        CHECK(broker.EnableSharedRounds(recognizer) == HIGGS_HAND_OK);
        CHECK(broker.Executor(provider, Selected, this, executor) == HIGGS_HAND_OK);
        CHECK(broker.Authorize(provider, executor, recognizer, Left) == HIGGS_HAND_OK);
        CHECK(broker.Recognizer(recognizer, Recognize, this, registration) == HIGGS_HAND_OK);
        for (unsigned i = 0; i < keys.size(); ++i) {
            std::strcpy(keys[i].ownerProvider, "potion-provider");
            std::strcpy(keys[i].equipmentKey, "worn-belt");
            std::strcpy(keys[i].slotKey, i ? "right-pocket" : "left-pocket");
        }
        std::array<HIGGS_HandRecord, 2> hands{};
        for (auto& hand : hands) hand.observationMask = kKnownHandObservation;
        broker.MarkMainThread();
        broker.ObserveWorld(1, false, HiggsHands::Now());
        broker.Publish(hands, HiggsHands::Now());
        epoch = broker.Epoch();
    }

    static void HIGGS_HAND_CALL Selected(const HIGGS_ActionRequest* request, void* context) {
        auto& self = *static_cast<SharedSlots*>(context);
        self.selected = *request;
        ++self.notifications;
    }

    static void HIGGS_HAND_CALL Recognize(const HIGGS_HandInput* input, void* context) {
        auto& self = *static_cast<SharedSlots*>(context);
        if (input->edge != HIGGS_INPUT_DOWN) return;
        self.input = input->inputId;
        self.createdAt = input->observedAtNs;
        for (unsigned n = 0; n < 2; ++n) {
            const auto i = self.reverse ? 1 - n : n;
            HIGGS_ActionCandidate candidate{sizeof(candidate)};
            candidate.physicalHands = Left;
            candidate.inputId = input->inputId;
            candidate.epoch = input->epoch;
            candidate.handGeneration = input->handGeneration;
            candidate.executor = self.executor;
            candidate.actionId = 900; // same semantic action in both pockets
            candidate.contextId = i + 1;
            candidate.contextGeneration = 1;
            candidate.grantDurationMs = 1000;
            CHECK(self.broker.Submit(self.recognizer, candidate, self.requests[i], HiggsHands::Now(),
                self.ordered ? SlotTargetKey(self.keys[i]) : 0) == HIGGS_HAND_OK);
        }
    }

    SlotAdmission Copy(unsigned index, std::uint64_t now) {
        HIGGS_InputDecision decision{sizeof(decision)};
        const auto status = broker.ObservedDecision(recognizer, input, decision);
        return ClassifySlotAdmission(status, decision, epoch, input, requests[index],
            broker.ClientLabel(provider), createdAt, now);
    }
};

void SharedAdmission() {
    unsigned previous{};
    for (bool reverse : {false, true}) {
        SharedSlots fixture(reverse, true);
        CHECK(fixture.broker.Input(Left, HIGGS_INPUT_TRIGGER, true, HiggsHands::Now()));
        CHECK(!fixture.notifications && fixture.requests[0] && fixture.requests[1]);
        // The controller update may occur after this frame's maintenance: a
        // successful copy is still pending. Ordinary action and drag share this rule.
        CHECK(fixture.Copy(0, fixture.createdAt + 1) == SlotAdmission::Waiting);
        CHECK(fixture.Copy(1, fixture.createdAt + 1) == SlotAdmission::Waiting);
        fixture.broker.DrainHandUses();
        CHECK(fixture.notifications == 1 && fixture.selected.contextId >= 1 && fixture.selected.contextId <= 2);
        const auto chosen = static_cast<unsigned>(fixture.selected.contextId);
        CHECK(!previous || previous == chosen);
        previous = chosen;
        CHECK(fixture.Copy(chosen - 1, fixture.createdAt + 2) == SlotAdmission::Selected);
        CHECK(fixture.Copy(2 - chosen, fixture.createdAt + 2) == SlotAdmission::Discard);
        fixture.broker.DrainHandUses();
        CHECK(fixture.notifications == 1); // cannot dispatch twice
    }
    SharedSlots ambiguous(false, false);
    CHECK(ambiguous.broker.Input(Left, HIGGS_INPUT_TRIGGER, true, HiggsHands::Now()));
    ambiguous.broker.DrainHandUses();
    CHECK(!ambiguous.notifications); // old submission really loses both offers
}

void TimeoutAndScope() {
    SharedSlots fixture(false, true);
    CHECK(fixture.broker.Input(Left, HIGGS_INPUT_TRIGGER, true, HiggsHands::Now()));
    CHECK(fixture.Copy(0, fixture.createdAt + 99999999) == SlotAdmission::Waiting);
    CHECK(fixture.Copy(0, fixture.createdAt + 100000000) == SlotAdmission::Discard);
    for (const auto request : fixture.requests)
        CHECK(fixture.broker.CancelRecognized(fixture.recognizer, request, fixture.epoch, 1) == HIGGS_HAND_OK);
    fixture.broker.DrainHandUses();
    CHECK(!fixture.notifications); // timed-out candidates cannot win later

    HIGGS_InputDecision decision{sizeof(decision)};
    decision.epoch = fixture.epoch;
    decision.inputId = fixture.input;
    decision.reason = HIGGS_DECISION_PENDING;
    const auto classify = [&](HIGGS_HandStatus status) {
        return ClassifySlotAdmission(status, decision, fixture.epoch, fixture.input, fixture.requests[0],
            fixture.broker.ClientLabel(fixture.provider), 100, 101);
    };
    CHECK(classify(HIGGS_HAND_NOT_FOUND) == SlotAdmission::Waiting);
    CHECK(classify(HIGGS_HAND_INVALID_CLIENT) == SlotAdmission::Discard);
    ++decision.epoch;
    CHECK(classify(HIGGS_HAND_OK) == SlotAdmission::Discard);
    --decision.epoch;
    ++decision.inputId;
    CHECK(classify(HIGGS_HAND_OK) == SlotAdmission::Discard);
    --decision.inputId;
    --decision.structSize;
    CHECK(classify(HIGGS_HAND_OK) == SlotAdmission::Discard);
}

void StableTargets() {
    InstanceKey a{}, b{};
    std::strcpy(a.ownerProvider, "ab"); std::strcpy(a.equipmentKey, "c"); std::strcpy(a.slotKey, "slot");
    std::strcpy(b.ownerProvider, "a"); std::strcpy(b.equipmentKey, "bc"); std::strcpy(b.slotKey, "slot");
    CHECK(SlotTargetKey(a) != SlotTargetKey(b)); // fields have unambiguous boundaries
    b = a;
    CHECK(SlotTargetKey(a) && SlotTargetKey(a) == SlotTargetKey(b));
    std::strcpy(b.slotKey, "other-slot");
    CHECK(SlotTargetKey(a) != SlotTargetKey(b));
}

void StableDragSelection() {
    std::array<RuntimeRecord, 2> rows{};
    for (auto& row : rows) {
        row.insideHands = Left;
        std::strcpy(row.configured.instance.key.ownerProvider, "pouches");
        std::strcpy(row.configured.instance.key.equipmentKey, "belt");
    }
    std::strcpy(rows[0].configured.instance.key.slotKey, "left");
    std::strcpy(rows[1].configured.instance.key.slotKey, "right");
    rows[0].configured.instance.instanceHandle = 100;
    rows[1].configured.instance.instanceHandle = 1;
    for (unsigned order = 0; order < 2; ++order) {
        const auto* chosen = SelectDragTarget(rows, Left);
        CHECK(chosen && !std::strcmp(chosen->configured.instance.key.slotKey, "left"));
        CHECK(!SelectDragTarget(rows, Right) && !SelectDragTarget(rows, Both));
        std::swap(rows[0], rows[1]);
        std::swap(rows[0].configured.instance.instanceHandle, rows[1].configured.instance.instanceHandle);
    }
    rows[0].availability = static_cast<std::uint32_t>(RuntimeAvailability::AnchorUnavailable);
    const auto* chosen = SelectDragTarget(rows, Left);
    CHECK(chosen == &rows[1]);
    rows[1].insideHands = 0;
    CHECK(!SelectDragTarget(rows, Left));
}
}
int main() {
    StableTargets(); StableDragSelection(); SharedAdmission(); TimeoutAndScope();
    std::printf("Slot admission: %u checks passed; actual HIGGS broker; zero game starts\n", checks);
}
