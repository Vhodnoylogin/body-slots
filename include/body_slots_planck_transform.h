#pragma once
#include "body_slots_runtime_api.h"
#include <cmath>

namespace body_slots::detail {
inline bool WorldFromPlanckPose(float px, float py, float pz, const float (&q)[4], float units,
                                RuntimeTransform& out) {
    if (!std::isfinite(units) || units <= 0)
        return false;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double norm = x * x + y * y + z * z + w * w;
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 0.002)
        return false;
    const double k = 2.0 / norm;
    // PLANCK supplies worldFromBody as xyzw. Body Slots multiplies a
    // column vector by this row-major matrix, in the same world axes.
    out = {{px * units, py * units, pz * units},
           {float(1 - k * (y * y + z * z)), float(k * (x * y - w * z)), float(k * (x * z + w * y)),
            float(k * (x * y + w * z)), float(1 - k * (x * x + z * z)), float(k * (y * z - w * x)),
            float(k * (x * z - w * y)), float(k * (y * z + w * x)), float(1 - k * (x * x + y * y))},
           1};
    return std::isfinite(out.position[0]) && std::isfinite(out.position[1]) && std::isfinite(out.position[2]);
}
} // namespace body_slots::detail
