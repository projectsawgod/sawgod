// What the game can't report itself: watches each presented frame for a stall, a picture stuck on black and a
// retained picture that never lets go, and reports each to the debug log (docs/DEBUG_LOG.md). Reads only.
#pragma once
#include <cstdint>
#include <string>

#include "engine/debug_log.h"

namespace sawgod::app {

class DebugWatch {
  public:
    static constexpr uint64_t kStallNs = 2'000'000'000;
    static constexpr uint64_t kBlackFrames = 75 * 15; // no scene of the game is black this long
    static constexpr uint64_t kHeldFrames = 75 * 5;   // a screen handoff takes a moment, not this
    struct Frame {
        uint64_t now_ns = 0;
        uint64_t game_frame = 0; // stops while the game is paused, so neither count runs on then
        bool visible = true;     // the window is on screen: time away isn't a stall
        bool black = false;
        bool holding = false;
    };
    void frame(const Frame &f) {
        if (last_ns_ && f.visible && was_visible_ && f.now_ns - last_ns_ >= kStallNs)
            debug_log::warning("stall",
                               "a frame took " + std::to_string((f.now_ns - last_ns_) / 1'000'000) + " ms");
        last_ns_ = f.now_ns;
        was_visible_ = f.visible;
        watch(black_since_, black_reported_, f.black, f.game_frame, kBlackFrames, debug_log::Severity::Error,
              "picture", "the picture has been black for 15 seconds");
        watch(held_since_, held_reported_, f.holding, f.game_frame, kHeldFrames, debug_log::Severity::Error,
              "picture", "a screen handoff has held the old picture for 5 seconds");
    }

  private:
    static void watch(uint64_t &since, bool &reported, bool on, uint64_t frame, uint64_t limit,
                      debug_log::Severity severity, const char *area, const char *message) {
        if (!on) {
            since = 0;
            reported = false;
            return;
        }
        if (!since)
            since = frame + 1;
        if (!reported && frame + 1 - since >= limit) {
            reported = true;
            debug_log::report(severity, area, message);
        }
    }
    uint64_t last_ns_ = 0;
    bool was_visible_ = true;
    uint64_t black_since_ = 0, held_since_ = 0;
    bool black_reported_ = false, held_reported_ = false;
};

} // namespace sawgod::app
