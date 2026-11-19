#include "game/bank34_misc.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/bank36_battle.h"
#include "game/bank36_misc.h"
#include "game/bank37.h"
#include "game/bank3d_sound.h"
#include "game/computed_flags.h"
#include "game/computed_handlers.h"
#include "game/gb_intro.h"
#include "game/hooks.h"
#include "game/system.h"
// Decompiled title/intro helpers (bank 0x3C, segment C000h).
#include "game/bank3c_title.h"

#include "game/bank34.h"
#include <bit>

namespace sawgod::game {
namespace {
void observe_intro(Machine &m, IntroStage stage, int b = 0, int c = 0) {
    hooks::emit(hooks::Event::IntroPresentation, m, {int(stage), b, c});
}
constexpr uint16_t kFrameCounter = 0xC80C;
constexpr uint16_t kAssetSegment = 0x2000;
constexpr uint16_t kTileSegment = 0x0400;
constexpr uint16_t kIntroTiles = 0x4EC0;
constexpr uint16_t kIntroPalettes = 0x4E40;
constexpr uint16_t kIntroTilemap = 0x8EC0;
constexpr uint16_t kPaletteTarget = 0xC600;
constexpr uint16_t kTitlePulsePalette = 0xC5A0; // palette_buffer + palette 13 * 32
constexpr uint16_t kTitlePulseTarget = 0xC7A0;
constexpr uint8_t kRomBankPort = 0xC2;
constexpr uint8_t kBackgroundScrollXPort = 0x10;

// One step of palette 13's title-menu pulse. The existing palette helpers have no exported cores.
// Work per colour component to retain their saturation and discarded upper colour nibble.
void step_title_palette_core(Machine &m, bool darken) {
    for (int color = 0; color < 16; ++color) {
        uint16_t offset = uint16_t(color * 2);
        uint16_t current = m.rd16(m.ss(), uint16_t(kTitlePulsePalette + offset));
        uint16_t target = darken ? 0 : m.rd16(m.ss(), uint16_t(kTitlePulseTarget + offset));
        uint16_t result = 0;
        for (int shift = 0; shift < 12; shift += 4) {
            uint8_t component = uint8_t((current >> shift) & 0xF);
            uint8_t goal = uint8_t((target >> shift) & 0xF);
            if (component < goal)
                ++component;
            else if (component > goal)
                --component;
            result = uint16_t(result | (uint16_t(component) << shift));
        }
        m.wr16(m.ss(), uint16_t(kTitlePulsePalette + offset), result);
    }
}
} // namespace

// Shared body of ROM 0x3C4700
// Map bank EAh at segment 2000h, load 512 intro tiles and four target palettes, then draw the
// 32x18 background. DS=2000h, AX=BX=0, CX=1220h, SI=8EC0h, DI=C680h; clear DF, ES preserved.
void load_intro_background_core(Machine &m) {
    m.out8(kRomBankPort, 0xEA);
    m.flags() = uint16_t(m.flags() & ~emu::DF);
    m.ds() = kAssetSegment;
    for (int word = 0; word < 0x2000; ++word)
        m.wr16(kTileSegment, uint16_t(word * 2), m.rd16(kAssetSegment, uint16_t(kIntroTiles + word * 2)));
    for (int word = 0; word < 0x40; ++word)
        m.wr16(0, uint16_t(kPaletteTarget + word * 2),
               m.rd16(kAssetSegment, uint16_t(kIntroPalettes + word * 2)));
    m.ax() = 0;
    m.bx() = 0;
    m.cx() = 0x1220;
    m.si() = kIntroTilemap;
    m.di() = uint16_t(kPaletteTarget + 0x80);
    draw_tilemap_core(m);
    return;
}

// rom: 0x3C4700 (near)
void load_intro_background(Machine &m) {
    load_intro_background_core(m);
    m.ret();
}

// Shared body of ROM 0x3C473B
// Scroll intro background one pixel every fourth frame. AH retains the frame-counter high byte;
// AL ends with frame modulo 4 or the incremented scroll port. Other registers and memory preserved.
void advance_intro_background_scroll_core(Machine &m) {
    m.ax() = m.rd16(m.ss(), kFrameCounter);
    m.set_al(uint8_t(m.al() & 3));
    if (m.al() == 0) {
        m.set_al(uint8_t(m.in8(kBackgroundScrollXPort) + 1));
        m.out8(kBackgroundScrollXPort, m.al());
    }
    return;
}

// rom: 0x3C473B (near)
void advance_intro_background_scroll(Machine &m) {
    advance_intro_background_scroll_core(m);
    m.ret();
}

// Shared body of ROM 0x3C4ECA
// CX is the title-menu pulse state: CH bit 7 selects fade-to-black, CL is its timer. Step palette 13
// every four calls, switching direction at CL=40h/0. AX becomes 13 only on palette-step frames.
void advance_title_palette_pulse_core(Machine &m) {
    bool darken = (m.cx() & 0x8000) != 0;
    if ((m.cx() & 3) == 0) {
        m.ax() = 13;
        step_title_palette_core(m, darken);
    }
    uint8_t timer = uint8_t(uint8_t(m.cx()) + (darken ? -1 : 1));
    m.cx() = uint16_t((m.cx() & 0xFF00) | timer);
    if (!darken && timer == 0x40)
        m.cx() = uint16_t(0xFF00 | timer);
    else if (darken && timer == 0)
        m.cx() = 0x100;
    return;
}

// rom: 0x3C4ECA (near)
void advance_title_palette_pulse(Machine &m) {
    advance_title_palette_pulse_core(m);
    m.ret();
}

namespace {
constexpr uint16_t kIntroScene = 0xC81A, kIntroStrip = 0xC81C, kIntroSceneTable = 0x4872;
struct IntroRegisters {
    uint16_t ax, bx, cx, dx, bp, si, di, ds, es, ss;
    explicit IntroRegisters(Machine &m)
        : ax(m.ax()), bx(m.bx()), cx(m.cx()), dx(m.dx()), bp(m.bp()), si(m.si()), di(m.di()), ds(m.ds()),
          es(m.es()), ss(m.ss()) {}
    void restore(Machine &m) const {
        m.ax() = ax;
        m.bx() = bx;
        m.cx() = cx;
        m.dx() = dx;
        m.bp() = bp;
        m.si() = si;
        m.di() = di;
        m.ds() = ds;
        m.es() = es;
        m.ss() = ss;
    }
};
void intro_scene_descriptor(Machine &m) {
    m.bp() = uint16_t(kIntroSceneTable + (m.rd16(m.ss(), kIntroScene) << 3));
    m.set_al(m.rd8(m.cs(), m.bp()));
    m.out8(kRomBankPort, m.al());
    m.ax() = kAssetSegment;
    m.ds() = m.ax();
}
} // namespace
// Shared body of ROM 0x3C483E
// Load 504 scene tiles into tile page CX&1. Uses the scene table at C000:4872; all registers preserved.
void load_intro_scene_tiles_core(Machine &m) {
    const IntroRegisters saved(m);
    intro_scene_descriptor(m);
    m.ax() = uint16_t((m.cx() & 1) << 9);
    m.cx() = 0x1f8;
    m.si() = m.rd16(m.cs(), uint16_t(m.bp() + 2));
    copy_tiles_core(m);
    saved.restore(m);
    return;
}

// rom: 0x3C483E (near)
void load_intro_scene_tiles(Machine &m) {
    load_intro_scene_tiles_core(m);
    m.ret();
}
// Shared body of ROM 0x3C480B
// Draw the current scene's 28x18 tilemap, preserving registers and selecting its mapped ROM bank.
void draw_intro_scene_tilemap_core(Machine &m) {
    const IntroRegisters saved(m);
    intro_scene_descriptor(m);
    m.si() = m.rd16(m.cs(), uint16_t(m.bp() + 4));
    m.ax() = 0;
    m.bx() = 0;
    m.cx() = 0x121c;
    draw_tilemap_core(m);
    saved.restore(m);
    return;
}

// rom: 0x3C480B (near)
void draw_intro_scene_tilemap(Machine &m) {
    draw_intro_scene_tilemap_core(m);
    m.ret();
}
// Shared body of ROM 0x3C47C2
// Draw one 28-tile scene strip at the current background-scroll row; all registers preserved.
void draw_intro_scene_strip_core(Machine &m) {
    const IntroRegisters saved(m);
    m.bp() = uint16_t(kIntroSceneTable + (m.rd16(m.ss(), kIntroScene) << 3));
    m.ax() = m.rd16(m.ss(), kIntroStrip);
    m.set_al(uint8_t(m.al() - 1));
    m.set_ah(56);
    m.ax() = uint16_t(m.al() * 56);
    m.si() = uint16_t(m.ax() + m.rd16(m.cs(), uint16_t(m.bp() + 4)));
    m.set_al(m.rd8(m.cs(), m.bp()));
    m.out8(kRomBankPort, m.al());
    m.ax() = kAssetSegment;
    m.ds() = m.ax();
    m.ax() = uint8_t(m.in8(0x11) - 8);
    m.ax() >>= 3;
    m.bx() = uint16_t(m.al() << 8);
    m.ax() = 0;
    m.cx() = 0x11c;
    draw_tilemap_core(m);
    saved.restore(m);
    return;
}

// rom: 0x3C47C2 (near)
void draw_intro_scene_strip(Machine &m) {
    draw_intro_scene_strip_core(m);
    m.ret();
}

// Shared body of ROM 0x3C00AE
// Invoke the installed near field callback, if any; all registers preserved.
void run_field_pre_sprite_callback_core(Machine &m) {
    const IntroRegisters saved(m);
    m.bx() = m.rd16(m.ss(), 0xD501);
    if (m.bx())
        dispatch_computed_handler_core(m, m.bx());
    saved.restore(m);
    return;
}

// rom: 0x3C00AE
void run_field_pre_sprite_callback(Machine &m) {
    run_field_pre_sprite_callback_core(m);
    m.retf();
}

// Shared body of ROM 0x3C00C2
// Invoke the installed near field callback, if any; all registers preserved.
void run_field_post_sprite_callback_core(Machine &m) {
    const IntroRegisters saved(m);
    m.bx() = m.rd16(m.ss(), 0xD503);
    if (m.bx())
        dispatch_computed_handler_core(m, m.bx());
    saved.restore(m);
    return;
}

// rom: 0x3C00C2
void run_field_post_sprite_callback(Machine &m) {
    run_field_post_sprite_callback_core(m);
    m.retf();
}

// Shared body of ROM 0x3C00D6
// Invoke the installed near field callback, if any; all registers preserved.
void run_field_npc_callback_core(Machine &m) {
    const IntroRegisters saved(m);
    m.bx() = m.rd16(m.ss(), 0xD505);
    if (m.bx())
        dispatch_computed_handler_core(m, m.bx());
    saved.restore(m);
    return;
}

// rom: 0x3C00D6
void run_field_npc_callback(Machine &m) {
    run_field_npc_callback_core(m);
    m.retf();
}
namespace {
constexpr uint16_t kIntroSavedSp = 0xC818; // intro_saved_sp
constexpr uint16_t kIntroFieldWaiting = 0xC813;
struct IntroAbort {};
void intro_call(Machine &m, uint16_t offset, uint16_t return_ip = Machine::kTrampolineIp);
void intro_wait_frame_core(Machine &m) {
    m.sp() = uint16_t(m.sp() - 2);
    m.wr16(m.ss(), m.sp(), m.ax());
    m.call_far(0x4000, 0x0009);
    // These discarded call slots become observable if cleanup restores an earlier stack.
    m.wr16(m.ss(), uint16_t(m.sp() - 4), 0x48A0);
    m.call_far(0x4000, 0x005C);
    m.wr16(m.ss(), uint16_t(m.sp() - 4), 0x48A5);
    m.ax() &= 0x000E;
    m.flags() &= ~(emu::CF | emu::OF | emu::AF | emu::SF | emu::ZF | emu::PF);
    if (!m.ax())
        m.flags() |= emu::ZF;
    if ((std::popcount(m.al()) & 1) == 0)
        m.flags() |= emu::PF;
    if (m.ax()) {
        m.jmp_far(0xC000, 0x3D51);
        // Abort abandons every intervening intro frame. Propagate to the native entry,
        // then let the interpreter execute the original cleanup and restore C818's stack.
        throw IntroAbort{};
    }
    m.ax() = m.rd16(m.ss(), m.sp());
    m.sp() = uint16_t(m.sp() + 2);
}
void intro_decrement_counter(Machine &m) {
    const uint16_t old = m.cx();
    m.cx() = uint16_t(old - 1);
    m.flags() &= ~(emu::OF | emu::AF | emu::SF | emu::ZF | emu::PF);
    if (old == 0x8000)
        m.flags() |= emu::OF;
    if ((old & 15) == 0)
        m.flags() |= emu::AF;
    if (m.cx() & 0x8000)
        m.flags() |= emu::SF;
    if (!m.cx())
        m.flags() |= emu::ZF;
    if ((std::popcount(uint8_t(m.cx())) & 1) == 0)
        m.flags() |= emu::PF;
}
void intro_native_entry(Machine &m, void (*body)(Machine &)) {
    try {
        body(m);
        m.ret();
    } catch (const IntroAbort &) {
        // The interpreter continues at the common cleanup selected by the abort.
    }
}
} // namespace
// rom: 0x3C489A (near)
// Wait one intro frame and poll the abort keys. Abort abandons this frame for the intro cleanup path.

void wait_intro_frame(Machine &m) { intro_native_entry(m, intro_wait_frame_core); }
// Shared body of ROM 0x3C48B8
// Wait CX intro frames while scrolling the background every fourth frame; preserve AX, leave CX zero.
void wait_scrolling_intro_frames_core(Machine &m) {
    // The child can unwind into intro cleanup: keep the saved AX on the emulated stack.
    m.sp() = uint16_t(m.sp() - 2);
    m.wr16(m.ss(), m.sp(), m.ax());
    do {
        intro_call(m, 0x489A, 0x48BC);
        advance_intro_background_scroll_core(m);
        intro_decrement_counter(m);
    } while (m.cx());
    m.ax() = m.rd16(m.ss(), m.sp());
    m.sp() = uint16_t(m.sp() + 2);
    return;
}

// rom: 0x3C48B8 (near)
// Wait CX intro frames while scrolling the background every fourth frame; preserve AX, leave CX zero.
void wait_scrolling_intro_frames(Machine &m) { intro_native_entry(m, wait_scrolling_intro_frames_core); }
namespace {
void intro_out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}
// These frames must exist in RAM: intro abort reloads SP and pops the saved registers itself.
void intro_push(Machine &m, uint16_t value) {
    m.sp() = uint16_t(m.sp() - 2);
    m.wr16(m.ss(), m.sp(), value);
}
uint16_t intro_pop(Machine &m) {
    uint16_t value = m.rd16(m.ss(), m.sp());
    m.sp() = uint16_t(m.sp() + 2);
    return value;
}
void intro_save_frame(Machine &m) {
    uint16_t sp = m.sp();
    for (uint16_t value :
         {m.ax(), m.cx(), m.dx(), m.bx(), sp, m.bp(), m.si(), m.di(), m.ds(), m.es(), m.ss()})
        intro_push(m, value);
}
void intro_restore_frame(Machine &m) {
    m.ss() = intro_pop(m);
    m.es() = intro_pop(m);
    m.ds() = intro_pop(m);
    m.di() = intro_pop(m);
    m.si() = intro_pop(m);
    m.bp() = intro_pop(m);
    (void)intro_pop(m);
    m.bx() = intro_pop(m);
    m.dx() = intro_pop(m);
    m.cx() = intro_pop(m);
    m.ax() = intro_pop(m);
}
// Change all palettes over sixteen steps with the selected wait cadence.
void intro_fade(Machine &m, void (*step)(Machine &), uint16_t delay, bool scrolling, uint16_t return_ip) {
    intro_save_frame(m);
    m.cx() = 16;
    do {
        intro_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 16;
        step(m);
        m.cx() = delay;
        intro_call(m, scrolling ? 0x48B8 : 0x48AF, return_ip);
        dma_palettes_core(m);
        m.cx() = intro_pop(m);
    } while (--m.cx());
    intro_restore_frame(m);
}
} // namespace
// rom: 0x3C3A4C
// Display the first one-time achievement vignette; preserve registers.
void show_achievement_vignette_one_core(Machine &m) {
    intro_save_frame(m);
    m.bp() = 0x3A10;
    show_achievement_vignette_core(m);
    intro_restore_frame(m);
}
void show_achievement_vignette_one(Machine &m) {
    show_achievement_vignette_one_core(m);
    m.retf();
}
// Shared body of ROM 0x3C49C2
// Fade the intro toward its target palettes, scrolling for six frames between steps.
void fade_scrolling_intro_to_target_core(Machine &m) {
    intro_fade(m, palette_step_toward_core, 6, true, 0x49DB);
    return;
}

// rom: 0x3C49C2 (near)
void fade_scrolling_intro_to_target(Machine &m) {
    intro_native_entry(m, fade_scrolling_intro_to_target_core);
}
// rom: 0x3C3D92
// Fade in the opening splash, hold it, then fade out; leave the bank mapped for its assets.
void opening(Machine &m) {
    hooks::EventScope presentation(m, hooks::Event::IntroPresentation, {int(IntroStage::Splash)});
    m.ax() = 0;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_request_core(m);
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), 0xC820, 0);
    m.wr8(m.ss(), 0xC822, 0);
    m.ax() = 0;
    m.cx() = 16;
    m.dx() = 0;
    m.di() = 0xC400;
    fill_palette_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    m.ax() = 0xF000;
    intro_out16(m, 0, m.ax());
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    intro_out16(m, 0x12, 0);
    m.set_al(0xE7);
    m.out8(0xC2, m.al());
    m.ax() = 0x2000;
    m.ds() = m.ax();
    m.ax() = 0;
    m.cx() = 25;
    m.si() = 0x20;
    copy_tiles_core(m);
    m.ax() = 0;
    m.cx() = 4;
    m.si() = 0;
    m.di() = 0xC600;
    copy_palettes_core(m);
    m.si() = 0x340;
    m.ax() = 0;
    m.bx() = 0;
    m.cx() = 0x121C;
    draw_tilemap_core(m);
    m.call_far(0x4000, 9);
    m.ax() = 0xF001;
    intro_out16(m, 0, m.ax());
    for (int phase = 0; phase < 2; ++phase) {
        m.cx() = 16;
        do {
            intro_push(m, m.cx());
            m.ax() = 0;
            m.cx() = 16;
            if (phase)
                darken_palette_step_core(m);
            else
                palette_step_toward_core(m);
            m.call_far(0x4000, 0x5c);
            m.cx() = phase ? 5 : 4;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x4000;
                wait_frames_core(m);
                m.cs() = caller_cs;
            }
            dma_palettes_core(m);
            m.cx() = intro_pop(m);
        } while (--m.cx());
        m.cx() = phase ? 30 : 75;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
    }
    m.retf();
}
// rom: 0x3C3CC0
// Initialize title intro displays and run its scenes; the common abort cleanup unwinds this frame.
void title_screen(Machine &m) {
    hooks::EventScope presentation(m, hooks::Event::IntroPresentation, {int(IntroStage::White)});
    try {
        intro_save_frame(m);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            initialize_field_controls_core(m);
            m.cs() = caller_cs;
        }
        m.wr16(m.ss(), kIntroSavedSp, m.sp());
        m.ax() = 0;
        m.es() = 0;
        m.di() = 0xBFC0;
        m.cx() = 16;
        m.ax() = 0x1111;
        do {
            m.wr16(m.es(), m.di(), m.ax());
            m.di() = uint16_t(m.di() + ((m.flags() & emu::DF) ? -2 : 2));
        } while (--m.cx());
        m.ax() = 0;
        m.es() = 0;
        m.di() = 0xBFE0;
        m.cx() = 16;
        do {
            m.wr16(m.es(), m.di(), 0);
            m.di() = uint16_t(m.di() + ((m.flags() & emu::DF) ? -2 : 2));
        } while (--m.cx());
        for (uint16_t screen : {uint16_t(0), uint16_t(1)}) {
            m.ax() = screen;
            m.bx() = 0;
            m.cx() = 0x2020;
            m.dx() = uint16_t(0x3FFE + screen);
            fill_screen_rect_core(m);
        }
        m.ax() = 0;
        m.cx() = 16;
        m.dx() = 0;
        m.di() = 0xC400;
        fill_palette_core(m);
        m.ax() = 0xF103;
        intro_out16(m, 0, m.ax());
        m.ax() = 0;
        intro_out16(m, 0x10, 0);
        intro_out16(m, 0x12, 0);
        m.ax() = 0x92;
        m.wr8(m.ss(), 0xC820, m.al());
        for (uint16_t scene : {uint16_t(0x3E6E), uint16_t(0x3F63), uint16_t(0x418F), uint16_t(0x4279),
                               uint16_t(0x4331), uint16_t(0x474A), uint16_t(0x43AE)})
            intro_call(m, scene);
    } catch (const IntroAbort &) {
        // All child frames are abandoned by the saved-SP restoration below.
    }
    m.cs() = 0xC000;
    m.wr8(m.ss(), kIntroFieldWaiting, 0);
    m.ax() = m.rd16(m.ss(), kIntroScene);
    m.flags() &= ~(emu::CF | emu::OF | emu::AF | emu::SF | emu::ZF | emu::PF);
    if (!m.ax())
        m.flags() |= emu::ZF;
    if (m.ax() & 0x8000)
        m.flags() |= emu::SF;
    if ((std::popcount(m.al()) & 1) == 0)
        m.flags() |= emu::PF;
    if (!m.ax()) {
        m.ax() = 2;
        m.cx() = 0x0101;
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xD000;
        sound_channel_reset_entry_core(m);
        m.cs() = caller_cs;
    }
    m.cx() = 16;
    do {
        intro_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 16;
        darken_palette_step_core(m);
        m.call_far(0x4000, 9);
        dma_palettes_core(m);
        m.cx() = intro_pop(m);
        intro_decrement_counter(m);
    } while (m.cx());
    m.sp() = m.rd16(m.ss(), kIntroSavedSp);
    intro_restore_frame(m);
    m.retf();
}
// Shared body of ROM 0x3C4578
// Reveal a scene's text/tile strip by copying one plane column at a time, with scrolling waits.
static void reveal_intro_scene_strip_with_delay(Machine &m, uint16_t delay) {
    observe_intro(m, IntroStage::Caption, (m.bp() - 0x46C0) / 8 + 1,
                  m.rd8(m.cs(), uint16_t(m.bp() + 4)) * 4 * delay);
    const int caption_id = (m.bp() - 0x46C0) / 8 + 1;
    const int total_columns = m.rd8(m.cs(), uint16_t(m.bp() + 4)) * 4;
    int copied_columns = 0;
    intro_save_frame(m);
    intro_push(m, m.bx());
    m.set_al(0xE7);
    m.out8(0xC2, m.al());
    m.ax() = 0x2000;
    m.ds() = m.ax();
    m.ax() = 4;
    m.cx() = 1;
    m.si() = 0xCE00;
    m.di() = 0xC400;
    copy_palettes_core(m);
    m.bx() = intro_pop(m);
    m.si() = m.rd16(m.cs(), uint16_t(m.bp() + 2));
    m.cx() = uint16_t(m.rd8(m.cs(), uint16_t(m.bp() + 4)) | (m.rd8(m.cs(), uint16_t(m.bp() + 6)) << 8));
    m.ax() = 1;
    m.dx() = 0x2000;
    draw_tilemap_offset_core(m);
    m.di() = 0x8000;
    m.si() = m.rd16(m.cs(), m.bp());
    m.cx() = m.rd16(m.cs(), uint16_t(m.bp() + 4));
    m.bp() = m.rd16(m.cs(), uint16_t(m.bp() + 2));
    do {
        intro_push(m, m.si());
        intro_push(m, m.di());
        intro_push(m, m.cx());
        m.ax() = uint16_t(m.rd16(m.ds(), m.bp()) & 0x3F);
        m.ax() = uint16_t(m.ax() << 5);
        m.si() = uint16_t(m.si() + m.ax());
        m.ax() = uint16_t(m.rd16(m.ds(), m.bp()) & 0xFF);
        m.ax() = uint16_t(m.ax() << 5);
        m.di() = uint16_t(m.di() + m.ax());
        m.cx() = 4;
        do {
            intro_push(m, m.si());
            intro_push(m, m.di());
            intro_push(m, m.cx());
            m.cx() = 8;
            do {
                m.set_al(m.rd8(m.ds(), m.si()));
                m.wr8(m.ss(), m.di(), m.al());
                m.set_al(m.rd8(m.ds(), uint16_t(m.si() + 0x40)));
                m.wr8(m.ss(), uint16_t(m.di() + 0x40), m.al());
                m.si() = uint16_t(m.si() + 4);
                m.di() = uint16_t(m.di() + 4);
            } while (--m.cx());
            hooks::emit(hooks::Event::IntroCaptionStrip, m, {caption_id, ++copied_columns, total_columns});
            m.cx() = delay;
            intro_call(m, 0x48B8, delay == 2 ? 0x4601 : 0x46A5);
            dma_palettes_core(m);
            m.cx() = intro_pop(m);
            m.di() = intro_pop(m);
            m.si() = intro_pop(m);
            ++m.di();
            ++m.si();
        } while (--m.cx());
        m.cx() = intro_pop(m);
        m.di() = intro_pop(m);
        m.si() = intro_pop(m);
        m.bp() = uint16_t(m.bp() + 2);
    } while (--m.cx());
    intro_restore_frame(m);
    return;
}

