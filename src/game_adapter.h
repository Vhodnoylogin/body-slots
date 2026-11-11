#pragma once
// Public-SDK-only engine boundary. No VRIK reconstruction or native holster hooks.
namespace body_slots::game {
void Initialize(const void* skse) noexcept;
void Connect() noexcept;
void Reset() noexcept;
void SetWorldPlayable(bool playable) noexcept;
}
