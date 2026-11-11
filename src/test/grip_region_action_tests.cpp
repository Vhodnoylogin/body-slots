#include "grip_region_action.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using namespace body_slots;
using namespace body_slots::examples;
namespace {
void Check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
double timeSeconds = 20;
double Now() noexcept { return timeSeconds; }
struct Broker {
    HiggsGripOffer offer{};
    HiggsGripSelected selected{};
    void* context{};
    int registrations{}, removals{};
    bool busy{};
} broker;
std::uint32_t Register(const char* id, HiggsGripOffer offer, HiggsGripSelected selected,
                       void* context, std::uint64_t* token) {
    Check(std::strcmp(id,"pouch.inspect") == 0 && offer && selected && context && token, "registration");
    ++broker.registrations;
    broker.offer = offer; broker.selected = selected; broker.context = context;
    *token = 123;
    return HIGGS_GRIP_OK;
}
std::uint32_t Remove(std::uint64_t token) {
    Check(token == 123, "remove matching registration");
    ++broker.removals;
    if (broker.busy) { broker.busy = false; return HIGGS_GRIP_BUSY; }
    broker.offer = nullptr; broker.selected = nullptr; broker.context = nullptr;
    return HIGGS_GRIP_OK;
}
struct Scene {
    RegionObservation row{};
    bool eligible{true};
    std::uint64_t last{};
    void Advance() {
        row.sequence = ++last;
        row.evaluated = {sizeof(RegionEvaluationFrame),0,3,4,last,timeSeconds};
        for (auto* s : {&row.anchor.source, &row.probe.source}) {
            s->sampleId = last; s->frame = last; s->epoch = 3; s->saveGeneration = 4;
            s->timeSeconds = timeSeconds; s->units = static_cast<std::uint32_t>(RegionUnits::SkyrimWorld);
        }
    }
    static bool Evaluate(void* context, RegionObservation& out) noexcept {
        const auto& scene = *static_cast<Scene*>(context);
        out = scene.row;
        return scene.eligible;
    }
};
std::uint64_t Offer(std::uint32_t left) { return broker.offer(left, broker.context); }
void Select(std::uint32_t left, std::uint64_t ticket) { broker.selected(left, ticket, broker.context); }
}

int main() {
    Scene scene;
    auto& row = scene.row;
    row.regionId = 12; row.regionRevision = 8; row.enteredAtSeconds = 18;
    row.state = static_cast<std::uint32_t>(RegionSpatialState::Ready);
    row.anchor.valid = row.probe.valid = 1;
    row.anchor.source.identity = 8; row.probe.source.identity = 15;
    row.anchor.source.generation = row.probe.source.generation = 1;
    std::memcpy(row.anchor.source.provider, "pouch", 6);
    std::memcpy(row.anchor.source.source, "rim", 4);
    std::memcpy(row.probe.source.provider, "vrik", 5);
    std::memcpy(row.probe.source.source, "leftwrist", 10);
    scene.Advance();
    GripRegionTarget target{12,8,3,4,row.anchor.source,row.probe.source,Left,0.25};
    HiggsGripActions001 api{Register, Remove};
    GripRegionAction action(target, Now);
    auto update = [&] { return action.Update(Scene::Evaluate, &scene); };
    Check(action.Connect(nullptr,"pouch.inspect",false) == HIGGS_GRIP_INVALID, "missing optional service");
    Check(action.Connect(&api,"pouch.inspect",true) == HIGGS_GRIP_CONFLICT && !broker.registrations, "full route wins");
    Check(action.Connect(&api,"pouch.inspect",false) == HIGGS_GRIP_OK, "connect");
    Check(!update() && !Offer(0) && !Offer(2), "first frame and physical hand");
    auto ticket = Offer(1); Check(ticket != 0, "left offer");
    Select(0,ticket); Select(1,ticket + 2);
    Check(Offer(1) == ticket, "wrong hand or ticket cannot select");
    Select(1,ticket); Select(1,ticket); Check(!Offer(1), "one pending selection");
    scene.Advance(); Check(update(), "one main-thread activation after a fresh sample");
    Select(1,ticket); scene.Advance(); Check(!update(), "old ticket never replays");

    Select(1,Offer(1)); scene.eligible = false; scene.Advance();
    Check(!update() && !Offer(1), "ineligible on dequeue");
    scene.eligible = true; scene.Advance(); update();
    Select(1,Offer(1)); row.enteredAtSeconds += 1; scene.Advance();
    Check(!update(), "re-entry invalidates earlier press");
    Select(1,Offer(1)); timeSeconds += 0.5;
    scene.Advance(); Check(!update(), "press expires even if a new pose is ready");
    ticket = Offer(1); Check(ticket != 0, "new offer");
    timeSeconds += 0.5;
    Check(!Offer(1), "stale cache between frames");
    Select(1,ticket); scene.Advance(); Check(!update(), "late callback does not select expired sample");

    scene.Advance(); row.evaluated.epoch = 7;
    Check(!update() && !Offer(1), "wrong epoch");
    scene.Advance(); row.evaluated.saveGeneration = 9;
    Check(!update() && !Offer(1), "wrong save");
    scene.Advance(); row.regionRevision = 10;
    Check(!update() && !Offer(1), "edited region");
    row.regionRevision = 8; scene.Advance(); row.regionId = 1;
    Check(!update() && !Offer(1), "different region");
    row.regionId = 12; scene.Advance(); ++row.probe.source.identity;
    Check(!update() && !Offer(1), "different probe");
    --row.probe.source.identity; scene.Advance(); ++row.anchor.source.generation;
    Check(!update() && !Offer(1), "replaced equipment");
    --row.anchor.source.generation; scene.Advance(); row.anchor.source.timeSeconds -= 1;
    Check(!update() && !Offer(1), "fresh frame with stale anchor");
    scene.Advance(); update(); Select(1,Offer(1));
    Check(!update() && !Offer(1), "repeat of the pre-selection row");
    scene.Advance(); update(); ticket = Offer(1);
    action.Invalidate(); Select(1,ticket); Check(!Offer(1), "withdraw cancels in-flight ticket");
    broker.busy = true; action.Disconnect();
    Check(broker.removals == 2 && !broker.context && !update(), "drain before lifetime ends");
    std::puts("Grip region action checks passed.");
}