void reveal_intro_scene_strip_core(Machine &m) { reveal_intro_scene_strip_with_delay(m, 2); }

// rom: 0x3C4578 (near)
void reveal_intro_scene_strip(Machine &m) { intro_native_entry(m, reveal_intro_scene_strip_core); }

// Shared body of ROM 0x3C3A90
// One-time vignette selected by CS:BP: load its two tile layers, animate the scanline window, fade out.
void show_achievement_vignette_core(Machine &m) {
    intro_save_frame(m);
    m.set_al(m.rd8(m.ss(), 0xD0AE));
    uint8_t mask = m.rd8(m.cs(), m.bp());
    if (!(m.al() & mask)) {
        m.set_al(uint8_t(m.al() | mask));
        m.wr8(m.ss(), 0xD0AE, m.al());
        m.ax() = 2;
        m.cx() = 0x0201;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            sound_channel_reset_entry_core(m);
            m.cs() = caller_cs;
        }
        m.cx() = 32;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            sound_channel_reset_entry_core(m);
            m.cs() = caller_cs;
        }
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            sound_request_core(m);
            m.cs() = caller_cs;
        }
        m.call_far(0x4000, 9);
        // Descriptor order is continent, ocean, sky, city. The host observes only;
        // all waits, input reads, palettes, sound and register effects stay native.
        hooks::EventScope presentation(m, hooks::Event::IntroPresentation,
                                       {int(IntroStage::WorldCard), (m.bp() - 0x3A10) / 15 + 1});
        m.ax() = 0xF100;
        intro_out16(m, 0, m.ax());
        m.ax() = 0;
        intro_out16(m, 0x10, 0);
        intro_out16(m, 0x12, 0);
        m.set_al(1);
        m.out8(4, m.al());
        m.set_al(0x32);
        m.out8(7, m.al());
        m.set_al(m.rd8(m.cs(), uint16_t(m.bp() + 1)));
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        for (int layer = 0; layer < 2; ++layer) {
            m.ax() = layer ? 0x200 : 0;
            m.cx() = layer ? 0x80 : 0x200;
            m.si() = m.rd16(m.cs(), uint16_t(m.bp() + (layer ? 9 : 3)));
            copy_tiles_core(m);
            m.ax() = layer ? 4 : 0;
            m.cx() = layer ? 1 : 4;
            m.si() = m.rd16(m.cs(), uint16_t(m.bp() + (layer ? 13 : 7)));
            m.di() = 0xC600;
            copy_palettes_core(m);
            m.si() = m.rd16(m.cs(), uint16_t(m.bp() + (layer ? 11 : 5)));
            m.bx() = 0;
            m.cx() = 0x121C;
            m.ax() = uint16_t(layer);
            draw_tilemap_core(m);
        }
        m.di() = 0xD6FE;
        m.cx() = 32;
        do {
            intro_push(m, m.cx());
            m.ax() = 0x1800;
            random_range_core(m);
            m.wr8(m.ss(), m.di(), m.al());
            m.cx() = intro_pop(m);
            ++m.di();
        } while (--m.cx());
        m.ax() = m.rd8(m.cs(), uint16_t(m.bp() + 2));
        m.wr8(m.ss(), 0xC820, m.al());
        m.wr8(m.ss(), 0xC822, m.al());
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            play_sound_effect_entry_core(m);
            m.cs() = caller_cs;
        }
        m.call_far(0x4000, 9);
        m.ax() = 0xF133;
        intro_out16(m, 0, m.ax());
        m.ax() = 0;
        intro_out16(m, 8, 0);
        m.ax() = 0x8FDF;
        intro_out16(m, 10, m.ax());
        m.set_al(0x30);
        m.out8(3, m.al());
        m.ax() = 0;
        m.ds() = 0;
        m.bx() = 0x90;
        m.wr16(m.ds(), m.bx(), 0x3C94);
        m.wr16(m.ds(), uint16_t(m.bx() + 2), 0xC000);
        m.set_al(uint8_t(m.in8(0xB2) | 0x10));
        m.out8(0xB2, m.al());
        m.cx() = 16;
        do {
            intro_push(m, m.cx());
            m.ax() = 0;
            m.cx() = 5;
            palette_step_up_toward_core(m);
            m.cx() = 4;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x4000;
                wait_frames_core(m);
                m.cs() = caller_cs;
            }
            dma_palettes_core(m);
            observe_intro(m, IntroStage::Fade, 16 - m.rd16(m.ss(), m.sp()));
            m.cx() = intro_pop(m);
        } while (--m.cx());
        observe_intro(m, IntroStage::WorldCardContent);
        m.cx() = 75;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
        m.cx() = 224;
        do {
            intro_push(m, m.cx());
            m.cx() = 32;
            m.di() = 0xD6FE;
            do {
                m.set_al(uint8_t(m.rd8(m.ss(), m.di()) + 1));
                m.wr8(m.ss(), m.di(), m.al());
                ++m.di();
            } while (--m.cx());
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
            m.cx() = intro_pop(m);
        } while (--m.cx());
        m.flags() &= ~emu::IF;
        m.set_al(0x40);
        m.out8(0xB2, m.al());
        m.flags() |= emu::IF;
        m.cx() = 225;
        do {
            intro_push(m, m.cx());
            m.call_far(0x4000, 9);
            m.call_far(0x4000, 0x5c);
            m.ax() = uint16_t(m.rd16(m.ss(), 0xC804) & 4);
            m.cx() = intro_pop(m);
        } while (--m.cx());
        m.ax() = 2;
        m.cx() = 0x0401;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            sound_channel_reset_entry_core(m);
            m.cs() = caller_cs;
        }
        m.cx() = 16;
        do {
            intro_push(m, m.cx());
            m.ax() = 0;
            m.cx() = 16;
            darken_palette_step_core(m);
            m.cx() = 4;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x4000;
                wait_frames_core(m);
                m.cs() = caller_cs;
            }
            dma_palettes_core(m);
            observe_intro(m, IntroStage::Fade, m.rd16(m.ss(), m.sp()) - 1);
            m.cx() = intro_pop(m);
        } while (--m.cx());
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            sound_request_core(m);
            m.cs() = caller_cs;
        }
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            play_sound_effect_entry_core(m);
            m.cs() = caller_cs;
        }
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            sound_channel_reset_entry_core(m);
            m.cs() = caller_cs;
        }
        m.call_far(0x4000, 9);
        m.call_far(0x4000, 9);
        m.ax() = 0x9000;
        m.ds() = m.ax();
        m.ax() = 0x100;
        m.cx() = 0x100;
        m.si() = 0x80;
        copy_tiles_core(m);
        m.ax() = 15;
        m.cx() = 1;
        m.si() = 0;
        m.di() = 0xC600;
        copy_palettes_core(m);
    }
    intro_restore_frame(m);
    return;
}

// rom: 0x3C3A90
void show_achievement_vignette(Machine &m) {
    show_achievement_vignette_core(m);
    m.retf();
}
// Shared body of ROM 0x3C3E6E
// Reveal the initial intro text, then scroll the field scene upward while fading through white.
void run_intro_field_scene_core(Machine &m) {
    observe_intro(m, IntroStage::White);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x3FFF;
    fill_screen_rect_core(m);
    uint16_t es = m.es();
    m.ax() = 0;
    m.es() = 0;
    m.di() = 0x8000;
    m.cx() = 0x800;
    do {
        m.wr16(m.es(), m.di(), 0);
        m.di() = uint16_t(m.di() + ((m.flags() & emu::DF) ? -2 : 2));
    } while (--m.cx());
    m.es() = es;
    m.ax() = 0;
    m.cx() = 16;
    m.dx() = 0xFFF;
    m.di() = 0xC400;
    m.ax() = 0xF103;
    intro_out16(m, 0, m.ax());
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    intro_out16(m, 0x12, 0);
    intro_call(m, 0x4700, 0x3EAE);
    intro_call(m, 0x49C2, 0x3EB1);
    m.cx() = 30;
    intro_call(m, 0x48B8, 0x3EB7);
    m.bp() = 0x46C0;
    m.bx() = uint16_t((m.bx() & 0) | 0x0802);
    intro_call(m, 0x4578, 0x3EC1);
    m.cx() = 75;
    intro_call(m, 0x48B8, 0x3EC7);
    intro_call(m, 0x4A37, 0x3ECA);
    m.wr8(m.ss(), 0xD0DC, 0xC9);
    m.wr8(m.ss(), 0xD0DD, 0x11);
    m.wr8(m.ss(), 0xD0DE, 0x1B);
    m.wr8(m.ss(), 0xC810, 1);
    m.wr8(m.ss(), 0xD005, 0xAD);
    m.wr8(m.ss(), 0xD0D6, 0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        m.cs() = caller_cs;
    }
    observe_intro(m, IntroStage::Field);
    m.wr8(m.ss(), 0xD528, 2);
    m.cx() = 0;
    do {
        intro_push(m, m.cx());
        if (!(m.cx() & 15)) {
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x5000;
                draw_map_top_edge_core(m);
                m.cs() = caller_cs;
            }
            m.wr8(m.ss(), 0xD0DE, uint8_t(m.rd8(m.ss(), 0xD0DE) - 1));
        }
        for (uint16_t off : {uint16_t(0xD512), uint16_t(0xD514)})
            m.wr8(m.ss(), off, uint8_t(m.rd8(m.ss(), off) - 1));
        m.wr16(m.ss(), 0xD59F, uint16_t(m.rd16(m.ss(), 0xD59F) - 1));
        m.cx() = intro_pop(m);
        intro_push(m, m.cx());
        if (m.cx() < 16) {
            m.ax() = 0;
            m.cx() = 16;
            palette_step_toward_core(m);
        } else if (m.cx() >= 128) {
            m.cx() &= 1;
            if (!m.cx()) {
                m.ax() = 0;
                m.cx() = 16;
                brighten_palette_step_core(m);
            }
        }
        intro_call(m, 0x4923, 0x3F49);
        m.cx() = intro_pop(m);
        ++m.cx();
    } while (m.cx() != 160);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x3FFF;
    fill_screen_rect_core(m);
    return;
}

// rom: 0x3C3E6E (near)
// Reveal the initial intro text, then scroll the field scene upward while fading through white.
void run_intro_field_scene(Machine &m) { intro_native_entry(m, run_intro_field_scene_core); }
// rom: 0x3C440E
// Load both prologue layers, fade in, scroll the overlay until confirm, hold, then fade out and unwind.
void prologue(Machine &m) {
    hooks::EventScope presentation(m, hooks::Event::IntroPresentation, {int(IntroStage::Prologue)});
    intro_save_frame(m);
    m.wr16(m.ss(), kIntroSavedSp, m.sp());
    m.ax() = 0;
    m.cx() = 16;
    m.dx() = 0;
    m.di() = 0xC400;
    fill_palette_core(m);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x2800;
    fill_screen_rect_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    m.ax() = 0xF023;
    intro_out16(m, 0, m.ax());
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    intro_out16(m, 0x12, 0);
    intro_out16(m, 8, 0);
    m.ax() = 0xDF;
    intro_out16(m, 10, m.ax());
    for (int layer = 0; layer < 2; ++layer) {
        m.set_al(0xE8);
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        m.ax() = layer ? 0x200 : 0;
        m.cx() = layer ? 0x58 : 0x1F8;
        m.si() = layer ? 0xC830 : 0x84C0;
        copy_tiles_core(m);
        m.ax() = layer ? 4 : 0;
        m.cx() = 4;
        m.si() = layer ? 0xC7B0 : 0x8440;
        m.di() = 0xC600;
        copy_palettes_core(m);
        m.set_al(0xE8);
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        m.si() = layer ? 0xD330 : 0xC3C0;
        m.ax() = uint16_t(layer);
        m.bx() = 0;
        m.cx() = 0x121C;
        draw_tilemap_core(m);
    }
    m.cx() = 16;
    do {
        intro_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 8;
        palette_step_up_toward_core(m);
        m.cx() = 4;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
        dma_palettes_core(m);
        observe_intro(m, IntroStage::Fade, 16 - m.rd16(m.ss(), m.sp()));
        m.cx() = intro_pop(m);
    } while (--m.cx());
    observe_intro(m, IntroStage::Content);
    m.cx() = 144;
    do {
        intro_push(m, m.cx());
        m.cx() = 4;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
        m.set_al(uint8_t(m.in8(11) + 1));
        m.out8(11, m.al());
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(m.rd16(m.ss(), 0xC804) & 4);
        if (m.ax())
            break;
        m.cx() = intro_pop(m);
    } while (--m.cx());
    m.ax() = 0xF003;
    intro_out16(m, 0, m.ax());
    observe_intro(m, IntroStage::Hold);
    m.cx() = 300;
    do {
        intro_push(m, m.cx());
        m.call_far(0x4000, 9);
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(m.rd16(m.ss(), 0xC804) & 4);
        if (m.ax())
            break;
        m.cx() = intro_pop(m);
    } while (--m.cx());
    observe_intro(m, IntroStage::FadeOut);
    m.cx() = 16;
    do {
        intro_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 16;
        darken_palette_step_core(m);
        observe_intro(m, IntroStage::Fade, m.rd16(m.ss(), m.sp()) - 1);
        m.cx() = 4;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
        dma_palettes_core(m);
        m.cx() = intro_pop(m);
    } while (--m.cx());
    m.cx() = 30;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        m.cs() = caller_cs;
    }
    m.sp() = m.rd16(m.ss(), kIntroSavedSp);
    intro_restore_frame(m);
    m.retf();
}

