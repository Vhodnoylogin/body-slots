#pragma once
#include "body_slots_api.h"
#include <cstddef>
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
struct GameFrameObservation {
    std::uint32_t structSize,reserved;
    // Provider steady-clock nanoseconds. Frame IDs alone do not establish age.
    std::uint64_t epoch,frame,sampledAtNs;
};
struct GameApi3 {
    GameApi base; // exact v2 prefix; version=3, structSize=sizeof(GameApi3)
    Status (BS_CALL *CopyInputServices)(std::uint64_t* capabilities);
    // Coherent metadata for the adapter's last published spatial frame.
    // Unavailable before a frame or after reset; never fabricates a fresh time.
    Status (BS_CALL *CopyFrameObservation)(GameFrameObservation*);
};
static_assert(sizeof(GameFrameObservation)==32,"GameFrameObservation ABI changed (x64)");
static_assert(sizeof(GameApi3)==32,"GameApi3 ABI changed (x64)");
static_assert(offsetof(GameApi3,CopyFrameObservation)==sizeof(GameApi2),"GameApi3 v2 prefix changed (x64)");
using QueryGameInterface = const GameApi* (BS_CALL *)(std::uint32_t, std::uint32_t);
}
// Export: BodySlots_QueryGameInterface(1, sizeof(GameApi)). No mutation or input fallback.
