// Builds shared item-detail lines and lays them out in host UI panels.
#pragma once

#include <cstdint>
#include <string>

#include "game/ui_chrome.h"

namespace sawgod::game {

// Shared flavor -> stats -> flavor detail viewport. Time is game frames, not picture passes,
// so drawing WSC and GB (or redrawing a paused game) cannot advance the marquee.
class DetailLine {
  public:
    static constexpr int kHoldFrames = 151, kTextWidth = 212;
    // Text at y+4: nine glyph rows, then exactly one empty GB row before its 3 px bottom frame.
    // WSC has room for the tenth (shadow) row above its 2 px frame. Both pictures use the same height.
    static constexpr int kHeight = 17, kY = emu::Wsc::kHeight - kHeight;
    void reset();
    void update(uint64_t frame, int selection, std::u32string_view flavor, std::u32string_view stats,
                int width = kTextWidth);
    int offset() const { return offset_; }
    void draw(Chrome &c, int y = kY, int x = 0, int width = 224) const;

  private:
    std::u32string flavor_, stats_, line_;
    uint64_t start_ = 0;
    int selection_ = -1, travel_ = 0, offset_ = 0, width_ = kTextWidth;
    bool initialized_ = false;
};

} // namespace sawgod::game