// rom: 0x3C4A60
// Build the title menu, play its optional reveal animation, pulse the palette, and return a choice or
// timeout.
void title_menu(Machine &m) {
    m.ax() = 0;
    m.cx() = 16;
    m.dx() = 0;
    m.di() = 0xC400;
    fill_palette_core(m);
    m.di() = 0xC600;
    fill_palette_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    m.ax() = 0xF100;
    intro_out16(m, 0, m.ax());
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    intro_out16(m, 0x12, 0);
    for (uint16_t screen : {uint16_t(0), uint16_t(1)}) {
        m.ax() = screen;
        m.bx() = 0;
        m.cx() = 0x2020;
        m.dx() = screen ? 0x1FF : 0x1EF;
        fill_screen_rect_core(m);
    }
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x80;
    copy_tiles_core(m);
    m.ax() = 15;
    m.cx() = 1;
    m.si() = 0;
    m.di() = 0xC600;
    copy_palettes_core(m);
    m.set_al(0xEF);
    m.out8(0xC2, m.al());
    m.ax() = 0x2000;
    m.ds() = m.ax();
    m.ax() = 0;
    m.cx() = 0x83;
    m.si() = 0x6090;
    copy_tiles_core(m);
    m.ax() = 0x83;
    m.cx() = 0x24;
    m.si() = 0x7540;
    copy_tiles_core(m);
    m.ax() = 0;
    m.cx() = 3;
    m.si() = 0x6030;
    m.di() = 0xC600;
    copy_palettes_core(m);
    m.si() = 0x70F0;
    m.bx() = 0;
    m.cx() = 0x121C;
    m.ax() = 0;
    draw_tilemap_core(m);
    m.si() = 0x79C0;
    m.bx() = 0x0805;
    m.cx() = 0x0212;
    m.ax() = 1;
    m.dx() = 0x83;
    draw_tilemap_offset_core(m);
    m.ax() = 0x200;
    m.cx() = 0x27;
    m.si() = 0x7A28;
    copy_tiles_core(m);
    m.ax() = 14;
    m.cx() = 1;
    m.si() = 0x7A08;
    m.di() = 0xC600;
    copy_palettes_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        load_menu_core(m);
        m.cs() = caller_cs;
    } // load_menu, returns whether a save exists in CF
    bool save = (m.flags() & emu::CF) != 0;
    m.ax() = save ? 1 : 0;
    m.bx() = uint16_t(hooks::filter(hooks::Value::TitleMenuOptions, m, 3));
    m.wr16(m.ss(), kIntroSavedSp, m.ax());
    m.wr16(m.ss(), kIntroScene, m.bx());
    m.ax() = m.rd16(m.ss(), kIntroStrip);
    auto pressed = [&]() {
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(m.rd16(m.ss(), 0xC804) & 4);
        return m.ax() != 0;
    };
    auto title_text = [&](uint16_t si, uint16_t bx, uint16_t cx, uint16_t attr) {
        m.si() = si;
        m.bx() = bx;
        m.cx() = cx;
        m.ax() = 1;
        m.dx() = attr;
        draw_tilemap_offset_core(m);
    };
    if (!m.ax()) {
        m.set_al(0);
        m.wr8(m.ss(), 0xC820, 0);
        m.di() = 0xD6FE;
        m.cx() = 32;
        do {
            intro_push(m, m.cx());
            m.ax() = 0x1000;
            random_range_core(m);
            m.wr8(m.ss(), m.di(), m.al());
            m.cx() = intro_pop(m);
            ++m.di();
        } while (--m.cx());
        m.cx() = 30;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
        m.set_al(0x97);
        m.wr8(m.ss(), 0xC820, m.al());
        m.cx() = 75;
        bool abort = false;
        do {
            m.call_far(0x4000, 9);
            if (pressed()) {
                abort = true;
                break;
            }
        } while (--m.cx());
        if (!abort) {
            m.ax() = 0xF133;
            intro_out16(m, 0, m.ax());
            m.ax() = 0;
            intro_out16(m, 8, 0);
            m.ax() = 0x8FDF;
            intro_out16(m, 10, m.ax());
            m.set_al(0x38);
            m.out8(3, m.al());
            m.ax() = 0;
            m.ds() = 0;
            m.bx() = 0x90;
            m.wr16(0, 0x90, 0x4EA0);
            m.wr16(0, 0x92, 0xC000);
            m.set_al(uint8_t(m.in8(0xB2) | 0x10));
            m.out8(0xB2, m.al());
            m.cx() = 16;
            do {
                intro_push(m, m.cx());
                m.ax() = 0;
                m.cx() = 4;
                palette_step_up_toward_core(m);
                m.cx() = 5;
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x4000;
                    wait_frames_core(m);
                    m.cs() = caller_cs;
                }
                dma_palettes_core(m);
                m.cx() = intro_pop(m);
                if (pressed()) {
                    abort = true;
                    break;
                }
            } while (--m.cx());
            if (!abort) {
                m.cx() = 75;
                do {
                    m.call_far(0x4000, 9);
                    if (pressed()) {
                        abort = true;
                        break;
                    }
                } while (--m.cx());
            }
            if (!abort) {
                m.cx() = 224;
                do {
                    intro_push(m, m.cx());
                    m.cx() = 32;
                    m.di() = 0xD6FE;
                    do {
                        m.set_al(uint8_t(m.rd8(m.ss(), m.di()) + 1));
                        m.wr8(m.ss(), m.di(), m.al());
                        ++m.di();
                    } while (--m.cx());
                    m.call_far(0x4000, 9);
                    dma_palettes_core(m);
                    m.cx() = intro_pop(m);
                    if (pressed()) {
                        abort = true;
                        break;
                    }
                } while (--m.cx());
            }
            if (!abort) {
                m.cx() = 30;
                do {
                    m.call_far(0x4000, 9);
                    if (pressed())
                        break;
                } while (--m.cx());
            }
        }
        m.flags() &= ~emu::IF;
        m.set_al(0x40);
        m.out8(0xB2, m.al());
        m.flags() |= emu::IF;
        m.set_al(0xEF);
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        title_text(0x800C, 0x0D06, 0x010F, 0x1A00);
        title_text(0x7FA4, 0x1001, 0x011A, 0x1C00);
        title_text(0x7FD8, 0x1105, 0x0112, 0x1C00);
        hooks::emit(hooks::Event::TitleMenu, m, {0, 1}); // PUSH ANY BUTTON is drawn
        m.call_far(0x4000, 9);
        m.ax() = 0xF103;
        intro_out16(m, 0, m.ax());
        m.ax() = 0;
        intro_out16(m, 0x10, 0);
        intro_out16(m, 0x12, 0);
        m.set_al(1);
        m.out8(4, m.al());
        m.set_al(0x32);
        m.out8(7, m.al());
        m.ax() = m.ss();
        m.ds() = m.ax();
        m.es() = m.ax();
        m.si() = 0xC600;
        m.di() = 0xC400;
        m.cx() = 0x100;
        do {
            m.wr16(m.es(), m.di(), m.rd16(m.ds(), m.si()));
            int step = (m.flags() & emu::DF) ? -2 : 2;
            m.si() = uint16_t(m.si() + step);
            m.di() = uint16_t(m.di() + step);
        } while (--m.cx());
        m.set_al(0xEF);
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        m.ax() = 13;
        m.cx() = 1;
        m.si() = 0x7A08;
        m.di() = 0xC600;
        copy_palettes_core(m);
        m.set_al(0x97);
        m.wr8(m.ss(), 0xC820, m.al());
        m.ax() = 0x8CA;
        m.wr16(m.ss(), 0xC81E, m.ax());
        m.cx() = 0x100;
        do {
            advance_title_palette_pulse_core(m);
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
            m.call_far(0x4000, 0x5c);
            m.ax() = m.rd16(m.ss(), 0xC804);
            if (m.ax())
                break;
            m.wr16(m.ss(), 0xC81E, uint16_t(m.rd16(m.ss(), 0xC81E) - 1));
            if (!m.rd16(m.ss(), 0xC81E)) {
                hooks::emit(hooks::Event::TitleMenu, m, {0}); // times out to the opening
                m.ax() = 0;
                m.retf();
                return;
            }
        } while (true);
    }
    m.set_al(0xEF);
    m.out8(0xC2, m.al());
    m.ax() = 0x2000;
    m.ds() = m.ax();
    m.ax() = 1;
    m.bx() = 0x0C00;
    m.cx() = 0x0320;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    title_text(0x7FA4, 0x1001, 0x011A, 0x1C00);
    title_text(0x7FD8, 0x1105, 0x0112, 0x1C00);
    m.si() = 0x79C0;
    m.bx() = 0x0805;
    m.cx() = 0x0212;
    m.ax() = 0;
    m.dx() = 0x83;
    draw_tilemap_offset_core(m);
    title_text(0x7F08, 0x0C0C, uint16_t((m.rd8(m.ss(), kIntroScene) << 8) | 0x1A), 0x1C00);
    hooks::emit(hooks::Event::TitleMenu, m, {m.rd8(m.ss(), kIntroScene)});
    m.set_al(0);
    m.out8(5, 0);
    m.set_al(4);
    m.out8(6, m.al());
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    m.ax() = 0xF103;
    intro_out16(m, 0, m.ax());
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    intro_out16(m, 0x12, 0);
    m.set_al(1);
    m.out8(4, m.al());
    m.set_al(0x32);
    m.out8(7, m.al());
    m.ax() = m.rd16(m.ss(), kIntroStrip);
    if (m.ax()) {
        m.cx() = 16;
        do {
            intro_push(m, m.cx());
            m.ax() = 0;
            m.cx() = 16;
            palette_step_up_toward_core(m);
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
            m.cx() = intro_pop(m);
        } while (--m.cx());
    }
    m.ax() = 0x13EC;
    m.wr16(m.ss(), 0xC81E, m.ax());
    for (;;) {
        m.ax() = uint16_t((m.rd16(m.ss(), kIntroSavedSp) << 3) + 0x62);
        m.bx() = uint16_t((m.al() << 8) | 0x4F);
        m.ax() = 0;
        m.dx() = 0x1FFB;
        set_sprite_16x16_core(m);
        dma_sprite_table_core(m);
        m.call_far(0x4000, 9);
        m.ax() = 0xF107;
        intro_out16(m, 0, m.ax());
        m.set_al(0);
        m.out8(5, 0);
        m.set_al(4);
        m.out8(6, m.al());
        dma_palettes_core(m);
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ss(), 0xC806);
        if (m.ax() & 6) {
            m.ax() = uint16_t(m.rd16(m.ss(), kIntroSavedSp) + 1);
            m.retf();
            return;
        }
        if (m.ax() & 0x10) {
            m.ax() = m.rd16(m.ss(), kIntroSavedSp);
            m.ax() = m.ax() ? uint16_t(m.ax() - 1) : uint16_t(m.rd16(m.ss(), kIntroScene) - 1);
            m.wr16(m.ss(), kIntroSavedSp, m.ax());
        } else if (m.ax() & 0x40) {
            m.ax() = uint16_t(m.rd16(m.ss(), kIntroSavedSp) + 1);
            if (m.ax() >= m.rd16(m.ss(), kIntroScene))
                m.ax() = 0;
            m.wr16(m.ss(), kIntroSavedSp, m.ax());
        } else {
            m.wr16(m.ss(), 0xC81E, uint16_t(m.rd16(m.ss(), 0xC81E) - 1));
            if (!m.rd16(m.ss(), 0xC81E)) {
                hooks::emit(hooks::Event::TitleMenu, m, {0}); // times out to the opening
                m.ax() = 0;
                m.retf();
                return;
            }
        }
    }
}

// Shared body of ROM 0x3C3A5D
// Display the one-time achievement vignette using descriptor 3A1F; preserve registers.
void show_achievement_vignette_two_core(Machine &m) {
    intro_save_frame(m);
    m.bp() = 0x3A1F;
    show_achievement_vignette_core(m);
    intro_restore_frame(m);
    return;
}

// rom: 0x3C3A5D
// Display the one-time achievement vignette using descriptor 3A1F; preserve registers.
void show_achievement_vignette_two(Machine &m) {
    show_achievement_vignette_two_core(m);
    m.retf();
}
// Shared body of ROM 0x3C3A6E
// Display the one-time achievement vignette using descriptor 3A2E; preserve registers.
void show_achievement_vignette_three_core(Machine &m) {
    intro_save_frame(m);
    m.bp() = 0x3A2E;
    show_achievement_vignette_core(m);
    intro_restore_frame(m);
    return;
}

// rom: 0x3C3A6E
// Display the one-time achievement vignette using descriptor 3A2E; preserve registers.
void show_achievement_vignette_three(Machine &m) {
    show_achievement_vignette_three_core(m);
    m.retf();
}
// Shared body of ROM 0x3C3A7F
// Display the one-time achievement vignette using descriptor 3A3D; preserve registers.
void show_achievement_vignette_four_core(Machine &m) {
    intro_save_frame(m);
    m.bp() = 0x3A3D;
    show_achievement_vignette_core(m);
    intro_restore_frame(m);
    return;
}

// rom: 0x3C3A7F
// Display the one-time achievement vignette using descriptor 3A3D; preserve registers.
void show_achievement_vignette_four(Machine &m) {
    show_achievement_vignette_four_core(m);
    m.retf();
}

// Shared body of ROM 0x3C48AF
// Wait CX intro frames with the abort-key check, retaining the caller's AX.
void wait_intro_frames_core(Machine &m) {
    intro_push(m, m.ax());
    do {
        intro_call(m, 0x489A, 0x48B3);
        intro_decrement_counter(m);
    } while (m.cx());
    m.ax() = intro_pop(m);
    return;
}

// rom: 0x3C48AF (near)
// Wait CX intro frames with the abort-key check, retaining the caller's AX.
void wait_intro_frames(Machine &m) { intro_native_entry(m, wait_intro_frames_core); }
// Shared body of ROM 0x3C48C4
// Fade field actors to their target palettes during the intro.
void fade_intro_field_to_target_core(Machine &m) {
    intro_save_frame(m);
    m.ax() = 0xF107;
    intro_out16(m, 0, m.ax());
    m.cx() = 16;
    do {
        intro_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 16;
        palette_step_toward_core(m);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            draw_player_sprite_core(m);
            m.cs() = caller_cs;
        }
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            advance_and_draw_npcs_core(m);
            m.cs() = caller_cs;
        }
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            advance_player_animation_timers_core(m);
            m.cs() = caller_cs;
        }
        intro_call(m, 0x489A, 0x48EE);
        dma_palettes_core(m);
        m.cx() = intro_pop(m);
    } while (--m.cx());
    intro_restore_frame(m);
    return;
}

// rom: 0x3C48C4 (near)
// Fade field actors to their target palettes during the intro.
void fade_intro_field_to_target(Machine &m) { intro_native_entry(m, fade_intro_field_to_target_core); }
// Shared body of ROM 0x3C48FC
// Brighten all intro-field palettes with two actor frames per step.
void brighten_intro_field_core(Machine &m) {
    intro_save_frame(m);
    m.ax() = 0xF107;
    intro_out16(m, 0, m.ax());
    m.cx() = 16;
    do {
        intro_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 16;
        brighten_palette_step_core(m);
        intro_call(m, 0x4923, 0x4917);
        intro_call(m, 0x4923, 0x491A);
        m.cx() = intro_pop(m);
    } while (--m.cx());
    intro_restore_frame(m);
    return;
}

// rom: 0x3C48FC (near)
// Brighten all intro-field palettes with two actor frames per step.
void brighten_intro_field(Machine &m) { intro_native_entry(m, brighten_intro_field_core); }
// Shared body of ROM 0x3C4923
// Present one intro field-actor frame, temporarily enabling the intro-update flag.
void present_intro_field_frame_core(Machine &m) {
    intro_save_frame(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_and_draw_npcs_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), kIntroFieldWaiting, 1);
    intro_call(m, 0x489A, 0x493F);
    m.wr8(m.ss(), kIntroFieldWaiting, 0);
    intro_restore_frame(m);
    return;
}

// rom: 0x3C4923 (near)
// Present one intro field-actor frame, temporarily enabling the intro-update flag.
void present_intro_field_frame(Machine &m) { intro_native_entry(m, present_intro_field_frame_core); }
// Shared body of ROM 0x3C494A
// Present CX intro field-actor frames while preserving the caller's other registers.
void wait_intro_field_frames_core(Machine &m) {
    do {
        intro_save_frame(m);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            draw_player_sprite_core(m);
            m.cs() = caller_cs;
        }
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            advance_and_draw_npcs_core(m);
            m.cs() = caller_cs;
        }
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            advance_player_animation_timers_core(m);
            m.cs() = caller_cs;
        }
        m.wr8(m.ss(), kIntroFieldWaiting, 1);
        intro_call(m, 0x489A, 0x4966);
        m.wr8(m.ss(), kIntroFieldWaiting, 0);
        intro_restore_frame(m);
    } while (--m.cx());
    return;
}

// rom: 0x3C494A (near)
// Present CX intro field-actor frames while preserving the caller's other registers.
void wait_intro_field_frames(Machine &m) { intro_native_entry(m, wait_intro_field_frames_core); }
// Shared body of ROM 0x3C499B
// Fade scrolling intro palettes toward their target with four frames per step.
void fade_scrolling_intro_in_core(Machine &m) {
    intro_fade(m, palette_step_toward_core, 4, true, 0x49B4);
    return;
}

// rom: 0x3C499B (near)
// Fade scrolling intro palettes toward their target with four frames per step.
void fade_scrolling_intro_in(Machine &m) { intro_native_entry(m, fade_scrolling_intro_in_core); }
// Shared body of ROM 0x3C49E9
// Darken intro palettes with eight stationary frames between steps.
void darken_intro_scene_core(Machine &m) {
    intro_fade(m, darken_palette_step_core, 8, false, 0x4A02);
    return;
}

// rom: 0x3C49E9 (near)
// Darken intro palettes with eight stationary frames between steps.
void darken_intro_scene(Machine &m) { intro_native_entry(m, darken_intro_scene_core); }

// Shared body of ROM 0x3C4A10
// Brighten the stationary intro scene with four frames between palette steps.
void brighten_intro_scene_core(Machine &m) {
    intro_fade(m, brighten_palette_step_core, 4, false, 0x4A29);
    return;
}

// rom: 0x3C4A10 (near)
// Brighten the stationary intro scene with four frames between palette steps.
void brighten_intro_scene(Machine &m) { intro_native_entry(m, brighten_intro_scene_core); }
// Shared body of ROM 0x3C4A37
// Brighten the scrolling intro scene with four frames between palette steps.
void brighten_scrolling_intro_scene_core(Machine &m) {
    intro_fade(m, brighten_palette_step_core, 4, true, 0x4A50);
    return;
}

// rom: 0x3C4A37 (near)
// Brighten the scrolling intro scene with four frames between palette steps.
void brighten_scrolling_intro_scene(Machine &m) {
    intro_native_entry(m, brighten_scrolling_intro_scene_core);
}

// Shared body of ROM 0x3C43AE
// Fade out the intro field, reveal the closing text strip, and clear the scene.
void show_intro_closing_scene_core(Machine &m) {
    observe_intro(m, IntroStage::White);
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    m.ax() = 0;
    intro_out16(m, 0x12, 0);
    m.ax() = 2;
    m.cx() = 0x201;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        m.cs() = caller_cs;
    }
    hooks::EventScope closing_music(m, hooks::Event::IntroClosingMusicFade, {1});
    intro_call(m, 0x49E9, 0x43C7);
    m.ax() = 0;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x3FFF;
    fill_screen_rect_core(m);
    intro_push(m, m.es());
    m.ax() = 0;
    m.es() = 0;
    m.di() = 0x8000;
    m.cx() = 0x800;
    m.flags() = uint16_t((m.flags() & ~(emu::CF | emu::OF | emu::SF | emu::AF)) | emu::ZF | emu::PF);
    do {
        m.wr16(m.es(), m.di(), 0);
        m.di() = uint16_t(m.di() + ((m.flags() & emu::DF) ? -2 : 2));
    } while (--m.cx());
    m.es() = intro_pop(m);
    m.bp() = 0x46F8;
    m.bx() = 0x804;
    intro_call(m, 0x461C, 0x43F3);
    m.cx() = 0x4B;
    intro_call(m, 0x48AF, 0x43F9);
    intro_call(m, 0x49E9, 0x43FC);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x3FFF;
    fill_screen_rect_core(m);
    return;
}

// rom: 0x3C43AE (near)
// Fade out the intro field, reveal the closing text strip, and clear the scene.
void show_intro_closing_scene(Machine &m) { intro_native_entry(m, show_intro_closing_scene_core); }

namespace {
constexpr uint16_t kIntroMap = 0xD0DC;
constexpr uint16_t kIntroCellX = 0xD0DD;
constexpr uint16_t kIntroCellY = 0xD0DE;
constexpr uint16_t kIntroFacing = 0xC810;
constexpr uint16_t kIntroPlayerType = 0xD005;
constexpr uint16_t kIntroTravelMode = 0xD0D6;
constexpr uint16_t kIntroAnimation = 0xD521;
constexpr uint16_t kIntroPanel = 0xC81A;
constexpr uint16_t kIntroPanelRow = 0xC81C;

// Field cores read tables relative to their own CS, even during a bank-C intro scene.
template <void (*Body)(Machine &)> void intro_field_native(Machine &m) {
    uint16_t caller_cs = m.cs();
    m.cs() = 0x5000;
    Body(m);
    m.cs() = caller_cs;
}
void clear_intro_layer(Machine &m) {
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x3FFF;
    fill_screen_rect_core(m);
}
// The repeated scene prelude clears the text tiles, whites the palette, and fades in its backdrop.
void start_intro_caption(Machine &m) {
    observe_intro(m, IntroStage::White);
    clear_intro_layer(m);
    uint16_t saved_es = m.es();
    m.ax() = 0;
    m.es() = 0;
    m.di() = 0x8000;
    m.cx() = 0x800;
    do {
        m.wr16(m.es(), m.di(), m.ax());
        m.di() = uint16_t(m.di() + ((m.flags() & emu::DF) ? -2 : 2));
    } while (--m.cx());
    m.es() = saved_es;
    m.ax() = 0;
    m.cx() = 16;
    m.dx() = 0xFFF;
    m.di() = 0xC400;
    fill_palette_core(m);
    m.ax() = 0xF103;
    intro_out16(m, 0, m.ax());
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    intro_out16(m, 0x12, 0);
    load_intro_background_core(m);
    intro_call(m, 0x499B);
    m.cx() = 30;
    intro_call(m, 0x48B8);
}
void intro_caption(Machine &m, uint16_t descriptor, uint16_t position) {
    m.bp() = descriptor;
    m.bx() = position;
    reveal_intro_scene_strip_core(m);
}
void intro_caption_pause(Machine &m, uint16_t frames) {
    m.cx() = frames;
    intro_call(m, 0x48B8);
    intro_call(m, 0x4A37);
}
void set_intro_field(Machine &m, uint8_t map, uint8_t x, uint8_t y, uint8_t actor) {
    m.wr8(m.ss(), kIntroMap, map);
    m.wr8(m.ss(), kIntroCellX, x);
    m.wr8(m.ss(), kIntroCellY, y);
    m.wr8(m.ss(), kIntroFacing, 2);
    m.wr8(m.ss(), kIntroPlayerType, actor);
    m.wr8(m.ss(), kIntroTravelMode, 0);
    intro_field_native<initialize_field_display_core>(m);
    observe_intro(m, IntroStage::Field);
}
void set_intro_progress(Machine &m, uint8_t value) {
    m.set_al(14);
    m.dx() = uint16_t((m.dx() & 0xFF00) | value);
    intro_field_native<write_packed_progress_core>(m);
}
// The second scene directs two walkers and a third, oppositely moving NPC.
void set_intro_walker(Machine &m, uint8_t slot, uint8_t type, uint8_t direction, bool horizontal) {
    m.set_al(slot);
    intro_field_native<npc_record_pointer_core>(m);
    m.set_al(type);
    m.wr8(m.ss(), m.si(), m.al());
    m.set_al(direction);
    m.wr8(m.ss(), uint16_t(m.si() + 14), m.al());
    m.ax() = horizontal ? 0xFFFF : 0;
    m.wr16(m.ss(), uint16_t(m.si() + 10), m.ax());
    m.ax() = horizontal ? 0 : 1;
    m.wr16(m.ss(), uint16_t(m.si() + 12), m.ax());
}
void animate_intro_walkers(Machine &m, uint16_t frames, unsigned shift) {
    m.cx() = frames;
    do {
        intro_push(m, m.cx());
        m.ax() = m.cx();
        m.set_al(uint8_t(((m.al() >> shift) & 1) * 2 + 9));
        m.wr8(m.ss(), kIntroAnimation, m.al());
        present_intro_field_frame_core(m);
        m.cx() = intro_pop(m);
    } while (--m.cx());
}
} // namespace

// Shared body of ROM 0x3C3F63
// Second caption/field vignette: four forced player steps, then staged motion of three NPC records.
void run_intro_second_field_scene_core(Machine &m) {
    start_intro_caption(m);
    intro_caption(m, 0x46C8, 0x0802);
    intro_caption_pause(m, 75);
    set_intro_field(m, 0xCA, 0x15, 0x27, 0x96);
    intro_call(m, 0x48C4, 0x3FF0);
    m.cx() = 4;
    do {
        intro_push(m, m.cx());
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            force_player_step_core(m);
            m.cs() = caller_cs;
        };
        m.ax() = uint16_t(m.rd16(m.ss(), 0xC800) & 0xE);
        if (m.ax()) {
            m.jmp_far(0xC000, 0x3D51);
            throw IntroAbort{};
        }
        m.cx() = intro_pop(m);
    } while (--m.cx());
    m.cx() = 30;
    intro_call(m, 0x48AF, 0x400F);
    set_intro_progress(m, 1);
    intro_field_native<initialize_map_npcs_core>(m);
    for (uint8_t slot : {uint8_t(0), uint8_t(1)})
        set_intro_walker(m, slot, 0xC0, 3, false);
    animate_intro_walkers(m, 32, 5);
    set_intro_progress(m, 2);
    intro_field_native<initialize_map_npcs_core>(m);
    for (uint8_t slot : {uint8_t(0), uint8_t(1)})
        set_intro_walker(m, slot, 0xC0, 2, false);
    set_intro_walker(m, 2, 0xC4, 2, true);
    animate_intro_walkers(m, 96, 4);
    for (uint8_t slot : {uint8_t(0), uint8_t(1)})
        set_intro_walker(m, slot, 0xC0, 1, false);
    set_intro_walker(m, 2, 0xC4, 1, true);
    animate_intro_walkers(m, 32, 5);
    intro_call(m, 0x48FC, 0x4174);
    set_intro_progress(m, 0);
    clear_intro_layer(m);
}
// rom: 0x3C3F63 (near)
// Second caption/field vignette: four forced player steps, then staged motion of three NPC records.
void run_intro_second_field_scene(Machine &m) { intro_native_entry(m, run_intro_second_field_scene_core); }

