// SPDX-License-Identifier: MIT
// Test-only consumer. No engine hooks, provider internals, synthetic input or NPC epochs.
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <Windows.h>
#include "DevBenchAPI.h"
#include "higgsinterface001.h"
#include "higgshandapi.h"
#include "higgsinput.h"
#include "planck_synergy.h"
#include "body_slots_configuration_api.h"
#include "body_slots_region_actions_api.h"
#include "body_slots_region_continuation_api.h"
#include "body_slots_region_sources_api.h"
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace vr_interaction_framework_test {
using Json = nlohmann::json;
using namespace body_slots;
constexpr char provider[] = "VRInteractionFrameworkTest";
constexpr char inspectKey[] = "vr_interaction_framework_test";
constexpr char commandTool[] = "vr_interaction_framework_test_command";
constexpr std::uint64_t action = 0x5652494654455354;
std::mutex mirrorMutex, callbackMutex;
Json mirror = {{"schemaVersion",1},{"state","loading"}}, commandResults = Json::array();
std::deque<Json> commands;
std::deque<RegionActionExecution> executions;
std::deque<RegionInputTerminal> terminals;
std::uint64_t highestCommand{}, processedCommand{}, statusSequence{}, frame{}, generation{};
std::atomic<bool> armed{false};
std::atomic<std::uint32_t> armedHand{Right};
std::atomic<std::uint64_t> armedBinding{};
std::atomic<std::uint64_t> offered{}, callbackOverflow{}, saveCount{}, preLoadCount{}, postLoadCount{}, newGameCount{};
std::atomic<bool> loadSucceeded{};
std::string birth, error;
std::filesystem::path directory;
const Api* registry{};
const ConfigurationApi* configuration{};
const RegionsApi* regions{};
const RegionActionsApi* actions{};
const RegionContinuationApi* continuation{};
const RegionSourcesApi* sources{};
const PlanckSynergyAPI* planck{};
HIGGS_HandInterface hands{};
HIGGS_HandRoutingInterface routing{};
HIGGS_InputRoutesInterface routes{};
std::uint64_t writer{}, customizer{};
HIGGS_HandClient handClient{};
PlanckToken planckClient{};
HIGGS_HandSnapshot snapshot{};
RegionActionsIntegration integration{sizeof(integration),1};
InteractionRegionRecord worldRegion{}, npcRegion{};
RegionActionRegistration worldBinding{}, npcBinding{};
RegionAnchorSnapshot worldAnchor{};
RegionPlanckBody originalBody{};
PlanckActorPhysics liveActor{};
PlanckBodyPhysics liveBody{};
std::uint32_t livePlanckStatus = PLANCK_NOT_FOUND;
RegionObservation worldObservation{}, npcObservation{};
std::uint32_t worldObservationStatus = 8, npcObservationStatus = 8;
std::uint32_t integrationStatus=8,snapshotStatus=HIGGS_HAND_NOT_READY;
std::uint64_t anchorRevision{}, executed{}, completed{}, reportFailures{}, terminalCount{}, matchedRelease{}, cancelled{}, lost{}, mismatch{};
std::uint64_t npcAvailable{}, npcUnavailable{}, npcReplacementObserved{}, npcRebinds{};
std::uint64_t npcUnavailableAfterReplacement{}, npcResetIssued{}, capturedPalmPose{}, capturedPalmAtNs{}, capturedPalmGeneration{};
std::uint32_t fixtureActorForm{}, fixtureActorHandle{};
bool replacementSeen{}, npcWasAvailable{};
std::uint64_t lastPress{}, lastRequest{}, lastEpoch{}, lastBinding{}, lastCorrelation{}, lastTerminalInput{};
std::uint32_t lastReportStatus = HIGGS_HAND_NOT_READY;
HIGGS_ActionResult lastActionResult{};
HIGGS_InputDecision lastDecision{};
std::deque<RegionActionExecution> winners;
double expires{};
std::uint32_t devbenchBuild{};
bool inspectRegistered{}, commandRegistered{}, phaseRegistered{}, taskPending{}, definitionRegistered{}, tasksAvailable{};
EffectiveSlotRecord effective{};
std::uint32_t effectiveStatus=8;
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
template <std::size_t N> void Copy(char (&dest)[N], const char* text) { strncpy_s(dest,text,_TRUNCATE); }
std::uint32_t Code(Status status) { return static_cast<std::uint32_t>(status); }
void Identity(float* r) { r[0]=r[4]=r[8]=1; }
Json Source(const RegionSampleSource& s) {
    return {{"provider",s.provider},{"source",s.source},{"identity",s.identity},{"generation",s.generation},
        {"sampleId",s.sampleId},{"epoch",s.epoch},{"saveGeneration",s.saveGeneration},{"frame",s.frame},{"timeSeconds",s.timeSeconds}};
}
Json Observation(const RegionObservation& o,std::uint32_t status) {
    return {{"status",status},{"state",o.state},{"unavailableReason",o.unavailableReason},{"sequence",o.sequence},
        {"regionId",o.regionId},{"anchor",Source(o.anchor.source)},{"probe",Source(o.probe.source)},
        {"anchorValid",o.anchor.valid},{"probeValid",o.probe.valid},
        {"anchorWorld",{o.anchor.world.position[0],o.anchor.world.position[1],o.anchor.world.position[2]}},
        {"probeWorld",{o.probe.world[0],o.probe.world[1],o.probe.world[2]}}};
}
void ReadEffective() {
    if (!configuration || !writer) return;
    std::array<EffectiveSlotRecord,kMaxDefinitions> records{}; std::uint32_t count{}; std::uint64_t revision{};
    effectiveStatus=Code(configuration->CopyEffectiveDefinitions(writer,0,records.data(),static_cast<std::uint32_t>(records.size()),&count,&revision));
    if(effectiveStatus) return;
    effectiveStatus=Code(Status::NotFound);
    for(std::uint32_t i=0;i<count;++i) if(!strcmp(records[i].slot.definition.provider,provider)&&!strcmp(records[i].slot.definition.key,"saved_position")) {
        effective=records[i]; effectiveStatus=0; break;
    }
}
Status BS_CALL Offers(void*,const RegionActionContext* c,RegionActionProposal* p,std::uint32_t capacity,std::uint32_t* count) noexcept {
    if(!c||!count) return Status::InvalidArgument;
    *count=0;
    if(!armed.load() || c->bindingId!=armedBinding.load() || c->input.physicalHand!=armedHand.load() || c->input.edge!=HIGGS_INPUT_DOWN) return Status::Ok;
    if(!p||!capacity) return Status::BufferTooSmall;
    p[0]={sizeof(*p),c->input.physicalHand,0,500,action,0}; *count=1; ++offered;
    return Status::Ok;
}
Status BS_CALL Execute(void*,const RegionActionExecution* e) noexcept {
    try {
        if(!e||e->context.structSize!=sizeof(e->context)) return Status::InvalidArgument;
        std::lock_guard lock(callbackMutex);
        if(executions.size()>=32) { ++callbackOverflow; return Status::Unavailable; }
        executions.push_back(*e); // This bounded logical test action is the factual domain effect.
        return Status::Ok;
    } catch(...) { ++callbackOverflow; return Status::InternalError; }
}
void BS_CALL Ended(void*,const RegionInputTerminal* terminal) noexcept {
    try { if(!terminal||terminal->structSize!=sizeof(*terminal)) return; std::lock_guard lock(callbackMutex);
        if(terminals.size()>=32) {++callbackOverflow;return;} terminals.push_back(*terminal);
    } catch(...) {++callbackOverflow;}
}
Status NewBinding(const char* key,RegionAnchorKind kind,std::uint32_t hand,InteractionRegionRecord& region,RegionActionRegistration& binding) {
    if(binding.bindingId) {
        const auto removed=actions->RemoveBinding(writer,binding.bindingId);
        if(removed!=Status::Ok) return removed;
        binding={};
    }
    InteractionRegionSpec spec{}; spec.structSize=sizeof(spec); spec.anchorKind=static_cast<std::uint32_t>(kind);
    Copy(spec.provider,provider); Copy(spec.key,key); spec.shape.structSize=sizeof(spec.shape);
    spec.shape.kind=static_cast<std::uint32_t>(ZoneShapeKind::Sphere); spec.shape.radius=24;
    spec.exitOutset=4; spec.maxSampleAgeSeconds=.25; spec.maxSampleSkewSeconds=.25; spec.maxContinuityGapSeconds=.25;
    auto result=regions->UpsertRegion(writer,&spec,region.revision,&region); if(result!=Status::Ok) return result;
    RegionActionBindingSpec b{sizeof(b),hand,HIGGS_INPUT_GRIP,HIGGS_INPUT_DOWN,region.regionId,region.revision,handClient,Offers,Execute,nullptr};
    binding.structSize=sizeof(binding); result=actions->RegisterBinding(writer,&b,&binding); if(result!=Status::Ok) return result;
    result=continuation->Subscribe(writer,binding.bindingId,Ended);
    if(result==Status::Ok && routing.AuthorizeRecognizer(handClient,binding.higgsExecutor,binding.recognizerClient,hand)!=HIGGS_HAND_OK) result=Status::Unavailable;
    if(result!=Status::Ok) { actions->RemoveBinding(writer,binding.bindingId); binding={}; }
    return result;
}
bool Ready() { return registry&&configuration&&regions&&actions&&continuation&&sources&&writer&&customizer&&handClient&&planckClient&&phaseRegistered&&definitionRegistered&&routes.CopySequence&&tasksAvailable; }
Status CopyNpc(std::uint32_t handle,std::uint32_t index) {
    if(!planck||!planckClient) return Status::Unavailable;
    std::array<PlanckBodyPhysics,256> bodies{}; for(auto& b:bodies)b.size=sizeof(b);
    PlanckActorPhysics actor{}; actor.size=sizeof(actor); std::uint32_t copied{};
    livePlanckStatus=planck->CopyActorPhysics(planckClient,handle,&actor,bodies.data(),static_cast<std::uint32_t>(bodies.size()),&copied);
    if(livePlanckStatus!=PLANCK_OK) {liveActor={};liveBody={};return Status::Unavailable;}
    liveActor=actor; liveBody={};
    if(copied>bodies.size())return Status::InternalError;
    for(std::uint32_t i=0;i<copied;++i) if(bodies[i].body.index==index){liveBody=bodies[i];return Status::Ok;}
    return Status::NotFound;
}
RE::Actor* FixtureActor() {
    auto* data=RE::TESDataHandler::GetSingleton();
    auto* actor=data?data->LookupForm<RE::Actor>(0x801,"test_VRInteractionFramework.esp"):nullptr;
    fixtureActorForm=actor?actor->GetFormID():0;
    fixtureActorHandle=actor?actor->GetHandle().native_handle():0;
    return actor;
}
Status Command(const Json& c) {
    const auto op=c.at("command").get<std::string>();
    if(op=="read") {ReadEffective();return static_cast<Status>(effectiveStatus);}
    if(!Ready()) return Status::Unavailable;
    if(op=="disarm") {armed=false;return Status::Ok;}
    if(op=="reset_fixture_npc") {
        if(!snapshot.worldReady||!integration.connected)return Status::Unavailable;
        auto* actor=FixtureActor();
        if(!actor||!actor->Get3D()||!originalBody.actorHandle||fixtureActorHandle!=originalBody.actorHandle)return Status::Unavailable;
        // Only the fixture's exact placed ACHR may receive this actual engine stimulus.
        // Issuing it proves nothing about PLANCK replacement: observe its copied identity.
        actor->DoReset3D(false);++npcResetIssued;return Status::Ok;
    }
    if(op=="set_position") {
        const auto v=c.at("position").get<std::array<float,3>>();
        for(float f:v)if(!std::isfinite(f)||std::abs(f)>1000)return Status::InvalidArgument;
        ReadEffective(); if(effectiveStatus) return static_cast<Status>(effectiveStatus);
        Customization edit{};edit.structSize=sizeof(edit);edit.fieldMask=CustomizePosition;
        Copy(edit.provider,provider);Copy(edit.key,"saved_position");std::copy(v.begin(),v.end(),edit.position);
        const auto result=configuration->ApplyCustomization(customizer,&edit,effective.configurationRevision,&effective);ReadEffective();return result;
    }
    const auto hand=c.value("hand",Right);if(hand!=Left&&hand!=Right)return Status::InvalidArgument;
    if(snapshotStatus!=HIGGS_HAND_OK||integrationStatus||!snapshot.worldReady || !integration.connected || !integration.epoch || snapshot.epoch!=integration.epoch) return Status::Unavailable;
    if(op=="arm_world") {
        const auto& palm=snapshot.hands[hand==Left?0:1];
        if(!(palm.observationMask&HIGGS_OBS_PALM_VALID)||!(palm.observationMask&HIGGS_OBS_TRACKING_VALID)||!palm.poseObservationId)return Status::Unavailable;
        armed=false;auto status=NewBinding("world_grip",RegionAnchorKind::World,hand,worldRegion,worldBinding);if(status!=Status::Ok)return status;
        worldAnchor={};worldAnchor.structSize=sizeof(worldAnchor);worldAnchor.kind=static_cast<std::uint32_t>(RegionAnchorKind::World);worldAnchor.valid=1;
        worldAnchor.world.scale=1;Identity(worldAnchor.world.rotation);std::copy(palm.palmPosition,palm.palmPosition+3,worldAnchor.world.position);
        capturedPalmPose=palm.poseObservationId;capturedPalmAtNs=palm.poseObservedAtNs;capturedPalmGeneration=palm.generation;
        anchorRevision=0;++generation;armedHand=hand;armedBinding=worldBinding.bindingId;armed=true;expires=Now()+30;return Status::Ok;
    }
    if(op=="bind_npc" || op=="rebind_npc" || op=="bind_fixture_npc" || op=="rebind_fixture_npc") {
        std::uint32_t handle{};
        if(op=="bind_fixture_npc"||op=="rebind_fixture_npc") {if(!FixtureActor())return Status::NotFound;handle=fixtureActorHandle;}
        else handle=c.at("actorHandle").get<std::uint32_t>();
        const auto index=c.value("bodyIndex",0u);
        if(!handle)return Status::InvalidArgument;
        auto status=CopyNpc(handle,index);if(status!=Status::Ok)return status;
        armed=false;status=NewBinding("npc_grip",RegionAnchorKind::Npc,hand,npcRegion,npcBinding);if(status!=Status::Ok)return status;
        const auto& b=liveBody.body;
        originalBody={sizeof(originalBody),b.actor.handle,b.index,0,b.actor.epoch,b.actor.actorGeneration,b.bodyGeneration};
        status=sources->BindPlanckBody(writer,npcBinding.bindingId,&originalBody);
        if(status==Status::Ok) {
            if(op=="rebind_npc"||op=="rebind_fixture_npc")++npcRebinds;
            replacementSeen=false;npcWasAvailable=false;armedHand=hand;armedBinding=npcBinding.bindingId;armed=true;expires=Now()+30;
        }
        return status;
    }
    return Status::InvalidArgument;
}
void ProcessCommands(std::deque<Json> pending) noexcept {
    for(const auto& c:pending) {
        std::uint32_t status=Code(Status::InternalError);std::string message;
        try {
            if(actions&&writer)integrationStatus=Code(actions->CopyIntegration(writer,&integration));
            if(handClient){snapshot.structSize=sizeof(snapshot);snapshotStatus=hands.CopySnapshot(handClient,&snapshot);if(snapshotStatus!=HIGGS_HAND_OK){snapshot={};snapshot.structSize=sizeof(snapshot);}}
            if(Now()>c.at("_fixtureDeadline").get<double>()) {status=Code(Status::Unavailable);message="five-second command deadline expired; discarded";}
            else if(integrationStatus || c.at("expectedBirth").get<std::string>()!=birth || c.at("expectedEpoch").get<std::uint64_t>()!=integration.epoch) {status=Code(Status::Conflict);message="process or world scope changed";}
            else status=Code(Command(c));
        } catch(const std::exception& e) {status=Code(Status::InvalidArgument);message=e.what();}
        std::lock_guard lock(mirrorMutex); processedCommand=c.at("id").get<std::uint64_t>();
        commandResults.push_back({{"id",processedCommand},{"command",c.value("command","")},{"status",status},{"message",message}});
        if(commandResults.size()>32)commandResults.erase(commandResults.begin());
    }
    taskPending=false;
}
Json Enqueue(const Json& c) {
    if(!c.is_object()||!c.contains("id")||!c.at("id").is_number_unsigned()||!c.contains("command")||!c.at("command").is_string()||!c.contains("expectedBirth")||!c.contains("expectedEpoch"))
        return {{"accepted",false},{"error","id (positive uint64), command, expectedBirth and expectedEpoch required"}};
    const auto id=c.at("id").get<std::uint64_t>();std::lock_guard lock(mirrorMutex);
    if(mirror.value("state","")!="ready")return {{"accepted",false},{"error","fixture interfaces or main phase unavailable"}};
    if(!id||id<=highestCommand)return {{"accepted",false},{"error","id must advance"},{"highestCommand",highestCommand}};
    if(c.at("expectedBirth")!=birth)return {{"accepted",false},{"error","process birth mismatch"}};
    if(commands.size()>=16)return {{"accepted",false},{"error","queue full"}};
    auto queued=c;queued["_fixtureDeadline"]=Now()+5.0;
    commands.push_back(std::move(queued));highestCommand=id;return {{"accepted",true},{"id",id},{"pending",true},{"deadlineSeconds",5}};
}
void Inspect(void*,const char*,void* sink,DevBenchAPI::WriteFn write) noexcept {
    try {std::string result;{std::lock_guard lock(mirrorMutex);result=mirror.dump();}write(sink,result.c_str());}
    catch(...) {write(sink,"{\"error\":\"snapshot unavailable\"}");}
}
void Submit(void*,const char* args,void* sink,DevBenchAPI::WriteFn write) noexcept {
    try {if(!args||strlen(args)>4096){write(sink,"{\"accepted\":false,\"error\":\"arguments limit\"}");return;}const auto result=Enqueue(Json::parse(args)).dump();write(sink,result.c_str());}
    catch(...) {write(sink,"{\"accepted\":false,\"error\":\"invalid command JSON\"}");}
}
void WriteFile(const Json& value) noexcept {
    try {std::error_code ec;std::filesystem::create_directories(directory,ec);if(ec)return;
        const auto temp=directory/L"status.tmp",path=directory/L"status.json";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<value.dump(2);out.close();if(!out)return;}
        MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    } catch(...) {}
}
void Mirror() {
    const bool boundMatchesLive=livePlanckStatus==PLANCK_OK&&liveBody.size==sizeof(liveBody)&&originalBody.actorHandle&&
        liveBody.body.actor.handle==originalBody.actorHandle&&liveBody.body.index==originalBody.bodyIndex&&
        liveBody.body.actor.epoch==originalBody.worldEpoch&&liveBody.body.actor.actorGeneration==originalBody.actorGeneration&&liveBody.body.bodyGeneration==originalBody.bodyGeneration;
    const bool observationMatchesBound=!npcObservationStatus&&npcObservation.state&&
        npcObservation.anchor.source.identity==((std::uint64_t(originalBody.actorHandle)<<32)|originalBody.bodyIndex)&&
        npcObservation.anchor.source.generation==originalBody.bodyGeneration&&npcObservation.anchor.source.epoch==integration.epoch&&npcObservation.anchor.source.saveGeneration==integration.saveGeneration;
    Json j={{"schemaVersion",1},{"fixtureVersion","0.1.0"},{"runtimeSource","a9b3ac9"},{"pid",GetCurrentProcessId()},{"processBirth",birth},
        {"sequence",++statusSequence},{"frame",frame},{"state",Ready()?"ready":"unavailable"},{"error",error},
        {"epoch",integration.epoch},{"saveGeneration",integration.saveGeneration},{"integrationStatus",integrationStatus},{"snapshotStatus",snapshotStatus},{"higgsObservation",snapshot.observationId},{"higgsWorldReady",snapshot.worldReady},
        {"phaseRegistered",phaseRegistered},{"frameworkConnected",integration.connected},{"planckConnected",planckClient!=0},
        {"devbench",{{"build",devbenchBuild},{"inspectRegistered",inspectRegistered},{"commandRegistered",commandRegistered},{"inspectKind",inspectKey},{"commandTool",commandTool}}},
        {"input",{{"armed",armed.load()},{"hand",armedHand.load()},{"offered",offered.load()},{"executed",executed},{"reportedCompleted",completed},{"reportFailures",reportFailures},
            {"lastReportStatus",lastReportStatus},{"actualResultStage",lastActionResult.stage},{"decisionConsumed",lastDecision.consumed},{"decisionRequest",lastDecision.requestId},
            {"terminals",terminalCount},{"matchedReleases",matchedRelease},{"cancelled",cancelled},{"observationLost",lost},{"mismatches",mismatch},{"callbackOverflow",callbackOverflow.load()},
            {"lastPressInput",lastPress},{"lastRequest",lastRequest},{"lastBinding",lastBinding},{"lastEpoch",lastEpoch},{"lastCorrelation",lastCorrelation},{"lastTerminalInput",lastTerminalInput},{"armedBinding",armedBinding.load()}}},
        {"world",{{"binding",worldBinding.bindingId},{"anchorRevision",anchorRevision},{"capturedPalmPose",capturedPalmPose},{"capturedPalmAtNs",capturedPalmAtNs},{"capturedPalmGeneration",capturedPalmGeneration},{"capturedPosition",{worldAnchor.world.position[0],worldAnchor.world.position[1],worldAnchor.world.position[2]}},{"observation",Observation(worldObservation,worldObservationStatus)}}},
        {"npc",{{"binding",npcBinding.bindingId},{"actorHandle",originalBody.actorHandle},{"bodyIndex",originalBody.bodyIndex},{"boundWorldEpoch",originalBody.worldEpoch},{"boundActorGeneration",originalBody.actorGeneration},{"boundBodyGeneration",originalBody.bodyGeneration},
            {"boundMatchesLive",boundMatchesLive},{"observationMatchesBound",observationMatchesBound},
            {"copyStatus",livePlanckStatus},{"liveWorldEpoch",liveBody.body.actor.epoch},{"liveActorGeneration",liveBody.body.actor.actorGeneration},{"liveBodyGeneration",liveBody.body.bodyGeneration},{"liveSample",liveActor.sample},{"liveObservedAtNs",liveActor.observedAtNs},{"boneName",liveBody.boneName},
            {"availableObservations",npcAvailable},{"unavailableObservations",npcUnavailable},{"unavailableAfterReplacement",npcUnavailableAfterReplacement},{"replacementObserved",npcReplacementObserved},{"rebinds",npcRebinds},{"resetIssued",npcResetIssued},
            {"fixturePlugin","test_VRInteractionFramework.esp"},{"fixtureLocalRef",0x801},{"fixtureActorFormId",fixtureActorForm},{"fixtureActorHandle",fixtureActorHandle},{"observation",Observation(npcObservation,npcObservationStatus)}}},
        {"customization",{{"status",effectiveStatus},{"provider",provider},{"key","saved_position"},{"mask",effective.customizationMask},{"revision",effective.configurationRevision},
            {"position",{effective.slot.definition.position[0],effective.slot.definition.position[1],effective.slot.definition.position[2]}}}},
        {"lifecycle",{{"saves",saveCount.load()},{"preLoads",preLoadCount.load()},{"postLoads",postLoadCount.load()},{"newGames",newGameCount.load()},{"lastLoadSucceeded",loadSucceeded.load()}}}};
    {std::lock_guard lock(mirrorMutex);j["highestCommand"]=highestCommand;j["processedCommand"]=processedCommand;j["commandResults"]=commandResults;mirror=j;}
    if(frame%30==0||!phaseRegistered)WriteFile(j);
}
void Tick() noexcept {
    try {
        ++frame;
        if(actions&&writer)integrationStatus=Code(actions->CopyIntegration(writer,&integration));
        if(handClient){snapshot.structSize=sizeof(snapshot);snapshotStatus=hands.CopySnapshot(handClient,&snapshot);if(snapshotStatus!=HIGGS_HAND_OK){snapshot={};snapshot.structSize=sizeof(snapshot);}}
        if(armed && Now()>expires) armed=false;
        if(worldBinding.bindingId && armed && armedBinding==worldBinding.bindingId && snapshot.worldReady && snapshot.epoch==integration.epoch) {
            auto& a=worldAnchor.source;Copy(a.provider,provider);Copy(a.source,"fixed-world-from-observed-palm");
            a.identity=worldRegion.regionId;a.generation=generation;a.sampleId=snapshot.observationId;a.epoch=integration.epoch;a.saveGeneration=integration.saveGeneration;
            a.frame=snapshot.observationId;a.timeSeconds=Now();a.units=static_cast<std::uint32_t>(RegionUnits::SkyrimWorld);
            actions->PublishAnchor(writer,worldBinding.bindingId,anchorRevision,&worldAnchor,&anchorRevision);
        }
        std::deque<RegionActionExecution> done;std::deque<RegionInputTerminal> ended;
        {std::lock_guard lock(callbackMutex);done.swap(executions);ended.swap(terminals);}
        for(const auto& e:done) {
            ++executed;lastPress=e.request.inputId;lastRequest=e.request.requestId;lastEpoch=e.request.epoch;lastBinding=e.context.bindingId;
            winners.push_back(e);if(winners.size()>32)winners.pop_front();
            HIGGS_ActionResult result{};result.structSize=sizeof(result);result.stage=HIGGS_ACTION_COMPLETED;result.requestId=e.request.requestId;result.executor=e.request.executor;result.epoch=e.request.epoch;result.inputId=e.request.inputId;
            lastReportStatus=hands.ReportResult(handClient,&result);if(lastReportStatus==HIGGS_HAND_OK)++completed;else ++reportFailures;
            lastActionResult={};lastActionResult.structSize=sizeof(lastActionResult);hands.CopyActionResult(handClient,e.request.requestId,&lastActionResult);
            lastDecision={};lastDecision.structSize=sizeof(lastDecision);hands.CopyInputDecision(handClient,e.request.inputId,&lastDecision);
        }
        for(const auto& t:ended) {
            ++terminalCount;lastCorrelation=t.correlation;lastTerminalInput=t.terminalInput;
            const auto found=std::find_if(winners.begin(),winners.end(),[&](const auto& e){return e.request.requestId==t.request&&e.request.inputId==t.pressInput&&e.request.epoch==t.epoch&&e.request.actionId==t.action&&e.context.bindingId==t.binding&&e.context.input.physicalHand==t.hand&&e.context.input.button==t.button;});
            HIGGS_InputSequence sequence{};sequence.structSize=sizeof(sequence);
            const bool correlated=routes.CopySequence&&routes.CopySequence(handClient,t.pressInput,&sequence)==HIGGS_HAND_OK&&
                sequence.epoch==t.epoch&&sequence.pressInputId==t.pressInput&&sequence.requestId==t.request&&sequence.correlation==t.correlation&&
                sequence.actionId==t.action&&sequence.physicalHand==t.hand&&sequence.button==t.button&&
                found!=winners.end()&&sequence.executor==found->request.executor;
            if(t.end==static_cast<std::uint32_t>(RegionInputEnd::ObservationLost)){++lost;if(found!=winners.end())winners.erase(found);}
            else if(!correlated||!t.correlation)++mismatch;
            else if(t.end==static_cast<std::uint32_t>(RegionInputEnd::Released)&&sequence.phase==HIGGS_SEQUENCE_RELEASED&&sequence.terminalInputId==t.terminalInput&&t.terminalInput>t.pressInput){++matchedRelease;winners.erase(found);}
            else if(t.end==static_cast<std::uint32_t>(RegionInputEnd::Cancelled)&&sequence.phase==HIGGS_SEQUENCE_CANCELLED&&!t.terminalInput){++cancelled;winners.erase(found);}
            else ++mismatch;
        }
        if(worldBinding.bindingId)worldObservationStatus=Code(actions->CopyObservation(writer,worldBinding.bindingId,armedHand,&worldObservation));
        if(npcBinding.bindingId) {
            npcObservationStatus=Code(actions->CopyObservation(writer,npcBinding.bindingId,armedHand,&npcObservation));
            if(!npcObservationStatus){if(npcObservation.state){++npcAvailable;npcWasAvailable=true;}else {++npcUnavailable;if(replacementSeen&&npcWasAvailable)++npcUnavailableAfterReplacement;}}
            if(CopyNpc(originalBody.actorHandle,originalBody.bodyIndex)==Status::Ok &&
                (liveBody.body.actor.epoch!=originalBody.worldEpoch||liveBody.body.actor.actorGeneration!=originalBody.actorGeneration||liveBody.body.bodyGeneration!=originalBody.bodyGeneration)){++npcReplacementObserved;replacementSeen=true;}
        }
        ReadEffective();
        if(frame%10==0) {
            const auto path=directory/L"command.json";std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
            if(!ec&&size<=4096){std::ifstream in(path);try{Json c;in>>c;Enqueue(c);}catch(...){}}
        }
        if(!taskPending) {
            std::deque<Json> pending;{std::lock_guard lock(mirrorMutex);pending.swap(commands);}
            if(!pending.empty()){if(const auto* tasks=SKSE::GetTaskInterface()){taskPending=true;tasks->AddTask([pending=std::move(pending)]()mutable{ProcessCommands(std::move(pending));});}}
        }
        Mirror();
    } catch(const std::exception& e){error=e.what();} catch(...){error="main phase exception";}
}
template<class T> const T* Query(HMODULE module,const char* name) {
    const auto query=reinterpret_cast<const T* (BS_CALL*)(std::uint32_t,std::uint32_t)>(GetProcAddress(module,name));return query?query(1,sizeof(T)):nullptr;
}
void Connect() {
    const auto framework=GetModuleHandleW(L"BodySlots.dll"),higgs=GetModuleHandleW(L"higgs_vr.dll");
    if(framework){registry=Query<Api>(framework,"BodySlots_QueryInterface");configuration=Query<ConfigurationApi>(framework,"BodySlots_QueryConfigurationInterface");
        regions=Query<RegionsApi>(framework,"BodySlots_QueryRegionsInterface");actions=Query<RegionActionsApi>(framework,"BodySlots_QueryRegionActionsInterface");
        continuation=Query<RegionContinuationApi>(framework,"BodySlots_QueryRegionContinuationInterface");sources=Query<RegionSourcesApi>(framework,"BodySlots_QueryRegionSourcesInterface");}
    if(registry&&registry->RegisterClient(provider,&writer)==Status::Ok) {
        Definition definition{};definition.structSize=sizeof(definition);definition.flags=kEnabled;Copy(definition.provider,provider);Copy(definition.key,"saved_position");Copy(definition.anchor,"NPC Pelvis [Pelv]");
        Copy(definition.referenceRig,"skyrimvr-humanoid-v1");Copy(definition.kind,"test");Identity(definition.rotation);definition.radius=12;definition.handMask=Both;definition.contentMask=AllContent;
        SlotRecord record{};definitionRegistered=registry->UpsertDefinition(writer,&definition,0,&record)==Status::Ok;if(!definitionRegistered)error="definition registration failed";
    }
    if(configuration)configuration->RegisterCustomizer("VRInteractionFrameworkTest.Customizer",&customizer);
    if(higgs){const auto query=reinterpret_cast<HIGGS_QueryHandInterfaceFn>(GetProcAddress(higgs,"HIGGS_QueryHandInterface"));
        const auto route=reinterpret_cast<HIGGS_QueryHandRoutingInterfaceFn>(GetProcAddress(higgs,"HIGGS_QueryHandRoutingInterface"));
        const auto inputRoutes=reinterpret_cast<HIGGS_QueryInputRoutesInterfaceFn>(GetProcAddress(higgs,"HIGGS_QueryInputRoutesInterface"));
        if(inputRoutes)inputRoutes(1,sizeof(routes),&routes);
        if(query&&route&&query(HIGGS_HAND_ABI_VERSION,sizeof(hands),&hands)==HIGGS_HAND_OK&&route(HIGGS_HAND_ROUTING_ABI_VERSION,sizeof(routing),&routing)==HIGGS_HAND_OK){
            HIGGS_HandClientInfo info{};info.structSize=sizeof(info);Copy(info.name,provider);hands.RegisterClient(&info,&handClient);}}
    const auto* bus=SKSE::GetMessagingInterface();struct Request{void*(*get)(unsigned int){};} p;
    if(bus&&bus->Dispatch(0x92F38745,&p,sizeof(p),"PLANCK_SKSE")&&p.get){
        planck=static_cast<const PlanckSynergyAPI*>(p.get(2));
        if(planck&&planck->version==2&&planck->size>=sizeof(PlanckSynergyAPI)&&planck->CopyActorPhysics&&planck->RegisterClient){PlanckClientIdentity id{sizeof(id),0,"VRInteractionFrameworkTest"};planck->RegisterClient(&id,&planckClient);}}
    auto* dev=DevBenchAPI::GetDevBenchInterface001();if(dev){devbenchBuild=dev->GetBuildNumber();
        if(devbenchBuild>=10500){inspectRegistered=dev->RegisterToolExtension("inspect",inspectKey,R"({"description":"Test-only VR Interaction Framework actual domain state; no verdict inferred.","readOnly":true,"inputSchema":{"type":"object"}})",Inspect,nullptr);
            commandRegistered=dev->RegisterTool(commandTool,R"({"description":"Queue a bounded test-only VR Interaction Framework command; inspect processedCommand and commandResults. Requires observed processBirth and epoch. reset_fixture_npc targets only test_VRInteractionFramework.esp local ACHR 0x801.","readOnly":false,"inputSchema":{"type":"object","required":["id","command","expectedBirth","expectedEpoch"],"properties":{"id":{"type":"integer","minimum":1},"command":{"enum":["read","arm_world","disarm","set_position","bind_npc","rebind_npc","bind_fixture_npc","rebind_fixture_npc","reset_fixture_npc"]},"expectedBirth":{"type":"string"},"expectedEpoch":{"type":"integer"},"hand":{"enum":[1,2]},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"actorHandle":{"type":"integer","minimum":1},"bodyIndex":{"type":"integer","minimum":0}}}})",Submit,nullptr);}}
    Request phase;if(bus&&bus->Dispatch(0xF9279A57,&phase,sizeof(phase),"HIGGS")&&phase.get){
        if(auto* api=static_cast<HiggsPluginAPI::IHiggsInterface001*>(phase.get(1))){api->AddPostVrikPostHiggsCallback(Tick);phaseRegistered=true;}}
    if(!Ready()&&error.empty())error="one or more required public interfaces/leases/phases unavailable";
    ReadEffective();Mirror();WriteFile(mirror);
}
void Message(SKSE::MessagingInterface::Message* m) noexcept {
    try {if(!m)return;switch(m->type){case SKSE::MessagingInterface::kPostPostLoad:Connect();break;
        case SKSE::MessagingInterface::kPreLoadGame:++preLoadCount;armed=false;break;
        case SKSE::MessagingInterface::kPostLoadGame:++postLoadCount;loadSucceeded=m->dataLen==1&&reinterpret_cast<std::uintptr_t>(m->data)==1;break;
        case SKSE::MessagingInterface::kSaveGame:++saveCount;break;
        case SKSE::MessagingInterface::kNewGame:++newGameCount;armed=false;break;default:break;}}
    catch(const std::exception& e){error=e.what();}catch(...){error="connection exception";}
}
} // namespace vr_interaction_framework_test

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface* skse,SKSE::PluginInfo* info) {
    info->infoVersion=SKSE::PluginInfo::kVersion;info->name="test_VRInteractionFramework";info->version=1;
    return skse&&!skse->IsEditor()&&skse->RuntimeVersion()==REL::Version{1,4,15,0};
}
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    using namespace vr_interaction_framework_test;
    SKSE::Init(skse);
    tasksAvailable=SKSE::GetTaskInterface()!=nullptr;
    FILETIME created{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user);
    birth=std::to_string((std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime);
    std::array<wchar_t,32768> host{};GetModuleFileNameW(nullptr,host.data(),static_cast<DWORD>(host.size()));
    directory=std::filesystem::path(host.data()).parent_path()/L"Data/SKSE/Plugins/VRInteractionFrameworkTest";
    const auto* bus=SKSE::GetMessagingInterface();return bus&&bus->RegisterListener("SKSE",Message);
}
extern "C" __declspec(dllexport) std::uint32_t VRInteractionFrameworkTest_Version() noexcept {return 1;}
// Caller-owned bytes only. Safe even before SKSE initialization; no game access.
extern "C" __declspec(dllexport) std::uint32_t VRInteractionFrameworkTest_CopyStatus(
    std::uint32_t version,char* buffer,std::uint32_t capacity,std::uint32_t* needed) noexcept {
    using namespace vr_interaction_framework_test;
    if(version!=1)return Code(Status::UnsupportedVersion);
    if(!needed)return Code(Status::InvalidArgument);
    try {std::lock_guard lock(mirrorMutex);const auto text=mirror.dump();*needed=static_cast<std::uint32_t>(text.size()+1);
        if(!buffer||capacity<*needed)return Code(Status::BufferTooSmall);
        memcpy(buffer,text.c_str(),*needed);return Code(Status::Ok);
    }catch(...){return Code(Status::InternalError);}
}
