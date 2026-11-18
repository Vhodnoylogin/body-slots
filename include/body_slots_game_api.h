#pragma once
#include "body_slots_api.h"
namespace body_slots {
constexpr std::uint32_t kGameApiVersion = 1;
struct GameIntegration {
    std::uint32_t structSize, version;
    std::uint32_t connected, reserved;
    std::uint64_t higgsRecognizerClient, higgsEpoch, frame, bodyGeneration; // recognizerClient is a public HandRouting 2 label
};
struct GameApi {
    std::uint32_t structSize, version;
    Status (BS_CALL *CopyIntegration)(GameIntegration*);
};
constexpr std::uint64_t kGameHeldReleaseInput=1;
struct GameApi2 {
    GameApi base; // exact v1 prefix; version=2, structSize=sizeof(GameApi2)
    // Actual connected optional services. Successful copy may report zero.
    Status (BS_CALL *CopyInputServices)(std::uint64_t* capabilities);
};
static_assert(sizeof(GameApi2)==24,"GameApi2 ABI changed (x64)");
using QueryGameInterface = const GameApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
}
// Export: BodySlots_QueryGameInterface(1, sizeof(GameApi)). No mutation or input fallback.