// Shared body of ROM 0x3C474A
// Reveal three banked intro panels from bottom to top, with sixteen scrolling frames per row.
void run_intro_scroll_panels_core(Machine &m) {
    observe_intro(m, IntroStage::Pans);
    m.ax() = 0;
    intro_out16(m, 0x10, 0);
    intro_out16(m, 0x12, 0);
    m.set_al(0xE7);
    m.out8(0xC2, m.al());
    m.ax() = 0x2000;
    m.ds() = m.ax();
    m.ax() = 0;
    m.cx() = 4;
    m.si() = 0x0730;
    m.di() = 0xC600;
    copy_palettes_core(m);
    m.cx() = 3;
    m.wr16(m.ss(), kIntroPanel, m.cx());
    load_intro_scene_tiles_core(m);
    draw_intro_scene_tilemap_core(m);
    --m.cx();
    do {
        m.wr16(m.ss(), kIntroPanel, m.cx());
        intro_push(m, m.cx());
        load_intro_scene_tiles_core(m);
        m.cx() = 18;
        do {
            m.wr16(m.ss(), kIntroPanelRow, m.cx());
            intro_push(m, m.cx());
            draw_intro_scene_strip_core(m);
            m.ax() = 0;
            m.cx() = 4;
            palette_step_toward_core(m);
            m.cx() = 16;
            do {
                intro_push(m, m.cx());
                intro_call(m, 0x489A, 0x47A0);
                dma_palettes_core(m);
                if (m.cx() & 1) {
                    m.set_al(m.in8(0x11));
                    m.set_al(uint8_t(m.al() - 1));
                    m.out8(0x11, m.al());
                }
                m.cx() = intro_pop(m);
            } while (--m.cx());
            m.cx() = intro_pop(m);
        } while (--m.cx());
        m.cx() = intro_pop(m);
        m.cx() = uint16_t(m.cx() - 1);
    } while (m.cx() != 0xFFFF);
    brighten_intro_scene_core(m);
    return;
}

// rom: 0x3C474A (near)
// Reveal three banked intro panels from bottom to top, with sixteen scrolling frames per row.
void run_intro_scroll_panels(Machine &m) { intro_native_entry(m, run_intro_scroll_panels_core); }

// Shared body of ROM 0x3C461C
// Four-frame-per-plane variant of the two-frame caption reveal at 3C4578; registers preserved.
void reveal_intro_scene_strip_slow_core(Machine &m) {
    reveal_intro_scene_strip_with_delay(m, 4);
    return;
}

// rom: 0x3C461C (near)
// Four-frame-per-plane variant of the two-frame caption reveal at 3C4578; registers preserved.
void reveal_intro_scene_strip_slow(Machine &m) { intro_native_entry(m, reveal_intro_scene_strip_slow_core); }

// Shared body of ROM 0x3C418F
// Third field vignette: pan the scene right every fourth loop step, then hold and whiten it.
void run_intro_third_field_scene_core(Machine &m) {
    start_intro_caption(m);
    intro_caption(m, 0x46D0, 0x0803);
    intro_caption_pause(m, 75);
    set_intro_field(m, 0xCB, 0x11, 0x14, 0xAF);
    m.ax() = 0xE0;
    m.wr16(m.ss(), 0xD522, m.ax());
    m.ax() = 0xA0;
    m.wr16(m.ss(), 0xD524, m.ax());
    intro_call(m, 0x48C4, 0x422A);
    m.cx() = 0;
    do {
        intro_push(m, m.cx());
        if (!(m.cx() & 3)) {
            m.cx() &= 0x3C;
            if (!m.cx()) {
                intro_field_native<draw_map_right_edge_core>(m);
                m.wr8(m.ss(), kIntroCellX, uint8_t(m.rd8(m.ss(), kIntroCellX) + 1));
            }
            for (uint16_t scroll : {uint16_t(0xD511), uint16_t(0xD513)})
                m.wr8(m.ss(), scroll, uint8_t(m.rd8(m.ss(), scroll) + 1));
            m.wr16(m.ss(), 0xD59D, uint16_t(m.rd16(m.ss(), 0xD59D) + 1));
            present_intro_field_frame_core(m);
        }
        m.cx() = intro_pop(m);
        ++m.cx();
    } while (m.cx() != 0x200);
    m.cx() = 107;
    wait_intro_field_frames_core(m);
    intro_call(m, 0x48FC, 0x4267);
    clear_intro_layer(m);
    return;
}

// rom: 0x3C418F (near)
// Third field vignette: pan the scene right every fourth loop step, then hold and whiten it.
void run_intro_third_field_scene(Machine &m) { intro_native_entry(m, run_intro_third_field_scene_core); }

// Shared body of ROM 0x3C4331
// Fifth text-only scene: reveal two captions, pause sixty frames and clear the text layer.
void run_intro_fifth_caption_scene_core(Machine &m) {
    start_intro_caption(m);
    intro_caption(m, 0x46E8, 0x0704);
    intro_caption(m, 0x46F0, 0x0A08);
    intro_caption_pause(m, 60);
    clear_intro_layer(m);
    return;
}

// rom: 0x3C4331 (near)
// Fifth text-only scene: reveal two captions, pause sixty frames and clear the text layer.
void run_intro_fifth_caption_scene(Machine &m) { intro_native_entry(m, run_intro_fifth_caption_scene_core); }

// Shared body of ROM 0x3C4279
// Fourth scene's two captions lead into a stationary player pose on map CC, held for 150 frames.
void run_intro_fourth_field_scene_core(Machine &m) {
    start_intro_caption(m);
    intro_caption(m, 0x46D8, 0x0707);
    intro_caption(m, 0x46E0, 0x0A01);
    intro_caption_pause(m, 75);
    set_intro_field(m, 0xCC, 0x13, 0x10, 0xAD);
    m.wr8(m.ss(), kIntroAnimation, 0x11);
    intro_call(m, 0x48C4, 0x4316);
    m.cx() = 150;
    intro_call(m, 0x48AF, 0x431C);
    intro_call(m, 0x48FC, 0x431F);
    clear_intro_layer(m);
    return;
}

// rom: 0x3C4279 (near)
// Fourth scene's two captions lead into a stationary player pose on map CC, held for 150 frames.
void run_intro_fourth_field_scene(Machine &m) { intro_native_entry(m, run_intro_fourth_field_scene_core); }

namespace {
void intro_call(Machine &m, uint16_t offset, uint16_t return_ip) {
    // Keep the original near-call frame present for saves and the nonlocal cleanup.
    intro_push(m, return_ip);
    switch (offset) {
    case 0x4700:
        load_intro_background_core(m);
        break;
    case 0x4578:
        reveal_intro_scene_strip_core(m);
        break;
    case 0x489A:
        intro_wait_frame_core(m);
        break;
    case 0x48B8:
        wait_scrolling_intro_frames_core(m);
        break;
    case 0x48AF:
        wait_intro_frames_core(m);
        break;
    case 0x48C4:
        fade_intro_field_to_target_core(m);
        break;
    case 0x48FC:
        brighten_intro_field_core(m);
        break;
    case 0x4923:
        present_intro_field_frame_core(m);
        break;
    case 0x494A:
        wait_intro_field_frames_core(m);
        break;
    case 0x499B:
        fade_scrolling_intro_in_core(m);
        break;
    case 0x49C2:
        fade_scrolling_intro_to_target_core(m);
        break;
    case 0x49E9:
        darken_intro_scene_core(m);
        break;
    case 0x4A10:
        brighten_intro_scene_core(m);
        break;
    case 0x4A37:
        brighten_scrolling_intro_scene_core(m);
        break;
    case 0x3E6E:
        run_intro_field_scene_core(m);
        break;
    case 0x3F63:
        run_intro_second_field_scene_core(m);
        break;
    case 0x418F:
        run_intro_third_field_scene_core(m);
        break;
    case 0x4279:
        run_intro_fourth_field_scene_core(m);
        break;
    case 0x4331:
        run_intro_fifth_caption_scene_core(m);
        break;
    case 0x474A:
        run_intro_scroll_panels_core(m);
        break;
    case 0x43AE:
        show_intro_closing_scene_core(m);
        break;
    case 0x461C:
        reveal_intro_scene_strip_slow_core(m);
        break;
    default:
        break;
    }
    (void)intro_pop(m);
}
} // namespace
} // namespace sawgod::game

namespace sawgod::game {
namespace {
[[maybe_unused]] constexpr uint16_t kSpriteSixTileAttribute = 0xC019;
[[maybe_unused]] constexpr uint16_t kRam_keys_pressed = 0xC804;
[[maybe_unused]] constexpr uint16_t kRam_keys_repeat = 0xC806;
[[maybe_unused]] constexpr uint16_t kRam_key_repeat_timer = 0xC808;
[[maybe_unused]] constexpr uint16_t kRam_random_seed = 0xC80C;
[[maybe_unused]] constexpr uint16_t kOamCount = 0xC80F;
[[maybe_unused]] constexpr uint16_t kPlayerFacing = 0xC810;
[[maybe_unused]] constexpr uint16_t kBlinkEnabled = 0xC811;
[[maybe_unused]] constexpr uint16_t kFieldOverlayMode = 0xC812;
[[maybe_unused]] constexpr uint16_t kFieldWindowTopLeft = 0xC814;
[[maybe_unused]] constexpr uint16_t kFieldWindowBottomRight = 0xC816;
[[maybe_unused]] constexpr uint16_t kRam_intro_saved_sp = 0xC818;
[[maybe_unused]] constexpr uint16_t kRam_scene_index = 0xC81A;
[[maybe_unused]] constexpr uint16_t kRam_intro_strip = 0xC81C;
[[maybe_unused]] constexpr uint16_t kBattleSceneScratch = 0xC81E;
[[maybe_unused]] constexpr uint16_t kRam_music_request = 0xC820;
[[maybe_unused]] constexpr uint16_t kRam_sfx_request = 0xC821;
[[maybe_unused]] constexpr uint16_t kRam_music_current = 0xC822;
[[maybe_unused]] constexpr uint16_t kFieldEventPending = 0xC825;
[[maybe_unused]] constexpr uint16_t kFieldEventKind = 0xC826;
[[maybe_unused]] constexpr uint16_t kFieldEventArgument = 0xC827;
[[maybe_unused]] constexpr uint16_t kInteractionState = 0xC82A;
[[maybe_unused]] constexpr uint16_t kDialogueSkipDelay = 0xC82B;
[[maybe_unused]] constexpr uint16_t kMemberEquipmentFlags = 0xD00A;
[[maybe_unused]] constexpr uint16_t kSecondMemberEquipmentFlags = 0xD029;
[[maybe_unused]] constexpr uint16_t kThirdMemberEquipmentFlags = 0xD048;
[[maybe_unused]] constexpr uint16_t kFourthMemberEquipmentFlags = 0xD067;
[[maybe_unused]] constexpr uint16_t kRam_inventory_slots = 0xD07C;
[[maybe_unused]] constexpr uint16_t kInventorySlot0Uses = 0xD07D;
[[maybe_unused]] constexpr uint16_t kInventorySlot1Id = 0xD07E;
[[maybe_unused]] constexpr uint16_t kInventorySlot1Uses = 0xD07F;
[[maybe_unused]] constexpr uint16_t kInventorySlot2Id = 0xD080;
[[maybe_unused]] constexpr uint16_t kInventorySlot2Uses = 0xD081;
[[maybe_unused]] constexpr uint16_t kInventorySlot3Id = 0xD082;
[[maybe_unused]] constexpr uint16_t kInventorySlot3Uses = 0xD083;
[[maybe_unused]] constexpr uint16_t kInventorySlot4Id = 0xD084;
[[maybe_unused]] constexpr uint16_t kInventorySlot4Uses = 0xD085;
[[maybe_unused]] constexpr uint16_t kInventorySlot5Id = 0xD086;
[[maybe_unused]] constexpr uint16_t kInventorySlot5Uses = 0xD087;
[[maybe_unused]] constexpr uint16_t kInventorySlot6Id = 0xD088;
[[maybe_unused]] constexpr uint16_t kInventorySlot6Uses = 0xD089;
[[maybe_unused]] constexpr uint16_t kInventorySlot7Id = 0xD08A;
[[maybe_unused]] constexpr uint16_t kInventorySlot7Uses = 0xD08B;
[[maybe_unused]] constexpr uint16_t kRam_gold = 0xD0A2;
[[maybe_unused]] constexpr uint16_t kGoldHigh = 0xD0A4;
[[maybe_unused]] constexpr uint16_t kSecondVehicleMap = 0xD0B3;
[[maybe_unused]] constexpr uint16_t kSecondVehicleX = 0xD0B4;
[[maybe_unused]] constexpr uint16_t kSecondVehicleY = 0xD0B5;
[[maybe_unused]] constexpr uint16_t kSecondVehicleState = 0xD0B6;
[[maybe_unused]] constexpr uint16_t kTravelMode = 0xD0D6;
[[maybe_unused]] constexpr uint16_t kPreviousMap = 0xD0D7;
[[maybe_unused]] constexpr uint16_t kPreviousPlayerX = 0xD0D8;
[[maybe_unused]] constexpr uint16_t kPreviousPlayerY = 0xD0D9;
[[maybe_unused]] constexpr uint16_t kRam_current_map = 0xD0DC;
[[maybe_unused]] constexpr uint16_t kRam_player_x = 0xD0DD;
[[maybe_unused]] constexpr uint16_t kRam_player_y = 0xD0DE;
[[maybe_unused]] constexpr uint16_t kDialogueLeader = 0xD0E3;
[[maybe_unused]] constexpr uint16_t kEndingSeen = 0xD0E5;
[[maybe_unused]] constexpr uint16_t kDialogueSpeed = 0xD0E6;
[[maybe_unused]] constexpr uint16_t kRam_battle_options = 0xD0E7;
[[maybe_unused]] constexpr uint16_t kFormationBackTileSource = 0xD500;
[[maybe_unused]] constexpr uint16_t kFieldPreSpriteCallback = 0xD501;
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteSource = 0xD502;
[[maybe_unused]] constexpr uint16_t kFieldPostSpriteCallback = 0xD503;
[[maybe_unused]] constexpr uint16_t kFormationBackMapSource = 0xD504;
[[maybe_unused]] constexpr uint16_t kFieldNpcCallback = 0xD505;
[[maybe_unused]] constexpr uint16_t kFormationBackTileCount = 0xD506;
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteVariant = 0xD508;
[[maybe_unused]] constexpr uint16_t kBattleWipeScroll = 0xD509;
[[maybe_unused]] constexpr uint16_t kBattleWipeFirstX = 0xD50A;
[[maybe_unused]] constexpr uint16_t kBattleWipeSecondScroll = 0xD50B;
[[maybe_unused]] constexpr uint16_t kBattleWipeSecondX = 0xD50C;
[[maybe_unused]] constexpr uint16_t kBattleWipeThirdScroll = 0xD50D;
[[maybe_unused]] constexpr uint16_t kFormationMiddlePaletteSource = 0xD50E;
[[maybe_unused]] constexpr uint16_t kBattleWipeFourthScroll = 0xD50F;
[[maybe_unused]] constexpr uint16_t kBattleWipeFourthX = 0xD510;
[[maybe_unused]] constexpr uint16_t kFieldScroll = 0xD511;
[[maybe_unused]] constexpr uint16_t kFieldScrollHigh = 0xD512;
[[maybe_unused]] constexpr uint16_t kIntroOverlayProgress = 0xD513;
[[maybe_unused]] constexpr uint16_t kIntroOverlayProgressHigh = 0xD514;
[[maybe_unused]] constexpr uint16_t kMoveDelta = 0xD515;
[[maybe_unused]] constexpr uint16_t kFormationMiddleHeight = 0xD516;
[[maybe_unused]] constexpr uint16_t kMoveShift = 0xD517;
[[maybe_unused]] constexpr uint16_t kFormationFrontTileSource = 0xD518;
[[maybe_unused]] constexpr uint16_t kFieldFadeActive = 0xD519;
[[maybe_unused]] constexpr uint16_t kFieldPaletteFirstFrame = 0xD51A;
[[maybe_unused]] constexpr uint16_t kFieldPaletteTimer = 0xD51B;
[[maybe_unused]] constexpr uint16_t kFieldPaletteSecondFrame = 0xD51C;
[[maybe_unused]] constexpr uint16_t kFieldTileAnimationFrame = 0xD51E;
[[maybe_unused]] constexpr uint16_t kFieldTileAnimationTimer = 0xD51F;
[[maybe_unused]] constexpr uint16_t kSpriteSetIndex = 0xD520;
[[maybe_unused]] constexpr uint16_t kPlayerSpriteFacing = 0xD521;
[[maybe_unused]] constexpr uint16_t kOamBaseY = 0xD522;
[[maybe_unused]] constexpr uint16_t kFormationFrontPaletteCount = 0xD523;
[[maybe_unused]] constexpr uint16_t kOamBaseX = 0xD524;
[[maybe_unused]] constexpr uint16_t kBlinkToggleA = 0xD526;
[[maybe_unused]] constexpr uint16_t kBlinkPeriodA = 0xD528;
[[maybe_unused]] constexpr uint16_t kPlayerStepped = 0xD52A;
[[maybe_unused]] constexpr uint16_t kOverlayEnabled = 0xD52B;
[[maybe_unused]] constexpr uint16_t kScriptActive = 0xD52C;
[[maybe_unused]] constexpr uint16_t kFormationFrontPaletteSlot = 0xD52E;
[[maybe_unused]] constexpr uint16_t kFormationFrontTileWidth = 0xD52F;
[[maybe_unused]] constexpr uint16_t kFormationFrontTileHeight = 0xD530;
[[maybe_unused]] constexpr uint16_t kFormationFrontDrawCount = 0xD531;
[[maybe_unused]] constexpr uint16_t kFieldAnimationPaletteSource = 0xD538;
[[maybe_unused]] constexpr uint16_t kCamX = 0xD59D;
[[maybe_unused]] constexpr uint16_t kCamY = 0xD59F;
[[maybe_unused]] constexpr uint16_t kNpcLastMatch = 0xD5A1;
[[maybe_unused]] constexpr uint16_t kRasterWaveOffset = 0xD6B5;
[[maybe_unused]] constexpr uint16_t kRasterWavePhase = 0xD6B6;
[[maybe_unused]] constexpr uint16_t kRasterWaveSource = 0xD6B7;
[[maybe_unused]] constexpr uint16_t kRam_enemy_group_count = 0xD6D4;
[[maybe_unused]] constexpr uint16_t kEncounterVariant = 0xD6EE;
[[maybe_unused]] constexpr uint16_t kRam_line_scroll_table = 0xD6FE;
[[maybe_unused]] constexpr uint16_t kIntroProjectileTicks = 0xD6FF;
[[maybe_unused]] constexpr uint16_t kIntroProjectileY = 0xD700;
[[maybe_unused]] constexpr uint16_t kIntroProjectileBasePosition = 0xD701;
[[maybe_unused]] constexpr uint16_t kIntroProjectileState = 0xD702;
[[maybe_unused]] constexpr uint16_t kIntroProjectileXOffset = 0xD703;
[[maybe_unused]] constexpr uint16_t kIntroProjectilePhase = 0xD705;
[[maybe_unused]] constexpr uint16_t kIntroProjectileFrame = 0xD706;
[[maybe_unused]] constexpr uint16_t kIntroProjectileYOffset = 0xD707;
[[maybe_unused]] constexpr uint16_t kIntroProjectilePosition = 0xD708;
[[maybe_unused]] constexpr uint16_t kIntroProjectileOrigin = 0xD70A;
[[maybe_unused]] constexpr uint16_t kIntroProjectileStep = 0xD70C;
[[maybe_unused]] constexpr uint16_t kFormationKind = 0xD70D;
[[maybe_unused]] constexpr uint16_t kRam_battle_flags = 0xD70E;
[[maybe_unused]] constexpr uint16_t kMenuActionIndex = 0xD840;
[[maybe_unused]] constexpr uint16_t kRam_race_select_index = 0xD841;
[[maybe_unused]] constexpr uint16_t kRam_class_select_index = 0xD842;
[[maybe_unused]] constexpr uint16_t kRam_gender_select_index = 0xD843;
[[maybe_unused]] constexpr uint16_t kMenuThirdSelection = 0xD844;
[[maybe_unused]] constexpr uint16_t kMenuFourthSelection = 0xD845;
[[maybe_unused]] constexpr uint16_t kMenuFifthSelection = 0xD846;
[[maybe_unused]] constexpr uint16_t kMenuSixthSelection = 0xD847;
[[maybe_unused]] constexpr uint16_t kMenuExitRequested = 0xD84A;
[[maybe_unused]] constexpr uint16_t kMenuActionResult = 0xD84B;
[[maybe_unused]] constexpr uint16_t kMenuPriceLow = 0xD84C;
[[maybe_unused]] constexpr uint16_t kMenuPriceHigh = 0xD84E;
[[maybe_unused]] constexpr uint16_t kMenuDescriptionSpriteStart = 0xD850;
[[maybe_unused]] constexpr uint16_t kCreationLeader = 0xD852;
[[maybe_unused]] constexpr uint16_t kCreationRace = 0xD853;
[[maybe_unused]] constexpr uint16_t kNamePage = 0xD854;
[[maybe_unused]] constexpr uint16_t kNameInsertionIndex = 0xD855;
[[maybe_unused]] constexpr uint16_t kNameText = 0xD856;
[[maybe_unused]] constexpr uint16_t kMenuSecondItemBuffer = 0xD857;
[[maybe_unused]] constexpr uint16_t kMenuTransferState = 0xD859;
[[maybe_unused]] constexpr uint16_t kShopCursorBlink = 0xD85D;
[[maybe_unused]] constexpr uint16_t kBattleStatusSuccessCount = 0xDBF3;
[[maybe_unused]] constexpr uint16_t kItemUseCount = 0xDBF4;
[[maybe_unused]] constexpr uint16_t kBattleKnockoutMode = 0xDBF6;
[[maybe_unused]] constexpr uint16_t kBattleFlashCounter = 0xDBF7;
[[maybe_unused]] constexpr uint16_t kBattleGroupEliminationScratch = 0xDBF9;
[[maybe_unused]] constexpr uint16_t kBattleEffectReflected = 0xDBFA;
[[maybe_unused]] constexpr uint16_t kBattleReflectionActor = 0xDBFB;
[[maybe_unused]] constexpr uint16_t kActionId = 0xDBFC;
[[maybe_unused]] constexpr uint16_t kRam_action_effect_id = 0xDBFD;
[[maybe_unused]] constexpr uint16_t kRam_acting_actor = 0xDBFE;
[[maybe_unused]] constexpr uint16_t kActionTarget = 0xDBFF;
[[maybe_unused]] constexpr uint16_t kRam_target_group = 0xDC00;
[[maybe_unused]] constexpr uint16_t kBattleSavedAttackValue = 0xDC09;
[[maybe_unused]] constexpr uint16_t kBattleItemPower = 0xDC0A;
[[maybe_unused]] constexpr uint16_t kRam_battle_amount = 0xDC0B;
[[maybe_unused]] constexpr uint16_t kBattleMultiHitCount = 0xDC11;
[[maybe_unused]] constexpr uint16_t kEffectPaletteVariant = 0xDC12;
[[maybe_unused]] constexpr uint16_t kEffectTicks = 0xDC18;
[[maybe_unused]] constexpr uint16_t kEffectFrame = 0xDC19;
[[maybe_unused]] constexpr uint16_t kEffectX = 0xDCBB;
[[maybe_unused]] constexpr uint16_t kEffectY = 0xDCBC;
[[maybe_unused]] constexpr uint16_t kEffectRowY = 0xDCBD;
[[maybe_unused]] constexpr uint16_t kEffectHeight = 0xDCBE;
[[maybe_unused]] constexpr uint16_t kEffectSpriteOffset = 0xDCBF;
[[maybe_unused]] constexpr uint16_t kGroupDrainSavedTarget = 0xDCC1;
[[maybe_unused]] constexpr uint16_t kTextColorScratch = 0xDCD3;
[[maybe_unused]] constexpr uint16_t kBattleImmunityMode = 0xDCD4;
[[maybe_unused]] constexpr uint16_t kEffectMotionCounter = 0xDCEC;
[[maybe_unused]] constexpr uint16_t kEffectTravelOffset = 0xDCFC;
[[maybe_unused]] constexpr uint16_t kDialogueCursorPosition = 0xDD28;
[[maybe_unused]] constexpr uint16_t kFlarePhaseTicks = 0xDD29;
[[maybe_unused]] constexpr uint16_t kEffectTargetMode = 0xDD73;
[[maybe_unused]] constexpr uint16_t kFooterPanelMode = 0xEC83;
[[maybe_unused]] constexpr uint16_t kGroupDrainTargetCount = 0xEC84;
[[maybe_unused]] constexpr uint16_t kGroupDrainAmount = 0xEC85;
[[maybe_unused]] constexpr uint16_t kGroupDrainPreviousGroup = 0xEC86;
[[maybe_unused]] constexpr uint16_t kVignetteBackdropColor = 0xFFE0;
} // namespace
// rom: 0x3C0000
// Dispatch AL through the intro command table while preserving registers and segments.
// A malformed opcode indexes instruction bytes beyond the table. Continue that accidental execution
// at the original CALL BX rather than inventing a native handler or a trampoline return address.
bool dispatch_intro_command_core(Machine &m) {
    const bool outside_table = m.al() >= 73;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bx() = computed::logic<uint16_t>(m, 0);
    m.bx() = m.al();
    m.bx() = computed::add<uint16_t>(m, m.bx(), m.bx());
    m.bx() = computed::add<uint16_t>(m, m.bx(), 0x1C);
    m.bx() = m.rd16(m.cs(), m.bx());
    m.bx() = computed::logic<uint16_t>(m, m.bx());
    if (outside_table) {
        m.jmp_far(0xC000, m.bx() ? 0x0015 : 0x0017);
        return false;
    }
    if (m.bx()) {
        dispatch_computed_handler_core(m, m.bx());
        if (m.cs() != 0xC000)
            return false;
    }
    m.ss() = computed::pop(m);
    m.es() = computed::pop(m);
    m.ds() = computed::pop(m);
    computed::restore_registers(m);
    return true;
}
void dispatch_intro_command(Machine &m) {
    if (dispatch_intro_command_core(m) && m.cs() == 0xC000)
        m.retf();
}
// rom: 0x3C00EA (near)
// Play AX's sound effect unless the intro mute flag is set.
void play_intro_sound_if_enabled_core(Machine &m) {
    computed::push(m, m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldOverlayMode))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    m.ax() = uint16_t(computed::pop(m));
    if (!(m.flags() & emu::ZF))
        goto branch_00f9;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_00f9:;
    return;
}
void play_intro_sound_if_enabled(Machine &m) {
    play_intro_sound_if_enabled_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C00FA (near)
// Store AL's music request unless the intro mute flag is set.
void request_intro_music_if_enabled_core(Machine &m) {
    computed::push(m, m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldOverlayMode))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    m.ax() = uint16_t(computed::pop(m));
    if (!(m.flags() & emu::ZF))
        goto branch_0108;
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
branch_0108:;
    return;
}
void request_intro_music_if_enabled(Machine &m) {
    request_intro_music_if_enabled_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0109 (near)
// Change player facing and animation using the current facing's lookup pair.
void turn_intro_player_from_facing_table_core(Machine &m) {
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kPlayerFacing))));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x0125))));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x0126))));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    return;
}
void turn_intro_player_from_facing_table(Machine &m) {
    turn_intro_player_from_facing_table_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C012D
// Walk the player to BL:BH, completing vertical movement before horizontal movement.
void move_intro_player_vertical_first_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBlinkEnabled))));
    computed::push(m, m.ax());
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(
                                              m, uint8_t(m.bx()), m.rd8(m.ss(), uint16_t(kRam_player_x)))));
    m.bx() =
        uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(computed::subtract<uint8_t>(
                                          m, uint8_t(m.bx() >> 8), m.rd8(m.ss(), uint16_t(kRam_player_y)))))
                                      << 8));
    m.dx() = uint16_t(m.bx());
