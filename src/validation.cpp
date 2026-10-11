#include "validation.h"
#include <cmath>
#include <cstring>

namespace body_slots::detail {
bool ValidString(const char* value, std::size_t size, bool identity) noexcept {
    if (!value || !size || !value[0]) return false;
    bool terminated = false;
    for (std::size_t i = 0; i < size; ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (terminated) { if (c != 0) return false; continue; }
        if (c == 0) { terminated = true; continue; }
        if (c < 32 || c > 126) return false;
        if (identity && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return false;
    }
    return terminated;
}

Status ValidateDefinition(const Definition& d) noexcept {
    if (d.structSize != sizeof(Definition) || (d.flags & ~kEnabled) || d.reserved ||
        !ValidString(d.provider, sizeof(d.provider), true) ||
        !ValidString(d.key, sizeof(d.key), true) ||
        !ValidString(d.anchor, sizeof(d.anchor), false) ||
        !ValidString(d.referenceRig, sizeof(d.referenceRig), true) ||
        !ValidString(d.kind, sizeof(d.kind), true) ||
        d.handMask == 0 || (d.handMask & ~Both) || d.contentMask == 0 ||
        (d.contentMask & ~AllContent) || d.scaleMode != AnchorScale) return Status::InvalidArgument;
    if (std::strcmp(d.referenceRig, "skyrimvr-humanoid-v1") != 0) return Status::UnsupportedVersion;
    for (float v : d.position) if (!std::isfinite(v) || std::abs(v) > 1000000.0f) return Status::InvalidArgument;
    for (float v : d.regionCenter) if (!std::isfinite(v) || std::abs(v) > 1000000.0f) return Status::InvalidArgument;
    if (!std::isfinite(d.radius) || d.radius <= 0 || d.radius > 1000000.0f) return Status::InvalidArgument;
    for (float v : d.rotation) if (!std::isfinite(v)) return Status::InvalidArgument;
    for (int i = 0; i != 3; ++i) for (int j = i; j != 3; ++j) {
        double dot = 0;
        for (int k = 0; k != 3; ++k) dot += static_cast<double>(d.rotation[i * 3 + k]) * d.rotation[j * 3 + k];
        if (std::abs(dot - (i == j ? 1.0 : 0.0)) > 0.0001) return Status::InvalidArgument;
    }
    const float* r = d.rotation;
    const double determinant = static_cast<double>(r[0]) * (static_cast<double>(r[4]) * r[8] - static_cast<double>(r[5]) * r[7])
        - static_cast<double>(r[1]) * (static_cast<double>(r[3]) * r[8] - static_cast<double>(r[5]) * r[6])
        + static_cast<double>(r[2]) * (static_cast<double>(r[3]) * r[7] - static_cast<double>(r[4]) * r[6]);
    if (std::abs(determinant - 1.0) > 0.0001) return Status::InvalidArgument;
    return Status::Ok;
}
} // namespace body_slots::detail
