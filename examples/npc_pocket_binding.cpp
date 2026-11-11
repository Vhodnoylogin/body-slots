#include "npc_pocket_binding.h"

namespace body_slots::examples {
namespace {
constexpr std::uint64_t pocketPreviewAction = 0x504f434b4554;
}
Status NpcPocketBinding::Attach(const RegionActionsApi& actions, const RegionSourcesApi& sources,
                                const RegionContinuationApi& continuation, std::uint64_t writer,
                                HIGGS_HandClient client, const InteractionRegionRecord& region,
                                const RegionPlanckBody& body, ShowPocket open, EndPreview end, void* context) {
    if (handle_.bindingId)
        return Status::Conflict;
    if (!open || !end || !client || !writer || !region.regionId || !region.revision)
        return Status::InvalidArgument;
    if (actions.version != kRegionActionsApiVersion || actions.structSize != sizeof(actions) ||
        sources.version != 1 || sources.structSize != sizeof(sources) ||
        continuation.version != 1 || continuation.structSize != sizeof(continuation) ||
        !actions.RegisterBinding || !actions.RemoveBinding || !sources.BindPlanckBody || !continuation.Subscribe)
        return Status::UnsupportedVersion;
    actions_ = &actions;
    writer_ = writer;
    open_ = open;
    end_ = end;
    context_ = context;
    // Only Down offers an action. Terminal release follows that exact request.
    RegionActionBindingSpec binding{sizeof(binding), Both,   1,     1,    region.regionId,
                                    region.revision, client, Offer, Open, this};
    auto status = actions.RegisterBinding(writer, &binding, &handle_);
    if (status != Status::Ok)
        return status;
    status = continuation.Subscribe(writer, handle_.bindingId, End);
    if (status == Status::Ok)
        status = sources.BindPlanckBody(writer, handle_.bindingId, &body);
    if (status != Status::Ok) {
        const auto drained = Detach();
        if (drained != Status::Ok)
            return drained;
    }
    return status;
}
Status NpcPocketBinding::Detach() {
    if (!handle_.bindingId)
        return Status::Ok;
    const auto status = actions_->RemoveBinding(writer_, handle_.bindingId);
    if (status != Status::Ok && status != Status::NotFound)
        return status;
    handle_ = {sizeof(handle_)};
    return Status::Ok;
}
Status BS_CALL NpcPocketBinding::Offer(void*, const RegionActionContext* context, RegionActionProposal* out,
                                       std::uint32_t count, std::uint32_t* written) {
    if (!count)
        return Status::BufferTooSmall;
    *written = 0;
    const auto hand = context->input.physicalHand;
    if (context->input.snapshot.hands[hand - 1].occupancy != HIGGS_OCCUPANCY_EMPTY)
        return Status::Ok;
    out[0] = {sizeof(*out), hand, 0, 250, pocketPreviewAction, 0};
    *written = 1;
    return Status::Ok;
}
Status BS_CALL NpcPocketBinding::Open(void* self, const RegionActionExecution* request) {
    auto& pocket = *static_cast<NpcPocketBinding*>(self);
    // The mod still owns the UI and its factual HIGGS receipts.
    return pocket.open_(pocket.context_, *request);
}
void BS_CALL NpcPocketBinding::End(void* self, const RegionInputTerminal* terminal) {
    auto& pocket = *static_cast<NpcPocketBinding*>(self);
    pocket.end_(pocket.context_, *terminal);
}
} // namespace body_slots::examples