branch_0148:;
    m.dx() = uint16_t(
        (m.dx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) | uint8_t(m.dx() >> 8)))))
         << 8));
    if (m.flags() & emu::ZF)
        goto branch_0177;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_0164;
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx() >> 8), true))) << 8));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_0148;
branch_0164:;
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx() >> 8), true))) << 8));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_0148;
branch_0177:;
    m.dx() = uint16_t((m.dx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) | uint8_t(m.dx())))));
    if (m.flags() & emu::ZF)
        goto branch_01a6;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_0193;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx()))));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_0177;
branch_0193:;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx()), true)));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_0177;
branch_01a6:;
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void move_intro_player_vertical_first(Machine &m) {
    move_intro_player_vertical_first_core(m);
    if (m.cs() == 0xC000)
        m.retf();
}
// rom: 0x3C01B0
// Walk the player to BL:BH, completing horizontal movement before vertical movement.
void move_intro_player_horizontal_first_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBlinkEnabled))));
    computed::push(m, m.ax());
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(
                                              m, uint8_t(m.bx()), m.rd8(m.ss(), uint16_t(kRam_player_x)))));
    m.bx() =
        uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(computed::subtract<uint8_t>(
                                          m, uint8_t(m.bx() >> 8), m.rd8(m.ss(), uint16_t(kRam_player_y)))))
                                      << 8));
    m.dx() = uint16_t(m.bx());
branch_01cb:;
    m.dx() = uint16_t((m.dx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) | uint8_t(m.dx())))));
    if (m.flags() & emu::ZF)
        goto branch_01fa;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_01e7;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx()))));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_01cb;
branch_01e7:;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx()), true)));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_01cb;
branch_01fa:;
    m.dx() = uint16_t(
        (m.dx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) | uint8_t(m.dx() >> 8)))))
         << 8));
    if (m.flags() & emu::ZF)
        goto branch_0229;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_0216;
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx() >> 8), true))) << 8));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_01fa;
branch_0216:;
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx() >> 8)))) << 8));
    computed::push(m, m.dx());
    m.bx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        attempt_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        finish_player_move_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    goto branch_01fa;
branch_0229:;
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void move_intro_player_horizontal_first(Machine &m) {
    move_intro_player_horizontal_first_core(m);
    if (m.cs() == 0xC000)
        m.retf();
}
// rom: 0x3C0233
// Shake both field layers with random offsets over thirty two-frame steps.
void shake_intro_field_core(Machine &m) {
    // A field effect after the dialogue window closed: the native picture is complete, so a held host picture
    // releases and the effect shows.
    hooks::emit(hooks::Event::PresentationPicture, m, {2});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_top_edge_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_bottom_edge_call_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_left_edge_call_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_right_edge_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x10)) | (uint16_t(m.in8(uint8_t(0x10 + 1))) << 8)));
    m.bx() = uint16_t(m.ax());
    m.cx() = uint16_t(0x1E);
branch_0252:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x1000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x1000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.bx());
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.dx()))));
    m.set_ah(uint8_t(computed::add<uint8_t>(m, m.ah(), uint8_t(m.dx() >> 8))));
    computed::out_word(m, uint8_t(0x10), m.ax());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0252;
    m.ax() = uint16_t(m.bx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void shake_intro_field(Machine &m) {
    shake_intro_field_core(m);
    if (m.cs() == 0xC000)
        m.retf();
}
// rom: 0x3C028C
// Draw a sprite tile grid and advance the shared sprite cursor by its area.
void append_intro_sprite_grid_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOamCount))));
    computed::push(m, m.cx());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_sprite_tile_grid_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(uint8_t(m.cx())));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.cx() >> 8);
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kOamCount),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kOamCount)), m.al())));
    return;
}
void append_intro_sprite_grid(Machine &m) {
    append_intro_sprite_grid_core(m);
    if (m.cs() == 0xC000)
        m.retf();
}
// rom: 0x3C02A3
// Draw an offset sprite tile grid and advance the shared sprite cursor by its area.
void append_intro_sprite_grid_offset_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOamCount))));
    computed::push(m, m.cx());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_sprite_tile_grid_offset_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(uint8_t(m.cx())));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.cx() >> 8);
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kOamCount),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kOamCount)), m.al())));
    return;
}
void append_intro_sprite_grid_offset(Machine &m) {
    append_intro_sprite_grid_offset_core(m);
    if (m.cs() == 0xC000)
        m.retf();
}
// rom: 0x3C02BA (near)
// Start the falling-particle sequence with its signed mode enabled.
void animate_intro_falling_particles_core(Machine &m) {
    m.set_al(uint8_t(0xFF));
    {
        const uint16_t callback_cs = m.cs();
        animate_intro_particle_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void animate_intro_falling_particles(Machine &m) {
    animate_intro_falling_particles_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C02C0 (near)
// Start the falling-particle sequence with its stationary mode selected.
void animate_intro_stationary_particles_core(Machine &m) {
    m.set_al(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        animate_intro_particle_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void animate_intro_stationary_particles(Machine &m) {
    animate_intro_stationary_particles_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C02C6 (near)
// Run the stationary particle sequence through its alternate intro entry point.
void animate_intro_return_particles_core(Machine &m) {
    m.set_al(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        animate_intro_particle_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void animate_intro_return_particles(Machine &m) {
    animate_intro_return_particles_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C02CC (near)
// Load the particle tiles and palette and animate sprite particles over field frames.
void animate_intro_particle_sequence_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    computed::push(m, m.ax());
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(0x64EC);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x62EC);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_0326;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.bx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.bp() = uint16_t(m.ax());
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xD5AE));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0006)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0008)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    goto branch_0330;
branch_0326:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kOamBaseY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kOamBaseX)))) << 8));
branch_0330:;
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.bx());
    m.dx() = uint16_t(0x2C80);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    m.dx() = uint16_t(0x2C81);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    m.dx() = uint16_t(0x2C82);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void animate_intro_particle_sequence(Machine &m) {
    animate_intro_particle_sequence_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0382 (near)
// Load effect tiles and animate two fixed-point projectile groups with timed bursts.
void animate_intro_projectile_burst_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x42));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.ax() = uint16_t(0xFC);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.wr8(m.ss(), uint16_t(kIntroProjectileBasePosition), uint8_t(m.al()));
    m.ax() = uint16_t(0xE8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.wr8(m.ss(), uint16_t(kIntroProjectileState), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table), uint8_t(m.al()));
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kIntroProjectileTicks), uint8_t(m.al()));
    m.ax() = uint16_t(0x100);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x8));
    m.wr16(m.ss(), uint16_t(kIntroProjectilePosition), uint16_t(m.ax()));
    m.ax() = uint16_t(0xEC);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x8));
    m.wr16(m.ss(), uint16_t(kIntroProjectileOrigin), uint16_t(m.ax()));
    m.ax() = uint16_t(0xFF80);
    m.wr16(m.ss(), uint16_t(kIntroProjectileStep), uint16_t(m.ax()));
    m.ax() = uint16_t(0xFF00);
    m.wr16(m.ss(), uint16_t(kRam_battle_flags), uint16_t(m.ax()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kIntroProjectilePhase), uint8_t(m.al()));
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kIntroProjectileFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x44);
    m.si() = uint16_t(0x20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2D);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_0423:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kIntroProjectilePhase))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_049c;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_044c;
    m.wr8(m.ss(), uint16_t(kIntroProjectileFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kIntroProjectileFrame)))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kIntroProjectileFrame))));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.bx() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x0502))));
    m.wr8(m.ss(), uint16_t(kIntroProjectileYOffset), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x0501))));
branch_044c:;
    m.wr8(m.ss(), uint16_t(kIntroProjectilePhase), uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_battle_flags)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x10));
    m.wr16(m.ss(), uint16_t(kRam_battle_flags), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kIntroProjectilePosition)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kIntroProjectileStep))));
    m.wr16(m.ss(), uint16_t(kIntroProjectilePosition), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kIntroProjectileOrigin)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kRam_battle_flags))));
    m.wr16(m.ss(), uint16_t(kIntroProjectileOrigin), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x8));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kRam_random_seed))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.dx()), 0x2)));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x1))));
    m.dx() = uint16_t((m.dx() & 0xFF00) |
                      uint8_t(computed::add<uint8_t>(m, uint8_t(m.dx()),
                                                     m.rd8(m.ss(), uint16_t(kIntroProjectileYOffset)))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2C80));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_049c:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_line_scroll_table))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_04d0;
    m.wr8(m.ss(), uint16_t(kIntroProjectileTicks),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kIntroProjectileTicks)))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kIntroProjectileTicks))));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.bx() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x050A))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_04fc;
    m.wr8(m.ss(), uint16_t(kIntroProjectileY), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x050B))));
    m.wr8(m.ss(), uint16_t(kIntroProjectileXOffset), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x0509))));
branch_04d0:;
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kIntroProjectileBasePosition)));
    m.bx() = uint16_t((m.bx() & 0xFF00) |
                      uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()),
                                                     m.rd8(m.ss(), uint16_t(kIntroProjectileXOffset)))));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kIntroProjectileY))));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.dx()), uint8_t(m.dx()))));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.dx()), uint8_t(m.dx()))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2C80));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_0423;
branch_04fc:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void animate_intro_projectile_burst(Machine &m) {
    animate_intro_projectile_burst_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0555 (near)
// Darken the selected effect palette over sixteen frames while polling input.
void darken_intro_effect_palette_core(Machine &m) {
    m.cx() = uint16_t(0x8);
branch_0558:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0558;
    return;
}
void darken_intro_effect_palette(Machine &m) {
    darken_intro_effect_palette_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0580 (near)
// Initialize thirty-two falling particles and install their frame callback.
void initialize_intro_falling_particles_core(Machine &m) {
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFieldPreSpriteCallback)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_058b;
    goto branch_061e;
branch_058b:;
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x2);
    m.si() = uint16_t(0x8C0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x8A0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD6FE);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x2));
    m.cx() = uint16_t(0x20);
branch_05c1:;
    m.ax() = uint16_t(0x4000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x20));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x170));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x4));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.ax() = uint16_t(0x4000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x20));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x170));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x4));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(0x1800);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.al())));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.al())));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0008), uint8_t(m.al()));
    m.ax() = uint16_t(0x1001);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0009), uint8_t(m.al()));
    m.ax() = uint16_t(0x1810);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.ax()));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xB));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_05c1;
    m.ax() = uint16_t(0x61F);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
branch_061e:;
    return;
}
void initialize_intro_falling_particles(Machine &m) {
    initialize_intro_falling_particles_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C061F (near)
// Advance particle positions, reset particles past their lifetimes and draw their sprites.
void update_intro_falling_particles_core(Machine &m) {
    m.bp() = uint16_t(0xD6FE);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x12C);
    if (m.flags() & emu::CF)
        goto branch_062e;
    goto branch_06b7;
branch_062e:;
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x19);
    if (!(m.flags() & emu::ZF))
        goto branch_063e;
    m.ax() = uint16_t(0x5F);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_063e:;
    m.bp() = uint16_t(0xD700);
    m.cx() = uint16_t(0x20);
branch_0644:;
    computed::push(m, m.cx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0008))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_0653;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0008), uint8_t(m.al()));
    goto branch_06af;
branch_0653:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(m.bp() + 0x0004))));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(m.bp() + 0x0006))));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x4, true));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0xF0);
    if (!(m.flags() & emu::CF))
        goto branch_06af;
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x10));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x4, true));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0xA0);
    if (!(m.flags() & emu::CF))
        goto branch_06af;
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x10));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0009),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp() + 0x0009)), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_069c;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0009), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x000A),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp() + 0x000A)))));
branch_069c:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x000A))));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x1))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0xC80));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_06af:;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xB));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0644;
    return;
branch_06b7:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    return;
}
void update_intro_falling_particles(Machine &m) {
    update_intro_falling_particles_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C06BE (near)
// Draw the moving effect's 16x16 sprite at its saved position.
void draw_intro_moving_effect_sprite_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0xC90);
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_line_scroll_table)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void draw_intro_moving_effect_sprite(Machine &m) {
    draw_intro_moving_effect_sprite_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C06CE (near)
// Move a projectile across the field and install its persistent draw callback.
void animate_intro_horizontal_projectile_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x14);
    m.si() = uint16_t(0x3A74);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x3A54);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x26);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x340);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t(0x2F0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t(0x27);
    m.dx() = uint16_t(0x0);
branch_0718:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0xC))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0xC80));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x2))) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx()))));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0xF))));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0718;
    m.ax() = uint16_t(0x60);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.wr16(m.ss(), uint16_t(kRam_line_scroll_table), uint16_t(m.bx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0xC90);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x6BE);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    return;
}
void animate_intro_horizontal_projectile(Machine &m) {
    animate_intro_horizontal_projectile_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0760 (near)
// Clear the primary per-frame intro callback.
void clear_intro_primary_callback_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    return;
}
void clear_intro_primary_callback(Machine &m) {
    clear_intro_primary_callback_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0767 (near)
// Scroll the field upward sixteen pixels, pause, then return to its starting position.
void scroll_intro_field_up_and_back_core(Machine &m) {
    m.cx() = uint16_t(0x7);
branch_076a:;
    computed::push(m, m.cx());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_top_edge_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_0773:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(kFieldScrollHigh),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldScrollHigh)), true)));
    m.wr8(m.ss(), uint16_t(kIntroOverlayProgressHigh),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kIntroOverlayProgressHigh)), true)));
    m.wr16(m.ss(), uint16_t(kCamY),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(kCamY)), true)));
    m.wr16(m.ss(), uint16_t(kOamBaseX),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(kOamBaseX)))));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0773;
    m.wr8(m.ss(), uint16_t(kRam_player_y),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kRam_player_y)), true)));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_076a;
    m.cx() = uint16_t(0x4B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
branch_07a5:;
    computed::push(m, m.cx());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_bottom_edge_call_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_07ae:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(kFieldScrollHigh),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldScrollHigh)))));
    m.wr8(m.ss(), uint16_t(kIntroOverlayProgressHigh),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kIntroOverlayProgressHigh)))));
    m.wr16(m.ss(), uint16_t(kCamY),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(kCamY)))));
    m.wr16(m.ss(), uint16_t(kOamBaseX),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(kOamBaseX)), true)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_07ae;
    m.wr8(m.ss(), uint16_t(kRam_player_y),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kRam_player_y)))));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_07a5;
    return;
}
void scroll_intro_field_up_and_back(Machine &m) {
    scroll_intro_field_up_and_back_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C07D6 (near)
// Fade the field, animate orbiting sprite groups and restore NPCs and palettes.
void animate_intro_orbiting_sprite_scene_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x8);
    m.si() = uint16_t(0xBE7C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBE5C);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_primary_orbit_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldOverlayMode))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_081a;
    m.ax() = uint16_t(0x2);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_081a:;
    m.cx() = uint16_t(0x8);
branch_081d:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x5);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_081d;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        backup_field_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        clear_field_npcs_call_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x88);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBF9C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBF7C);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD6FE);
    m.bx() = uint16_t(0x35);
    m.cx() = uint16_t(0x6E0);
    m.dx() = uint16_t(0xA);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.bx()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0008), uint16_t(m.cx()));
    m.ax() = uint16_t(0xC00);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.dx()));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xC));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.bx()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0008), uint16_t(m.cx()));
    m.ax() = uint16_t(0x1150);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.dx()));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xC));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.bx()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0008), uint16_t(m.cx()));
    m.ax() = uint16_t(0x16B0);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.dx()));
    m.cx() = uint16_t(0x1E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x71);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x1E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x32);
branch_08d4:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x3);
    m.bp() = uint16_t(0xD6FE);
branch_08db:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0004)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x1));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        update_intro_orbiting_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xC));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_08db;
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_primary_orbit_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_08d4;
    m.cx() = uint16_t(0x14);
branch_090d:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x3);
    m.bp() = uint16_t(0xD6FE);
branch_0914:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0006)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x3));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        update_intro_orbiting_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xC));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0914;
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_primary_orbit_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_090d;
    m.ax() = uint16_t(0x72);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x64);
branch_094c:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x3);
    m.bp() = uint16_t(0xD6FE);
branch_0953:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        update_intro_orbiting_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0xC));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0953;
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_primary_orbit_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_094c;
    m.ax() = uint16_t(0x29);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x10);
branch_0982:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0982;
    m.cx() = uint16_t(0x4B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x8);
    m.si() = uint16_t(0xBFDC);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBFBC);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_09cf:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_secondary_orbit_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x5);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_09cf;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldOverlayMode))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_0a28;
    m.ax() = uint16_t(0x73);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x20);
branch_0a0f:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_secondary_orbit_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0a0f;
    m.ax() = uint16_t(0x1);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_0a28:;
    m.cx() = uint16_t(0xC);
