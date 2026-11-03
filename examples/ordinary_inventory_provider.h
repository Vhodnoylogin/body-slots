#pragma once
// Mod-author SDK helper. Only copied public Body Slots/HIGGS contracts.
#include "body_slots_behavior_api.h"
#include "body_slots_game_api.h"
#include "higgsinventory.h"
#include "higgshandrouting.h"
#include <array>
#include <mutex>
namespace body_slots::examples {
struct ContentProjection { std::uint64_t identity=0,quantity=0; };
struct OrdinaryItemPlan {
    std::uint32_t transferKind=0,baseFormId=0,stackOrdinal=0,quantity=1;
    ContentProjection after;
};
// Read-only, thread-safe policy. No inventory calls, mutation or implicit stock.
using OrdinaryItemPolicy=bool (*)(void*,const SourceActionContext&,OrdinaryItemPlan&);
struct InventoryProviderTables {
    const RuntimeApi* slots=nullptr;
    const BehaviorApi* behaviors=nullptr;
    const GameApi* game=nullptr;
    HIGGS_HandInterface hands{};
    HIGGS_HandRoutingInterface routing{};
    HIGGS_InventoryInterface inventory{};
    HIGGS_TransactionsInterface transactions{};
};
struct InventoryProviderConfig {
    std::uint64_t writer=0,instance=0,higgsClient=0;
    std::uint32_t physicalHands=Both,timeoutMs=1000;
    std::int32_t priority=0;
    OrdinaryItemPolicy policy=nullptr;
    void* policyContext=nullptr;
    // Withdrawal always uses Down. Up storage requires a fresh, unconsumed
    // release; it cannot reuse the Up owned by an earlier consumed Down.
    std::uint32_t storeEdge=HIGGS_INPUT_DOWN;
};
enum class InventoryProviderStage : std::uint32_t {
    Detached,Ready,Queued,Selecting,Transferring,Reporting,Completed,Cancelled,RecoveryRequired
};
struct InventoryProviderProgress {
    InventoryProviderStage stage=InventoryProviderStage::Detached;
    Status slotStatus=Status::Ok;
    HIGGS_HandStatus handStatus=HIGGS_HAND_OK;
    std::uint64_t slotRequest=0,higgsAction=0,selectionTicket=0,transfer=0;
    HIGGS_TransferState receipt{};
};
class OrdinaryInventoryProvider final {
public:
    OrdinaryInventoryProvider()=default;
    OrdinaryInventoryProvider(const OrdinaryInventoryProvider&)=delete;
    OrdinaryInventoryProvider& operator=(const OrdinaryInventoryProvider&)=delete;
    // Dedicated HIGGS client supplied/owned by caller. The helper owns only its
    // generic action executor, route and Body Slots behavior registration.
    // Attach/Tick/Cancel/Detach must be serialized by the owner thread.
    Status Attach(const InventoryProviderTables&,const InventoryProviderConfig&);
    // Owner's safe main update, after HIGGS native maintenance. Never from input.
    InventoryProviderProgress Tick();
    InventoryProviderProgress CopyProgress()const;
    void Cancel();
    // Conflict while an operation/recovery is unresolved. Keep this object and
    // policy alive until callback drainage returns Ok; never unload on Conflict.
    Status Detach();
private:
    struct OfferPlan {std::uint64_t token=0,epoch=0,input=0,contentVersion=0;OrdinaryItemPlan plan;HIGGS_ItemIdentity held{};};
    struct Work {
        SourceActionRequest selected{};
        OrdinaryItemPlan plan;
        HIGGS_ItemIdentity held{};
        HIGGS_ActionRequest authority{};
        HIGGS_TransferRequest transferRequest{};
        std::uint64_t ticket=0,transfer=0;
        bool cancel=false,mirror=false,completed=false;
    };
    static Status BS_CALL Offers(void*,const SourceActionContext*,SourceActionProposal*,std::uint32_t,std::uint32_t*);
    static Status BS_CALL Execute(void*,const SourceActionRequest*);
    static void HIGGS_HAND_CALL Selected(const HIGGS_ActionRequest*,void*);
    Status Offer(const SourceActionContext&,SourceActionProposal*,std::uint32_t,std::uint32_t*);
    Status Queue(const SourceActionRequest&);
    void Advance(Work&,InventoryProviderProgress&);
    bool OriginalResult(const Work&,HIGGS_ActionResult&,InventoryProviderProgress&);
    bool ReportOriginal(const Work&,std::uint32_t,std::uint32_t,InventoryProviderProgress&);
    Status Mirror(const Work&,CommitStage,const ContentProjection&,std::uint64_t);
    bool Finish(Work&,InventoryProviderProgress&,bool success);
    mutable std::mutex mutex_;
    InventoryProviderTables tables_;
    InventoryProviderConfig config_;
    std::uint64_t actionExecutor_=0,behavior_=0,recognizer_=0,nextOffer_=1;
    std::array<OfferPlan,16> offers_{};
    std::array<HIGGS_ActionRequest,2> selected_{};
    Work work_;
    InventoryProviderProgress progress_;
    bool attached_=false,stopping_=false;
};
}
