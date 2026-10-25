#pragma once
#include "body_slots_native_placement_api.h"
#include "owned_native_bank.h"

namespace body_slots::detail {
using NativePlacementStore = OwnedNativeBank<NativePlacementSnapshot, kNativePlacementApiVersion, RuntimeDomain::NativePlacement>;
} // namespace body_slots::detail
