#pragma once
#include "body_slots_zone_api.h"
namespace body_slots::detail {
Status ValidateZoneShape(const ZoneShape&) noexcept;
ZoneShape DefaultZoneShape(const Definition&) noexcept;
bool ZoneContainsPoint(const ZoneShape&, const RuntimeTransform&, const float point[3]) noexcept;
// Signed distance in local Skyrim units; NaN means invalid geometry or transform.
// Uniform world scale applies to the distance as well as the shape.
double ZonePointSignedDistance(const ZoneShape&, const RuntimeTransform&, const float point[3]) noexcept;
const ZoneApi& GetZoneApi() noexcept;
} // namespace body_slots::detail