branch_0a2b:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_secondary_orbit_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x6);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0a2b;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        restore_field_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(0xC600);
    m.di() = uint16_t(0xC400);
    m.cx() = uint16_t(0x100);
    computed::copy_words(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void animate_intro_orbiting_sprite_scene(Machine &m) {
    animate_intro_orbiting_sprite_scene_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0A66 (near)
// Draw the second pair of orbiting 16x16 sprites.
void draw_intro_secondary_orbit_sprites_core(Machine &m) {
    m.ax() = uint16_t(0x130);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x180);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() & 0x4)));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2C80));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void draw_intro_secondary_orbit_sprites(Machine &m) {
    draw_intro_secondary_orbit_sprites_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0A8D (near)
// Advance orbit phases, sample wave offsets and draw the orbiting sprites.
void update_intro_orbiting_sprites_core(Machine &m) {
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0008)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(m.bp() + 0x0004))));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0008), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x000A)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(m.bp() + 0x0006))));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x000A)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x4, true));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_alternate_music_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0008)));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x4));
    {
        const int32_t product = int32_t(int16_t(m.ax())) * int16_t(m.dx());
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(uint32_t(product) >> 16);
        computed::flag(m, emu::CF | emu::OF, product < -32768 || product > 32767);
    }
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x8, true));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x134));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x000A)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x4, true));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.ax() = m.rd16(m.cs(), uint16_t(0xcd7 + m.al() * 2));
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0008)));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x4));
    {
        const int32_t product = int32_t(int16_t(m.ax())) * int16_t(m.dx());
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(uint32_t(product) >> 16);
        computed::flag(m, emu::CF | emu::OF, product < -32768 || product > 32767);
    }
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x8, true));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x184));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE0);
    if (!(m.flags() & emu::CF))
        goto branch_0b03;
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x90);
    if (!(m.flags() & emu::CF))
        goto branch_0b03;
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.dx() = uint16_t(0x888);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_0b03:;
    return;
}
void update_intro_orbiting_sprites(Machine &m) {
    update_intro_orbiting_sprites_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0B04 (near)
// Draw the first pair of orbiting 16x16 sprites.
void draw_intro_primary_orbit_sprites_core(Machine &m) {
    m.ax() = uint16_t(0x130);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x170);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.dx() = uint16_t(0x2C80);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x130);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x180);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.dx() = uint16_t(0x2C84);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void draw_intro_primary_orbit_sprites(Machine &m) {
    draw_intro_primary_orbit_sprites_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0B3D (near)
// Shake the field, perform the expanding-window split wipe and restore the map.
void animate_intro_window_split_transition_core(Machine &m) {
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_player_x)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x13);
    if (!(m.flags() & emu::ZF))
        goto branch_0b68;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx() >> 8), 0x15);
    if (!(m.flags() & emu::ZF))
        goto branch_0b5a;
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    goto branch_0b87;
branch_0b5a:;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    goto branch_0b87;
branch_0b68:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x10);
    if (!(m.flags() & emu::ZF))
        goto branch_0b7b;
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x6));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    goto branch_0b87;
branch_0b7b:;
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
branch_0b87:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x63);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        shake_intro_field_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(0xC000);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0005))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0005), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0009), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000D))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x000D), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t callback_cs = m.cs();
        request_intro_music_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x5);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        animate_field_split_wipe_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FEF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.es());
    m.ax() = uint16_t(0x1000);
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(0x1FEF);
    m.cx() = uint16_t(0x100);
    m.di() = uint16_t(0x1900);
branch_0c03:;
    m.wr16(m.es(), uint16_t(m.di()), uint16_t(m.ax()));
    m.wr16(m.es(), uint16_t(m.di() + 0x0002), uint16_t(m.ax()));
    m.wr16(m.es(), uint16_t(m.di() + 0x0004), uint16_t(m.ax()));
    m.wr16(m.es(), uint16_t(m.di() + 0x0006), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x8));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0c03;
    m.es() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x8F);
    {
        const uint16_t callback_cs = m.cs();
        request_intro_music_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_0c27:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_and_draw_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0c27;
    m.cx() = uint16_t(0x10);
branch_0c53:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_and_draw_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0c53;
    return;
}
void animate_intro_window_split_transition(Machine &m) {
    animate_intro_window_split_transition_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0C85 (near)
// Shake the field, perform the short split fade and restore the map.
void animate_intro_split_fade_transition_core(Machine &m) {
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kFieldFadeActive), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        shake_intro_field_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(0xC000);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0005))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0005), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0009), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000D))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x000D), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        animate_field_split_fade_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FEF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_0cd9:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0cd9;
    m.cx() = uint16_t(0x10);
branch_0cf1:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0cf1;
    return;
}
void animate_intro_split_fade_transition(Machine &m) {
    animate_intro_split_fade_transition_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0D0F (near)
// Flash white, force a step, run the scripted battle and restore the field palettes.
void run_intro_white_flash_battle_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kFieldFadeActive), uint8_t(0x1));
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.dx() = uint16_t(0x0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(0x1));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPlayerFacing))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0d3d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_0d3d;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        force_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
branch_0d3d:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        force_player_step_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(0x0));
    m.ax() = uint16_t(0x5);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_0d53:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0d53;
    m.ax() = uint16_t(0x2);
    m.wr16(m.ds(), uint16_t(kRam_intro_saved_sp), uint16_t(m.ax()));
    m.ax() = uint16_t(0x27);
    m.wr16(m.ss(), uint16_t(kRam_scene_index), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kIntroStrip), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleSceneScratch), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        if (!run_battle_core(m))
            return;
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kFieldFadeActive), uint8_t(m.al()));
    m.cx() = uint16_t(0x10);
branch_0d97:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x8);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0d97;
    return;
}
void run_intro_white_flash_battle(Machine &m) {
    run_intro_white_flash_battle_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0DBD (near)
// Animate the upper horizontal sprite strip and fade its effect palette back afterward.
void animate_intro_upper_sprite_strip_core(Machine &m) {
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kFieldFadeActive), uint8_t(m.al()));
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x70);
    m.si() = uint16_t(0x1220);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x1200);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xE7E);
branch_0dee:;
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp())));
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0004)));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x6));
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (m.flags() & emu::ZF)
        goto branch_0e3f;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_0e0f;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
branch_0e0f:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.bp());
    m.ax() = uint16_t(0x120);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x160);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x8));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0e0f;
    goto branch_0dee;
branch_0e3f:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
    m.cx() = uint16_t(0x28);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_0e55:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0e55;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kFieldFadeActive), uint8_t(m.al()));
    return;
}
void animate_intro_upper_sprite_strip(Machine &m) {
    animate_intro_upper_sprite_strip_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0ECC (near)
// Animate the lower horizontal sprite strip over the field.
void animate_intro_lower_sprite_strip_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x70);
    m.si() = uint16_t(0x1220);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x1200);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xF4A);
branch_0ef7:;
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp())));
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0004)));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x6));
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (m.flags() & emu::ZF)
        goto branch_0f48;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_0f18;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
branch_0f18:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.bp());
    m.ax() = uint16_t(0x140);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xC0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x8));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0f18;
    goto branch_0ef7;
branch_0f48:;
    return;
}
void animate_intro_lower_sprite_strip(Machine &m) {
    animate_intro_lower_sprite_strip_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0F98 (near)
// Install the periodic-sound callback and lower effect palettes toward their targets.
void start_intro_periodic_sound_fade_core(Machine &m) {
    m.ax() = uint16_t(0xFE3);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    m.ax() = uint16_t(0x1);
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldFadeActive), uint8_t(m.al()));
    m.si() = uint16_t(0xC600);
    m.cx() = uint16_t(0x40);
branch_0fb0:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0xF00)));
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.ax()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0fb0;
    m.cx() = uint16_t(0x8);
branch_0fc2:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0fc2;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kFieldFadeActive), uint8_t(m.al()));
    return;
}
void start_intro_periodic_sound_fade(Machine &m) {
    start_intro_periodic_sound_fade_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0FE3 (near)
// Play the recurring intro sound when its forty-frame timer expires.
void update_intro_periodic_sound_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kRam_line_scroll_table)), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_0ff8;
    m.set_al(uint8_t(0x28));
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table), uint8_t(m.al()));
    m.ax() = uint16_t(0x6B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_0ff8:;
    return;
}
void update_intro_periodic_sound(Machine &m) {
    update_intro_periodic_sound_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C0FF9 (near)
// Load effect graphics and install the secondary moving-sprite draw callback.
void start_intro_secondary_moving_sprite_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x40);
    m.si() = uint16_t(0xA00);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x9C0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1087);
    m.wr16(m.ss(), uint16_t(kFieldPostSpriteCallback), uint16_t(m.ax()));
    m.ax() = uint16_t(0x1F8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x1C8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.wr16(m.ss(), uint16_t(kRam_line_scroll_table), uint16_t(m.bx()));
    return;
}
void start_intro_secondary_moving_sprite(Machine &m) {
    start_intro_secondary_moving_sprite_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1042 (near)
// Advance the moving sprite over sixteen frames, pause and remove its callback.
void finish_intro_secondary_moving_sprite_core(Machine &m) {
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_player_x)));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x20)));
    m.set_ah(uint8_t(computed::subtract<uint8_t>(m, m.ah(), 0x1D)));
    m.dx() = uint16_t(m.ax());
    m.cx() = uint16_t(0x10);
branch_1050:;
    computed::push(m, m.cx());
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_line_scroll_table)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), uint8_t(m.dx()))));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), uint8_t(m.dx() >> 8)))) << 8));
    m.wr16(m.ss(), uint16_t(kRam_line_scroll_table), uint16_t(m.bx()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1050;
    m.cx() = uint16_t(0x1E);
branch_1070:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1070;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldPostSpriteCallback), uint16_t(m.ax()));
    m.ax() = uint16_t(0x2B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void finish_intro_secondary_moving_sprite(Machine &m) {
    finish_intro_secondary_moving_sprite_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1087 (near)
// Draw the secondary moving effect sprite from its saved position.
void draw_intro_secondary_moving_sprite_core(Machine &m) {
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_line_scroll_table)));
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() & 0xC)));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0xC80));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x10)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x10)));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x10))) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x10)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void draw_intro_secondary_moving_sprite(Machine &m) {
    draw_intro_secondary_moving_sprite_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C10C7 (near)
// Flash the map white, advance story progress and rebuild its NPCs and viewport.
void animate_intro_map_progress_flash_core(Machine &m) {
    m.ax() = uint16_t(0x66);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    m.dx() = uint16_t(0xFFF);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_player_y))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x3)));
    m.wr8(m.ss(), uint16_t(kRam_player_y), uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kCamY)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x30));
    m.wr16(m.ss(), uint16_t(kCamY), uint16_t(m.ax()));
    m.set_al(uint8_t(0x11));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0xE));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_viewport_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x1E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_111e:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_field_frames_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_111e;
    return;
}
void animate_intro_map_progress_flash(Machine &m) {
    animate_intro_map_progress_flash_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1137 (near)
// Install the random player-and-NPC sprite frame callback.
void start_intro_random_sprite_animation_core(Machine &m) {
    m.ax() = uint16_t(0x113F);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    return;
}
void start_intro_random_sprite_animation(Machine &m) {
    start_intro_random_sprite_animation_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C113F (near)
// Select random player and first-NPC sprite frames.
void update_intro_random_sprite_animation_core(Machine &m) {
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3F))));
    if (!(m.flags() & emu::ZF))
        goto branch_1173;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.cx() = uint16_t(0x3);
    m.si() = uint16_t(0xD5AE);
branch_115a:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_1170;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
branch_1170:;
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_115a;
branch_1173:;
    return;
}
void update_intro_random_sprite_animation(Machine &m) {
    update_intro_random_sprite_animation_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1174 (near)
// Remove the random-sprite callback and reset player and NPC animation state.
void stop_intro_random_sprite_animation_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.si() = uint16_t(0xD5AE);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x140));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0008), uint16_t(m.ax()));
    m.cx() = uint16_t(0x3);
branch_1191:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_11a0;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
branch_11a0:;
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1191;
    return;
}
void stop_intro_random_sprite_animation(Machine &m) {
    stop_intro_random_sprite_animation_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C11A4 (near)
// Force the first NPC upward and wait for its movement to finish.
void move_intro_first_npc_up_core(Machine &m) {
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.si() = uint16_t(0xD5AE);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x80))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0xFFFF);
    m.wr16(m.ss(), uint16_t(m.si() + 0x000C), uint16_t(m.ax()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.si() + 0x000E), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_npc_movements_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    return;
}
void move_intro_first_npc_up(Machine &m) {
    move_intro_first_npc_up_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C11CE (near)
// Walk the player to cell twelve by twelve and set its final facing and animation.
void move_intro_player_to_center_core(Machine &m) {
    m.bx() = uint16_t(0xC0C);
    {
        const uint16_t callback_cs = m.cs();
        move_intro_player_vertical_first_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    return;
}
void move_intro_player_to_center(Machine &m) {
    move_intro_player_to_center_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C11E3 (near)
// Send three NPCs in separate directions and wait for their movements to finish.
void move_intro_three_npcs_away_core(Machine &m) {
    m.bp() = uint16_t(0xD5AE);
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x50));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_1200;
    m.ax() = uint16_t(0xFFFF);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.set_al(uint8_t(0xC4));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x000E), uint8_t(m.al()));
branch_1200:;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_121a;
    m.ax() = uint16_t(0x1);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.set_al(uint8_t(0xC6));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x000E), uint8_t(m.al()));
branch_121a:;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_1234;
    m.ax() = uint16_t(0x1);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000C), uint16_t(m.ax()));
    m.set_al(uint8_t(0xC0));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x000E), uint8_t(m.al()));
branch_1234:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_npc_movements_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD5AE);
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x50));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_124b;
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
branch_124b:;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_125a;
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
branch_125a:;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_1269;
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
branch_1269:;
    return;
}
void move_intro_three_npcs_away(Machine &m) {
    move_intro_three_npcs_away_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C126A (near)
// Fade in the departure sprite grid, advance progress, move three NPCs and restore player graphics.
void animate_intro_npc_departure_scene_core(Machine &m) {
    // A field effect after the dialogue window closed: the native picture is complete, so a held host picture
    // releases and the effect shows.
    hooks::emit(hooks::Event::PresentationPicture, m, {2});
    m.set_al(uint8_t(0xEF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0xE0);
    m.si() = uint16_t(0x361A);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x35FA);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_1295:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xD);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1295;
    m.set_al(uint8_t(0x0));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xA8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x80);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.bp() = uint16_t(0x13E6);
branch_12d4:;
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp())));
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (m.flags() & emu::ZF)
        goto branch_1317;
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0004)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_12e9;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_12e9:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.set_al(uint8_t(0xEF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0002)));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x521A));
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_12e9;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x6));
    goto branch_12d4;
branch_1317:;
    m.ax() = uint16_t(0x27);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_1322:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1322;
    m.set_al(uint8_t(0x0));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x32);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_135a:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_135a;
    m.bp() = uint16_t(0xD5AE);
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x50));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_1399;
    m.ax() = uint16_t(0x1);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.set_al(uint8_t(0xC6));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x000E), uint8_t(m.al()));
branch_1399:;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_13b3;
    m.ax() = uint16_t(0xFFFF);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.set_al(uint8_t(0xC4));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x000E), uint8_t(m.al()));
branch_13b3:;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x14));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_13cd;
    m.ax() = uint16_t(0xFFFF);
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000C), uint16_t(m.ax()));
    m.set_al(uint8_t(0xC2));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x000E), uint8_t(m.al()));
branch_13cd:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_npc_movements_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x100);
    m.cx() = uint16_t(0x100);
    m.si() = uint16_t(0x80);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void animate_intro_npc_departure_scene(Machine &m) {
    animate_intro_npc_departure_scene_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1644 (near)
// Move the player, draw a departing effect strip, advance progress and reload player graphics.
void animate_intro_player_departure_strip_core(Machine &m) {
    m.bx() = uint16_t(0x1111);
    {
        const uint16_t callback_cs = m.cs();
        move_intro_player_vertical_first_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x8C);
    m.si() = uint16_t(0x4C5C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x4C3C);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x27);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(0x0);
    m.cx() = uint16_t(0x12);
branch_1695:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    m.ax() = uint16_t(0x108);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xC8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x5DDC));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x0009);
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x48));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1695;
    m.cx() = uint16_t(0x8);
branch_16d5:;
    computed::push(m, m.cx());
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) | uint8_t(m.cx())))));
    if (m.flags() & emu::ZF)
        goto branch_16f3;
branch_16da:;
    computed::push(m, m.cx());
    m.set_al(uint8_t(0x5));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_16da;
branch_16f3:;
    m.set_al(uint8_t(0x8));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_1716;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
branch_16fd:;
    computed::push(m, m.cx());
    m.set_al(uint8_t(0x5));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_16fd;
branch_1716:;
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_16d5;
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x100);
    m.cx() = uint16_t(0x100);
    m.si() = uint16_t(0x80);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void animate_intro_player_departure_strip(Machine &m) {
    animate_intro_player_departure_strip_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C172E (near)
// Animate the timed effect strip at the right-hand origin, with its sound cues.
void animate_intro_strip_at_right_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x7A);
    m.si() = uint16_t(0x2820);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x2620);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x0);
    m.cx() = uint16_t(0x8);
branch_1765:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x108);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xD0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.si() = uint16_t(0x3760);
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x108);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xD0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.si() = uint16_t(0x3760);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1765;
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x0)) | (uint16_t(m.in8(uint8_t(0x0 + 1))) << 8)));
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0xF2));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(0x3);
branch_17e3:;
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    computed::push(m, m.ax());
    m.ax() = uint16_t(0x3B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x0));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_17e3;
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x90);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x3760);
    m.ax() = uint16_t(0x108);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xD0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t(0x5);
branch_1834:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.si());
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6C));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1834;
    return;
}
void animate_intro_strip_at_right(Machine &m) {
    animate_intro_strip_at_right_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C185C (near)
// Animate the timed effect strip at the left-hand origin, with its sound cues.
void animate_intro_strip_at_left_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x7A);
    m.si() = uint16_t(0x2820);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x2620);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x0);
    m.cx() = uint16_t(0x8);
branch_1893:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xC0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.si() = uint16_t(0x3760);
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xC0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.si() = uint16_t(0x3760);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1893;
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x0)) | (uint16_t(m.in8(uint8_t(0x0 + 1))) << 8)));
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0xF2));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(0x3);
branch_1911:;
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    computed::push(m, m.ax());
    m.ax() = uint16_t(0x3B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x0));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1911;
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x90);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x3760);
    m.ax() = uint16_t(0xF8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xC0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t(0x5);
branch_1962:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.si());
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1C80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6C));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1962;
    return;
}
void animate_intro_strip_at_left(Machine &m) {
    animate_intro_strip_at_left_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C198A (near)
// Animate wave-positioned particles, flash the field and dispatch the scripted map change.
void animate_intro_wave_particles_and_warp_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBlinkEnabled))));
    computed::push(m, m.ax());
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x2);
    m.si() = uint16_t(0x980);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.dx() = uint16_t(0x1E80);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(0x1E81);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x72);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x80);
branch_19eb:;
    computed::push(m, m.cx());
    m.dx() = uint16_t(m.cx());
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x2));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x20));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(m.cx());
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.ax() = m.rd16(m.cs(), uint16_t(0xcd7 + m.al() * 2));
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(m.cx());
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x2));
    {
        const int32_t product = int32_t(int16_t(m.ax())) * int16_t(m.dx());
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(uint32_t(product) >> 16);
        computed::flag(m, emu::CF | emu::OF, product < -32768 || product > 32767);
    }
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x8, true));
    computed::push(m, m.ax());
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kOamBaseY))));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_wave_particle_second_frame_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.dx()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_wave_particle_first_frame_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::subtract<uint16_t>(m, m.cx(), 0x1));
    if (!(m.flags() & emu::CF))
        goto branch_19eb;
    m.ax() = uint16_t(0x29);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    m.dx() = uint16_t(0xFFF);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_1a5a:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x20));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_wave_particle_second_frame_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1a5a;
    m.cx() = uint16_t(0x80);
branch_1a8f:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.cx() = uint16_t(computed::shift_right<uint16_t>(m, m.cx(), 0x2));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.cx()));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_wave_particle_second_frame_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1a8f;
    m.ax() = uint16_t(0x73);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kFieldEventKind), uint8_t(m.al()));
    m.set_al(uint8_t(0xF));
    m.wr8(m.ss(), uint16_t(kFieldEventArgument), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kFieldEventPending), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        dispatch_field_event_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void animate_intro_wave_particles_and_warp(Machine &m) {
    animate_intro_wave_particles_and_warp_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1ADE (near)
// Append the wave particle's second tile frame.
void draw_intro_wave_particle_second_frame_core(Machine &m) {
    m.dx() = uint16_t(0x1E81);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void draw_intro_wave_particle_second_frame(Machine &m) {
    draw_intro_wave_particle_second_frame_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1AE7 (near)
// Append the wave particle's first tile frame.
void draw_intro_wave_particle_first_frame_core(Machine &m) {
    m.dx() = uint16_t(0x1E80);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void draw_intro_wave_particle_first_frame(Machine &m) {
    draw_intro_wave_particle_first_frame_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1AF0 (near)
// Back up NPCs, run the scripted battle, restore the field and move the surviving NPC.
void run_intro_battle_with_npc_restore_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        clear_field_overlay_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        backup_field_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        animate_battle_entry_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x8A);
    m.wr16(m.ds(), uint16_t(kRam_intro_saved_sp), uint16_t(m.ax()));
    m.ax() = uint16_t(0x19);
    m.wr16(m.ss(), uint16_t(kRam_scene_index), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kIntroStrip), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleSceneScratch), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        if (!run_battle_core(m))
            return;
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        restore_field_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_field_frames_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        npc_record_pointer_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.cx() = uint16_t(0x10);
branch_1b41:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x4);
    m.wr16(m.ss(), uint16_t(m.si() + 0x0006),
           uint16_t(computed::add<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.si() + 0x0006)), m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1b41;
    m.set_al(uint8_t(0x11));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x2C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void run_intro_battle_with_npc_restore(Machine &m) {
    run_intro_battle_with_npc_restore_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1B60 (near)
// Fade effect palettes in and out while animating a large grid and a smaller overlay.
void animate_intro_large_sprite_overlay_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        darken_intro_effect_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x24);
    m.si() = uint16_t(0x4590);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xD);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x4570);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xA4);
    m.cx() = uint16_t(0xC);
    m.si() = uint16_t(0x4A9C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x4A7C);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table), uint8_t(m.al()));
    m.ax() = uint16_t(0x72);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x96);
