#pragma once
#include "body_slots_api.h"
#include <cstddef>

namespace body_slots::detail {
// Canonical bounded strings have one terminator and zero-filled trailing bytes.
bool ValidString(const char* value, std::size_t size, bool identity) noexcept;
Status ValidateDefinition(const Definition& definition) noexcept;
} // namespace body_slots::detail
