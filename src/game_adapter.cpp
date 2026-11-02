#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <Windows.h>
#include "game_adapter.h"
#include "body_slots_game_api.h"
#include "runtime.h"
#include "interaction_validation.h"
#include "higgshandapi.h"
#include "higgshandrouting.h"
#include "higgsinterface001.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace body_slots::game {
const Api* RegistryApi() noexcept;
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }
struct CachedInput {
    std::uint64_t epoch{}, frame{}, observation{}, sampledAt{};
    std::array<std::uint32_t, 2> categories{};
    std::array<std::uint64_t, 2> samples{};
};
struct Candidate {
    HIGGS_HandRequestId request{}; ActionOffer offer{}; std::uint64_t createdAt{};
    HIGGS_HandSnapshot observation{}; std::uint32_t resources{};
};
struct Visual {
    std::uint64_t instance{}, revision{}, content{}, body{};
    RE::NiPointer<RE::NiNode> parent, wrapper;
    bool failed{};
};
struct AnchorIdentity { std::uint64_t instance{}, generation{}; RE::NiPointer<RE::NiAVObject> node; };
class Adapter final {
public:
    void Initialize(const void* skse) {
        SKSE::Init(static_cast<const SKSE::LoadInterface*>(skse));
    }
    void Connect();
    void Reset() { std::lock_guard lock(mutex_); resetRequested_ = true; cache_ = {}; }
    void Tick();
    void Input(const HIGGS_HandInput&);
    Status ValidateCommit(const ContentCommit&, const ActionRequest&) noexcept;
    Status CopyIntegration(GameIntegration* out) {
        if (!out || out->structSize != sizeof(*out)) return Status::InvalidArgument;
        std::lock_guard lock(mutex_);
        *out = {sizeof(*out), kGameApiVersion, connected_ ? 1u : 0u, 0, client_, cache_.epoch, cache_.frame, bodyGeneration_};
        return Status::Ok;
    }
private:
    static void OnFrame() noexcept;
    static void HIGGS_HAND_CALL OnInput(const HIGGS_HandInput*, void*) noexcept;
    void ClearVisuals();
    void Present(const RuntimeRecord&, RE::NiNode*);
    RE::NiAVObject* ResolveAnchor(const RuntimeRecord&, RE::PlayerCharacter*, RE::NiAVObject*);
    void FinishCandidates(const HIGGS_HandSnapshot&);
    void SynchronizeActions();
    std::mutex mutex_;
    HIGGS_HandInterface api_{};
    HIGGS_HandRoutingInterface routing_{};
    HIGGS_HandClient client_{};
    HIGGS_HandRegistration recognizer_{};
    bool connected_{}, resetRequested_{};
    CachedInput cache_{};
    std::vector<Candidate> candidates_;
    std::vector<Visual> visuals_;
    std::vector<AnchorIdentity> anchors_;
    std::uint64_t reader_{}, frame_{}, nextAnchor_{1};
    std::atomic<std::uint64_t> bodyGeneration_{1};
    RE::NiAVObject* lastBody_{};
    RE::TESObjectCELL* lastCell_{};
    // Raw pointers are comparison cookies only; retained references keep old nodes
    // alive until the next main-phase cleanup, preventing same-address ABA.
    RE::NiPointer<RE::NiAVObject> retainedBody_;
};
Adapter& State() { static Adapter state; return state; }
Status ValidateContentCommit(const ContentCommit& commit, const ActionRequest& request) noexcept {
    return State().ValidateCommit(commit, request);
}
RuntimeTransform CopyTransform(const RE::NiTransform& t) {
    RuntimeTransform out{};
    out.position[0] = t.translate.x; out.position[1] = t.translate.y; out.position[2] = t.translate.z;
    std::copy(&t.rotate.entry[0][0], &t.rotate.entry[0][0] + 9, out.rotation);
    out.scale = t.scale;
    return out;
}
std::uint32_t Category(std::uint32_t refId, std::uint32_t equippedId) {
    auto* form = RE::TESForm::LookupByID(refId ? refId : equippedId);
    if (auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr) form = ref->GetBaseObject();
    if (!form) return Other;
    if (auto* alchemy = form->As<RE::AlchemyItem>()) return alchemy->IsFood() ? Food : Potion;
    if (form->Is(RE::FormType::Weapon)) return Weapon;
    if (auto* armor = form->As<RE::TESObjectARMO>()) return armor->IsShield() ? Shield : Other;
    if (form->Is(RE::FormType::Light)) return Torch;
    return Other;
}
RE::NiAVObject* FindPath(RE::NiAVObject* root, const char* path) {
    if (!root || !path || !path[0]) return nullptr;
    // A simple name is scoped beneath the selected body/equipment root. Slash
    // paths walk immediate children so another worn item cannot satisfy a binding.
    if (!std::strchr(path, '/')) return root->GetObjectByName(RE::BSFixedString(path));
    const char* begin = path;
    RE::NiAVObject* current = root;
    for (unsigned depth = 0; depth < 32; ++depth) {
        const char* end = std::strchr(begin, '/');
        const std::string component(begin, end ? end : begin + std::strlen(begin));
        if (component.empty()) return nullptr;
        auto* node = current->AsNode(); if (!node) return nullptr;
        RE::NiAVObject* found = nullptr;
        for (auto& child : node->GetChildren()) if (child && child->name == component.c_str()) {
            if (found) return nullptr;
            found = child.get();
        }
        if (!found) return nullptr;
        current = found;
        if (!end) return current;
        begin = end + 1;
    }
    return nullptr;
}
RE::NiAVObject* Adapter::ResolveAnchor(const RuntimeRecord& record, RE::PlayerCharacter* player, RE::NiAVObject* root) {
    const auto& binding = record.binding;
    if (binding.requiredEquipmentFormId && !player->GetWornArmor(binding.requiredEquipmentFormId)) return nullptr;
    if (binding.kind == static_cast<std::uint32_t>(RuntimeBindingKind::BodyNode))
        return FindPath(root, binding.node[0] ? binding.node : record.configured.instance.effective.anchor);
    if (binding.kind != static_cast<std::uint32_t>(RuntimeBindingKind::EquipmentNode) || !binding.requiredEquipmentFormId) return nullptr;
    const auto& biped = player->GetBiped(false); if (!biped) return nullptr;
    RE::NiAVObject* found = nullptr;
    for (const auto& part : biped->objects) {
        if (!part.item || part.item->GetFormID() != binding.requiredEquipmentFormId || !part.partClone) continue;
        auto* anchor = FindPath(part.partClone.get(), binding.node);
        if (anchor) { if (found && found != anchor) return nullptr; found = anchor; }
    }
    return found;
}
void Adapter::Connect() {
    { std::lock_guard lock(mutex_); if (connected_) return; }
    const auto module = GetModuleHandleW(L"higgs_vr.dll");
    const auto query = module ? reinterpret_cast<HIGGS_QueryHandInterfaceFn>(GetProcAddress(module, "HIGGS_QueryHandInterface")) : nullptr;
    HIGGS_HandInterface api{};
    if (!GetModuleHandleW(L"vrik.dll") || !query || query(HIGGS_HAND_ABI_VERSION, sizeof(api), &api) != HIGGS_HAND_OK) return;
    constexpr auto needed = HIGGS_HAND_CAP_SNAPSHOT | HIGGS_HAND_CAP_INPUT_BROKER | HIGGS_HAND_CAP_OWNED_GRANTS | HIGGS_HAND_CAP_EXECUTOR_RECEIPTS;
    if ((api.capabilities & needed) != needed) return;
    const auto queryRouting = reinterpret_cast<HIGGS_QueryHandRoutingInterfaceFn>(GetProcAddress(module, "HIGGS_QueryHandRoutingInterface"));
    HIGGS_HandRoutingInterface routing{};
    if (!queryRouting || queryRouting(HIGGS_HAND_ROUTING_ABI_VERSION, sizeof(routing), &routing) != HIGGS_HAND_OK) return;
    HIGGS_HandClientInfo info{}; info.structSize = sizeof(info); std::strcpy(info.name, "BodySlots");
    // Neutral default. HIGGS owns the user's effective provider priority.
    HIGGS_HandClient client{}; HIGGS_HandRegistration recognizer{};
    if (api.RegisterClient(&info, &client) != HIGGS_HAND_OK) return;
    if (api.SubscribeRecognizer(client, &OnInput, this, &recognizer) != HIGGS_HAND_OK) { api.UnregisterClient(client); return; }
    std::uint64_t reader{};
    struct PhaseRequest { void* (*getInterface)(unsigned int) = nullptr; } phase;
    const auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) messaging->Dispatch(0xF9279A57, &phase, sizeof(PhaseRequest*), "HIGGS");
    auto* phases = phase.getInterface ? static_cast<HiggsPluginAPI::IHiggsInterface001*>(phase.getInterface(1)) : nullptr;
    if (RegistryApi()->RegisterObserver(&reader) != Status::Ok || !phases) {
        if (reader) RegistryApi()->ReleaseClient(reader);
        api.UnregisterClient(client); return;
    }
    phases->AddPostVrikPostHiggsCallback(&OnFrame);
    std::lock_guard lock(mutex_);
    api_ = api; routing_ = routing; client_ = client; recognizer_ = recognizer; reader_ = reader; connected_ = true;
    detail::SetContentCommitValidator(&ValidateContentCommit);
}
Status Adapter::ValidateCommit(const ContentCommit& commit, const ActionRequest& request) noexcept {
    HIGGS_ActionResult result{}; result.structSize = sizeof(result);
    if (!client_ || api_.CopyActionResult(client_, request.grant.requestId, &result) != HIGGS_HAND_OK) return Status::Unavailable;
    if (result.epoch != commit.epoch || result.inputId != request.grant.inputSequence || result.executor != request.offer.higgsExecutor)
        return Status::Conflict;
    const auto stage = static_cast<CommitStage>(commit.stage);
    if (stage == CommitStage::Committed)
        return result.stage == HIGGS_ACTION_COMMITTED || result.stage == HIGGS_ACTION_COMPLETED ? Status::Ok : Status::Conflict;
    if (stage == CommitStage::Completed) return result.stage == HIGGS_ACTION_COMPLETED ? Status::Ok : Status::Conflict;
    if (stage == CommitStage::Failed) return result.stage == HIGGS_ACTION_REJECTED ? Status::Ok : Status::Conflict;
    if (stage == CommitStage::Cancelled) return result.stage == HIGGS_ACTION_CANCELLED ? Status::Ok : Status::Conflict;
    HIGGS_HandSnapshot snapshot{}; snapshot.structSize = sizeof(snapshot);
    if (result.stage != HIGGS_ACTION_ACCEPTED || api_.CopySnapshot(client_, &snapshot) != HIGGS_HAND_OK || snapshot.epoch != commit.epoch)
        return Status::Conflict;
    const auto& hand = snapshot.hands[request.grant.physicalHand - 1];
    return hand.grant == commit.higgsGrantId && hand.owner == request.offer.higgsClient ? Status::Ok : Status::Conflict;
}
void Adapter::SynchronizeActions() {
    std::array<ActionProgress, kMaxPendingActions> progress{}; std::uint32_t count{};
    if (detail::CopyActionProgress(progress.data(), progress.size(), &count) != Status::Ok) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& action = progress[i];
        if (action.stage == static_cast<std::uint32_t>(CommitStage::Completed) ||
            action.stage == static_cast<std::uint32_t>(CommitStage::Failed) || action.stage == static_cast<std::uint32_t>(CommitStage::Cancelled)) continue;
        HIGGS_ActionResult result{}; result.structSize = sizeof(result);
        const auto status = api_.CopyActionResult(client_, action.grant.requestId, &result);
        if (status == HIGGS_HAND_OK && (result.stage == HIGGS_ACTION_COMMITTED || result.stage == HIGGS_ACTION_COMPLETED))
            detail::ApplyHandResult(action.grant.requestId, action.grant.epoch, CommitStage::Committed);
        if (status == HIGGS_HAND_NOT_FOUND || (status == HIGGS_HAND_OK && result.stage == HIGGS_ACTION_CANCELLED))
            detail::ApplyHandResult(action.grant.requestId, action.grant.epoch, CommitStage::Cancelled);
        else if (status == HIGGS_HAND_OK && result.stage == HIGGS_ACTION_REJECTED)
            detail::ApplyHandResult(action.grant.requestId, action.grant.epoch, CommitStage::Failed);
    }
}
void Adapter::OnFrame() noexcept { try { State().Tick(); } catch (...) { State().Reset(); detail::InvalidateRuntime(); } }
void HIGGS_HAND_CALL Adapter::OnInput(const HIGGS_HandInput* input, void* context) noexcept {
    if (!input || !context) return;
    try { static_cast<Adapter*>(context)->Input(*input); } catch (...) {}
}
void Adapter::Input(const HIGGS_HandInput& input) {
    if (input.structSize != sizeof(input) || input.physicalHand < 1 || input.physicalHand > 2) return;
    CachedInput cached{};
    { std::lock_guard lock(mutex_); if (!connected_) return; cached = cache_; }
    const auto now = Now(); const auto handIndex = input.physicalHand - 1;
    if (!cached.frame || cached.epoch != input.epoch || cached.observation != input.snapshot.observationId ||
        now < cached.sampledAt || now - cached.sampledAt > 100000000) return;
    const auto& hand = input.snapshot.hands[handIndex];
    RuntimeInput observation{}; observation.structSize = sizeof(observation);
    observation.physicalHand = input.physicalHand; observation.button = input.button; observation.edge = input.edge;
    observation.contentCategory = cached.categories[handIndex];
    observation.available = detail::MayOfferSlotAction(hand);
    observation.epoch = cached.epoch; observation.frame = cached.frame; observation.inputSequence = input.inputId;
    observation.poseSample = cached.samples[handIndex]; observation.itemIdentity = hand.heldRefId ? hand.heldRefId : hand.equippedFormId;
    observation.handStateRevision = input.snapshot.observationId;
    observation.handOccupancy = hand.occupancy; observation.handObservationMask = hand.observationMask;
    observation.heldRefId = hand.heldRefId; observation.equippedFormId = hand.equippedFormId;
    auto offers = std::make_unique<std::array<ActionOffer, kMaxActionOffers>>(); std::uint32_t count{};
    if (detail::CollectOffers(observation, offers->data(), offers->size(), &count) != Status::Ok) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& offer = (*offers)[i]; if (!offer.higgsClient || !offer.higgsExecutor) continue;
        HIGGS_ActionCandidate candidate{}; candidate.structSize = sizeof(candidate);
        candidate.physicalHands = offer.proposal.requiredResources ? offer.proposal.requiredResources : input.physicalHand;
        candidate.inputId = input.inputId; candidate.epoch = input.epoch; candidate.handGeneration = input.handGeneration;
        candidate.executor = offer.higgsExecutor; candidate.actionId = offer.proposal.executorAction;
        candidate.contextId = offer.offerId; candidate.contextGeneration = offer.policyRevision; candidate.grantDurationMs = 2000;
        if (routing_.CanRoute(client_, candidate.executor, candidate.physicalHands) != HIGGS_HAND_OK) continue;
        HIGGS_HandRequestId request{};
        std::lock_guard lock(mutex_);
        if (candidates_.size() < kMaxActionOffers && api_.SubmitCandidate(client_, &candidate, &request) == HIGGS_HAND_OK)
            candidates_.push_back({request, offer, now, input.snapshot, candidate.physicalHands});
    }
}
void Adapter::FinishCandidates(const HIGGS_HandSnapshot& snapshot) {
    std::vector<Candidate> pending;
    { std::lock_guard lock(mutex_); pending.swap(candidates_); }
    for (const auto& candidate : pending) {
        const auto& offer = candidate.offer;
        HIGGS_InputDecision decision{}; decision.structSize = sizeof(decision);
        const auto status = api_.CopyInputDecision(client_, offer.context.input.inputSequence, &decision);
        if (status != HIGGS_HAND_OK) {
            if (Now() - candidate.createdAt < 100000000) { std::lock_guard lock(mutex_); if (candidates_.size() < kMaxActionOffers) candidates_.push_back(candidate); }
            continue;
        }
        if (!decision.consumed || decision.requestId != candidate.request || decision.owner != offer.higgsClient) continue;
        HIGGS_ActionResult result{}; result.structSize = sizeof(result);
        if (api_.CopyActionResult(client_, candidate.request, &result) != HIGGS_HAND_OK || result.stage != HIGGS_ACTION_ACCEPTED) continue;
        if (!detail::GrantMatchesObservations(candidate.observation, snapshot, candidate.resources, decision.owner, decision.grant)) {
            routing_.CancelRecognizedAction(client_, candidate.request, decision.epoch, 1); continue;
        }
        ActionGrant grant{}; grant.epoch = decision.epoch; grant.requestId = decision.requestId; grant.grantId = decision.grant;
        grant.contextId = offer.offerId; grant.contextGeneration = offer.policyRevision;
        grant.inputSequence = decision.inputId; grant.physicalHand = offer.context.input.physicalHand;
        std::uint64_t request{};
        if (detail::DispatchSelected(offer, grant, &request) != Status::Ok)
            routing_.CancelRecognizedAction(client_, candidate.request, decision.epoch, 2);
        // Inventory/physics outcomes are reported by the assigned provider under
        // its own HIGGS client, then mirrored through ReportContentCommit.
    }
}
void Adapter::ClearVisuals() {
    for (auto& visual : visuals_) if (visual.wrapper && visual.wrapper->parent) visual.wrapper->parent->DetachChild(visual.wrapper.get());
    visuals_.clear();
}
void Adapter::Present(const RuntimeRecord& record, RE::NiNode* parent) {
    if (!parent) return;
    auto found = std::find_if(visuals_.begin(), visuals_.end(), [&](const auto& v) { return v.instance == record.configured.instance.instanceHandle; });
    if (found != visuals_.end() && (found->revision != record.visualRevision || found->body != bodyGeneration_ || found->content != record.contentVersion)) {
        if (found->wrapper && found->wrapper->parent) found->wrapper->parent->DetachChild(found->wrapper.get());
        visuals_.erase(found); found = visuals_.end();
    }
    if (record.availability != static_cast<std::uint32_t>(RuntimeAvailability::Available)) {
        if (found != visuals_.end() && found->wrapper) found->wrapper->SetAppCulled(true);
        return;
    }
    if (found != visuals_.end() && found->failed) return;
    if (found == visuals_.end()) {
        const bool holder = !(record.visual.flags & 1) && record.visual.holderModel[0];
        const bool preview = detail::MayDisplayPreview(record);
        if (!holder && !preview) {
            return;
        }
        Visual visual{}; visual.instance = record.configured.instance.instanceHandle; visual.revision = record.visualRevision;
        visual.content = record.contentVersion; visual.body = bodyGeneration_; visual.parent.reset(parent); visual.wrapper.reset(RE::NiNode::Create(2));
        if (!visual.wrapper) return;
        bool success = true;
        for (const char* path : {holder ? record.visual.holderModel : "", preview ? record.visual.previewModel : ""}) {
            if (!path[0]) continue;
            RE::NiPointer<RE::NiNode> source;
            RE::BSModelDB::DBTraits::ArgsType args{};
            if (RE::BSModelDB::Demand(path, source, args) != RE::BSResource::ErrorCode::kNone || !source) { success = false; break; }
            RE::NiPointer<RE::NiObject> clone; source->CreateDeepCopy(clone);
            auto* node = clone ? clone->AsNode() : nullptr;
            if (!node || node == source.get()) { success = false; break; }
            // Presentation must not instantiate a second physics body.
            std::vector<RE::NiAVObject*> nodes{node};
            for (std::size_t i = 0; i < nodes.size() && i < 4096; ++i) {
                nodes[i]->collisionObject.reset();
                if (auto* branch = nodes[i]->AsNode()) for (auto& child : branch->GetChildren()) if (child) nodes.push_back(child.get());
            }
            if (nodes.size() > 4096) { success = false; break; }
            visual.wrapper->AttachChild(node, true);
        }
        if (!success) {
            detail::ReportPresentation(visual.instance, record.visualRevision, record.contentVersion, PresentationState::Failed);
            visual.wrapper.reset(); visual.failed = true; visuals_.push_back(std::move(visual)); return;
        }
        parent->AttachChild(visual.wrapper.get(), true);
        visuals_.push_back(std::move(visual)); found = visuals_.end() - 1;
    }
    auto& wrapper = *found->wrapper;
    const auto parentTransform = parent->world;
    if (!(parentTransform.scale > 0)) return;
    RE::NiTransform world{};
    std::copy(record.world.rotation, record.world.rotation + 9, &world.rotate.entry[0][0]);
    world.translate = {record.world.position[0], record.world.position[1], record.world.position[2]}; world.scale = record.world.scale;
    wrapper.local = parentTransform.Invert() * world;
    wrapper.SetAppCulled(false);
    RE::NiUpdateData update{}; wrapper.Update(update);
    detail::ReportPresentation(record.configured.instance.instanceHandle, record.visualRevision, record.contentVersion, PresentationState::Ready);
}
void Adapter::Tick() {
    bool reset{};
    { std::lock_guard lock(mutex_); if (!connected_) return; reset = resetRequested_; resetRequested_ = false; }
    if (reset) {
        ClearVisuals(); anchors_.clear(); retainedBody_.reset(); lastBody_ = nullptr; lastCell_ = nullptr; ++bodyGeneration_;
        std::vector<Candidate> cancelled;
        { std::lock_guard lock(mutex_); cancelled.swap(candidates_); }
        for (const auto& c : cancelled) routing_.CancelRecognizedAction(client_, c.request, c.offer.context.input.epoch, 3);
    }
    HIGGS_HandSnapshot snapshot{}; snapshot.structSize = sizeof(snapshot);
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* root = player ? player->Get3D(false) : nullptr;
    auto* cell = player ? player->GetParentCell() : nullptr;
    if (api_.CopySnapshot(client_, &snapshot) != HIGGS_HAND_OK || !snapshot.worldReady || !root || !cell || !cell->IsAttached()) {
        detail::InvalidateRuntime(); ClearVisuals();
        std::lock_guard lock(mutex_); cache_ = {}; return;
    }
    if (root != lastBody_ || cell != lastCell_) {
        SynchronizeActions();
        detail::InvalidateRuntime(); ClearVisuals(); anchors_.clear(); retainedBody_.reset(root); lastBody_ = root; lastCell_ = cell; ++bodyGeneration_;
    }
    // Preserve the fact that the assigned executor already committed, before a
    // configuration/zone update invalidates uncommitted spatial offers.
    SynchronizeActions();
    const auto& runtime = detail::GetRuntimeApi();
    auto records = std::make_unique<std::array<RuntimeRecord, kMaxInstances>>();
    RuntimeView view{}; view.structSize = sizeof(view); view.version = kRuntimeApiVersion;
    if (runtime.CopyRuntime(reader_, records->data(), records->size(), &view) != Status::Ok) return;
    auto anchors = std::make_unique<std::array<detail::AnchorSample, kMaxInstances>>();
    detail::RuntimeFrame frame{}; frame.epoch = snapshot.epoch; frame.frame = ++frame_; frame.bodyGeneration = bodyGeneration_;
    frame.timeSeconds = static_cast<double>(Now()) / 1e9;
    anchors_.erase(std::remove_if(anchors_.begin(), anchors_.end(), [&](const auto& identity) {
        for (std::uint32_t i = 0; i < view.count; ++i)
            if ((*records)[i].configured.instance.instanceHandle == identity.instance) return false;
        return true;
    }), anchors_.end());
    for (unsigned i = 0; i < 2; ++i) {
        auto& probe = frame.hands[i]; probe.physicalHand = i + 1; probe.sampleId = frame.frame * 2 + i;
        auto* hand = FindPath(root, i == 0 ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]");
        const auto flags = snapshot.hands[i].observationMask;
        if (hand && (flags & HIGGS_OBS_TRACKING_VALID)) {
            probe.valid = 1; probe.world[0] = hand->world.translate.x; probe.world[1] = hand->world.translate.y; probe.world[2] = hand->world.translate.z;
        }
    }
    for (std::uint32_t i = 0; i < view.count; ++i) {
        auto& sample = (*anchors)[i]; sample.instanceHandle = (*records)[i].configured.instance.instanceHandle;
        auto* anchor = ResolveAnchor((*records)[i], player, root);
        auto identity = std::find_if(anchors_.begin(), anchors_.end(), [&](const auto& a) { return a.instance == sample.instanceHandle; });
        if (identity == anchors_.end()) { anchors_.push_back({sample.instanceHandle, ++nextAnchor_, {}}); identity = anchors_.end() - 1; identity->node.reset(anchor); }
        else if (identity->node.get() != anchor) { identity->node.reset(anchor); identity->generation = ++nextAnchor_; }
        sample.anchorGeneration = identity->generation;
        if (anchor) { sample.valid = 1; sample.world = CopyTransform(anchor->world); }
    }
    frame.anchors = anchors->data(); frame.anchorCount = view.count;
    if (detail::UpdateRuntime(frame) != Status::Ok) return;
    FinishCandidates(snapshot);
    SynchronizeActions();
    view = {}; view.structSize = sizeof(view); view.version = kRuntimeApiVersion;
    if (runtime.CopyRuntime(reader_, records->data(), records->size(), &view) == Status::Ok) {
        auto* parent = root->AsNode();
        for (std::uint32_t i = 0; i < view.count; ++i) Present((*records)[i], parent);
        for (auto it = visuals_.begin(); it != visuals_.end();) {
            bool exists = false;
            for (std::uint32_t i = 0; i < view.count; ++i) if ((*records)[i].configured.instance.instanceHandle == it->instance) { exists = true; break; }
            if (!exists) { if (it->wrapper && it->wrapper->parent) it->wrapper->parent->DetachChild(it->wrapper.get()); it = visuals_.erase(it); } else ++it;
        }
    }
    CachedInput cached{}; cached.epoch = snapshot.epoch; cached.frame = frame.frame; cached.observation = snapshot.observationId; cached.sampledAt = Now();
    for (unsigned i = 0; i < 2; ++i) {
        cached.samples[i] = frame.hands[i].sampleId;
        const auto& hand = snapshot.hands[i];
        cached.categories[i] = hand.occupancy == HIGGS_OCCUPANCY_EMPTY &&
            (hand.observationMask & (HIGGS_OBS_HELD_KNOWN | HIGGS_OBS_EQUIPMENT_KNOWN)) == (HIGGS_OBS_HELD_KNOWN | HIGGS_OBS_EQUIPMENT_KNOWN)
            ? 0 : Category(hand.heldRefId, hand.equippedFormId);
    }
    std::lock_guard lock(mutex_); cache_ = cached;
}
Status BS_CALL CopyIntegration(GameIntegration* output) noexcept {
    try { return State().CopyIntegration(output); } catch (...) { return Status::InternalError; }
}
}
void Initialize(const void* skse) noexcept { try { State().Initialize(skse); } catch (...) {} }
void Connect() noexcept { try { State().Connect(); } catch (...) {} }
void Reset() noexcept { State().Reset(); }
}
extern "C" const body_slots::Api* BS_CALL BodySlots_QueryInterface(std::uint32_t, std::uint32_t) noexcept;
namespace body_slots::game {
const Api* RegistryApi() noexcept { return BodySlots_QueryInterface(kApiVersion, sizeof(Api)); }
}
extern "C" __declspec(dllexport) const body_slots::GameApi* BS_CALL BodySlots_QueryGameInterface(std::uint32_t version, std::uint32_t size) noexcept {
    static const body_slots::GameApi table{sizeof(table), body_slots::kGameApiVersion, &body_slots::game::CopyIntegration};
    return version == table.version && size == sizeof(table) ? &table : nullptr;
}
