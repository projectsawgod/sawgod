// GB opening presentation. Native scene cores own all waits, music, and transitions.
#pragma once
#include "game/hooks.h"
#include "game/text_wrap.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace sawgod::emu {
class Wsc;
}
namespace sawgod::game {
// Observation payload for IntroPresentation. Caption carries id/budget; WorldCard carries world id.
// WorldCardRow carries the actual raster IRQ row/edge, never a time estimate.
enum class IntroStage {
    Off,
    White,
    Splash,
    Caption,
    Field,
    Pans,
    Prologue,
    Content,
    Hold,
    FadeOut,
    Fade,
    WorldCard,
    WorldCardContent,
    WorldCardRow // b = row 0..31, c = native horizontal window edge, from irq_achievement_window
};
class GbIntro {
  public:
    void attach(emu::Wsc &w);
    void frame(emu::Wsc &w, bool enabled);
    // Leave any opening/title page (a loaded save starts in the field; the next stage event restarts it).
    void reset(emu::Wsc &w);
    // Called at the host input boundary, only when GB is the selected graphics mode.
    uint16_t keys(emu::Wsc &w, uint16_t held, bool selected);
    // Rows above `repeat_top` (map pixels) repeat the 16-pixel row there, for a climb past the map edge.
    using PanDraw = std::function<void(int map, int x, int y, std::vector<uint16_t> &, int repeat_top)>;
    PanDraw draw_map;
    static constexpr int kCaptionWidth = 160, kPrologueWidth = 144, kPageFrames = 438;
    static std::vector<std::u32string> pages(std::u32string_view paragraph);
    static int caption_width(int id);
    static WrapResult layout(std::u32string_view text, bool prologue, int caption = 0);
    bool active() const { return stage_ != IntroStage::Off && stage_ != IntroStage::Field; }

  private:
    void observe(emu::Wsc &w, const hooks::Args &a);
    void page(emu::Wsc &w, std::u32string text, int budget, bool prologue, int caption = 0);
    void sync_page(emu::Wsc &w);
    bool typing(const emu::Wsc &w) const;
    emu::Wsc *owner_ = nullptr;
    hooks::Subscription subscription_;
    uint64_t generation_ = 0, start_ = 0, content_start_ = 0;
    IntroStage stage_ = IntroStage::Off;
    WrapResult lines_;
    std::vector<std::u32string> prologue_;
    int budget_ = 1, page_ = 0, fade_ = 15;
    size_t letters_ = 0;
    bool complete_ = false, in_prologue_ = false, in_world_card_ = false;
    uint16_t previous_ = 0;
};
} // namespace sawgod::game
