// Builds shared item-detail lines and lays them out in host UI panels.
#include "game/ui_detail.h"

#include <algorithm>

namespace sawgod::game {

void DetailLine::reset() {
    initialized_ = false;
    offset_ = 0;
}

void DetailLine::update(uint64_t frame, int selection, std::u32string_view flavor, std::u32string_view stats,
                        int width) {
    if (!initialized_ || selection != selection_ || flavor != flavor_ || stats != stats_ || width != width_ ||
        frame < start_) {
        initialized_ = true;
        start_ = frame;
        selection_ = selection;
        flavor_ = flavor;
        stats_ = stats;
        width_ = width;
        line_ = flavor_;
        travel_ = std::max(0, Chrome::text_width(line_) - width_);
        if (!stats_.empty()) {
            if (!flavor_.empty()) {
                while (!line_.empty() && line_.back() == U' ')
                    line_.pop_back();
                line_ += U"  "; // exactly two spaces between the description and the stats
                travel_ = Chrome::text_width(line_ + stats_) - Chrome::text_width(stats_);
            } else {
                travel_ = 0;
            }
            line_ += stats_;
        }
    }
    offset_ = 0;
    if (!travel_)
        return;
    // Half-pixel ticks accumulate in the elapsed frame count. Virtual trailing padding lets
    // the stats endpoint start at the left edge even when its text is shorter than the viewport.
    const uint64_t move = uint64_t(travel_) * 2;
    const uint64_t t = (frame - start_) % (2 * kHoldFrames + 2 * move);
    if (t < kHoldFrames)
        return;
    if (t < kHoldFrames + move)
        offset_ = int((t - kHoldFrames + 1) / 2);
    else if (t < 2 * kHoldFrames + move)
        offset_ = travel_;
    else
        offset_ = travel_ - int((t - 2 * kHoldFrames - move + 1) / 2);
}

void DetailLine::draw(Chrome &c, int y, int x, int width) const {
    c.window(x, y, width, kHeight);
    c.marquee(x + 5, y + 4, width_ + 1, 10,
              [&] { c.text(line_, x + 5 - offset_, y + 4, Chrome::Ink::Text, Chrome::text_width(line_)); });
}

} // namespace sawgod::game