branch_1bc4:;
    computed::push(m, m.cx());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x1)));
    if (!(m.flags() & emu::ZF))
        goto branch_1bd6;
    goto branch_1c6b;
branch_1bd6:;
    m.cx() = uint16_t(computed::pop(m));
    computed::push(m, m.cx());
    (void)computed::subtract<uint16_t>(m, m.cx(), 0x4B);
    if (!(m.flags() & emu::CF))
        goto branch_1c2e;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    if (!(m.flags() & emu::ZF))
        goto branch_1c17;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_line_scroll_table))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (!(m.flags() & emu::ZF))
        goto branch_1c17;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kRam_line_scroll_table), uint8_t(m.al()));
branch_1c17:;
    m.set_ah(uint8_t(0x12));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4A10));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1A80);
    m.cx() = uint16_t(0x303);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_1c2e:;
    m.ax() = uint16_t(0xA0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xB0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x3));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4C1C));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(0x1CA4);
    m.cx() = uint16_t(0x202);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_1c6b:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_and_draw_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (m.flags() & emu::ZF)
        goto branch_1c86;
    goto branch_1bc4;
branch_1c86:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
branch_1c9e:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_up_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1c9e;
    return;
}
void animate_intro_large_sprite_overlay(Machine &m) {
    animate_intro_large_sprite_overlay_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1CC6 (near)
// Save the current position, warp to map 136 at cell 35:14 and activate vehicle travel mode.
void board_intro_vehicle_and_warp_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        clear_field_overlay_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_palettes_16_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_current_map))));
    m.wr8(m.ss(), uint16_t(kPreviousMap), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_player_x))));
    m.wr8(m.ss(), uint16_t(kPreviousPlayerX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_player_y))));
    m.wr8(m.ss(), uint16_t(kPreviousPlayerY), uint8_t(m.al()));
    m.set_al(uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kRam_current_map), uint8_t(m.al()));
    m.set_al(uint8_t(0x23));
    m.wr8(m.ss(), uint16_t(kRam_player_x), uint8_t(m.al()));
    m.set_al(uint8_t(0xE));
    m.wr8(m.ss(), uint16_t(kRam_player_y), uint8_t(m.al()));
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kTravelMode), uint8_t(m.al()));
    m.si() = uint16_t(0xD0B7);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0002), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0003), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void board_intro_vehicle_and_warp(Machine &m) {
    board_intro_vehicle_and_warp_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1D25 (near)
// Load effect graphics, install a scrolling-grid callback and move the player horizontally first.
void start_intro_scrolling_sprite_grid_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x20);
    m.si() = uint16_t(0x3D14);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xD);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x3CF4);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1D7D);
    m.wr16(m.ss(), uint16_t(kFieldNpcCallback), uint16_t(m.ax()));
    m.di() = uint16_t(0xD6FE);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.ax() = uint16_t(0xFFFC);
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTravelMode))));
    computed::push(m, m.ax());
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kTravelMode), uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2F));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        move_intro_player_horizontal_first_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kTravelMode), uint8_t(m.al()));
    return;
}
void start_intro_scrolling_sprite_grid(Machine &m) {
    start_intro_scrolling_sprite_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1D7D (near)
// Scroll and wrap the saved sprite-grid coordinates before drawing its tile grid.
void update_intro_scrolling_sprite_grid_core(Machine &m) {
    m.di() = uint16_t(0xD6FE);
    m.cx() = uint16_t(m.rd16(m.ss(), uint16_t(m.di())));
    m.bp() = uint16_t(m.rd16(m.ss(), uint16_t(m.di() + 0x0002)));
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (!(m.flags() & emu::ZF))
        goto branch_1d9e;
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x1DDE)));
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (m.flags() & emu::ZF)
        goto branch_1dd0;
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.cx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.bp()));
branch_1d9e:;
    m.wr16(m.ss(), uint16_t(m.di()),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.di())), true)));
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x1DE0)));
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x10));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x10));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.dx() = uint16_t(0x1A80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
branch_1dd0:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.ax() = uint16_t(0xFFFC);
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.ax()));
    return;
}
void update_intro_scrolling_sprite_grid(Machine &m) {
    update_intro_scrolling_sprite_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1DFA (near)
// Load the static-grid graphics and install its tertiary frame callback.
void start_intro_static_sprite_grid_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0xC);
    m.si() = uint16_t(0x42E4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xD);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x42C4);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1E2A);
    m.wr16(m.ss(), uint16_t(kFieldNpcCallback), uint16_t(m.ax()));
    return;
}
void start_intro_static_sprite_grid(Machine &m) {
    start_intro_static_sprite_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1E2A (near)
// Draw the fixed effect grid through the tertiary intro callback.
void draw_intro_static_sprite_grid_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x2));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() & 0x3)));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(0x1A80);
    m.cx() = uint16_t(0x4);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x40));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
branch_1e58:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    m.si() = uint16_t(0x4464);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x10))) << 8));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1e58;
    return;
}
void draw_intro_static_sprite_grid(Machine &m) {
    draw_intro_static_sprite_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1E73 (near)
// Run the scripted battle and restore the field with music request 146.
void run_intro_battle_and_resume_music_core(Machine &m) {
    m.ax() = uint16_t(0x4);
    m.wr16(m.ds(), uint16_t(kRam_intro_saved_sp), uint16_t(m.ax()));
    m.ax() = uint16_t(0x29);
    m.wr16(m.ss(), uint16_t(kRam_scene_index), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kIntroStrip), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleSceneScratch), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        clear_field_overlay_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        animate_battle_entry_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        if (!run_battle_core(m))
            return;
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kRam_music_current), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_request_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(0x92));
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void run_intro_battle_and_resume_music(Machine &m) {
    run_intro_battle_and_resume_music_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1EC3 (near)
// Advance story progress and animate the effect grid from its first origin.
void animate_intro_progress_sprite_grid_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0xE);
    m.si() = uint16_t(0x6774);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x6574);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x27);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bp() = uint16_t(0x1F68);
branch_1ef4:;
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp())));
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0004)));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x6));
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (m.flags() & emu::ZF)
        goto branch_1f67;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_1f21;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0x6));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xA));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
branch_1f21:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.bp());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x3C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xC8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.dx() = uint16_t(0x1C80);
    m.si() = uint16_t(0xB724);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1f21;
    goto branch_1ef4;
branch_1f67:;
    return;
}
void animate_intro_progress_sprite_grid(Machine &m) {
    animate_intro_progress_sprite_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C1FAA (near)
// Animate the same effect grid from its alternate origin without advancing progress.
void animate_intro_alternate_sprite_grid_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0xE);
    m.si() = uint16_t(0x6774);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xE);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x6574);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x27);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0x2044);
branch_1fdd:;
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp())));
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0004)));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x6));
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (m.flags() & emu::ZF)
        goto branch_2042;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_1ff9;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
branch_1ff9:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.bp());
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x3C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x188);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x100);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.dx() = uint16_t(0x1C80);
    m.si() = uint16_t(0xB724);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1ff9;
    goto branch_1fdd;
branch_2042:;
    return;
}
void animate_intro_alternate_sprite_grid(Machine &m) {
    animate_intro_alternate_sprite_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2086 (near)
// Run the particle sequence, brighten the field and install two overlay callbacks.
void animate_intro_particles_with_dual_overlays_core(Machine &m) {
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        animate_intro_particle_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x1E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x67);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_209e:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_209e;
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x10);
    m.si() = uint16_t(0xBC14);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBBF4);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x90);
    m.cx() = uint16_t(0x24);
    m.si() = uint16_t(0x4590);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xA);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x4570);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2147);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    m.ax() = uint16_t(0x2187);
    m.wr16(m.ss(), uint16_t(kFieldPostSpriteCallback), uint16_t(m.ax()));
    m.cx() = uint16_t(0x32);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_2129:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2129;
    return;
}
void animate_intro_particles_with_dual_overlays(Machine &m) {
    animate_intro_particles_with_dual_overlays_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2147 (near)
// Draw the first persistent overlay grid at its fixed position.
void draw_intro_first_overlay_grid_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.set_ah(uint8_t(0x12));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xBE14));
    m.ax() = uint16_t(0x11C);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xD0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.dx() = uint16_t(0x1280);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void draw_intro_first_overlay_grid(Machine &m) {
    draw_intro_first_overlay_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2187 (near)
// Draw the second persistent overlay grid at its fixed position.
void draw_intro_second_overlay_grid_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_21cb;
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.set_ah(uint8_t(0x12));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4A10));
    m.ax() = uint16_t(0x11C);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xDC);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.dx() = uint16_t(0x1490);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_21cb:;
    return;
}
void draw_intro_second_overlay_grid(Machine &m) {
    draw_intro_second_overlay_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C21CC (near)
// Brighten the field, advance progress and clear all three frame callbacks.
void finish_intro_dual_overlay_scene_core(Machine &m) {
    m.cx() = uint16_t(0x10);
branch_21cf:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_21cf;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kFieldPostSpriteCallback), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kFieldNpcCallback), uint16_t(m.ax()));
    m.set_al(uint8_t(0x6));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xC));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_220b:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_220b;
    return;
}
void finish_intro_dual_overlay_scene(Machine &m) {
    finish_intro_dual_overlay_scene_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2229 (near)
// Load the oscillating-grid assets, initialize offsets and install its callback.
void start_intro_oscillating_grid_core(Machine &m) {
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x18);
    m.si() = uint16_t(0xA764);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xA744);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2268);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    m.bp() = uint16_t(0xD6FE);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(0x9D);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void start_intro_oscillating_grid(Machine &m) {
    start_intro_oscillating_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2268 (near)
// Adjust saved horizontal and vertical offsets and draw the oscillating sprite grid.
void update_intro_oscillating_grid_core(Machine &m) {
    m.bp() = uint16_t(0xD6FE);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (!(m.flags() & emu::ZF))
        goto branch_228b;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (!(m.flags() & emu::ZF))
        goto branch_228b;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
branch_228b:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)));
    m.set_ah(uint8_t(0x18));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xAA64));
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xE8);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xD0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.dx() = uint16_t(0x1280);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void update_intro_oscillating_grid(Machine &m) {
    update_intro_oscillating_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C22C0 (near)
// Load three effect tile groups and install their multi-grid animation callback.
void start_intro_multi_grid_sequence_core(Machine &m) {
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0xB);
    m.si() = uint16_t(0xA5C0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x8B);
    m.cx() = uint16_t(0x32);
    m.si() = uint16_t(0xAACC);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xBD);
    m.cx() = uint16_t(0x18);
    m.si() = uint16_t(0xB1BC);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xA5A0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD6FE);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.ax()));
    m.ax() = uint16_t(0x27);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2326);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void start_intro_multi_grid_sequence(Machine &m) {
    start_intro_multi_grid_sequence_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2326 (near)
// Advance the shared phase and draw the sequence's three sprite-grid groups.
void update_intro_multi_grid_sequence_core(Machine &m) {
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.bp() = uint16_t(0xD6FE);
    m.wr16(m.ss(), uint16_t(m.bp()),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.bp())))));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x30);
    if (!(m.flags() & emu::CF))
        goto branch_2368;
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.set_ah(uint8_t(0x18));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xB10C));
    m.ax() = uint16_t(0xE0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xD0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.dx() = uint16_t(0x128B);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_2368:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x20);
    if (m.flags() & emu::CF)
        goto branch_23a7;
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x2C);
    if (!(m.flags() & emu::CF))
        goto branch_23a7;
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x20));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    m.set_ah(uint8_t(0x24));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xB4BC));
    m.ax() = uint16_t(0x118);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xD0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.dx() = uint16_t(0x12BD);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_23a7:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x38);
    if (m.flags() & emu::CF)
        goto branch_23b2;
    goto branch_2474;
branch_23b2:;
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x20);
    if (m.flags() & emu::CF)
        goto branch_23ba;
    m.ax() = uint16_t(0x20);
branch_23ba:;
    m.dx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0xE0));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xE0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    m.dx() = uint16_t(0x1280);
    m.si() = uint16_t(0xA738);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0xE0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xE0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    m.dx() = uint16_t(0x1280);
    m.si() = uint16_t(0xA720);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x8);
    if (m.flags() & emu::CF)
        goto branch_2474;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x20);
    if (m.flags() & emu::CF)
        goto branch_241d;
    m.ax() = uint16_t(0x20);
branch_241d:;
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0xE0));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xE0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    m.dx() = uint16_t(0x1280);
    m.si() = uint16_t(0xA72C);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x20);
    if (m.flags() & emu::CF)
        goto branch_244d;
    m.ax() = uint16_t(0x20);
branch_244d:;
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0xE0));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0xE0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    m.dx() = uint16_t(0x1280);
    m.si() = uint16_t(0xA72C);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_2474:;
    return;
}
void update_intro_multi_grid_sequence(Machine &m) {
    update_intro_multi_grid_sequence_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2475 (near)
// Animate a sprite grid at the first NPC while flashing and restoring its palette.
void animate_intro_npc_grid_flash_core(Machine &m) {
    // A field effect after the dialogue window closed: the native picture is complete, so a held host picture
    // releases and the effect shows.
    hooks::emit(hooks::Event::PresentationPicture, m, {2});
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x11);
    m.si() = uint16_t(0xBB48);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xD);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBB28);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x28);
branch_24a4:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x1)));
    if (!(m.flags() & emu::ZF))
        goto branch_24c4;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.dx() = uint16_t(0xFFF);
    m.di() = uint16_t(0x400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_24d5;
branch_24c4:;
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(0xC600);
    m.di() = uint16_t(0xC400);
    m.cx() = uint16_t(0x40);
    computed::copy_words(m);
branch_24d5:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_and_draw_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x1)));
    m.set_ah(uint8_t(0x12));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xBD68));
    m.bp() = uint16_t(0xD5AE);
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x14));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0006)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0008)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.dx() = uint16_t(0x1A80);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (m.flags() & emu::ZF)
        goto branch_2540;
    goto branch_24a4;
branch_2540:;
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(0xC600);
    m.di() = uint16_t(0xC400);
    m.cx() = uint16_t(0x40);
    computed::copy_words(m);
    return;
}
void animate_intro_npc_grid_flash(Machine &m) {
    animate_intro_npc_grid_flash_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2552 (near)
// Draw a full-width field overlay, advance progress and fade it away.
void animate_intro_screen_overlay_progress_core(Machine &m) {
    hooks::emit(hooks::Event::ScriptedFieldOverlay, m, {1});
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x65);
    m.si() = uint16_t(0xBF8C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xD);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBD8C);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_player_x))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x30)));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kRam_player_y))));
    m.set_ah(uint8_t(computed::subtract<uint8_t>(m, m.ah(), 0x15)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x4)));
    m.set_ah(uint8_t(computed::shift_left<uint8_t>(m, m.ah(), 0x4)));
    m.set_ah(uint8_t(computed::add<uint8_t>(m, m.ah(), 0x20)));
    m.wr16(m.ss(), uint16_t(kIntroOverlayProgress), uint16_t(m.ax()));
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x4B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x0);
branch_25b4:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x1)));
    if (!(m.flags() & emu::ZF))
        goto branch_25d4;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.dx() = uint16_t(0x0);
    m.di() = uint16_t(0x400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_25e5;
branch_25d4:;
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(0xC600);
    m.di() = uint16_t(0xC400);
    m.cx() = uint16_t(0x40);
    computed::copy_words(m);
branch_25e5:;
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::pop(m));
    computed::push(m, m.ax());
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0xE);
    if (m.flags() & emu::CF)
        goto branch_25fa;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x1)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0xC));
branch_25fa:;
    m.dx() = uint16_t(0x1F8);
    {
        const uint32_t product = uint32_t(m.ax()) * m.dx();
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(product >> 16);
        computed::flag(m, emu::CF | emu::OF, m.dx() != 0);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xCC2C));
    m.bx() = uint16_t(0x0);
    m.ax() = uint16_t(0x1);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.dx() = uint16_t(0x1A80);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_offset_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx()));
    (void)computed::subtract<uint16_t>(m, m.cx(), 0x4B);
    if (!(m.flags() & emu::ZF))
        goto branch_25b4;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    m.dx() = uint16_t(0xFFF);
    m.di() = uint16_t(0x400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x0);
    m.wr16(m.ss(), uint16_t(kIntroOverlayProgress), uint16_t(m.ax()));
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xE));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        read_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0xE));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_2677:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2677;
    hooks::emit(hooks::Event::ScriptedFieldOverlay, m, {0});
    return;
}
void animate_intro_screen_overlay_progress(Machine &m) {
    animate_intro_screen_overlay_progress_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2695 (near)
// Enter encounter fifteen with formation selector thirty-eight.
void start_intro_fixed_encounter_core(Machine &m) {
    m.ax() = uint16_t(0xF);
    m.wr16(m.ds(), uint16_t(kRam_intro_saved_sp), uint16_t(m.ax()));
    m.ax() = uint16_t(0x26);
    m.wr16(m.ss(), uint16_t(kRam_scene_index), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kIntroStrip), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleSceneScratch), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        if (!enter_field_battle_core(m))
            return;
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void start_intro_fixed_encounter(Machine &m) {
    start_intro_fixed_encounter_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C26B2 (near)
// Scroll the field upward while darkening palettes and starting a sound glide.
void scroll_intro_field_with_sound_fade_core(Machine &m) {
    m.ax() = uint16_t(0x2);
    m.cx() = uint16_t(0x401);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x0);
branch_26c0:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.cx());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    if (!(m.flags() & emu::ZF))
        goto branch_26d5;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
branch_26d5:;
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() & 0xF)));
    if (!(m.flags() & emu::ZF))
        goto branch_26e0;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_map_top_edge_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
branch_26e0:;
    m.wr16(m.ss(), uint16_t(kOamBaseX),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(kOamBaseX)))));
    m.wr16(m.ss(), uint16_t(kCamY),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(kCamY)), true)));
    m.wr8(m.ss(), uint16_t(kFieldScrollHigh),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldScrollHigh)), true)));
    m.wr8(m.ss(), uint16_t(kIntroOverlayProgressHigh),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kIntroOverlayProgressHigh)), true)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx()));
    (void)computed::subtract<uint16_t>(m, m.cx(), 0x40);
    if (!(m.flags() & emu::ZF))
        goto branch_26c0;
    return;
}
void scroll_intro_field_with_sound_fade(Machine &m) {
    scroll_intro_field_with_sound_fade_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2701 (near)
// Load and animate two consecutive effect grids with separate sound cues.
void animate_intro_two_stage_sprite_effect_core(Machine &m) {
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x9);
    m.si() = uint16_t(0xE7DC);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xE7BC);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x41);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x1E);
branch_2730:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() & 0x1)));
    if (m.flags() & emu::ZF)
        goto branch_275c;
    m.ax() = uint16_t(0x20);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x68);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.dx() = uint16_t(0x1680);
    m.si() = uint16_t(0xE8FC);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_275c:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2730;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x41);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x1F);
    m.si() = uint16_t(0xE934);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xE914);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x0);
branch_2799:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.cx());
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x7)));
    m.set_ah(uint8_t(0x30));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xED14));
    m.ax() = uint16_t(0x0);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x70);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    m.dx() = uint16_t(0x1680);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx()));
    (void)computed::subtract<uint16_t>(m, m.cx(), 0x40);
    if (!(m.flags() & emu::ZF))
        goto branch_2799;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void animate_intro_two_stage_sprite_effect(Machine &m) {
    animate_intro_two_stage_sprite_effect_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C27E0 (near)
// Run a scripted battle, animate random particles and advance story progress afterward.
void run_intro_battle_and_random_particles_core(Machine &m) {
    m.ax() = uint16_t(0x5);
    m.wr16(m.ds(), uint16_t(kRam_intro_saved_sp), uint16_t(m.ax()));
    m.ax() = uint16_t(0x2B);
    m.wr16(m.ss(), uint16_t(kRam_scene_index), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kIntroStrip), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleSceneScratch), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        clear_field_overlay_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        animate_battle_entry_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        if (!run_battle_core(m))
            return;
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kRam_music_current), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_request_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x20);
    m.si() = uint16_t(0xB728);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xB528);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD6FE);
    m.si() = uint16_t(0x2964);
    m.cx() = uint16_t(0x10);
branch_2856:;
    m.ax() = uint16_t(0x2001);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001), uint8_t(m.al()));
    m.ax() = uint16_t(0x800);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.cs(), uint16_t(m.si()))));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0002), uint8_t(m.al()));
    m.ax() = uint16_t(0x800);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.cs(), uint16_t(m.si() + 0x0002))));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0003), uint8_t(m.al()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2856;
    m.ax() = uint16_t(0x6C);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_sound_if_enabled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x40);
branch_28a3:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x10);
    m.bp() = uint16_t(0xD6FE);
branch_28aa:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(m.bp()),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp())), true)));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_28d9;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.dx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x1680));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0002))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0003)))) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_28d9:;
    m.cx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_28aa;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_28a3;
    m.bp() = uint16_t(0xD6FE);
    m.cx() = uint16_t(0x10);
branch_28ef:;
    m.ax() = uint16_t(0x2000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001), uint8_t(m.ah()));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_28ef;
    m.set_al(uint8_t(0x8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xE));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x60);
branch_2914:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x10);
    m.bp() = uint16_t(0xD6FE);
branch_291b:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp() + 0x0001)))));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0001))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(m.bp())))));
    if (m.flags() & emu::CF)
        goto branch_292f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x40);
    if (!(m.flags() & emu::CF))
        goto branch_294d;
    m.set_ah(uint8_t(m.al()));
branch_292f:;
    m.set_al(uint8_t(m.ah()));
    m.set_ah(uint8_t(0x0));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.dx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x1680));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0002))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0003)))) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_294d:;
    m.cx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_291b;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2914;
    m.set_al(uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
    return;
}
void run_intro_battle_and_random_particles(Machine &m) {
    run_intro_battle_and_random_particles_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C29A4 (near)
// Animate random particles and advance story progress without a preceding battle.
void animate_intro_random_particles_with_progress_core(Machine &m) {
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x20);
    m.si() = uint16_t(0xB728);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xB528);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD6FE);
    m.si() = uint16_t(0x2AD3);
    m.cx() = uint16_t(0x10);
