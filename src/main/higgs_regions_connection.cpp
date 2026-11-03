#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "higgs_native_regions.h"
#include "higgshandapi.h"
#include <cstring>
#include <mutex>

extern "C" const body_slots::Api* BS_CALL BodySlots_QueryInterface(std::uint32_t,std::uint32_t) noexcept;
namespace body_slots::game {
namespace {
struct Connection {
    std::mutex mutex;
    HIGGS_HandClient client{};
    HIGGS_HandRegistration detector{};
    HiggsNativeRegions regions{*BodySlots_QueryInterface(kApiVersion,sizeof(Api))};
};
// SKSE plugins have process lifetime. No retained callback context is destroyed
// while HIGGS could invoke it; there is no unload/reload promise in this adapter.
Connection& State() { static auto* state=new Connection;return *state; }
}
void ConnectHiggsRegions(void* handle) noexcept {
    try {
        if(!handle)return;
        auto& s=State();std::lock_guard lock(s.mutex);if(s.detector)return;
        const auto module=static_cast<HMODULE>(handle);
        const auto queryHands=reinterpret_cast<HIGGS_QueryHandInterfaceFn>(GetProcAddress(module,"HIGGS_QueryHandInterface"));
        const auto queryRegions=reinterpret_cast<decltype(&HIGGS_QueryNativeRegionsInterface)>(GetProcAddress(module,"HIGGS_QueryNativeRegionsInterface"));
        HIGGS_HandInterface hands{};
        if(!queryHands||!queryRegions||queryHands(HIGGS_HAND_ABI_VERSION,sizeof(hands),&hands)!=HIGGS_HAND_OK||
           hands.structSize!=sizeof(hands)||hands.abiVersion!=HIGGS_HAND_ABI_VERSION||!hands.RegisterClient||!hands.UnregisterClient)return;
        const auto* regions=queryRegions(HIGGS_NATIVE_REGIONS_ABI_VERSION,sizeof(HIGGS_NativeRegionsInterface));
        if(!regions||regions->structSize!=sizeof(*regions)||regions->abiVersion!=HIGGS_NATIVE_REGIONS_ABI_VERSION||
           !(regions->capabilities&1)||!regions->RegisterDetector||!regions->RemoveDetector||!regions->CopyFrame)return;
        HIGGS_HandClientInfo info{};info.structSize=sizeof(info);std::strcpy(info.name,"BodySlots.Regions");
        HIGGS_HandClient client{};HIGGS_HandRegistration registration{};
        if(hands.RegisterClient(&info,&client)!=HIGGS_HAND_OK)return;
        if(regions->RegisterDetector(client,&HiggsNativeRegions::Detect,&s.regions,&registration)!=HIGGS_HAND_OK) {
            hands.UnregisterClient(client);return;
        }
        s.client=client;s.detector=registration;
    }catch(...){}
}
void ResetHiggsRegions() noexcept { try {State().regions.Reset();}catch(...){} }
} // namespace body_slots::game
