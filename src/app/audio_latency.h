// A fixed music cursor while Enhanced is reachable; fidelity fades never change sample duration.
#pragma once
#include "engine/fidelity.h"

namespace sawgod::app {
class AudioLatency {
  public:
    static constexpr int kLookahead = 8;
    struct Plan {
        Fidelity::Weights weights;
        int lookahead;
    };
    Plan frame(FidelityMode requested, bool available, uint64_t now, bool enhanced_reachable = true,
               uint64_t generation = 0) {
        if (requested == FidelityMode::Enhanced && !available)
            requested = FidelityMode::Wsc;
        if (!initialized_) {
            initialized_ = true;
            fade_ = Fidelity(FidelityMode::Wsc, requested);
        }
        fade_.settings(FidelityMode::Wsc, requested, now);
        const auto weights = fade_.music_weights(now);
        // Locking a running song must not skip its buffered music. Remove its delay at the next
        // background-song replacement; effect-slot cues do not move that boundary.
        if (generation != generation_ && !enhanced_reachable && weights[2] == 0)
            delay_ = 0;
        generation_ = generation;
        if (available && enhanced_reachable)
            delay_ = kLookahead;
        return {weights, available ? delay_ : 0};
    }

  private:
    bool initialized_ = false;
    uint64_t generation_ = 0;
    Fidelity fade_{FidelityMode::Wsc, FidelityMode::Wsc};
    int delay_ = 0;
};
} // namespace sawgod::app