branch_29d1:;
    m.ax() = uint16_t(0x2001);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001), uint8_t(m.al()));
    m.ax() = uint16_t(0x800);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.cs(), uint16_t(m.si()))));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0002), uint8_t(m.al()));
    m.ax() = uint16_t(0x800);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.cs(), uint16_t(m.si() + 0x0002))));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0003), uint8_t(m.al()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_29d1;
    m.cx() = uint16_t(0x40);
branch_2a18:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x10);
    m.bp() = uint16_t(0xD6FE);
branch_2a1f:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(m.bp()),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp())), true)));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_2a4e;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.dx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x1680));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0002))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0003)))) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_2a4e:;
    m.cx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2a1f;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2a18;
    m.bp() = uint16_t(0xD6FE);
    m.cx() = uint16_t(0x10);
branch_2a64:;
    m.ax() = uint16_t(0x2000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001), uint8_t(m.ah()));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2a64;
    m.set_al(uint8_t(0x8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xE));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        write_packed_progress_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x60);
branch_2a89:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x10);
    m.bp() = uint16_t(0xD6FE);
branch_2a90:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp() + 0x0001)))));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0001))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(m.bp())))));
    if (m.flags() & emu::CF)
        goto branch_2aa4;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x40);
    if (!(m.flags() & emu::CF))
        goto branch_2ac2;
    m.set_ah(uint8_t(m.al()));
branch_2aa4:;
    m.set_al(uint8_t(m.ah()));
    m.set_ah(uint8_t(0x0));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.dx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x1680));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0002))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0003)))) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_2ac2:;
    m.cx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2a90;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2a89;
    return;
}
void animate_intro_random_particles_with_progress(Machine &m) {
    animate_intro_random_particles_with_progress_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2B13 (near)
// Animate random particles with a sound cue and rebuild the map NPCs afterward.
void animate_intro_random_particles_with_sound_core(Machine &m) {
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x20);
    m.si() = uint16_t(0xB728);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xB528);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD6FE);
    m.si() = uint16_t(0x2C41);
    m.cx() = uint16_t(0x10);
branch_2b40:;
    m.ax() = uint16_t(0x2001);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001), uint8_t(m.al()));
    m.ax() = uint16_t(0x800);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.cs(), uint16_t(m.si()))));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0002), uint8_t(m.al()));
    m.ax() = uint16_t(0x800);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x4));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.cs(), uint16_t(m.si() + 0x0002))));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0003), uint8_t(m.al()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2b40;
    m.ax() = uint16_t(0x6C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x40);
branch_2b8f:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x10);
    m.bp() = uint16_t(0xD6FE);
branch_2b96:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(m.bp()),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp())), true)));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp()))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_2bc5;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.dx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x1680));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0002))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0003)))) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_2bc5:;
    m.cx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2b96;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2b8f;
    m.bp() = uint16_t(0xD6FE);
    m.cx() = uint16_t(0x10);
branch_2bdb:;
    m.ax() = uint16_t(0x2000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001), uint8_t(m.ah()));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2bdb;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_map_npcs_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x60);
branch_2bf7:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x10);
    m.bp() = uint16_t(0xD6FE);
branch_2bfe:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(m.bp() + 0x0001),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.bp() + 0x0001)))));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0001))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(m.bp())))));
    if (m.flags() & emu::CF)
        goto branch_2c12;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x40);
    if (!(m.flags() & emu::CF))
        goto branch_2c30;
    m.set_ah(uint8_t(m.al()));
branch_2c12:;
    m.set_al(uint8_t(m.ah()));
    m.set_ah(uint8_t(0x0));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.dx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x1680));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0002))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x0003)))) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_2c30:;
    m.cx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x4));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2bfe;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2bf7;
    return;
}
void animate_intro_random_particles_with_sound(Machine &m) {
    animate_intro_random_particles_with_sound_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2C81 (near)
// Load the upper-left overlay graphics and install their primary callback.
void start_intro_upper_left_overlay_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x10);
    m.si() = uint16_t(0xBC14);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xA);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0xBBD4);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2CB1);
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    return;
}
void start_intro_upper_left_overlay(Machine &m) {
    start_intro_upper_left_overlay_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2CB1 (near)
// Draw the fixed upper-left overlay sprite grid.
void draw_intro_upper_left_overlay_core(Machine &m) {
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.set_ah(uint8_t(0x12));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xBE14));
    m.ax() = uint16_t(0x44);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamX))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x5C);
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kCamY))));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.dx() = uint16_t(0x1480);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void draw_intro_upper_left_overlay(Machine &m) {
    draw_intro_upper_left_overlay_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2CF1 (near)
// Clear all three per-frame intro callbacks.
void clear_intro_frame_callbacks_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldPreSpriteCallback), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kFieldPostSpriteCallback), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kFieldNpcCallback), uint16_t(m.ax()));
    return;
}
void clear_intro_frame_callbacks(Machine &m) {
    clear_intro_frame_callbacks_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2D00 (near)
// Run vignette variant zero through the shared scene animation.
void play_intro_vignette_first_variant_core(Machine &m) {
    m.ax() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void play_intro_vignette_first_variant(Machine &m) {
    play_intro_vignette_first_variant_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2D07 (near)
// Run vignette variant one through the shared scene animation.
void play_intro_vignette_second_variant_core(Machine &m) {
    m.ax() = uint16_t(0x1);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void play_intro_vignette_second_variant(Machine &m) {
    play_intro_vignette_second_variant_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2D0E (near)
// Run vignette variant two through the shared scene animation.
void play_intro_vignette_third_variant_core(Machine &m) {
    m.ax() = uint16_t(0x2);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void play_intro_vignette_third_variant(Machine &m) {
    play_intro_vignette_third_variant_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2D15 (near)
// Run vignette variant three through the shared scene animation.
void play_intro_vignette_fourth_variant_core(Machine &m) {
    m.ax() = uint16_t(0x3);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void play_intro_vignette_fourth_variant(Machine &m) {
    play_intro_vignette_fourth_variant_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2D1C (near)
// Run vignette variant four through the shared scene animation.
void play_intro_vignette_fifth_variant_core(Machine &m) {
    m.ax() = uint16_t(0x4);
    {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void play_intro_vignette_fifth_variant(Machine &m) {
    play_intro_vignette_fifth_variant_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C2D23 (near)
// Save the field, animate the selected vignette with palette transitions and restore the field.
void play_intro_vignette_scene_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        backup_field_display_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(0xD6FE);
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0008), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000C), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000E), uint16_t(m.ax()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kSpriteSixTileAttribute), uint8_t(m.al()));
    m.cx() = uint16_t(0x10);
branch_2d54:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2d54;
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x8);
    m.dx() = uint16_t(0x0);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x7);
    m.dx() = uint16_t(0x0);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xB000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(0x9E58);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x9DD8);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x84);
    m.cx() = uint16_t(0x78);
    m.si() = uint16_t(0xC17C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xC0DC);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0xA);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ax() = uint16_t(0xEC);
    m.cx() = uint16_t(0x36);
    m.si() = uint16_t(0xD45C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xD3BC);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0xB);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_2e5a:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x8);
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_up_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_marker_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2e5a;
    m.cx() = uint16_t(0x96);
branch_2e87:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_marker_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        update_intro_vignette_grid_offsets_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2e87;
    m.ax() = uint16_t(0x66);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    m.dx() = uint16_t(0xFFF);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x1E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x80);
branch_2ecf:;
    computed::push(m, m.cx());
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() & 0x7)));
    if (!(m.flags() & emu::ZF))
        goto branch_2ee1;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_2ee1:;
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_large_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_marker_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        update_intro_vignette_grid_offsets_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2ecf;
    m.cx() = uint16_t(0x3C);
branch_2f05:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_large_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_marker_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        update_intro_vignette_grid_offsets_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2f05;
    m.cx() = uint16_t(0x10);
branch_2f2a:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_large_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_marker_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        update_intro_vignette_grid_offsets_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2f2a;
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0xE354);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.dx() = uint16_t(0x600);
    {
        const uint32_t product = uint32_t(m.ax()) * m.dx();
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(product >> 16);
        computed::flag(m, emu::CF | emu::OF, m.dx() != 0);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x30);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xE2D4);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xB0);
    m.cx() = uint16_t(0x2D);
    m.si() = uint16_t(0xDC5C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xDBBC);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0xA);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0006), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0008), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000A), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000C), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x000E), uint16_t(m.ax()));
    m.cx() = uint16_t(0x10);
branch_2fe1:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x8);
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_up_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_small_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2fe1;
    m.ax() = uint16_t(0x66);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x5);
branch_3016:;
    computed::push(m, m.cx());
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.bp() + 0x0004)))));
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_middle_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_small_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3016;
    m.ax() = uint16_t(0x6A);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
branch_3046:;
    computed::push(m, m.cx());
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)))));
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_small_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3046;
    m.cx() = uint16_t(0x10);
branch_306b:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_intro_vignette_small_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_306b;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_309f;
    goto branch_314d;
branch_309f:;
    m.cx() = uint16_t(0x5);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
    m.ax() = uint16_t(0x0);
branch_30b5:;
    computed::push(m, m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0xF000);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(computed::pop(m));
    m.wr16(m.ss(), uint16_t(kVignetteBackdropColor), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x111));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_30b5;
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
    m.ax() = uint16_t(0xF);
branch_30e1:;
    computed::push(m, m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0xF000);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(computed::pop(m));
    m.wr16(m.ss(), uint16_t(kVignetteBackdropColor), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax(), true));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_30e1;
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
    m.ax() = uint16_t(0xFFF);
branch_310b:;
    computed::push(m, m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0xF000);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(computed::pop(m));
    m.wr16(m.ss(), uint16_t(kVignetteBackdropColor), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x111));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_310b;
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
    m.ax() = uint16_t(0xF00);
branch_3137:;
    computed::push(m, m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0xF000);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(computed::pop(m));
    m.wr16(m.ss(), uint16_t(kVignetteBackdropColor), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x11));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3137;
branch_314d:;
    m.cx() = uint16_t(0x4B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kSpriteSixTileAttribute), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        restore_field_after_menu_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void play_intro_vignette_scene(Machine &m) {
    play_intro_vignette_scene_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C316A (near)
// Draw the vignette's large sprite grid.
void draw_intro_vignette_large_grid_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0xDB1C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3)));
    m.set_ah(uint8_t(0x28));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0x68);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xE3)) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.dx() = uint16_t(0x16EC);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_intro_vignette_large_grid(Machine &m) {
    draw_intro_vignette_large_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C31A4 (near)
// Draw the vignette's middle sprite grid using its saved phase.
void draw_intro_vignette_middle_grid_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bp() = uint16_t(0xD6FE);
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0xE1FC);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0004)));
    m.set_ah(uint8_t(0x24));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0x68);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x40);
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.dx() = uint16_t(0x14B0);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_intro_vignette_middle_grid(Machine &m) {
    draw_intro_vignette_middle_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C31DD (near)
// Draw the vignette's small sprite grid using its saved phase.
void draw_intro_vignette_small_grid_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bp() = uint16_t(0xD6FE);
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0xFB54);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)));
    m.set_ah(uint8_t(0x18));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0x68);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x18);
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.dx() = uint16_t(0x1280);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_intro_vignette_small_grid(Machine &m) {
    draw_intro_vignette_small_grid_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C3216 (near)
// Draw the vignette's 16x16 marker sprite.
void draw_intro_vignette_marker_sprite_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x10));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.dx() = uint16_t(0x1280);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_intro_vignette_marker_sprite(Machine &m) {
    draw_intro_vignette_marker_sprite_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C3236 (near)
// Advance the vignette's saved offsets and draw its moving grid.
void update_intro_vignette_grid_offsets_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xF1));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.bp() = uint16_t(0xD6FE);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0002)));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x8);
    if (m.flags() & emu::CF)
        goto branch_3266;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0004)));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x7);
    if (!(m.flags() & emu::ZF))
        goto branch_3263;
    m.ax() = uint16_t(0x4);
branch_3263:;
    m.wr16(m.ss(), uint16_t(m.bp() + 0x0004), uint16_t(m.ax()));
branch_3266:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp() + 0x0004)));
    m.set_ah(uint8_t(0x40));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xD07C));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseY)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x18));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x8));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    m.dx() = uint16_t(0x1484);
    {
        const uint16_t callback_cs = m.cs();
        append_intro_sprite_grid_offset_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void update_intro_vignette_grid_offsets(Machine &m) {
    update_intro_vignette_grid_offsets_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C3296 (near)
// Set NPC flag bit five in AX's NPC record.
void mark_intro_npc_script_controlled_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        npc_record_pointer_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0011))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x20))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0011), uint8_t(m.al()));
    return;
}
void mark_intro_npc_script_controlled(Machine &m) {
    mark_intro_npc_script_controlled_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C32A6 (near)
// Set script-control flag bit five on the first NPC.
void mark_intro_first_npc_script_controlled_core(Machine &m) {
    m.ax() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        mark_intro_npc_script_controlled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void mark_intro_first_npc_script_controlled(Machine &m) {
    mark_intro_first_npc_script_controlled_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C32AD (near)
// Set script-control flag bit five on NPCs one through three.
void mark_intro_other_npcs_script_controlled_core(Machine &m) {
    m.ax() = uint16_t(0x1);
    {
        const uint16_t callback_cs = m.cs();
        mark_intro_npc_script_controlled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2);
    {
        const uint16_t callback_cs = m.cs();
        mark_intro_npc_script_controlled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x3);
    {
        const uint16_t callback_cs = m.cs();
        mark_intro_npc_script_controlled_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void mark_intro_other_npcs_script_controlled(Machine &m) {
    mark_intro_other_npcs_script_controlled_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C32C0 (near)
// Walk the player horizontally first to column sixteen.
void move_intro_player_to_fixed_column_core(Machine &m) {
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x10));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xE)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        move_intro_player_horizontal_first_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void move_intro_player_to_fixed_column(Machine &m) {
    move_intro_player_to_fixed_column_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C32CA (near)
// Darken fifteen palettes over sixteen VBlank frames.
void darken_all_intro_palettes_core(Machine &m) {
    // A field effect after the dialogue window closed: the native picture is complete, so a held host picture
    // releases and the effect shows.
    hooks::emit(hooks::Event::PresentationPicture, m, {2});
    m.cx() = uint16_t(0x10);
branch_32cd:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x6);
    m.call_far(0x4000, 0x0009);
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_32cd;
    return;
}
void darken_all_intro_palettes(Machine &m) {
    darken_all_intro_palettes_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C32F0 (near)
// Start the shared sound glide and darken palettes over field frames.
void fade_intro_sound_and_palettes_core(Machine &m) {
    m.ax() = uint16_t(0x2);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_32ff:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_field_frames_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_32ff;
    return;
}
void fade_intro_sound_and_palettes(Machine &m) {
    fade_intro_sound_and_palettes_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C3318 (near)
// Cycle player sprites while scrolling the field, fade out and rebuild the player display.
void animate_intro_player_rising_scene_core(Machine &m) {
    m.cx() = uint16_t(0x40);
branch_331b:;
    {
        const uint16_t callback_cs = m.cs();
        update_intro_player_four_frame_animation_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_331b;
    m.ax() = uint16_t(0x2);
    m.cx() = uint16_t(0x201);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x20);
branch_3334:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        update_intro_player_four_frame_animation_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3334;
    m.cx() = uint16_t(0x23);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FEF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_player_sprite_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.ax() = uint16_t(0xFFE0);
    m.wr16(m.ss(), uint16_t(kOamBaseX), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x4C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x68);
branch_339d:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        update_intro_player_four_frame_animation_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kOamBaseX)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x2));
    m.wr16(m.ss(), uint16_t(kOamBaseX), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_339d;
    m.cx() = uint16_t(0x4B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x12);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x1E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void animate_intro_player_rising_scene(Machine &m) {
    animate_intro_player_rising_scene_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C33CE (near)
// Select the player's four-frame animation from the VBlank counter.
void update_intro_player_four_frame_animation_core(Machine &m) {
    computed::push(m, m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x3)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    return;
}
void update_intro_player_four_frame_animation(Machine &m) {
    update_intro_player_four_frame_animation_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C33E0 (near)
// Run the scripted final battle and restore the field with music request 151.
void run_intro_final_battle_and_music_core(Machine &m) {
    m.ax() = uint16_t(0x1);
    m.wr16(m.ds(), uint16_t(kRam_intro_saved_sp), uint16_t(m.ax()));
    m.ax() = uint16_t(0x2D);
    m.wr16(m.ss(), uint16_t(kRam_scene_index), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kIntroStrip), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleSceneScratch), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        clear_field_overlay_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        animate_battle_entry_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        if (!run_battle_core(m))
            return;
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kRam_music_current), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_request_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(0x97));
    m.wr8(m.ss(), uint16_t(kRam_music_request), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void run_intro_final_battle_and_music(Machine &m) {
    run_intro_final_battle_and_music_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C3430 (near)
// Start the alternate shared sound glide and fade field palettes over sixteen steps.
void fade_intro_sound_with_field_frames_core(Machine &m) {
    m.ax() = uint16_t(0x2);
    m.cx() = uint16_t(0x201);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_343e:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_field_frames_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_343e;
    return;
}
void fade_intro_sound_with_field_frames(Machine &m) {
    fade_intro_sound_with_field_frames_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C3457 (near)
// Clear the overlay, save both screen scrolls, enable the intro flag and reset the scene index.
void prepare_intro_scene_sequence_core(Machine &m) {
    m.ax() = uint16_t(0x3);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FEF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldWindowTopLeft), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kFieldWindowBottomRight), uint16_t(m.ax()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kFieldOverlayMode), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kRam_intro_saved_sp), uint16_t(m.ax()));
    return;
}
void prepare_intro_scene_sequence(Machine &m) {
    prepare_intro_scene_sequence_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C347F (near)
// Load and fade successive intro scene strips while advancing the scene index.
void play_intro_scene_sequence_core(Machine &m) {
    m.bp() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_intro_saved_sp)));
    m.bp() = uint16_t(computed::shift_left<uint16_t>(m, m.bp(), 0x4));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x35A2));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp())));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x100);
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0002)));
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0004)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    m.dx() = uint16_t(0x0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0xF0));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x0);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x820);
    m.wr16(m.ss(), uint16_t(kFieldWindowTopLeft), uint16_t(m.ax()));
    m.ax() = uint16_t(0x60C0);
    m.wr16(m.ss(), uint16_t(kFieldWindowBottomRight), uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bp() + 0x0008))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_3507;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    m.dx() = uint16_t(0x0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kFieldWindowTopLeft), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kFieldWindowBottomRight), uint16_t(m.ax()));
branch_3507:;
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp())));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x3);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.cs(), uint16_t(m.bp() + 0x0008)))) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x12)) << 8));
    m.cx() = uint16_t(
        (m.cx() & 0x00FF) |
        (uint16_t(uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.cx() >> 8), uint8_t(m.bx() >> 8)))) << 8));
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x0006)));
    m.dx() = uint16_t(0x1F00);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_offset_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_3530:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_up_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3530;
    m.cx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bp() + 0x000C)));
    m.cx() = uint16_t(computed::add<uint16_t>(m, m.cx(), 0x64));
branch_3551:;
    computed::push(m, m.cx());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3551;
    m.cx() = uint16_t(0x10);
branch_3563:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        update_field_frame_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3563;
    m.ax() = uint16_t(0x3);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FEF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x820);
    m.wr16(m.ss(), uint16_t(kFieldWindowTopLeft), uint16_t(m.ax()));
    m.ax() = uint16_t(0x60C0);
    m.wr16(m.ss(), uint16_t(kFieldWindowBottomRight), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kRam_intro_saved_sp),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(kRam_intro_saved_sp)))));
    return;
}
void play_intro_scene_sequence(Machine &m) {
    play_intro_scene_sequence_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C3854 (near)
// Load the completion-panel tiles and fade its palette in over field frames.
void show_intro_completion_panel_core(Machine &m) {
    m.ax() = uint16_t(0xEF);
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x100);
    m.si() = uint16_t(0x27F0);
    m.cx() = uint16_t(0x21);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x27D0);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x3);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0xA));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xF));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.si() = uint16_t(0x2C10);
    m.dx() = uint16_t(0x1F00);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_offset_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    m.dx() = uint16_t(0x0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_palette_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_38a7:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_up_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        wait_field_frames_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_38a7;
    return;
}
void show_intro_completion_panel(Machine &m) {
    show_intro_completion_panel_core(m);
    if (m.cs() == 0xC000)
        m.ret();
}
// rom: 0x3C38C0 (near)
// Fade to the ending screen, wait for acknowledgement and open the postgame party menu.
void run_ending_menu_sequence_core(Machine &m) {
    m.cx() = uint16_t(0x10);
branch_38c3:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_38c3;
    m.cx() = uint16_t(0x4B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF000);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(0x0);
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.ax() = uint16_t(0x0);
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.wr8(m.ss(), uint16_t(kFieldOverlayMode), uint8_t(0x0));
    m.ax() = uint16_t(0xEF);
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x29);
    m.si() = uint16_t(0x2CEA);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x2C6A);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x12)) << 8));
    m.si() = uint16_t(0x320A);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::EndingNotice, m, {1});
    m.cx() = uint16_t(0x10);
branch_3940:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_up_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF101);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.set_al(uint8_t(0x32));
    m.out8(uint8_t(0x7), m.al());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3940;
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3418);
    m.ax() = uint16_t(0x1F3);
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF);
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x0);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_3987:;
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_random_seed)));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x5));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() & 0x1)));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2FF3));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0xD0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x7C)) << 8));
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        set_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF105);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x4));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kRam_keys_pressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4)));
    if (m.flags() & emu::ZF)
        goto branch_3987;
    m.cx() = uint16_t(0x10);
branch_39d4:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF101);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_39d4;
    hooks::emit(hooks::Event::EndingNotice, m, {0});
    m.cx() = uint16_t(0x1E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEndingSeen), uint8_t(0x1));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.bx() = uint16_t(0x6);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_party_menu_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    // Abandon the ending's stack at the original main entry. A caller can observe main reloading
    // SS:SP before its first frame wait, so do not run the whole main core in this fiber.
    m.jmp_far(0x4000, 0x0EE0);
    return;
}
void run_ending_menu_sequence(Machine &m) { run_ending_menu_sequence_core(m); }
} // namespace sawgod::game
