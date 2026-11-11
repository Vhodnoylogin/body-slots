#pragma once
#include <cstdint>
#include <limits>

namespace body_slots::detail {

// Advances only on a real main-phase observation. The caller retains the named
// objects across observations so a recycled pointer cannot continue an old dwell.
class StandaloneRegionScope {
public:
    struct Scene {
        std::uint64_t save{};
        const void* body{};
        const void* world{};
        std::uint32_t cell{};
        bool operator==(const Scene&) const = default;
    };

    bool Playable() const noexcept { return playableSave_ != 0; }

    void BeforeReset(std::uint64_t currentSave) noexcept {
        saveBeforeReset_ = currentSave;
        playableSave_ = 0;
        scene_ = {};
    }

    void LoadFinished(bool success, std::uint64_t currentSave) noexcept {
        playableSave_ = success && saveBeforeReset_ && currentSave != saveBeforeReset_ ? currentSave : 0;
        saveBeforeReset_ = 0;
        scene_ = {};
    }

    template<class ReadScene>
    std::uint64_t ObserveFrame(ReadScene&& readScene) noexcept {
        if (!Playable()) return Observe({});
        try { return Observe(readScene()); } catch (...) { return Observe({}); }
    }

    std::uint64_t Observe(Scene scene) noexcept {
        if (!playableSave_ || scene.save != playableSave_ || !scene.body || !scene.world || !scene.cell) {
            scene_ = {};
            return 0;
        }
        if (scene == scene_) return epoch_;
        if (epoch_ == std::numeric_limits<std::uint64_t>::max()) return 0;
        scene_ = scene;
        return ++epoch_;
    }

private:
    std::uint64_t playableSave_{};
    Scene scene_{};
    std::uint64_t epoch_{}, saveBeforeReset_{};
};
}
