#include "ordinary_inventory_provider.h"
#include <cstring>
#include <limits>
namespace body_slots::examples {
namespace {
bool Projection(const ContentProjection& p){return (!p.identity)==(!p.quantity);}
bool SameItem(const HIGGS_ItemIdentity& a,const HIGGS_ItemIdentity& b){return a.structSize==b.structSize&&a.kind==b.kind&&a.epoch==b.epoch&&a.instance==b.instance&&a.generation==b.generation&&a.provider==b.provider&&a.providerKey==b.providerKey&&a.instanceDataVersion==b.instanceDataVersion&&a.validFields==b.validFields&&a.baseFormId==b.baseFormId&&a.worldRefId==b.worldRefId&&a.quantity==b.quantity;}
bool Same(const HIGGS_ActionRequest& h,const SourceActionRequest& s,std::uint64_t client,std::uint64_t executor){const auto& r=s.request;return h.structSize==sizeof(h)&&h.owner==client&&h.executor==executor&&h.requestId==r.grant.requestId&&h.grant==r.grant.grantId&&h.epoch==r.grant.epoch&&h.inputId==r.grant.inputSequence&&h.contextId==r.grant.contextId&&h.contextGeneration==r.grant.contextGeneration&&h.actionId==r.offer.proposal.executorAction&&h.physicalHands==r.grant.physicalHand;}
std::uint32_t Evidence(const HIGGS_TransferState& s){std::uint32_t flags=0;if(s.receiptFlags&HIGGS_TX_DOMAIN_ATTESTED)flags|=HIGGS_RECEIPT_INVENTORY_COMMITTED;if(s.receiptFlags&(HIGGS_TX_NATIVE_RELEASE_CONFIRMED|HIGGS_TX_NATIVE_GRIP_CONFIRMED))flags|=HIGGS_RECEIPT_PHYSICAL_CONFIRMED;if(s.receiptFlags&HIGGS_TX_COMPENSATION_CONFIRMED)flags|=HIGGS_RECEIPT_COMPENSATED;return flags;}
bool Released(HIGGS_HandStatus s){return s==HIGGS_HAND_OK||s==HIGGS_HAND_NOT_FOUND||s==HIGGS_HAND_STALE;}
}
Status OrdinaryInventoryProvider::Attach(const InventoryProviderTables& t,const InventoryProviderConfig& c){
    {std::lock_guard lock(mutex_);if(attached_||behavior_||actionExecutor_)return Status::Conflict;}
    if(!t.slots||!t.behaviors||!t.game)return Status::InvalidArgument;
    if(t.slots->structSize!=sizeof(RuntimeApi)||t.slots->version!=1||t.behaviors->structSize!=sizeof(BehaviorApi)||t.behaviors->version!=1||t.game->structSize!=sizeof(GameApi)||t.game->version!=1||t.hands.structSize!=sizeof(t.hands)||t.hands.abiVersion!=1||t.routing.structSize!=sizeof(t.routing)||t.routing.abiVersion!=1||t.inventory.structSize!=sizeof(t.inventory)||t.inventory.abiVersion!=1||t.transactions.structSize!=sizeof(t.transactions)||t.transactions.abiVersion!=1)return Status::UnsupportedVersion;
    if(!c.writer||!c.instance||!c.higgsClient||!c.policy||!c.physicalHands||(c.physicalHands&~3u)||!c.timeoutMs||c.timeoutMs>2000||!t.slots||!t.behaviors||!t.game||!t.game->CopyIntegration||!t.behaviors->RegisterSourceBehavior||!t.slots->RemoveBehavior||!t.slots->ReportContentCommit||!t.hands.RegisterExecutor||!t.hands.RemoveRegistration||!t.hands.CopySnapshot||!t.hands.CopyActionResult||!t.hands.ReportResult||!t.hands.ReleaseGrant||!t.routing.AuthorizeRecognizer||!t.routing.RevokeRecognizer||!t.inventory.GetExecutor||!t.inventory.RequestSelection||!t.inventory.CopySelection||!t.inventory.ReleaseSelection||!t.transactions.CopyHeldItem||!t.transactions.RequestTransfer||!t.transactions.CopyTransfer||!t.transactions.CancelTransfer)return Status::InvalidArgument;
    constexpr auto needed=HIGGS_INVENTORY_CAP_PLAYER_SELECTION|HIGGS_INVENTORY_CAP_DROP_TO_HAND|HIGGS_INVENTORY_CAP_STORE_HELD;
    if((t.inventory.capabilities&needed)!=needed)return Status::Unavailable;
    GameIntegration game{};game.structSize=sizeof(game);if(t.game->CopyIntegration(&game)!=Status::Ok||!game.connected||!game.higgsRecognizerClient)return Status::Unavailable;
    {std::lock_guard lock(mutex_);tables_=t;config_=c;recognizer_=game.higgsRecognizerClient;stopping_=true;}
    auto status=t.hands.RegisterExecutor(c.higgsClient,Selected,this,&actionExecutor_);if(status){progress_.handStatus=status;return Status::Unavailable;}
    status=t.routing.AuthorizeRecognizer(c.higgsClient,actionExecutor_,recognizer_,c.physicalHands);if(status){if(Released(t.hands.RemoveRegistration(c.higgsClient,actionExecutor_)))actionExecutor_=0;progress_.handStatus=status;return Status::Unavailable;}
    SourceBehaviorSpec spec{sizeof(spec),0,c.instance,this,Offers,Execute,c.higgsClient,actionExecutor_};
    const auto registered=t.behaviors->RegisterSourceBehavior(c.writer,&spec,&behavior_);
    if(registered!=Status::Ok){t.routing.RevokeRecognizer(c.higgsClient,actionExecutor_,recognizer_);if(Released(t.hands.RemoveRegistration(c.higgsClient,actionExecutor_)))actionExecutor_=0;return registered;}
    {std::lock_guard lock(mutex_);attached_=true;stopping_=false;progress_={};progress_.stage=InventoryProviderStage::Ready;}return Status::Ok;
}
void HIGGS_HAND_CALL OrdinaryInventoryProvider::Selected(const HIGGS_ActionRequest* request,void* context){if(!request||!context)return;try{auto& self=*static_cast<OrdinaryInventoryProvider*>(context);if(request->physicalHands!=1&&request->physicalHands!=2)return;std::lock_guard lock(self.mutex_);self.selected_[request->physicalHands-1]=*request;}catch(...){}}
Status BS_CALL OrdinaryInventoryProvider::Offers(void* p,const SourceActionContext* c,SourceActionProposal* out,std::uint32_t cap,std::uint32_t* n){if(!p||!c||!n||(cap&&!out))return Status::InvalidArgument;try{return static_cast<OrdinaryInventoryProvider*>(p)->Offer(*c,out,cap,n);}catch(...){return Status::InternalError;}}
Status OrdinaryInventoryProvider::Offer(const SourceActionContext& c,SourceActionProposal* out,std::uint32_t cap,std::uint32_t* n){
    InventoryProviderConfig config;{std::lock_guard lock(mutex_);if(!attached_||stopping_||work_.selected.request.requestId){*n=0;return Status::Ok;}config=config_;}
    const auto& input=c.context.input;if(!(config.physicalHands&input.physicalHand)||input.edge!=HIGGS_INPUT_DOWN||c.context.slot.contentVersion==UINT64_MAX){*n=0;return Status::Ok;}
    OrdinaryItemPlan plan{};if(!config.policy(config.policyContext,c,plan)){*n=0;return Status::Ok;}
    if(!plan.quantity||plan.quantity>static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())||!Projection(plan.after))return Status::InvalidArgument;
    std::uint32_t source=0;HIGGS_ItemIdentity heldItem{};
    if(plan.transferKind==HIGGS_TRANSFER_INVENTORY_TO_HAND){if(!plan.baseFormId||input.handOccupancy!=HIGGS_OCCUPANCY_EMPTY||(input.handObservationMask&(HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN))!=(HIGGS_OBS_HELD_KNOWN|HIGGS_OBS_EQUIPMENT_KNOWN)||input.heldRefId||input.equippedFormId){*n=0;return Status::Ok;}}
    else if(plan.transferKind==HIGGS_TRANSFER_HAND_TO_INVENTORY){const auto& held=c.items[0];if(held.source!=static_cast<std::uint32_t>(IncomingItemSource::HiggsHeld)||!held.known||!held.allowed||!held.referenceId){*n=0;return Status::Ok;}heldItem.structSize=sizeof(heldItem);const auto copied=tables_.transactions.CopyHeldItem(config.higgsClient,input.physicalHand,&heldItem);constexpr auto fields=HIGGS_ITEM_REFERENCE_KNOWN|HIGGS_ITEM_QUANTITY_KNOWN|HIGGS_ITEM_NATIVE_OBSERVED;if(copied||heldItem.structSize!=sizeof(heldItem)||heldItem.kind!=HIGGS_ITEM_WORLD_REFERENCE||!heldItem.instance||!heldItem.generation||heldItem.epoch!=input.epoch||held.epoch!=input.epoch||heldItem.worldRefId!=held.referenceId||heldItem.worldRefId!=input.heldRefId||(heldItem.validFields&fields)!=fields||heldItem.quantity!=plan.quantity||(held.baseFormId&&(!(heldItem.validFields&HIGGS_ITEM_BASE_KNOWN)||heldItem.baseFormId!=held.baseFormId))){*n=0;return Status::Ok;}source=held.source;}
    else return Status::InvalidArgument;
    if(!cap){*n=1;return Status::BufferTooSmall;}
    std::lock_guard lock(mutex_);if(!attached_||stopping_||work_.selected.request.requestId){*n=0;return Status::Ok;}if(nextOffer_==UINT64_MAX)return Status::LimitExceeded;
    const auto token=nextOffer_++;offers_[token%offers_.size()]={token,input.epoch,input.inputSequence,c.context.slot.contentVersion,plan,heldItem};
    out[0]={{plan.transferKind,input.physicalHand,config.priority,0,token},source,0};*n=1;return Status::Ok;
}
Status BS_CALL OrdinaryInventoryProvider::Execute(void* p,const SourceActionRequest* r){if(!p||!r)return Status::InvalidArgument;try{return static_cast<OrdinaryInventoryProvider*>(p)->Queue(*r);}catch(...){return Status::InternalError;}}
Status OrdinaryInventoryProvider::Queue(const SourceActionRequest& r){
    std::lock_guard lock(mutex_);const auto& action=r.request;const auto hand=action.grant.physicalHand;
    if(!attached_||stopping_||work_.selected.request.requestId||hand<1||hand>2||action.offer.higgsClient!=config_.higgsClient||action.offer.higgsExecutor!=actionExecutor_||action.offer.context.slot.configured.instance.instanceHandle!=config_.instance)return Status::Conflict;
    const auto token=action.offer.proposal.executorAction;const auto& cached=offers_[token%offers_.size()];if(!token||cached.token!=token||cached.epoch!=action.grant.epoch||cached.input!=action.grant.inputSequence||cached.contentVersion!=action.offer.context.slot.contentVersion||!Same(selected_[hand-1],r,config_.higgsClient,actionExecutor_))return Status::Conflict;
    if(cached.plan.transferKind==HIGGS_TRANSFER_HAND_TO_INVENTORY&&(!r.incoming.known||!r.incoming.allowed||r.incoming.source!=1||!r.incoming.referenceId))return Status::Conflict;
    work_={};work_.selected=r;work_.plan=cached.plan;work_.held=cached.held;work_.authority=selected_[hand-1];progress_={};progress_.stage=InventoryProviderStage::Queued;progress_.slotRequest=action.requestId;progress_.higgsAction=action.grant.requestId;return Status::Ok;
}
bool OrdinaryInventoryProvider::OriginalResult(const Work& w,HIGGS_ActionResult& out,InventoryProviderProgress& progress){out.structSize=sizeof(out);progress.handStatus=tables_.hands.CopyActionResult(config_.higgsClient,w.authority.requestId,&out);return !progress.handStatus&&out.requestId==w.authority.requestId&&out.epoch==w.authority.epoch&&out.inputId==w.authority.inputId&&out.executor==actionExecutor_;}
bool OrdinaryInventoryProvider::ReportOriginal(const Work& w,std::uint32_t stage,std::uint32_t flags,InventoryProviderProgress& p){HIGGS_ActionResult previous{};if(!OriginalResult(w,previous,p))return false;flags|=previous.receiptFlags;if((previous.stage==stage||(stage==HIGGS_ACTION_COMMITTED&&previous.stage==HIGGS_ACTION_COMPLETED))&&(previous.receiptFlags&flags)==flags)return true;if(previous.stage>=HIGGS_ACTION_COMPLETED)return false;HIGGS_ActionResult result=previous;result.stage=stage;result.receiptFlags=flags;result.itemRefId=p.receipt.actualRefId;result.quantity=p.receipt.quantity;result.reason=p.receipt.reason;p.handStatus=tables_.hands.ReportResult(config_.higgsClient,&result);return p.handStatus==HIGGS_HAND_OK;}
Status OrdinaryInventoryProvider::Mirror(const Work& w,CommitStage stage,const ContentProjection& content,std::uint64_t version){const auto& r=w.selected.request;ContentCommit receipt{};receipt.structSize=sizeof(receipt);receipt.stage=static_cast<std::uint32_t>(stage);receipt.requestId=r.requestId;receipt.epoch=r.grant.epoch;receipt.higgsRequestId=r.grant.requestId;receipt.higgsGrantId=r.grant.grantId;receipt.instanceHandle=config_.instance;receipt.expectedContentVersion=r.offer.context.slot.contentVersion;receipt.contentVersion=version;receipt.contentIdentity=content.identity;receipt.quantity=content.quantity;return tables_.slots->ReportContentCommit(config_.writer,&receipt);}
bool OrdinaryInventoryProvider::Finish(Work& w,InventoryProviderProgress& p,bool success){
    const auto& slot=w.selected.request.offer.context.slot;const auto flags=Evidence(p.receipt);
    if(success){
        if(!w.mirror){if(!ReportOriginal(w,HIGGS_ACTION_COMMITTED,flags,p))return false;p.slotStatus=Mirror(w,CommitStage::Committed,w.plan.after,slot.contentVersion+1);if(p.slotStatus!=Status::Ok)return false;w.mirror=true;}
        if(!ReportOriginal(w,HIGGS_ACTION_COMPLETED,flags,p))return false;
        if(!w.completed){p.slotStatus=Mirror(w,CommitStage::Completed,w.plan.after,slot.contentVersion+1);if(p.slotStatus!=Status::Ok)return false;w.completed=true;}
    }else{
        HIGGS_ActionResult original{};if(!OriginalResult(w,original,p))return false;
        const bool rejected=original.stage==HIGGS_ACTION_REJECTED;if(!rejected&&original.stage!=HIGGS_ACTION_CANCELLED&&!ReportOriginal(w,HIGGS_ACTION_CANCELLED,flags,p))return false;
        if(!w.completed){p.slotStatus=Mirror(w,rejected?CommitStage::Failed:CommitStage::Cancelled,{slot.contentIdentity,slot.quantity},slot.contentVersion);if(p.slotStatus!=Status::Ok)return false;w.completed=true;}
    }
    if(w.ticket){p.handStatus=tables_.inventory.ReleaseSelection(config_.higgsClient,w.ticket);if(!Released(p.handStatus))return false;w.ticket=0;}
    p.handStatus=tables_.hands.ReleaseGrant(config_.higgsClient,w.authority.grant);if(!Released(p.handStatus))return false;
    p.stage=success?InventoryProviderStage::Completed:InventoryProviderStage::Cancelled;w={};return true;
}
void OrdinaryInventoryProvider::Advance(Work& w,InventoryProviderProgress& p){
    HIGGS_ActionResult original{};const bool readable=OriginalResult(w,original,p);
    if(!w.transfer){
        if(!readable||original.stage==HIGGS_ACTION_CANCELLED||original.stage==HIGGS_ACTION_REJECTED||w.cancel){if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}
        HIGGS_HandSnapshot snapshot{};snapshot.structSize=sizeof(snapshot);p.handStatus=tables_.hands.CopySnapshot(config_.higgsClient,&snapshot);
        const auto hand=w.authority.physicalHands;if(p.handStatus||snapshot.epoch!=w.authority.epoch||snapshot.hands[hand-1].owner!=config_.higgsClient||snapshot.hands[hand-1].grant!=w.authority.grant){w.cancel=true;if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}
        HIGGS_ItemIdentity item{};item.structSize=sizeof(item);
        if(w.plan.transferKind==HIGGS_TRANSFER_INVENTORY_TO_HAND){
            if(!w.ticket){HIGGS_InventorySelectionRequest request{};request.structSize=sizeof(request);request.baseFormId=w.plan.baseFormId;request.epoch=w.authority.epoch;request.stackOrdinal=w.plan.stackOrdinal;request.quantity=w.plan.quantity;p.handStatus=tables_.inventory.RequestSelection(config_.higgsClient,&request,&w.ticket);if(p.handStatus){w.cancel=true;if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}}
            p.selectionTicket=w.ticket;HIGGS_InventorySelection selection{};selection.structSize=sizeof(selection);p.handStatus=tables_.inventory.CopySelection(config_.higgsClient,w.ticket,&selection);
            if(p.handStatus||selection.epoch!=w.authority.epoch||selection.state==HIGGS_SELECTION_INVALID){p.receipt.reason=selection.reason?selection.reason:p.handStatus;w.cancel=true;if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}
            if(selection.state==HIGGS_SELECTION_QUEUED){p.stage=InventoryProviderStage::Selecting;return;}if(selection.state!=HIGGS_SELECTION_READY||selection.item.kind!=HIGGS_ITEM_NATIVE_INVENTORY){p.stage=InventoryProviderStage::RecoveryRequired;return;}item=selection.item;
        }else{p.handStatus=tables_.transactions.CopyHeldItem(config_.higgsClient,hand,&item);if(p.handStatus||!SameItem(item,w.held)||item.worldRefId!=w.selected.incoming.referenceId||(w.selected.incoming.baseFormId&&item.baseFormId!=w.selected.incoming.baseFormId)){p.receipt.reason=p.handStatus?p.handStatus:HIGGS_HAND_STALE;w.cancel=true;if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}}
        if(item.quantity!=w.plan.quantity){w.cancel=true;if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}
        HIGGS_HandExecutor transferExecutor=0;p.handStatus=tables_.inventory.GetExecutor(config_.higgsClient,&transferExecutor);if(p.handStatus){w.cancel=true;if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}
        auto& r=w.transferRequest;r={};r.structSize=sizeof(r);r.kind=w.plan.transferKind;r.epoch=w.authority.epoch;r.idempotencyKey=w.authority.requestId;r.original=item;r.executor=transferExecutor;r.grant=w.authority.grant;r.quantity=w.plan.quantity;r.timeoutMs=config_.timeoutMs;r.contextGeneration=w.selected.request.offer.context.slot.contentVersion;r.sourceResource=r.destinationResource=config_.instance;
        if(r.kind==HIGGS_TRANSFER_INVENTORY_TO_HAND)r.destinationHand=hand;else r.sourceHand=hand;
        p.handStatus=tables_.transactions.RequestTransfer(config_.higgsClient,&r,&w.transfer);if(p.handStatus){w.cancel=true;if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}p.transfer=w.transfer;
    }
    if(w.cancel){p.handStatus=tables_.transactions.CancelTransfer(config_.higgsClient,w.transfer,w.authority.epoch);}
    HIGGS_TransferState state{};state.structSize=sizeof(state);p.handStatus=tables_.transactions.CopyTransfer(config_.higgsClient,w.transfer,&state);
    if(p.handStatus||state.owner!=config_.higgsClient||state.grant!=w.authority.grant||state.epoch!=w.authority.epoch||state.idempotencyKey!=w.authority.requestId){p.stage=InventoryProviderStage::RecoveryRequired;return;}p.receipt=state;
    if(state.stage==HIGGS_TX_RECOVERY_REQUIRED||((state.receiptFlags&HIGGS_TX_MUTATION_UNCERTAIN)&&!(state.receiptFlags&HIGGS_TX_RECOVERY_RECONCILED))){p.stage=InventoryProviderStage::RecoveryRequired;return;}
    if(state.stage==HIGGS_TX_COMPLETED){const bool restored=(state.receiptFlags&HIGGS_TX_COMPENSATION_CONFIRMED)!=0;const auto nativeFlag=w.plan.transferKind==HIGGS_TRANSFER_INVENTORY_TO_HAND?HIGGS_TX_NATIVE_GRIP_CONFIRMED:HIGGS_TX_NATIVE_RELEASE_CONFIRMED;const std::uint32_t expected=w.plan.transferKind==HIGGS_TRANSFER_INVENTORY_TO_HAND?HIGGS_DISPOSITION_HAND:HIGGS_DISPOSITION_INVENTORY;const bool success=!restored&&state.disposition==expected&&(state.receiptFlags&HIGGS_TX_DOMAIN_ATTESTED)&&(state.receiptFlags&nativeFlag);p.stage=InventoryProviderStage::Reporting;if(!Finish(w,p,success))p.stage=InventoryProviderStage::RecoveryRequired;return;}
    if(state.stage==HIGGS_TX_REJECTED||state.stage==HIGGS_TX_CANCELLED){if(!Finish(w,p,false))p.stage=InventoryProviderStage::RecoveryRequired;return;}
    if(state.receiptFlags&HIGGS_TX_DOMAIN_ATTESTED){if(!ReportOriginal(w,HIGGS_ACTION_COMMITTED,Evidence(state),p)){p.stage=InventoryProviderStage::RecoveryRequired;return;}}
    p.stage=InventoryProviderStage::Transferring;
}
InventoryProviderProgress OrdinaryInventoryProvider::Tick(){Work work;InventoryProviderProgress progress;{std::lock_guard lock(mutex_);if(!work_.selected.request.requestId)return progress_;work=work_;progress=progress_;}try{Advance(work,progress);}catch(...){progress.stage=InventoryProviderStage::RecoveryRequired;progress.handStatus=HIGGS_HAND_INTERNAL_ERROR;}{std::lock_guard lock(mutex_);work_=work;progress_=progress;}return progress;}
InventoryProviderProgress OrdinaryInventoryProvider::CopyProgress()const{std::lock_guard lock(mutex_);return progress_;}
void OrdinaryInventoryProvider::Cancel(){std::lock_guard lock(mutex_);work_.cancel=true;}
Status OrdinaryInventoryProvider::Detach(){
    {std::lock_guard lock(mutex_);if(work_.selected.request.requestId)return Status::Conflict;stopping_=true;}
    if(behavior_){const auto s=tables_.slots->RemoveBehavior(config_.writer,behavior_);if(s!=Status::Ok&&s!=Status::NotFound)return s;behavior_=0;}
    if(actionExecutor_){const auto revoked=tables_.routing.RevokeRecognizer(config_.higgsClient,actionExecutor_,recognizer_);if(revoked!=HIGGS_HAND_OK&&revoked!=HIGGS_HAND_NOT_FOUND)return Status::Unavailable;const auto removed=tables_.hands.RemoveRegistration(config_.higgsClient,actionExecutor_);if(removed!=HIGGS_HAND_OK&&removed!=HIGGS_HAND_NOT_FOUND)return Status::Unavailable;actionExecutor_=0;}
    {std::lock_guard lock(mutex_);attached_=false;progress_.stage=InventoryProviderStage::Detached;}return Status::Ok;
}
}
