// Host presentation levels. Both tracks share a clock; interrupted fades start at their live weights.
#pragma once
#include "engine/settings.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace sawgod {
class Fidelity {
  public:
    static constexpr uint64_t kFadeNs = 500'000'000;
    using Weights = std::array<float, 3>; // GB, WSC, Enhanced
    Fidelity(FidelityMode graphics = FidelityMode::Swap, FidelityMode music = FidelityMode::Swap)
        : graphics_mode_(graphics), music_mode_(music) {
        graphics_ = resolved(graphics, graphics_swap_);
        music_ = resolved(music, music_swap_);
        graphics_from_ = one(graphics_);
        music_from_ = one(music_);
    }
    void settings(FidelityMode graphics, FidelityMode music, uint64_t now) {
        graphics_mode_ = graphics;
        music_mode_ = music;
        target(resolved(graphics, graphics_swap_), resolved(music, music_swap_), now);
    }
    void step(int direction, uint64_t now) {
        if (graphics_mode_ == FidelityMode::Swap)
            graphics_swap_ = stepped(graphics_swap_, direction);
        if (music_mode_ == FidelityMode::Swap)
            music_swap_ = stepped(music_swap_, direction);
        target(resolved(graphics_mode_, graphics_swap_), resolved(music_mode_, music_swap_), now);
    }
    // Teach the picture step when unlocked; otherwise teach the unlocked music track.
    std::optional<FidelityMode> step_mode() const {
        if (graphics_mode_ == FidelityMode::Swap)
            return graphics_;
        if (music_mode_ == FidelityMode::Swap)
            return music_;
        return std::nullopt;
    }
    FidelityMode graphics() const { return graphics_; }
    FidelityMode music() const { return music_; }
    Weights graphics_weights(uint64_t now) const { return blend(graphics_from_, graphics_, now); }
    Weights music_weights(uint64_t now) const { return blend(music_from_, music_, now); }

  private:
    FidelityMode graphics_mode_, music_mode_, graphics_, music_;
    FidelityMode graphics_swap_ = FidelityMode::Enhanced, music_swap_ = FidelityMode::Enhanced;
    Weights graphics_from_{}, music_from_{};
    uint64_t start_ = 0;
    static FidelityMode resolved(FidelityMode mode, FidelityMode swapped) {
        return mode == FidelityMode::Swap ? swapped : mode;
    }
    static FidelityMode stepped(FidelityMode mode, int direction) {
        return FidelityMode(std::clamp(int(mode) + (direction > 0 ? 1 : direction < 0 ? -1 : 0), 0, 2));
    }
    static Weights one(FidelityMode mode) {
        Weights w{};
        w[size_t(mode)] = 1;
        return w;
    }
    Weights blend(const Weights &from, FidelityMode mode, uint64_t now) const {
        const float t = float(std::min<uint64_t>(now >= start_ ? now - start_ : 0, kFadeNs)) / float(kFadeNs);
        auto to = one(mode);
        for (size_t i = 0; i < to.size(); ++i)
            to[i] = from[i] + (to[i] - from[i]) * t;
        return to;
    }
    void target(FidelityMode graphics, FidelityMode music, uint64_t now) {
        if (graphics == graphics_ && music == music_)
            return;
        graphics_from_ = graphics_weights(now);
        music_from_ = music_weights(now);
        graphics_ = graphics;
        music_ = music;
        start_ = now;
    }
};
} // namespace sawgod
