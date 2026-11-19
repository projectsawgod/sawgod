#include "game/bank34_misc.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/bank36_battle.h"
#include "game/bank36_misc.h"
#include "game/bank3c_title.h"
#include "game/bank3d_sound.h"
#include "game/computed_flags.h"
#include "game/computed_handlers.h"
#include "game/hooks.h"
#include "game/system.h"
// Decompiled battle-effect sprite positioning helpers (bank 0x37, segment 7000h).
#include "game/bank34.h"
#include "game/bank37.h"
namespace sawgod::game {
namespace {
constexpr uint16_t kEffectX = 0xDCBB, kEffectY = 0xDCBC, kEffectRowY = 0xDCBD;
constexpr uint16_t kEffectHeight = 0xDCBE, kEffectSpriteOffset = 0xDCBF, kEffectSpritePosition = 0xC002;
constexpr uint16_t kTargetCount = 0xD6D4, kTargetSide = 0xDC00, kEffectDirection = 0xD713;
struct Registers {
    uint16_t ax, bx, cx, dx, si;
    explicit Registers(Machine &m) : ax(m.ax()), bx(m.bx()), cx(m.cx()), dx(m.dx()), si(m.si()) {}
    void restore(Machine &m) const {
        m.ax() = ax;
        m.bx() = bx;
        m.cx() = cx;
        m.dx() = dx;
        m.si() = si;
    }
};
void set_cl(Machine &m, uint8_t value) { m.cx() = uint16_t((m.cx() & 0xff00) | value); }
void set_ch(Machine &m, uint8_t value) { m.cx() = uint16_t((m.cx() & 255) | (uint16_t(value) << 8)); }
void grid_position_core(Machine &m, bool offset) {
    const Registers saved(m);
    set_cl(m, m.rd8(m.ss(), kEffectHeight));
    m.dx() = m.rd8(m.ss(), kEffectY);
    m.bx() = m.rd8(m.ss(), kEffectX);
    m.ax() = uint16_t(m.bx() + (m.dx() << 8));
    m.dx() = uint16_t(m.dx() << 8);
    m.si() = kEffectSpritePosition;
    if (offset)
        m.si() = uint16_t(m.si() + m.rd8(m.ss(), kEffectSpriteOffset) * 4);
    bool first_column = true;
    do {
        if (!first_column) {
            set_cl(m, m.rd8(m.ss(), kEffectHeight));
            m.si() = uint16_t(m.si() + 4);
            m.dx() = m.rd8(m.ss(), kEffectY);
            m.wr8(m.ss(), kEffectRowY, uint8_t(m.dx()));
            m.bx() = uint8_t(m.rd8(m.ss(), kEffectX) + 8);
            m.wr8(m.ss(), kEffectX, uint8_t(m.bx()));
            m.ax() = uint16_t(m.bx() + (m.dx() << 8));
            m.dx() = uint16_t(m.dx() << 8);
        }
        bool first_row = true;
        do {
            if (!first_row) {
                m.si() = uint16_t(m.si() + 4);
                m.dx() = uint8_t(m.rd8(m.ss(), kEffectRowY) + 8);
                m.wr8(m.ss(), kEffectRowY, uint8_t(m.dx()));
                m.bx() = m.rd8(m.ss(), kEffectX);
                m.ax() = uint16_t(m.bx() + (m.dx() << 8));
                m.dx() = uint16_t(m.dx() << 8);
            }
            m.wr16(m.ss(), m.si(), m.ax());
            set_cl(m, uint8_t(m.cx() - 1));
            first_row = false;
        } while (uint8_t(m.cx()) != 0);
        set_ch(m, uint8_t((m.cx() >> 8) - 1));
        first_column = false;
    } while ((m.cx() >> 8) != 0);
    saved.restore(m);
}
void hide_grid_core(Machine &m) {
    set_cl(m, m.rd8(m.ss(), kEffectHeight));
    m.si() = kEffectSpritePosition;
    do {
        m.ax() = 0x90;
        do {
            m.wr16(m.ds(), m.si(), m.ax());
            set_cl(m, uint8_t(m.cx() - 1));
            if (uint8_t(m.cx()) == 0)
                break;
            m.si() = uint16_t(m.si() + 4);
            m.ax() = m.dx();
        } while (true);
        set_ch(m, uint8_t((m.cx() >> 8) - 1));
        if ((m.cx() >> 8) == 0)
            break;
        set_cl(m, m.rd8(m.ss(), kEffectHeight));
        m.si() = uint16_t(m.si() + 4);
    } while (true);
}
void target_grid_core(Machine &m) {
    const bool hide = uint8_t(m.bx()) == 255;
    if (hide) {
        m.wr8(m.ss(), kEffectX, 0x90);
        m.wr8(m.ss(), kEffectY, 0);
        m.wr8(m.ss(), kEffectRowY, 0);
    } else {
        m.set_al(m.rd8(m.ss(), kTargetCount));
        uint8_t y = 0x48;
        if (m.al() == 2) {
            m.set_al(m.rd8(m.ss(), kTargetSide));
            y = m.al() == 0 ? 0x20 : 0x70;
        } else if (m.al() == 3) {
            m.set_al(m.rd8(m.ss(), kTargetSide));
            y = m.al() == 0 ? 8 : (m.al() == 1 ? 0x48 : 0x88);
        }
        m.wr8(m.ss(), kEffectX, 0);
        m.wr8(m.ss(), kEffectY, y);
        m.wr8(m.ss(), kEffectRowY, y);
    }
    m.wr8(m.ss(), kEffectHeight, 10);
    m.cx() = 0xa0a;
    if (hide)
        hide_grid_core(m);
    else
        grid_position_core(m, false);
}
} // namespace
// rom: 0x370000 (near)
// Arrange a CH-column grid of sprites using the SS-relative effect position/height bytes. Registers saved.
void position_effect_sprite_grid_call_core(Machine &m) { grid_position_core(m, false); }
void position_effect_sprite_grid(Machine &m) {
    position_effect_sprite_grid_call_core(m);
    m.ret();
}
// rom: 0x370088 (near)
// Position an effect sprite grid starting after DCBF sprites, preserving registers.
void position_offset_effect_sprite_grid_call_core(Machine &m) { grid_position_core(m, true); }
void position_offset_effect_sprite_grid(Machine &m) {
    position_offset_effect_sprite_grid_call_core(m);
    m.ret();
}
// rom: 0x370124 (near)
// Hide a grid: first sprite of each column gets X=90h, subsequent positions get entry DX (a ROM quirk).
void hide_effect_sprite_grid_call_core(Machine &m) { hide_grid_core(m); }
void hide_effect_sprite_grid(Machine &m) {
    hide_effect_sprite_grid_call_core(m);
    m.ret();
}
// Shared body of ROM 0x37025A
// Select the shared 10x10 battle-effect grid by target count/side, or hide it for BL=FFh.
void position_battle_effect_1_core(Machine &m) {
    target_grid_core(m);
    return;
}

// rom: 0x37025A (near)
void position_battle_effect_1(Machine &m) {
    position_battle_effect_1_core(m);
    m.ret();
}
// Shared body of ROM 0x370463
// Select the shared 10x10 battle-effect grid by target count/side, or hide it for BL=FFh.
void position_battle_effect_2_core(Machine &m) {
    target_grid_core(m);
    return;
}

// rom: 0x370463 (near)
void position_battle_effect_2(Machine &m) {
    position_battle_effect_2_core(m);
    m.ret();
}
// Shared body of ROM 0x370667
// Select the shared 10x10 battle-effect grid by target count/side, or hide it for BL=FFh.
void position_battle_effect_3_core(Machine &m) {
    target_grid_core(m);
    return;
}

// rom: 0x370667 (near)
void position_battle_effect_3(Machine &m) {
    position_battle_effect_3_core(m);
    m.ret();
}
// Shared body of ROM 0x371A01
// Select the shared 10x10 battle-effect grid by target count/side, or hide it for BL=FFh.
void position_battle_effect_4_core(Machine &m) {
    target_grid_core(m);
    return;
}

// rom: 0x371A01 (near)
void position_battle_effect_4(Machine &m) {
    position_battle_effect_4_core(m);
    m.ret();
}
// Shared body of ROM 0x371BF8
// Select the shared 10x10 battle-effect grid by target count/side, or hide it for BL=FFh.
void position_battle_effect_5_core(Machine &m) {
    target_grid_core(m);
    return;
}

// rom: 0x371BF8 (near)
void position_battle_effect_5(Machine &m) {
    position_battle_effect_5_core(m);
    m.ret();
}
// Shared body of ROM 0x372D81
// Select the shared 10x10 battle-effect grid by target count/side, or hide it for BL=FFh.
void position_battle_effect_6_core(Machine &m) {
    target_grid_core(m);
    return;
}

// rom: 0x372D81 (near)
void position_battle_effect_6(Machine &m) {
    position_battle_effect_6_core(m);
    m.ret();
}
// Shared body of ROM 0x376041
// Select the shared 10x10 battle-effect grid by target count/side, or hide it for BL=FFh.
void position_battle_effect_7_core(Machine &m) {
    target_grid_core(m);
    return;
}

// rom: 0x376041 (near)
void position_battle_effect_7(Machine &m) {
    position_battle_effect_7_core(m);
    m.ret();
}
// Shared body of ROM 0x379A06
// Position a 4-column, 7-row directional effect after eight sprites, or hide it for BL=FFh.
void position_directional_battle_effect_core(Machine &m) {
    const bool hide = uint8_t(m.bx()) == 255;
    uint8_t y = 0;
    if (!hide) {
        m.set_al(m.rd8(m.ss(), kEffectDirection));
        if (m.al() <= 3)
            y = uint8_t(m.al() * 0x38);
    }
    m.wr8(m.ss(), kEffectX, hide ? 0x90 : 0x70);
    m.wr8(m.ss(), kEffectY, y);
    m.wr8(m.ss(), kEffectRowY, y);
    m.wr8(m.ss(), kEffectHeight, 7);
    m.cx() = 0x407;
    if (hide)
        hide_grid_core(m);
    else {
        m.wr8(m.ss(), kEffectSpriteOffset, 8);
        grid_position_core(m, true);
    }
    return;
}

// rom: 0x379A06 (near)
void position_directional_battle_effect(Machine &m) {
    position_directional_battle_effect_core(m);
    m.ret();
}
namespace {
struct EffectFrameRegisters {
    uint16_t ax, bx, cx, dx, bp, si, di, ds, es, ss;
    explicit EffectFrameRegisters(Machine &m)
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
} // namespace
namespace {
// Reports a native battle animation's start and end (its ROM entry), with the palette variant and action/item
// id. Observe only: lets the presentation enhance the game's own animations.
struct AnimationScope {
    Machine &m;
    int rom;
    AnimationScope(Machine &machine, int entry) : m(machine), rom(entry) {
        hooks::emit(hooks::Event::BattleAnimation, m, {rom, m.rd8(m.ss(), 0xDC12), m.rd8(m.ss(), 0xDBFC)});
    }
    ~AnimationScope() { hooks::emit(hooks::Event::BattleAnimationEnd, m, {rom}); }
    AnimationScope(const AnimationScope &) = delete;
    AnimationScope &operator=(const AnimationScope &) = delete;
};
void play_sprite_sequence(Machine &m, bool impact);
} // namespace
// Shared body of ROM 0x370160
// Play the eight-frame hit effect (four VBlanks per frame), with its frame-3 sound and sprite cleanup.
void play_hit_sprite_effect_core(Machine &m) {
    play_sprite_sequence(m, false);
    return;
}

// rom: 0x370160
void play_hit_sprite_effect(Machine &m) {
    play_hit_sprite_effect_core(m);
    m.retf();
}
namespace {
// The hit and impact animations share tile/palette upload, a four-VBlank cadence and sprite attributes.
void play_configured_sprite_sequence(Machine &m, unsigned variant);
void play_sprite_sequence(Machine &m, bool impact) { play_configured_sprite_sequence(m, impact ? 1 : 0); }

} // namespace
// Shared body of ROM 0x370360
// Fourteen-frame impact sequence: palette variants, sounds on frames 2/6/10, then hide all sprites.
void play_impact_sprite_effect_core(Machine &m) {
    play_sprite_sequence(m, true);
    return;
}

// rom: 0x370360
// Fourteen-frame impact sequence: palette variants, sounds on frames 2/6/10, then hide all sprites.
void play_impact_sprite_effect(Machine &m) {
    play_impact_sprite_effect_core(m);
    m.retf();
}

namespace {
constexpr uint16_t kEffectTicks = 0xDC18, kEffectFrame = 0xDC19, kEffectPaletteVariant = 0xDC12;
struct SpriteSequence {
    uint8_t bank, last_frame, ticks, cleanup_count;
    uint16_t tiles, tile_count, palettes, frames, position, sound;
    uint32_t sound_frames;
    bool clear_bx, reset_ds, palette_first, cleanup_palette;
};
constexpr SpriteSequence kSpriteSequences[] = {
    {0xD7, 7, 3, 100, 0x120, 0x29, 0, 0x640, 0x25A, 0x18, 1u << 3, true, true, false, true},
    {0xD7, 13, 3, 0, 0xDC0, 0x48, 0xC80, 0x16C0, 0x463, 0x12, (1u << 2) | (1u << 6) | (1u << 10), false,
     false, false, false},
    {0xD7, 7, 2, 100, 0x23C0, 0x62, 0x2280, 0x3000, 0x667, 0x18, 1u << 3, false, false, true, true},
    {0xDC, 8, 3, 100, 0x120, 0x5E, 0, 0xCE0, 0x1A01, 0x30, 1u << 1, true, false, false, true},
    {0xDC, 14, 1, 100, 0x14F0, 0xA3, 0x13F0, 0x2950, 0x1BF8, 0x31, 1u << 1, true, false, false, true},
    {0xDA, 25, 1, 100, 0x140, 0xD6, 0, 0x1C00, 0x2D81, 0x37, 1u << 1, true, false, false, true},
    {0xDB, 18, 2, 100, 0x4930, 0xFA, 0x47F0, 0x6870, 0x6041, 0x41, 1u << 1, true, false, false, true}};
void position_sequence_sprites(Machine &m, uint16_t entry) {
    switch (entry) {
    case 0x25A:
        position_battle_effect_1_core(m);
        break;
    case 0x463:
        position_battle_effect_2_core(m);
        break;
    case 0x667:
        position_battle_effect_3_core(m);
        break;
    case 0x1A01:
        position_battle_effect_4_core(m);
        break;
    case 0x1BF8:
        position_battle_effect_5_core(m);
        break;
    case 0x2D81:
        position_battle_effect_6_core(m);
        break;
    case 0x6041:
        position_battle_effect_7_core(m);
        break;
    }
}
void sequence_sprite_words(Machine &m, uint16_t table, uint16_t stride, uint16_t destination,
                           uint16_t count) {
    m.si() = uint16_t(table + m.rd8(m.ss(), kEffectFrame) * stride);
    m.set_al(0);
    m.di() = destination;
    m.cx() = count;
    do {
        m.ax() = uint16_t((m.rd16(m.es(), m.si()) + m.dx()) | 0x2000);
        m.wr16(m.ss(), m.di(), m.ax());
        m.di() = uint16_t(m.di() + 4);
        m.si() = uint16_t(m.si() + 2);
    } while (--m.cx());
}
void sequence_upload(Machine &m, bool palette_first, uint8_t count, bool palettes = true) {
    if (palette_first && palettes)
        dma_palettes_core(m);
    dma_sprite_table_core(m);
    if (!palette_first && palettes)
        dma_palettes_core(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.set_al(count);
    m.out8(6, m.al());
}
void sequence_wait(Machine &m, uint8_t limit, bool reset_ds = false) {
    for (;;) {
        hooks::emit(hooks::Event::PresentationPicture, m, {5});
        m.call_far(0x4000, 9);
        if (reset_ds) {
            m.ax() = 0;
            m.ds() = 0;
        }
        const uint16_t segment = reset_ds ? m.ds() : m.ss();
        m.set_al(m.rd8(segment, kEffectTicks));
        if (m.al() == limit)
            break;
        m.set_al(uint8_t(m.al() + 1));
        m.wr8(segment, kEffectTicks, m.al());
    }
}
void sequence_sound(Machine &m, uint16_t sound) {
    m.ax() = sound;
    const uint16_t caller_cs = m.cs();
    m.cs() = 0xD000;
    play_sound_effect_entry_core(m);
    m.cs() = caller_cs;
}
void play_configured_sprite_sequence(Machine &m, unsigned variant) {
    const SpriteSequence &sequence = kSpriteSequences[variant];
    static constexpr int kEntries[] = {0x370160, 0x370360, 0x370570, 0x371900, 0x371B00, 0x372C80, 0x375F40};
    const AnimationScope animation(m, kEntries[variant]);
    const EffectFrameRegisters saved(m);
    m.wr8(m.ss(), kEffectTicks, 0);
    m.wr8(m.ss(), kEffectFrame, 0);
    m.set_al(sequence.bank);
    m.out8(0xC2, m.al());
    m.ax() = 0x2000;
    m.ds() = m.ax();
    m.ax() = 0;
    m.cx() = sequence.tile_count;
    m.si() = sequence.tiles;
    copy_tiles_core(m);
    m.si() = uint16_t(sequence.palettes + m.rd8(m.ss(), kEffectPaletteVariant) * 32);
    m.ax() = 12;
    m.cx() = 1;
    m.di() = 0xC400;
    copy_palettes_core(m);
    if (sequence.clear_bx)
        m.bx() = 0;
    position_sequence_sprites(m, sequence.position);
    m.ax() = 0x2000;
    m.es() = m.ax();
    m.dx() = 0x800;
    for (;;) {
        sequence_sprite_words(m, sequence.frames, 200, 0xC000, 100);
        sequence_upload(m, sequence.palette_first, 100);
        sequence_wait(m, sequence.ticks, sequence.reset_ds);
        const uint16_t segment = sequence.reset_ds ? m.ds() : m.ss();
        m.wr8(segment, kEffectTicks, 0);
        m.set_al(m.rd8(segment, kEffectFrame));
        if (m.al() == sequence.last_frame)
            break;
        m.set_al(uint8_t(m.al() + 1));
        m.wr8(segment, kEffectFrame, m.al());
        if (sequence.sound_frames & (1u << m.al()))
            sequence_sound(m, sequence.sound);
    }
    m.bx() = 0xFF;
    position_sequence_sprites(m, sequence.position);
    sequence_upload(m, sequence.palette_first, sequence.cleanup_count, sequence.cleanup_palette);
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 9);
    saved.restore(m);
}
} // namespace
// Shared body of ROM 0x370570
// Eight-frame effect 3: ROM bank D7, three VBlanks per frame, palette DMA before sprite DMA.
void play_battle_effect_3_core(Machine &m) {
    play_configured_sprite_sequence(m, 2);
    return;
}

// rom: 0x370570
// Eight-frame effect 3: ROM bank D7, three VBlanks per frame, palette DMA before sprite DMA.
void play_battle_effect_3(Machine &m) {
    play_battle_effect_3_core(m);
    m.retf();
}

// Shared body of ROM 0x371900
// Effect 4: 9 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_4_core(Machine &m) {
    play_configured_sprite_sequence(m, 3);
    return;
}

// rom: 0x371900
// Effect 4: 9 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_4(Machine &m) {
    play_battle_effect_4_core(m);
    m.retf();
}

// Shared body of ROM 0x371B00
// Effect 5: 15 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_5_core(Machine &m) {
    play_configured_sprite_sequence(m, 4);
    return;
}

// rom: 0x371B00
// Effect 5: 15 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_5(Machine &m) {
    play_battle_effect_5_core(m);
    m.retf();
}

// Shared body of ROM 0x372C80
// Effect 6: 26 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_6_core(Machine &m) {
    play_configured_sprite_sequence(m, 5);
    return;
}

// rom: 0x372C80
// Effect 6: 26 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_6(Machine &m) {
    play_battle_effect_6_core(m);
    m.retf();
}

// Shared body of ROM 0x375F40
// Effect 7: 19 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_7_core(Machine &m) {
    play_configured_sprite_sequence(m, 6);
    return;
}

// rom: 0x375F40
// Effect 7: 19 sprite-map frames, variant palette, timed sound and sprite cleanup.
void play_battle_effect_7(Machine &m) {
    play_battle_effect_7_core(m);
    m.retf();
}

// Shared body of ROM 0x377FC0
// Ten six-VBlank phases: brighten/tint, recolor the selected formation, then redraw its three portraits.
void play_formation_palette_effect_core(Machine &m) {
    const AnimationScope animation(m, 0x377FC0);
    const EffectFrameRegisters saved(m);
    m.wr8(m.ss(), kEffectTicks, 0);
    m.wr8(m.ss(), kEffectFrame, 0);
    for (;;) {
        sequence_upload(m, false, 0);
        sequence_wait(m, 5);
        m.wr8(m.ss(), kEffectTicks, 0);
        m.set_al(m.rd8(m.ss(), kEffectFrame));
        if (m.al() == 9)
            break;
        m.set_al(uint8_t(m.al() + 1));
        m.wr8(m.ss(), kEffectFrame, m.al());
        if (m.al() == 1) {
            m.set_al(m.rd8(m.ss(), kEffectPaletteVariant));
            if (m.al() == 3) {
                m.ax() = 0x8000;
                m.ds() = m.ax();
                m.si() = 0x44D0;
                m.ax() = 0;
                m.cx() = 4;
                m.di() = 0xC600;
                copy_palettes_core(m);
                m.cx() = 16;
                do {
                    const uint16_t count = m.cx();
                    m.ax() = 0;
                    m.cx() = 4;
                    palette_step_toward_core(m);
                    dma_sprite_table_core(m);
                    hooks::emit(hooks::Event::PresentationPicture, m, {5});
                    m.call_far(0x4000, 9);
                    dma_palettes_core(m);
                    m.cx() = count;
                } while (--m.cx());
            } else {
                m.cx() = 16;
                do {
                    const uint16_t count = m.cx();
                    m.ax() = 0;
                    m.cx() = 4;
                    brighten_palette_step_core(m);
                    m.cx() = count;
                    dma_palettes_core(m);
                    hooks::emit(hooks::Event::PresentationPicture, m, {5});
                    m.call_far(0x4000, 9);
                } while (--m.cx());
            }
        } else if (m.al() == 5) {
            {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                load_battle_palette_backup_core(m);
                m.cs() = native_caller_cs;
            }
            sequence_sound(m, 0x38);
            m.set_al(m.rd8(m.ss(), kEffectPaletteVariant));
            const uint8_t variant = m.al();
            const EffectFrameRegisters tint_saved(m);
            m.set_al(m.rd8(m.ss(), kTargetSide));
            m.wr8(m.ss(), 0xD70D, m.al());
            if (variant == 6) {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                apply_formation_portrait_tint2_core(m);
                m.cs() = native_caller_cs;
            } else if (variant == 3) {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                apply_formation_portrait_tint1_core(m);
                m.cs() = native_caller_cs;
            } else if (variant == 7) {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                apply_formation_portrait_tint3_core(m);
                m.cs() = native_caller_cs;
            } else {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                copy_formation_palette_core(m);
                m.cs() = caller_cs;
            };
            tint_saved.restore(m);
        } else if (m.al() == 8) {
            m.ax() = 2;
            {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                restore_formation_portrait_palette_core(m);
                m.cs() = native_caller_cs;
            }
            m.ax() = 1;
            {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                restore_formation_portrait_palette_core(m);
                m.cs() = native_caller_cs;
            }
            m.ax() = 0;
            {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                restore_formation_portrait_palette_core(m);
                m.cs() = native_caller_cs;
            }
        }
    }
    saved.restore(m);
    return;
}

// rom: 0x377FC0
// Ten six-VBlank phases: brighten/tint, recolor the selected formation, then redraw its three portraits.
void play_formation_palette_effect(Machine &m) {
    play_formation_palette_effect_core(m);
    m.retf();
}

// Shared body of ROM 0x379900
// Directional transformation sprites: mode 0 stops at frame 3; resumed mode FF skips asset/counter setup.
void play_directional_transformation_effect_core(Machine &m) {
    const EffectFrameRegisters saved(m);
    m.set_al(m.rd8(m.ss(), 0xDD73));
    if (m.al() != 0xFF) {
        m.wr8(m.ss(), kEffectTicks, 0);
        m.wr8(m.ss(), kEffectFrame, 0);
        m.set_al(0xDE);
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        m.ax() = 0xC0;
        m.cx() = 0x3C;
        m.si() = 0xDF90;
        copy_tiles_core(m);
        m.si() = 0xDF70;
        m.ax() = 12;
        m.cx() = 1;
        m.di() = 0xC400;
        copy_palettes_core(m);
        m.bx() = 0;
        position_directional_battle_effect_core(m);
    }
    m.ax() = 0x2000;
    m.es() = m.ax();
    m.dx() = 0x8C0;
    for (;;) {
        sequence_sprite_words(m, 0xE710, 56, 0xC020, 28);
        sequence_upload(m, false, 36);
        sequence_wait(m, 5);
        m.wr8(m.ss(), kEffectTicks, 0);
        m.set_al(m.rd8(m.ss(), kEffectFrame));
        const uint16_t saved_dx = m.dx();
        m.dx() = uint16_t((m.dx() & 0xFF00) | m.rd8(m.ss(), 0xDD73));
        const bool pause = uint8_t(m.dx()) == 0;
        m.dx() = saved_dx;
        if (pause && m.al() == 3)
            break;
        if (!pause && m.al() == 6) {
            m.bx() = 0xFF;
            position_directional_battle_effect_core(m);
            sequence_upload(m, false, 0);
            break;
        }
        m.set_al(uint8_t(m.al() + 1));
        m.wr8(m.ss(), kEffectFrame, m.al());
        if (m.al() == 1)
            sequence_sound(m, 0x3D);
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 9);
    saved.restore(m);
    return;
}

// rom: 0x379900
// Directional transformation sprites: mode 0 stops at frame 3; resumed mode FF skips asset/counter setup.
void play_directional_transformation_effect(Machine &m) {
    play_directional_transformation_effect_core(m);
    m.retf();
}

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
[[maybe_unused]] constexpr uint16_t kIntroStrip = 0xC81C;
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
// Formation records share the field movement workspace.
[[maybe_unused]] constexpr uint16_t kFormationBackTileSource = 0xD500;
[[maybe_unused]] constexpr uint16_t kFieldPreSpriteCallback = 0xD501;
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteSource = 0xD502;
[[maybe_unused]] constexpr uint16_t kFieldPostSpriteCallback = 0xD503;
[[maybe_unused]] constexpr uint16_t kFormationBackMapSource = 0xD504;
[[maybe_unused]] constexpr uint16_t kFieldNpcCallback = 0xD505;
[[maybe_unused]] constexpr uint16_t kFormationBackTileCount = 0xD506;
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteVariant = 0xD508;
[[maybe_unused]] constexpr uint16_t kFormationBackWidth = 0xD509;
[[maybe_unused]] constexpr uint16_t kFormationBackHeight = 0xD50A;
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteCount = 0xD50B;
[[maybe_unused]] constexpr uint16_t kFormationMiddleTileSource = 0xD50C;
[[maybe_unused]] constexpr uint16_t kBattleWipeThirdScroll = 0xD50D;
[[maybe_unused]] constexpr uint16_t kFormationMiddlePaletteSource = 0xD50E;
[[maybe_unused]] constexpr uint16_t kBattleWipeFourthScroll = 0xD50F;
[[maybe_unused]] constexpr uint16_t kFormationMiddleMapSource = 0xD510;
[[maybe_unused]] constexpr uint16_t kFieldScroll = 0xD511;
[[maybe_unused]] constexpr uint16_t kFormationMiddleTileCount = 0xD512;
[[maybe_unused]] constexpr uint16_t kIntroOverlayProgress = 0xD513;
[[maybe_unused]] constexpr uint16_t kFormationMiddlePaletteVariant = 0xD514;
[[maybe_unused]] constexpr uint16_t kFormationMiddleWidth = 0xD515;
[[maybe_unused]] constexpr uint16_t kFormationMiddleHeight = 0xD516;
[[maybe_unused]] constexpr uint16_t kFormationMiddlePaletteCount = 0xD517;
[[maybe_unused]] constexpr uint16_t kFormationFrontTileSource = 0xD518;
[[maybe_unused]] constexpr uint16_t kFieldFadeActive = 0xD519;
[[maybe_unused]] constexpr uint16_t kFormationFrontPaletteSource = 0xD51A;
[[maybe_unused]] constexpr uint16_t kFieldPaletteTimer = 0xD51B;
[[maybe_unused]] constexpr uint16_t kFormationFrontMapSource = 0xD51C;
[[maybe_unused]] constexpr uint16_t kFormationFrontTileCount = 0xD51E;
[[maybe_unused]] constexpr uint16_t kFieldTileAnimationTimer = 0xD51F;
[[maybe_unused]] constexpr uint16_t kFormationFrontPaletteVariant = 0xD520;
[[maybe_unused]] constexpr uint16_t kFormationFrontWidth = 0xD521;
[[maybe_unused]] constexpr uint16_t kFormationFrontHeight = 0xD522;
[[maybe_unused]] constexpr uint16_t kFormationFrontPaletteCount = 0xD523;
[[maybe_unused]] constexpr uint16_t kFormationFrontPosition = 0xD524;
[[maybe_unused]] constexpr uint16_t kFormationEffectTileSource = 0xD526;
[[maybe_unused]] constexpr uint16_t kFormationEffectPaletteSource = 0xD528;
[[maybe_unused]] constexpr uint16_t kFormationEffectMapSource = 0xD52A;
[[maybe_unused]] constexpr uint16_t kOverlayEnabled = 0xD52B;
[[maybe_unused]] constexpr uint16_t kFormationEffectTileCount = 0xD52C;
[[maybe_unused]] constexpr uint16_t kFormationEffectPaletteVariant = 0xD52E;
[[maybe_unused]] constexpr uint16_t kFormationEffectWidth = 0xD52F;
[[maybe_unused]] constexpr uint16_t kFormationEffectHeight = 0xD530;
[[maybe_unused]] constexpr uint16_t kFormationEffectPaletteCount = 0xD531;
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
[[maybe_unused]] constexpr uint16_t kPartyMenuText = 0xD701;
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
[[maybe_unused]] constexpr uint16_t kRam_effect_palette_variant = 0xDC12;
[[maybe_unused]] constexpr uint16_t kRam_effect_ticks = 0xDC18;
[[maybe_unused]] constexpr uint16_t kRam_effect_frame = 0xDC19;
[[maybe_unused]] constexpr uint16_t kRam_effect_x = 0xDCBB;
[[maybe_unused]] constexpr uint16_t kRam_effect_y = 0xDCBC;
[[maybe_unused]] constexpr uint16_t kRam_effect_row_y = 0xDCBD;
[[maybe_unused]] constexpr uint16_t kRam_effect_height = 0xDCBE;
[[maybe_unused]] constexpr uint16_t kRam_effect_sprite_offset = 0xDCBF;
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
// rom: 0x370770
// Play the bow battle animation.
void play_bow_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x370770);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x6F);
    m.si() = uint16_t(0x3740);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x3640);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_079f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_07ab;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_079f;
branch_07ab:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_bow_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x4520);
branch_07ca:;
    m.si() = uint16_t(0x4520);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_07d1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_07dd;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_07d1;
branch_07dd:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_07e3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_07e3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_0809:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_081e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_0809;
branch_081e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x23);
    if (m.flags() & emu::ZF)
        goto branch_087f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_0864;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_0864;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x11);
    if (m.flags() & emu::ZF)
        goto branch_086e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x15);
    if (m.flags() & emu::ZF)
        goto branch_086e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x19);
    if (m.flags() & emu::ZF)
        goto branch_086e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1D);
    if (m.flags() & emu::ZF)
        goto branch_086e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x21);
    if (m.flags() & emu::ZF)
        goto branch_086e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x13);
    if (m.flags() & emu::ZF)
        goto branch_0878;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x17);
    if (m.flags() & emu::ZF)
        goto branch_0878;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1B);
    if (m.flags() & emu::ZF)
        goto branch_0878;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1F);
    if (m.flags() & emu::ZF)
        goto branch_0878;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x23);
    if (m.flags() & emu::ZF)
        goto branch_0878;
    goto branch_087c;
branch_0864:;
    m.ax() = uint16_t(0x26);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_087c;
branch_086e:;
    m.ax() = uint16_t(0x12);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_087c;
branch_0878:;
    goto branch_087c;
branch_087c:;
    goto branch_07ca;
branch_087f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_bow_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_bow_animation(Machine &m) {
    play_bow_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3708A1 (near)
// Select the bow effect origin for the current battle target.
void position_bow_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_08a9;
    goto branch_097b;
branch_08a9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_08b9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_08bb;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_08c6;
branch_08b9:;
    goto branch_08f6;
branch_08bb:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0939;
    goto branch_095a;
branch_08c6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_08d4;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_08f6;
    goto branch_0918;
branch_08d4:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_099c;
branch_08f6:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_099c;
branch_0918:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_099c;
branch_0939:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_099c;
branch_095a:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_099c;
branch_097b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_099f;
branch_099c:;
    position_effect_sprite_grid_call_core(m);
branch_099f:;
    return;
}
void position_bow_effect(Machine &m) {
    position_bow_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3709A0
// Play the firearm battle animation.
void play_firearm_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3709A0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x71);
    m.si() = uint16_t(0x6200);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x6140);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_09cf:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_09db;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_09cf;
branch_09db:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_firearm_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x7020);
branch_09fa:;
    m.si() = uint16_t(0x7020);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_0a06:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0a12;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_0a06;
branch_0a12:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_0a18:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0a18;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_0a3e:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_0a53;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_0a3e;
branch_0a53:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (m.flags() & emu::ZF)
        goto branch_0a92;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0a81;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_0a81;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_0a8b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::ZF)
        goto branch_0a8b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_0a8d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_0a8d;
    goto branch_0a8f;
branch_0a81:;
    m.ax() = uint16_t(0x2E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_0a8f;
branch_0a8b:;
    goto branch_0a8f;
branch_0a8d:;
    goto branch_0a8f;
branch_0a8f:;
    goto branch_09fa;
branch_0a92:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_firearm_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_firearm_animation(Machine &m) {
    play_firearm_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x370AB4 (near)
// Select the firearm effect origin for the current battle target.
void position_firearm_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_0abc;
    goto branch_0b8e;
branch_0abc:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0acc;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_0ace;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_0ad9;
branch_0acc:;
    goto branch_0b09;
branch_0ace:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0b4c;
    goto branch_0b6d;
branch_0ad9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0ae7;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0b09;
    goto branch_0b2b;
branch_0ae7:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0baf;
branch_0b09:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0baf;
branch_0b2b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0baf;
branch_0b4c:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0baf;
branch_0b6d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0baf;
branch_0b8e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_0bb2;
branch_0baf:;
    position_effect_sprite_grid_call_core(m);
branch_0bb2:;
    return;
}
void position_firearm_effect(Machine &m) {
    position_firearm_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x370BC0
// Play the whip battle animation.
void play_whip_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x370BC0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x68);
    m.si() = uint16_t(0x7CA0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x7BE0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_0bef:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0bfb;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_0bef;
branch_0bfb:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_whip_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x89A0);
branch_0c1a:;
    m.si() = uint16_t(0x89A0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_0c21:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0c2d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_0c21;
branch_0c2d:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_0c33:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_0c33;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_0c59:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_0c6e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_0c59;
branch_0c6e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_0c9f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0c90;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_0c9a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_0c9c;
    goto branch_0c9c;
branch_0c90:;
    m.ax() = uint16_t(0x31);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_0c9c;
branch_0c9a:;
    goto branch_0c9c;
branch_0c9c:;
    goto branch_0c1a;
branch_0c9f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_whip_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_whip_animation(Machine &m) {
    play_whip_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x370CC1 (near)
// Select the whip effect origin for the current battle target.
void position_whip_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_0cc9;
    goto branch_0d9b;
branch_0cc9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0cd9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_0cdb;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_0ce6;
branch_0cd9:;
    goto branch_0d16;
branch_0cdb:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0d59;
    goto branch_0d7a;
branch_0ce6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0cf4;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0d16;
    goto branch_0d38;
branch_0cf4:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0dbc;
branch_0d16:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0dbc;
branch_0d38:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0dbc;
branch_0d59:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0dbc;
branch_0d7a:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_0dbc;
branch_0d9b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_0dbf;
branch_0dbc:;
    position_effect_sprite_grid_call_core(m);
branch_0dbf:;
    return;
}
void position_whip_effect(Machine &m) {
    position_whip_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x370DC0
// Play the missile projectile battle animation.
void play_missile_projectile_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x370DC0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(0x9110);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x90B0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_0dfb:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_0e0f;
    goto branch_0dfb;
branch_0e0f:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
branch_0e11:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x39CE);
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.set_ah(uint8_t(0x1D));
    {
        const uint16_t callback_cs = m.cs();
        position_missile_projectile_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x10);
    if (!(m.flags() & emu::ZF))
        goto branch_0e11;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
branch_0e30:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx() >> 8), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0e8c;
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx() >> 8), true))) << 8));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0e45;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0e49;
    m.set_ah(uint8_t(0x2));
    goto branch_0e4b;
branch_0e45:;
    m.set_ah(uint8_t(0x0));
    goto branch_0e4b;
branch_0e49:;
    m.set_ah(uint8_t(0x1));
branch_0e4b:;
    computed::push(m, m.ax());
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    {
        const uint16_t callback_cs = m.cs();
        prepare_missile_projectile_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    {
        const uint16_t callback_cs = m.cs();
        prepare_missile_projectile_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x39CE);
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.set_ah(uint8_t(computed::subtract<uint8_t>(m, m.ah(), uint8_t(m.dx()))));
    {
        const uint16_t callback_cs = m.cs();
        position_missile_projectile_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.set_ah(uint8_t(computed::subtract<uint8_t>(m, m.ah(), uint8_t(m.dx()))));
    (void)computed::subtract<uint8_t>(m, m.ah(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0e8c;
    m.set_ah(uint8_t(computed::increment<uint8_t>(m, m.ah(), true)));
    computed::push(m, m.ax());
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::pop(m));
    m.si() = uint16_t(0x3A1E);
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    {
        const uint16_t callback_cs = m.cs();
        position_missile_projectile_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_0e8c:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0e97;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx()))));
    goto branch_0e30;
branch_0e97:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x40));
    m.out8(uint8_t(0x6), m.al());
branch_0eb3:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0ec8;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_0eb3;
branch_0ec8:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1C);
    if (m.flags() & emu::ZF)
        goto branch_0ef7;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0eea;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0eea;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_0eea;
    goto branch_0ef4;
branch_0eea:;
    m.ax() = uint16_t(0x57);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_0ef4;
branch_0ef4:;
    goto branch_0e0f;
branch_0ef7:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_missile_projectile_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x40));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_missile_projectile_animation(Machine &m) {
    play_missile_projectile_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x370F19 (near)
// Initialize the tile attributes for the missile projectile sprites.
void prepare_missile_projectile_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x9190);
branch_0f2b:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0f37;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_0f2b;
branch_0f37:;
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_0f3f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0f4b;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_0f3f;
branch_0f4b:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
branch_0f50:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0f6a;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_0f50;
branch_0f6a:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_missile_projectile_sprites(Machine &m) {
    prepare_missile_projectile_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x370F70 (near)
// Position the missile projectile sprite grid using the current effect offsets.
void position_missile_projectile_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_0f7a:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0f86;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_0f7a;
branch_0f86:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_missile_projectile_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_missile_projectile_sprite_grid(Machine &m) {
    position_missile_projectile_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x370FA9 (near)
// Select the missile projectile effect origin for the current battle target.
void position_missile_projectile_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_0fb5;
    goto branch_1035;
branch_0fb5:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_0ff7;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0fcd;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_0fcf;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_0fd9;
branch_0fcd:;
    goto branch_0ff7;
branch_0fcf:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1015;
    goto branch_1025;
branch_0fd9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_0fe7;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_0ff7;
    goto branch_1005;
branch_0fe7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_1056;
branch_0ff7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_1056;
branch_1005:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_1056;
branch_1015:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_1056;
branch_1025:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_1056;
branch_1035:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_1056;
branch_1056:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_missile_projectile_effect(Machine &m) {
    position_missile_projectile_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x371060
// Play the explosive blast battle animation.
void play_explosive_blast_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x371060);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x8D);
    m.si() = uint16_t(0x9210);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x91B0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_explosive_blast_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xA3B0);
branch_10a9:;
    m.si() = uint16_t(0xA3B0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_10b0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_10bc;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xF0));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_10b0;
branch_10bc:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x78);
branch_10c2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_10c2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x78));
    m.out8(uint8_t(0x6), m.al());
branch_10e8:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_10fd;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_10e8;
branch_10fd:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_112e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_111f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_1129;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_112b;
    goto branch_112b;
branch_111f:;
    m.ax() = uint16_t(0x31);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_112b;
branch_1129:;
    goto branch_112b;
branch_112b:;
    goto branch_10a9;
branch_112e:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_explosive_blast_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_explosive_blast_animation(Machine &m) {
    play_explosive_blast_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x371150 (near)
// Select the explosive blast effect origin for the current battle target.
void position_explosive_blast_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_1158;
    goto branch_124a;
branch_1158:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_11c5;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_11c5;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_11a3;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_11e7;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1188;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_118a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_1195;
branch_1188:;
    goto branch_11c5;
branch_118a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1208;
    goto branch_1229;
branch_1195:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_11a3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_11c5;
    goto branch_11e7;
branch_11a3:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xC));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_126b;
branch_11c5:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x40));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x40));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xC));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_126b;
branch_11e7:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x80));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x80));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xC));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_126b;
branch_1208:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x18));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x18));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xC));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_126b;
branch_1229:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x68));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x68));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xC));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_126b;
branch_124a:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xC));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xC));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_126e;
branch_126b:;
    position_effect_sprite_grid_call_core(m);
branch_126e:;
    return;
}
void position_explosive_blast_effect(Machine &m) {
    position_explosive_blast_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x371270
// Play the missile trails battle animation.
void play_missile_trails_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x371270);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x2E);
    m.si() = uint16_t(0xA9B0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xA950);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_12ab:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_12bf;
    goto branch_12ab;
branch_12bf:;
    m.bx() = uint16_t(0x0);
branch_12c2:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3A7A);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_12cf:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_12db;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_12cf;
branch_12db:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_missile_trails_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.set_ah(uint8_t(0x4));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_12ef;
    m.set_ah(uint8_t(uint8_t(m.bx())));
branch_12ef:;
    m.si() = uint16_t(0x3A70);
    {
        const uint16_t callback_cs = m.cs();
        position_missile_trails_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_12c2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x40));
    m.out8(uint8_t(0x6), m.al());
branch_130d:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1322;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_130d;
branch_1322:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_1365;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1350;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_1350;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_1350;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1358;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_1358;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_1358;
    goto branch_1362;
branch_1350:;
    m.ax() = uint16_t(0x31);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_1358:;
    m.ax() = uint16_t(0x31);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_1362;
branch_1362:;
    goto branch_12bf;
branch_1365:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_missile_trails_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x40));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_missile_trails_animation(Machine &m) {
    play_missile_trails_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x371387 (near)
// Initialize the tile attributes for the missile trails sprites.
void prepare_missile_trails_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xAF70);
branch_1399:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_13a5;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_1399;
branch_13a5:;
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_13b0:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_13bc;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_13b0;
branch_13bc:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x10));
branch_13c1:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_13db;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_13c1;
branch_13db:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_missile_trails_sprites(Machine &m) {
    prepare_missile_trails_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3713E1 (near)
// Position the missile trails sprite grid using the current effect offsets.
void position_missile_trails_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_13ee:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_13fa;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_13ee;
branch_13fa:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_missile_trails_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_missile_trails_sprite_grid(Machine &m) {
    position_missile_trails_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37141D (near)
// Select the missile trails effect origin for the current battle target.
void position_missile_trails_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_1429;
    goto branch_14d1;
branch_1429:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_1493;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1493;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_1483;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_14a1;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_14b1;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_14c1;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1469;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_146b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_1475;
branch_1469:;
    goto branch_1493;
branch_146b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_14b1;
    goto branch_14c1;
branch_1475:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1483;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1493;
    goto branch_14a1;
branch_1483:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_14f2;
branch_1493:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_14f2;
branch_14a1:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_14f2;
branch_14b1:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_14f2;
branch_14c1:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_14f2;
branch_14d1:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_14f2;
branch_14f2:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_missile_trails_effect(Machine &m) {
    position_missile_trails_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x371500
// Play the destructive ray battle animation.
void play_destructive_ray_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x371500);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xAA);
    m.si() = uint16_t(0xC0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_152f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_153b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_152f;
branch_153b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_destructive_ray_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x1600);
branch_155a:;
    m.si() = uint16_t(0x1600);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_1561:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_156d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_1561;
branch_156d:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_1573:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1573;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_1599:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_15ae;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_1599;
branch_15ae:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1B);
    if (m.flags() & emu::ZF)
        goto branch_15da;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_15cc;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_15cc;
    goto branch_15d8;
branch_15cc:;
    m.ax() = uint16_t(0x49);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_15d8;
branch_15d8:;
    goto branch_155a;
branch_15da:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_destructive_ray_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_destructive_ray_animation(Machine &m) {
    play_destructive_ray_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3715FC (near)
// Select the destructive ray effect origin for the current battle target.
void position_destructive_ray_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_1604;
    goto branch_16d6;
branch_1604:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1614;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_1616;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_1621;
branch_1614:;
    goto branch_1651;
branch_1616:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1694;
    goto branch_16b5;
branch_1621:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_162f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1651;
    goto branch_1673;
branch_162f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_16f7;
branch_1651:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_16f7;
branch_1673:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_16f7;
branch_1694:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_16f7;
branch_16b5:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_16f7;
branch_16d6:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_16fa;
branch_16f7:;
    position_effect_sprite_grid_call_core(m);
branch_16fa:;
    return;
}
void position_destructive_ray_effect(Machine &m) {
    position_destructive_ray_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x371700
// Play the lightsaber battle animation.
void play_lightsaber_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x371700);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x84);
    m.si() = uint16_t(0xC0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_172f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_173b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_172f;
branch_173b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_lightsaber_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x1140);
branch_175a:;
    m.si() = uint16_t(0x1140);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_1761:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_176d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_1761;
branch_176d:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_1773:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1773;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_1799:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_17ae;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_1799;
branch_17ae:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x10);
    if (m.flags() & emu::ZF)
        goto branch_17d8;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_17cc;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_17cc;
    goto branch_17d6;
branch_17cc:;
    m.ax() = uint16_t(0x2F);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_17d6;
branch_17d6:;
    goto branch_175a;
branch_17d8:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_lightsaber_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_lightsaber_animation(Machine &m) {
    play_lightsaber_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3717FA (near)
// Select the lightsaber effect origin for the current battle target.
void position_lightsaber_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_1802;
    goto branch_18d4;
branch_1802:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1812;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_1814;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_181f;
branch_1812:;
    goto branch_184f;
branch_1814:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1892;
    goto branch_18b3;
branch_181f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_182d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_184f;
    goto branch_1871;
branch_182d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_18f5;
branch_184f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_18f5;
branch_1871:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_18f5;
branch_1892:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_18f5;
branch_18b3:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_18f5;
branch_18d4:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_18f8;
branch_18f5:;
    position_effect_sprite_grid_call_core(m);
branch_18f8:;
    return;
}
void position_lightsaber_effect(Machine &m) {
    position_lightsaber_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x371D00
// Play the chainsaw battle animation.
void play_chainsaw_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x371D00);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x81);
    m.si() = uint16_t(0xB030);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xB010);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_1d2f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1d3b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_1d2f;
branch_1d3b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_chainsaw_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xC050);
branch_1d5a:;
    m.si() = uint16_t(0xC050);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_1d61:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1d6d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_1d61;
branch_1d6d:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_1d73:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1d73;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_1d99:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_1dae;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_1d99;
branch_1dae:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x15);
    if (m.flags() & emu::ZF)
        goto branch_1dd8;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_1dcc;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_1dd6;
    goto branch_1dd6;
branch_1dcc:;
    m.ax() = uint16_t(0x32);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_1dd6;
branch_1dd6:;
    goto branch_1d5a;
branch_1dd8:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_chainsaw_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_chainsaw_animation(Machine &m) {
    play_chainsaw_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x371DFA (near)
// Select the chainsaw effect origin for the current battle target.
void position_chainsaw_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_1e02;
    goto branch_1ed4;
branch_1e02:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1e12;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_1e14;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_1e1f;
branch_1e12:;
    goto branch_1e4f;
branch_1e14:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1e92;
    goto branch_1eb3;
branch_1e1f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1e2d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1e4f;
    goto branch_1e71;
branch_1e2d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_1ef5;
branch_1e4f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_1ef5;
branch_1e71:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_1ef5;
branch_1e92:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_1ef5;
branch_1eb3:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_1ef5;
branch_1ed4:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_1ef8;
branch_1ef5:;
    position_effect_sprite_grid_call_core(m);
branch_1ef8:;
    return;
}
void position_chainsaw_effect(Machine &m) {
    position_chainsaw_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x371F00
// Play the spinning strike battle animation.
void play_spinning_strike_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x371F00);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xE0);
    m.si() = uint16_t(0x1F50);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x1E90);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_1f2f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1f3b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_1f2f;
branch_1f3b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_spinning_strike_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x3B50);
branch_1f5a:;
    m.si() = uint16_t(0x3B50);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_1f61:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_1f6d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_1f61;
branch_1f6d:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_1f73:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_1f73;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_1f99:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_1fae;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_1f99;
branch_1fae:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::ZF)
        goto branch_1fee;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_1fd0;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_1fda;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_1fe6;
    goto branch_1feb;
branch_1fd0:;
    m.ax() = uint16_t(0x97);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_1feb;
branch_1fda:;
    m.ax() = uint16_t(0x33);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_1feb;
branch_1fe6:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_repeated_diagonally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
branch_1feb:;
    goto branch_1f5a;
branch_1fee:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_spinning_strike_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_spinning_strike_animation(Machine &m) {
    play_spinning_strike_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x372010 (near)
// Select the spinning strike effect origin for the current battle target.
void position_spinning_strike_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_2018;
    goto branch_20ea;
branch_2018:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2028;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_202a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_2035;
branch_2028:;
    goto branch_2065;
branch_202a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_20a8;
    goto branch_20c9;
branch_2035:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2043;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2065;
    goto branch_2087;
branch_2043:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_210b;
branch_2065:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_210b;
branch_2087:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_210b;
branch_20a8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_210b;
branch_20c9:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_210b;
branch_20ea:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_210e;
branch_210b:;
    position_effect_sprite_grid_call_core(m);
branch_210e:;
    return;
}
void position_spinning_strike_effect(Machine &m) {
    position_spinning_strike_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372110
// Play the automatic gun battle animation.
void play_automatic_gun_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x372110);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x78);
    m.si() = uint16_t(0x4640);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x4580);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_213f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_214b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_213f;
branch_214b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_automatic_gun_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x5540);
branch_216a:;
    m.si() = uint16_t(0x5540);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_2171:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_217d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_2171;
branch_217d:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_2183:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2183;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1E);
    if (m.flags() & emu::ZF)
        goto branch_21d4;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_21c8;
    goto branch_21d2;
branch_21c8:;
    m.ax() = uint16_t(0x34);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_21d2;
branch_21d2:;
    goto branch_216a;
branch_21d4:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_automatic_gun_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_automatic_gun_animation(Machine &m) {
    play_automatic_gun_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3721F6 (near)
// Select the automatic gun effect origin for the current battle target.
void position_automatic_gun_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_21fe;
    goto branch_22d0;
branch_21fe:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_220e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2210;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_221b;
branch_220e:;
    goto branch_224b;
branch_2210:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_228e;
    goto branch_22af;
branch_221b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2229;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_224b;
    goto branch_226d;
branch_2229:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_22f1;
branch_224b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_22f1;
branch_226d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_22f1;
branch_228e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_22f1;
branch_22af:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_22f1;
branch_22d0:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_22f4;
branch_22f1:;
    position_effect_sprite_grid_call_core(m);
branch_22f4:;
    return;
}
void position_automatic_gun_effect(Machine &m) {
    position_automatic_gun_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372300
// Play the wave cannon battle animation.
void play_wave_cannon_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x372300);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const uint16_t callback_cs = m.cs();
        animate_wave_cannon_charge_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x10);
branch_230a:;
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
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_230a;
    {
        const uint16_t callback_cs = m.cs();
        load_wave_cannon_background_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x10);
branch_232f:;
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
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_232f;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        animate_wave_cannon_flash_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x10);
branch_2360:;
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
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_2360;
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_set_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_2389:;
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
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_2389;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_wave_cannon_animation(Machine &m) {
    play_wave_cannon_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3723AD (near)
// Cycle the wave cannon flash palette for its timed animation.
void animate_wave_cannon_flash_core(Machine &m) {
branch_23ad:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_wave_cannon_flash_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3B73);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.bx()));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    {
        const uint16_t callback_cs = m.cs();
        position_wave_cannon_flash_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x8AB0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_23e6:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_23f2;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_23e6;
branch_23f2:;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x1);
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
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x40));
    m.out8(uint8_t(0x6), m.al());
branch_2412:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_2427;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_2412;
branch_2427:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF);
    if (m.flags() & emu::ZF)
        goto branch_244e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2441;
    goto branch_244b;
branch_2441:;
    m.ax() = uint16_t(0x96);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_244b;
branch_244b:;
    goto branch_23ad;
branch_244e:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    return;
}
void animate_wave_cannon_flash(Machine &m) {
    animate_wave_cannon_flash_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372466 (near)
// Load the wave cannon background tiles, palette, and tilemap.
void load_wave_cannon_background_core(Machine &m) {
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x200);
    m.cx() = uint16_t(0x8C);
    m.si() = uint16_t(0x8C90);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x8AB0);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x1);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.si() = uint16_t(0x9E10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x64);
    m.si() = uint16_t(0xA060);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xA040);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void load_wave_cannon_background(Machine &m) {
    load_wave_cannon_background_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3724C2 (near)
// Load and animate the wave cannon charge sprites.
void animate_wave_cannon_charge_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x75);
    m.si() = uint16_t(0x6DA0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x6D80);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_24f6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.set_ah(uint8_t(0x3));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3AB0);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.bx()));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        prepare_wave_cannon_charge_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    {
        const uint16_t callback_cs = m.cs();
        position_wave_cannon_charge_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x54));
    m.out8(uint8_t(0x6), m.al());
branch_252e:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2543;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_252e;
branch_2543:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x40);
    if (m.flags() & emu::ZF)
        goto branch_2569;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_255d;
    goto branch_2567;
branch_255d:;
    m.ax() = uint16_t(0x35);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_2567;
branch_2567:;
    goto branch_24f6;
branch_2569:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    prepare_wave_cannon_charge_sprites_core(m);
    return;
}
void animate_wave_cannon_charge(Machine &m) {
    animate_wave_cannon_charge_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372580 (near)
// Initialize the tile attributes for the wave cannon charge sprites.
void prepare_wave_cannon_charge_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x7C40);
branch_2592:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_259f;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xA8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_2592;
branch_259f:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_25aa:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_25b6;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_25aa;
branch_25b6:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x54));
branch_25bb:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_25d5;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_25bb;
branch_25d5:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_wave_cannon_charge_sprites(Machine &m) {
    prepare_wave_cannon_charge_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3725DB (near)
// Position the wave cannon charge sprites using the current effect offsets.
void position_wave_cannon_charge_sprites_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xE));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_wave_cannon_charge_sprites(Machine &m) {
    position_wave_cannon_charge_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372602 (near)
// Initialize the tile attributes for the wave cannon flash sprites.
void prepare_wave_cannon_flash_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xACE0);
branch_2614:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2621;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x80));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_2614;
branch_2621:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_262c:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2638;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_262c;
branch_2638:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x40));
branch_263d:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2657;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_263d;
branch_2657:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_wave_cannon_flash_sprites(Machine &m) {
    prepare_wave_cannon_flash_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37265D (near)
// Position the wave cannon flash sprites using the current effect offsets.
void position_wave_cannon_flash_sprites_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x8));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_wave_cannon_flash_sprites(Machine &m) {
    position_wave_cannon_flash_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372690
// Play the thrown rock battle animation.
void play_thrown_rock_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x372690);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x1F);
    m.si() = uint16_t(0xB500);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xB4E0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_thrown_rock_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xB8E0);
branch_26d9:;
    m.si() = uint16_t(0xB8E0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_26e0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_26ec;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_26e0;
branch_26ec:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_26f2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_26f2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_2718:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_272d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_2718;
branch_272d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_274d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2747;
    goto branch_274b;
branch_2747:;
    goto branch_274b;
branch_274b:;
    goto branch_26d9;
branch_274d:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_thrown_rock_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_thrown_rock_animation(Machine &m) {
    play_thrown_rock_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x37276F (near)
// Select the thrown rock effect origin for the current battle target.
void position_thrown_rock_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_2777;
    goto branch_2849;
branch_2777:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2787;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2789;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_2794;
branch_2787:;
    goto branch_27c4;
branch_2789:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2807;
    goto branch_2828;
branch_2794:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_27a2;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_27c4;
    goto branch_27e6;
branch_27a2:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_286a;
branch_27c4:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_286a;
branch_27e6:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_286a;
branch_2807:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_286a;
branch_2828:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_286a;
branch_2849:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_286d;
branch_286a:;
    position_effect_sprite_grid_call_core(m);
branch_286d:;
    return;
}
void position_thrown_rock_effect(Machine &m) {
    position_thrown_rock_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372870
// Play the rock impact battle animation.
void play_rock_impact_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x372870);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x95);
    m.si() = uint16_t(0xBCF0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xBCD0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_rock_impact_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xCF90);
branch_28b9:;
    m.si() = uint16_t(0xCF90);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_28c0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_28cc;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_28c0;
branch_28cc:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_28d2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_28d2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_28f8:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_290d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_28f8;
branch_290d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_2948;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_292f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2939;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_2945;
    goto branch_2945;
branch_292f:;
    m.ax() = uint16_t(0x4D);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_2945;
branch_2939:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_repeated_diagonally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_repeated_diagonally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_2945;
branch_2945:;
    goto branch_28b9;
branch_2948:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_rock_impact_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_rock_impact_animation(Machine &m) {
    play_rock_impact_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x37296A (near)
// Select the rock impact effect origin for the current battle target.
void position_rock_impact_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_2972;
    goto branch_2a44;
branch_2972:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2982;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2984;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_298f;
branch_2982:;
    goto branch_29bf;
branch_2984:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2a02;
    goto branch_2a23;
branch_298f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_299d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_29bf;
    goto branch_29e1;
branch_299d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2a65;
branch_29bf:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2a65;
branch_29e1:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2a65;
branch_2a02:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2a65;
branch_2a23:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2a65;
branch_2a44:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_2a68;
branch_2a65:;
    position_effect_sprite_grid_call_core(m);
branch_2a68:;
    return;
}
void position_rock_impact_effect(Machine &m) {
    position_rock_impact_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372A70
// Play the life drain battle animation.
void play_life_drain_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x372A70);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD9));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x64);
    m.si() = uint16_t(0xD630);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xD510);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_2a9f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2aab;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_2a9f;
branch_2aab:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_life_drain_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xE2B0);
branch_2aca:;
    m.si() = uint16_t(0xE2B0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_2ad1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2add;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_2ad1;
branch_2add:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_2ae3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2ae3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_2b09:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2b1e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_2b09;
branch_2b1e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x19);
    if (m.flags() & emu::ZF)
        goto branch_2b4f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2b40;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2b4a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_2b4c;
    goto branch_2b4c;
branch_2b40:;
    m.ax() = uint16_t(0x36);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_2b4c;
branch_2b4a:;
    goto branch_2b4c;
branch_2b4c:;
    goto branch_2aca;
branch_2b4f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_life_drain_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_life_drain_animation(Machine &m) {
    play_life_drain_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x372B71 (near)
// Select the life drain effect origin for the current battle target.
void position_life_drain_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_2b79;
    goto branch_2c53;
branch_2b79:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_2bce;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2b91;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2b93;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_2b9e;
branch_2b91:;
    goto branch_2bce;
branch_2b93:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2c11;
    goto branch_2c32;
branch_2b9e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2bac;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2bce;
    goto branch_2bf0;
branch_2bac:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2c74;
branch_2bce:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2c74;
branch_2bf0:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2c74;
branch_2c11:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2c74;
branch_2c32:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_2c74;
branch_2c53:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_2c77;
branch_2c74:;
    position_effect_sprite_grid_call_core(m);
branch_2c77:;
    return;
}
void position_life_drain_effect(Machine &m) {
    position_life_drain_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x372E80
// Play the psychoblast battle animation.
void play_psychoblast_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x372E80);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDA));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xA2);
    m.si() = uint16_t(0x3110);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x3050);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_2eaf:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2ebb;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_2eaf;
branch_2ebb:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_psychoblast_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x4550);
branch_2eda:;
    m.si() = uint16_t(0x4550);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_2ee1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2eed;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_2ee1;
branch_2eed:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_2ef3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2ef3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_2f19:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_2f2e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_2f19;
branch_2f2e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (m.flags() & emu::ZF)
        goto branch_2f5f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2f50;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2f5a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_2f5c;
    goto branch_2f5c;
branch_2f50:;
    m.ax() = uint16_t(0x48);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_2f5c;
branch_2f5a:;
    goto branch_2f5c;
branch_2f5c:;
    goto branch_2eda;
branch_2f5f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_psychoblast_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_psychoblast_animation(Machine &m) {
    play_psychoblast_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x372F81 (near)
// Select the psychoblast effect origin for the current battle target.
void position_psychoblast_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_2f89;
    goto branch_3063;
branch_2f89:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_2fde;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2fa1;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2fa3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_2fae;
branch_2fa1:;
    goto branch_2fde;
branch_2fa3:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3021;
    goto branch_3042;
branch_2fae:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2fbc;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2fde;
    goto branch_3000;
branch_2fbc:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3084;
branch_2fde:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3084;
branch_3000:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3084;
branch_3021:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3084;
branch_3042:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3084;
branch_3063:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_3087;
branch_3084:;
    position_effect_sprite_grid_call_core(m);
branch_3087:;
    return;
}
void position_psychoblast_effect(Machine &m) {
    position_psychoblast_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373090
// Play the flame battle animation.
void play_flame_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x373090);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xDC);
    m.si() = uint16_t(0x20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_30bf:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_30cb;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_30bf;
branch_30cb:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        position_flame_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x28);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
branch_30ee:;
    m.si() = uint16_t(0x1BA0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_30f5:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3101;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_30f5;
branch_3101:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_3107:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_3107;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_312d:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_3142;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_312d;
branch_3142:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x15);
    if (m.flags() & emu::ZF)
        goto branch_315a;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    goto branch_3158;
branch_3158:;
    goto branch_30ee;
branch_315a:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xAD);
    m.si() = uint16_t(0x2CF0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x2CD0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_3185:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3191;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_3185;
branch_3191:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    {
        const uint16_t callback_cs = m.cs();
        position_flame_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x4290);
branch_31af:;
    m.si() = uint16_t(0x4290);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_31b6:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_31c2;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_31b6;
branch_31c2:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_31c8:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_31c8;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_31ee:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_3203;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_31ee;
branch_3203:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1B);
    if (m.flags() & emu::ZF)
        goto branch_324e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_3249;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_324b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_3249;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_324b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_3249;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_324b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF);
    if (m.flags() & emu::ZF)
        goto branch_3249;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x13);
    if (m.flags() & emu::ZF)
        goto branch_324b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x15);
    if (m.flags() & emu::ZF)
        goto branch_3249;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x17);
    if (m.flags() & emu::ZF)
        goto branch_324b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x19);
    if (m.flags() & emu::ZF)
        goto branch_3249;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1A);
    if (m.flags() & emu::ZF)
        goto branch_324b;
    goto branch_324b;
branch_3249:;
    goto branch_324b;
branch_324b:;
    goto branch_31af;
branch_324e:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_flame_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_flame_animation(Machine &m) {
    play_flame_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x373266 (near)
// Select the flame effect origin for the current battle target.
void position_flame_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_326e;
    goto branch_3340;
branch_326e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_327e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_3280;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_328b;
branch_327e:;
    goto branch_32bb;
branch_3280:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_32fe;
    goto branch_331f;
branch_328b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3299;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_32bb;
    goto branch_32dd;
branch_3299:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3364;
branch_32bb:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3364;
branch_32dd:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3364;
branch_32fe:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3364;
branch_331f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3364;
branch_3340:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_3367;
branch_3364:;
    position_effect_sprite_grid_call_core(m);
branch_3367:;
    return;
}
void position_flame_effect(Machine &m) {
    position_flame_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373370
// Play the blizzard battle animation.
void play_blizzard_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x373370);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xFB);
    m.si() = uint16_t(0x5890);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x5870);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_339f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_33ab;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_339f;
branch_33ab:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_blizzard_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x77F0);
branch_33ca:;
    m.si() = uint16_t(0x77F0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_33d1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_33dd;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_33d1;
branch_33dd:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_33e3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_33e3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_3409:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_341e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_3409;
branch_341e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_3448;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_343c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_3446;
    goto branch_3446;
branch_343c:;
    m.ax() = uint16_t(0x3A);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_3446;
branch_3446:;
    goto branch_33ca;
branch_3448:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_blizzard_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_blizzard_animation(Machine &m) {
    play_blizzard_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x37346A (near)
// Select the blizzard effect origin for the current battle target.
void position_blizzard_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_3472;
    goto branch_3544;
branch_3472:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3482;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_3484;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_348f;
branch_3482:;
    goto branch_34bf;
branch_3484:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3502;
    goto branch_3523;
branch_348f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_349d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_34bf;
    goto branch_34e1;
branch_349d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3565;
branch_34bf:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3565;
branch_34e1:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3565;
branch_3502:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3565;
branch_3523:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3565;
branch_3544:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_3568;
branch_3565:;
    position_effect_sprite_grid_call_core(m);
branch_3568:;
    return;
}
void position_blizzard_effect(Machine &m) {
    position_blizzard_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373570
// Play the innate lightning battle animation.
void play_innate_lightning_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x373570);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xCA);
    m.si() = uint16_t(0x8880);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x8860);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_359f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_35ab;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_359f;
branch_35ab:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_innate_lightning_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xA1C0);
branch_35ca:;
    m.si() = uint16_t(0xA1C0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_35d1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_35dd;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_35d1;
branch_35dd:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_35e3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_35e3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_3609:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_361e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_3609;
branch_361e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (m.flags() & emu::ZF)
        goto branch_3659;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_364c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_364c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_364c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_3656;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_3656;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_3656;
    goto branch_3656;
branch_364c:;
    m.ax() = uint16_t(0x21);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_3656;
branch_3656:;
    goto branch_35ca;
branch_3659:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_innate_lightning_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_innate_lightning_animation(Machine &m) {
    play_innate_lightning_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x37367B (near)
// Select the innate lightning effect origin for the current battle target.
void position_innate_lightning_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_3683;
    goto branch_3755;
branch_3683:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3693;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_3695;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_36a0;
branch_3693:;
    goto branch_36d0;
branch_3695:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3713;
    goto branch_3734;
branch_36a0:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_36ae;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_36d0;
    goto branch_36f2;
branch_36ae:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3776;
branch_36d0:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3776;
branch_36f2:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3776;
branch_3713:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3776;
branch_3734:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3776;
branch_3755:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_3779;
branch_3776:;
    position_effect_sprite_grid_call_core(m);
branch_3779:;
    return;
}
void position_innate_lightning_effect(Machine &m) {
    position_innate_lightning_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373780
// Play the scripture opening battle animation.
void play_scripture_opening_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x373780);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDA));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xDF);
    m.si() = uint16_t(0x5130);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x5110);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_scripture_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x6D10);
branch_37c9:;
    m.si() = uint16_t(0x6D10);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_37d0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_37dc;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_37d0;
branch_37dc:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_37e2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_37e2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_3808:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_381d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_3808;
branch_381d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_3843;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3837;
    goto branch_3841;
branch_3837:;
    m.ax() = uint16_t(0x9F);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_3841;
branch_3841:;
    goto branch_37c9;
branch_3843:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_scripture_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_scripture_opening_animation(Machine &m) {
    play_scripture_opening_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x373865 (near)
// Select the scripture opening effect origin for the current battle target.
void position_scripture_opening_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_386d;
    goto branch_393f;
branch_386d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_387d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_387f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_388a;
branch_387d:;
    goto branch_38ba;
branch_387f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_38fd;
    goto branch_391e;
branch_388a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3898;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_38ba;
    goto branch_38dc;
branch_3898:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3960;
branch_38ba:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3960;
branch_38dc:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3960;
branch_38fd:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3960;
branch_391e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3960;
branch_393f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_3963;
branch_3960:;
    position_effect_sprite_grid_call_core(m);
branch_3963:;
    return;
}
void position_scripture_opening_effect(Machine &m) {
    position_scripture_opening_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373970
// Play the scripture flash battle animation.
void play_scripture_flash_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x373970);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDA));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x5A);
    m.si() = uint16_t(0x7370);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x7350);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_scripture_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x7EB0);
branch_39b9:;
    m.si() = uint16_t(0x7EB0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_39c0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_39cc;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_39c0;
branch_39cc:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_39d2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_39d2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_39f8:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_3a0d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_39f8;
branch_3a0d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_3a2b;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3a27;
    goto branch_3a29;
branch_3a27:;
    goto branch_3a29;
branch_3a29:;
    goto branch_39b9;
branch_3a2b:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_scripture_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_scripture_flash_animation(Machine &m) {
    play_scripture_flash_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x373A4D (near)
// Select the scripture flash effect origin for the current battle target.
void position_scripture_flash_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_3a55;
    goto branch_3b27;
branch_3a55:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3a65;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_3a67;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_3a72;
branch_3a65:;
    goto branch_3aa2;
branch_3a67:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3ae5;
    goto branch_3b06;
branch_3a72:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3a80;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3aa2;
    goto branch_3ac4;
branch_3a80:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3b48;
branch_3aa2:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3b48;
branch_3ac4:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3b48;
branch_3ae5:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3b48;
branch_3b06:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_3b48;
branch_3b27:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_3b4b;
branch_3b48:;
    position_effect_sprite_grid_call_core(m);
branch_3b4b:;
    return;
}
void position_scripture_flash_effect(Machine &m) {
    position_scripture_flash_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373B50
// Play the flare battle animation.
void play_flare_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x373B50);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const uint16_t callback_cs = m.cs();
        hide_flare_sprite_grids_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0xDD));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x65);
    m.si() = uint16_t(0x20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x52);
    m.si() = uint16_t(0xE20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xF0);
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(0x1958);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kDialogueCursorPosition), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kFlarePhaseTicks), uint8_t(0x0));
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3C48);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
branch_3bcc:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_3bcc;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3C30);
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
branch_3bf7:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD08), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD00), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD10), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD18), uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x8);
    if (!(m.flags() & emu::ZF))
        goto branch_3bf7;
branch_3c1d:;
    {
        const uint16_t callback_cs = m.cs();
        hide_flare_sprite_grids_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(0x0);
branch_3c23:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD10))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3c45;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3c7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (!(m.flags() & emu::ZF))
        goto branch_3c37;
    goto branch_3cc2;
branch_3c37:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_3c61;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_3c42;
    goto branch_3d4f;
branch_3c42:;
    goto branch_3de3;
branch_3c45:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3C20);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), m.ah());
    if (!(m.flags() & emu::ZF))
        goto branch_3c5e;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD10), uint8_t(0x1));
branch_3c5e:;
    goto branch_3de5;
branch_3c61:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3C28);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), m.ah());
    if (!(m.flags() & emu::ZF))
        goto branch_3c5e;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD10), uint8_t(0x4));
    goto branch_3de5;
branch_3c7d:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3C40);
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD08))));
    (void)computed::subtract<uint8_t>(m, m.al(), m.ah());
    if (m.flags() & emu::ZF)
        goto branch_3cb3;
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x8)));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD08), uint8_t(m.al()));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        prepare_flare_flash_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.bx());
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD08)))) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD00))));
    m.bx() = uint16_t(m.dx());
    {
        const uint16_t callback_cs = m.cs();
        position_flare_flash_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    goto branch_3cbf;
branch_3cb3:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD10), uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD18), uint8_t(0x0));
branch_3cbf:;
    goto branch_3de5;
branch_3cc2:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD18))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_3d40;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3cd6;
    goto branch_3d0f;
branch_3cd6:;
    computed::push(m, m.bx());
    m.ax() = uint16_t(0x4D);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kDialogueCursorPosition))));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD20), uint8_t(m.al()));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_3cf1;
    m.set_al(uint8_t(0x0));
branch_3cf1:;
    m.wr8(m.ss(), uint16_t(kDialogueCursorPosition), uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD00)))) << 8));
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.dx() >> 8), 0x10))) << 8));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD00), uint8_t(uint8_t(m.dx() >> 8)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD08))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x8)));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD08), uint8_t(uint8_t(m.dx())));
branch_3d0f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD20))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD18))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_flare_large_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD20))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD08)))) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD00))));
    m.bx() = uint16_t(m.dx());
    {
        const uint16_t callback_cs = m.cs();
        position_flare_large_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD18))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD18), uint8_t(m.al()));
    goto branch_3d4c;
branch_3d40:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD10), uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD18), uint8_t(0x0));
branch_3d4c:;
    goto branch_3de5;
branch_3d4f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD18))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_3dcd;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3d5e;
    goto branch_3d97;
branch_3d5e:;
    computed::push(m, m.bx());
    m.ax() = uint16_t(0x4A);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFlarePhaseTicks))));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD20), uint8_t(m.al()));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (!(m.flags() & emu::ZF))
        goto branch_3d79;
    m.set_al(uint8_t(0x0));
branch_3d79:;
    m.wr8(m.ss(), uint16_t(kFlarePhaseTicks), uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD00)))) << 8));
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.dx() >> 8), 0x10))) << 8));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD00), uint8_t(uint8_t(m.dx() >> 8)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD08))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x30)));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD08), uint8_t(uint8_t(m.dx())));
branch_3d97:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_once_diagonally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD20))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD18))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_flare_small_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD20))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD08)))) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD00))));
    m.bx() = uint16_t(m.dx());
    {
        const uint16_t callback_cs = m.cs();
        position_flare_small_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD18))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD18), uint8_t(m.al()));
    goto branch_3de3;
branch_3dcd:;
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD20))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x90)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    m.bx() = uint16_t(m.dx());
    {
        const uint16_t callback_cs = m.cs();
        position_flare_small_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDD10), uint8_t(0xFF));
branch_3de3:;
    goto branch_3de5;
branch_3de5:;
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x1));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_3df0;
    goto branch_3c23;
branch_3df0:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x6), m.al());
branch_3e02:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xDD10))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_3e12;
    goto branch_3e1c;
branch_3e12:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3e2c;
    goto branch_3e24;
branch_3e1c:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3e2c;
branch_3e24:;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_3e02;
branch_3e2c:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4A);
    if (m.flags() & emu::ZF)
        goto branch_3e4b;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_3e46;
    goto branch_3e48;
branch_3e46:;
    goto branch_3e48;
branch_3e48:;
    goto branch_3c1d;
branch_3e4b:;
    {
        const uint16_t callback_cs = m.cs();
        hide_flare_sprite_grids_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_battle_palette_display_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_3e6d:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_3e6d;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_flare_animation(Machine &m) {
    play_flare_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x373E91 (near)
// Initialize the tile attributes for the small flare sprite grid.
void prepare_flare_small_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.si() = uint16_t(0xCC0);
branch_3ea0:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3eac;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x28));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_3ea0;
branch_3eac:;
    m.set_ah(uint8_t(0x14));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_3eb5:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3ec1;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_3eb5;
branch_3ec1:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x14));
branch_3ec6:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3ee0;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_3ec6;
branch_3ee0:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_flare_small_sprites(Machine &m) {
    prepare_flare_small_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373EE6 (near)
// Initialize the tile attributes for the large flare sprite grid.
void prepare_flare_large_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.si() = uint16_t(0x1860);
branch_3ef5:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3f01;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x24));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_3ef5;
branch_3f01:;
    m.set_ah(uint8_t(0x12));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.cx()), 0x20)));
    m.di() = uint16_t(0xC000);
branch_3f0d:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3f19;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_3f0d;
branch_3f19:;
    m.dx() = uint16_t(0x880);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x12));
branch_3f1e:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3f38;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_3f1e;
branch_3f38:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_flare_large_sprites(Machine &m) {
    prepare_flare_large_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373F3E (near)
// Initialize the tile attributes for the flare flash sprite grid.
void prepare_flare_flash_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.si() = uint16_t(0x19D8);
branch_3f4d:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3f59;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_3f4d;
branch_3f59:;
    m.set_ah(uint8_t(0x4));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_3f62:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3f6e;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_3f62;
branch_3f6e:;
    m.dx() = uint16_t(0x8F0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
branch_3f73:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_3f8d;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_3f73;
branch_3f8d:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_flare_flash_sprites(Machine &m) {
    prepare_flare_flash_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373F93 (near)
// Position the small flare sprite grid using the current effect offsets.
void position_flare_small_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0x14));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx() >> 8)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_flare_small_sprite_grid(Machine &m) {
    position_flare_small_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373FBA (near)
// Position the large flare sprite grid using the current effect offsets.
void position_flare_large_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0x12));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x20)));
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx() >> 8)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_flare_large_sprite_grid(Machine &m) {
    position_flare_large_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x373FE3 (near)
// Position the flare flash sprite grid using the current effect offsets.
void position_flare_flash_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0x4));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx() >> 8)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_flare_flash_sprite_grid(Machine &m) {
    position_flare_flash_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37400C (near)
// Move all flare sprite grids off screen.
void hide_flare_sprite_grids_core(Machine &m) {
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x40));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void hide_flare_sprite_grids(Machine &m) {
    hide_flare_sprite_grids_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374040
// Play the earthquake battle animation.
void play_earthquake_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x374040);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
branch_4050:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
branch_4062:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_4077;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_4062;
branch_4077:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_40b8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_40a5;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::CF)
        goto branch_408f;
    goto branch_40ac;
branch_408f:;
    m.ax() = uint16_t(0x50);
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
        m.cs() = 0x6000;
        shake_battle_screen_repeated_diagonally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_40ac;
branch_40a5:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        animate_battle_raster_wave_fade_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_40ac;
branch_40ac:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    goto branch_4050;
branch_40b8:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_earthquake_animation(Machine &m) {
    play_earthquake_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x374140
// Play the whirlpool sprites battle animation.
void play_whirlpool_sprites_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x374140);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xD5);
    m.si() = uint16_t(0x3530);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x3510);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x28));
    m.bx() = uint16_t(0x1);
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x58));
branch_4190:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_whirlpool_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_whirlpool_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3D4F);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_whirlpool_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3D30);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_whirlpool_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (!(m.flags() & emu::ZF))
        goto branch_41e4;
    m.set_al(uint8_t(0x0));
branch_41e4:;
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x78));
    m.out8(uint8_t(0x6), m.al());
branch_41fa:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_420f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_41fa;
branch_420f:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_4241;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_422d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (!(m.flags() & emu::CF))
        goto branch_4237;
    goto branch_423e;
branch_422d:;
    m.ax() = uint16_t(0x51);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_423e;
branch_4237:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_repeated_horizontally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_423e;
branch_423e:;
    goto branch_4190;
branch_4241:;
    m.set_al(uint8_t(0x0));
    m.bx() = uint16_t(0x9000);
    {
        const uint16_t callback_cs = m.cs();
        position_whirlpool_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.bx() = uint16_t(0x9000);
    {
        const uint16_t callback_cs = m.cs();
        position_whirlpool_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_whirlpool_sprites_animation(Machine &m) {
    play_whirlpool_sprites_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x37426D (near)
// Initialize the tile attributes for the whirlpool sprites.
void prepare_whirlpool_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x4FD0);
branch_427f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_428b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x78));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_427f;
branch_428b:;
    m.set_ah(uint8_t(0x3C));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_4294:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_42a0;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_4294;
branch_42a0:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3C));
branch_42a5:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_42bf;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_42a5;
branch_42bf:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_whirlpool_sprites(Machine &m) {
    prepare_whirlpool_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3742C5 (near)
// Position the whirlpool sprite grid using the current effect offsets.
void position_whirlpool_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0x3C));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx() >> 8)));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_whirlpool_sprite_grid(Machine &m) {
    position_whirlpool_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374300
// Play the whirlpool background battle animation.
void play_whirlpool_background_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x374300);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x5F);
    m.si() = uint16_t(0x5520);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x5500);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_433b:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_434f;
    goto branch_433b;
branch_434f:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.si() = uint16_t(0x3D70);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x6100);
branch_436d:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_437a;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x268));
    goto branch_436d;
branch_437a:;
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
    m.ax() = uint16_t(0x1);
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.dx() = uint16_t(0x1800);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
branch_43a0:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_43b5;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_43a0;
branch_43b5:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1D);
    if (m.flags() & emu::ZF)
        goto branch_43e2;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_43d3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_43dd;
    goto branch_43df;
branch_43d3:;
    m.ax() = uint16_t(0x52);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_43df;
branch_43dd:;
    goto branch_43df;
branch_43df:;
    goto branch_434f;
branch_43e2:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        clear_battle_background_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_formation_layer_records_call_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        draw_enemy_group_portraits_call_core(m);
        if (m.cs() != 0x6000)
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
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_whirlpool_background_animation(Machine &m) {
    play_whirlpool_background_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x374410
// Play the tornado battle animation.
void play_tornado_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x374410);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xC8);
    m.si() = uint16_t(0xD1A0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xD180);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x28));
    m.bx() = uint16_t(0x1);
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x58));
branch_4460:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_tornado_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_tornado_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3E2F);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_tornado_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3E10);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_tornado_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (!(m.flags() & emu::ZF))
        goto branch_44b4;
    m.set_al(uint8_t(0x2));
branch_44b4:;
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x78));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x18);
    if (m.flags() & emu::ZF)
        goto branch_44fb;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_repeated_horizontally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_44ee;
    goto branch_44f8;
branch_44ee:;
    m.ax() = uint16_t(0x53);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_44f8;
branch_44f8:;
    goto branch_4460;
branch_44fb:;
    m.set_al(uint8_t(0x0));
    m.bx() = uint16_t(0x9000);
    {
        const uint16_t callback_cs = m.cs();
        position_tornado_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.bx() = uint16_t(0x9000);
    {
        const uint16_t callback_cs = m.cs();
        position_tornado_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_tornado_animation(Machine &m) {
    play_tornado_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x374527 (near)
// Initialize the tile attributes for the tornado sprites.
void prepare_tornado_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xEAA0);
branch_4539:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4545;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x78));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4539;
branch_4545:;
    m.set_ah(uint8_t(0x3C));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_454e:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_455a;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_454e;
branch_455a:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3C));
branch_455f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4579;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_455f;
branch_4579:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_tornado_sprites(Machine &m) {
    prepare_tornado_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37457F (near)
// Position the tornado sprite grid using the current effect offsets.
void position_tornado_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0x3C));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx() >> 8)));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_tornado_sprite_grid(Machine &m) {
    position_tornado_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3745B0
// Play the sandtrap battle animation.
void play_sandtrap_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3745B0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xC8);
    m.si() = uint16_t(0xD1A0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xD180);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x28));
    m.bx() = uint16_t(0x1);
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x58));
branch_4600:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_sandtrap_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3E50);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_sandtrap_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (!(m.flags() & emu::ZF))
        goto branch_4630;
    m.set_al(uint8_t(0x2));
branch_4630:;
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x3C));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x18);
    if (m.flags() & emu::ZF)
        goto branch_4678;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_repeated_horizontally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_466a;
    goto branch_4676;
branch_466a:;
    m.ax() = uint16_t(0x53);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_4676;
branch_4676:;
    goto branch_4600;
branch_4678:;
    m.set_al(uint8_t(0x0));
    m.bx() = uint16_t(0x9000);
    {
        const uint16_t callback_cs = m.cs();
        position_sandtrap_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.bx() = uint16_t(0x9000);
    {
        const uint16_t callback_cs = m.cs();
        position_sandtrap_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_sandtrap_animation(Machine &m) {
    play_sandtrap_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3746A4 (near)
// Initialize the tile attributes for the sandtrap sprites.
void prepare_sandtrap_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xEAA0);
branch_46b6:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_46c2;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x78));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_46b6;
branch_46c2:;
    m.set_ah(uint8_t(0x3C));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_46cb:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_46d7;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_46cb;
branch_46d7:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3C));
branch_46dc:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_46f6;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_46dc;
branch_46f6:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_sandtrap_sprites(Machine &m) {
    prepare_sandtrap_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3746FC (near)
// Position the sandtrap sprite grid using the current effect offsets.
void position_sandtrap_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0x3C));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx() >> 8)));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_sandtrap_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_sandtrap_sprite_grid(Machine &m) {
    position_sandtrap_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37472B (near)
// Select the sandtrap effect origin for the current battle target.
void position_sandtrap_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_473f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_4741;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_474b;
branch_473f:;
    goto branch_4769;
branch_4741:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4787;
    goto branch_4797;
branch_474b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4759;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_4769;
    goto branch_4777;
branch_4759:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_47c8;
branch_4769:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_47c8;
branch_4777:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_47c8;
branch_4787:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_47c8;
branch_4797:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_47c8;
branch_47c8:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_sandtrap_effect(Machine &m) {
    position_sandtrap_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3747D0
// Play the acid rain droplets battle animation.
void play_acid_rain_droplets_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3747D0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x18);
    m.si() = uint16_t(0x6860);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x6840);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_480b:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_481f;
    goto branch_480b;
branch_481f:;
    m.bx() = uint16_t(0x0);
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3E70);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_482f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_483b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x1));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_482f;
branch_483b:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        prepare_acid_rain_droplets_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.si());
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4858;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_485d;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_4862;
branch_4858:;
    m.si() = uint16_t(0x3E82);
    goto branch_4867;
branch_485d:;
    m.si() = uint16_t(0x3E8A);
    goto branch_4867;
branch_4862:;
    m.si() = uint16_t(0x3E92);
    goto branch_4867;
branch_4867:;
    {
        const uint16_t callback_cs = m.cs();
        position_acid_rain_droplets_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_483b;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x6), m.al());
branch_4883:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_4898;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_4883;
branch_4898:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x10);
    if (m.flags() & emu::ZF)
        goto branch_48bd;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_48b0;
branch_48b0:;
    m.ax() = uint16_t(0x55);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_48ba;
branch_48ba:;
    goto branch_481f;
branch_48bd:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
branch_48bf:;
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.set_ah(uint8_t(0x4));
    m.si() = uint16_t(0x3E8A);
    {
        const uint16_t callback_cs = m.cs();
        position_acid_rain_droplets_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.cx()), 0x1)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_48bf;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_acid_rain_droplets_animation(Machine &m) {
    play_acid_rain_droplets_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3748ED (near)
// Initialize the tile attributes for the acid rain droplets sprites.
void prepare_acid_rain_droplets_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x6B60);
branch_48ff:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_490b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x40));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_48ff;
branch_490b:;
    m.set_ah(uint8_t(0x20));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_4914:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4920;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_4914;
branch_4920:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x20));
branch_4925:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_493f;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4925;
branch_493f:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_acid_rain_droplets_sprites(Machine &m) {
    prepare_acid_rain_droplets_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374945 (near)
// Position the acid rain droplets sprite grid using the current effect offsets.
void position_acid_rain_droplets_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0x20));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_4952:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_495e;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4952;
branch_495e:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_acid_rain_droplets_sprite_grid(Machine &m) {
    position_acid_rain_droplets_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374980
// Play the acid rain splash battle animation.
void play_acid_rain_splash_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x374980);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF9);
    m.si() = uint16_t(0x6BC0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x6BA0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_49bb:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_49cf;
    goto branch_49bb;
branch_49cf:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3EA4);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_49ee;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_acid_rain_splash_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_49f9;
branch_49ee:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x1));
    goto branch_4a02;
branch_49f9:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x0));
branch_4a02:;
    m.si() = uint16_t(0x3EA0);
    {
        const uint16_t callback_cs = m.cs();
        position_acid_rain_splash_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x70));
    m.out8(uint8_t(0x6), m.al());
branch_4a1a:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_4a2f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_4a1a;
branch_4a2f:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x16);
    if (m.flags() & emu::ZF)
        goto branch_4a56;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_4a49;
    goto branch_4a53;
branch_4a49:;
    m.ax() = uint16_t(0x59);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_4a53;
branch_4a53:;
    goto branch_49cf;
branch_4a56:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x1));
    m.si() = uint16_t(0x3EA0);
    {
        const uint16_t callback_cs = m.cs();
        position_acid_rain_splash_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_acid_rain_splash_animation(Machine &m) {
    play_acid_rain_splash_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x374A7C (near)
// Initialize the tile attributes for the acid rain splash sprites.
void prepare_acid_rain_splash_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x8AE0);
branch_4a8e:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4a9b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xE0));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4a8e;
branch_4a9b:;
    m.set_ah(uint8_t(0x70));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_4aa4:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4ab0;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_4aa4;
branch_4ab0:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x70));
branch_4ab5:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4acf;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4ab5;
branch_4acf:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_acid_rain_splash_sprites(Machine &m) {
    prepare_acid_rain_splash_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374AD5 (near)
// Position the acid rain splash sprite grid using the current effect offsets.
void position_acid_rain_splash_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0x70));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_4ae2:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4aee;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4ae2;
branch_4aee:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_acid_rain_splash_sprite_grid(Machine &m) {
    position_acid_rain_splash_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374B10
// Play the self destruct battle animation.
void play_self_destruct_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x374B10);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xD7));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x9);
    m.si() = uint16_t(0xED20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xED00);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        prepare_self_destruct_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_4b4f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_4b5c;
    m.bx() = uint16_t(0x7804);
    goto branch_4b5f;
branch_4b5c:;
    m.bx() = uint16_t(0x9000);
branch_4b5f:;
    m.set_al(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        position_self_destruct_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x9));
    m.out8(uint8_t(0x6), m.al());
branch_4b76:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_4b8b;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_4b76;
branch_4b8b:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_4ba3;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    goto branch_4ba1;
branch_4ba1:;
    goto branch_4b4f;
branch_4ba3:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_self_destruct_animation(Machine &m) {
    play_self_destruct_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x374BA8 (near)
// Initialize the tile attributes for the self destruct sprites.
void prepare_self_destruct_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xEE40);
branch_4bba:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4bc6;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x12));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4bba;
branch_4bc6:;
    m.set_ah(uint8_t(0x9));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_4bcf:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4bdb;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_4bcf;
branch_4bdb:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x9));
branch_4be0:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4bfa;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4be0;
branch_4bfa:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_self_destruct_sprites(Machine &m) {
    prepare_self_destruct_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374C00 (near)
// Position the self destruct sprite grid using the current effect offsets.
void position_self_destruct_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0x9));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx() >> 8)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        reset_self_destruct_sprite_offsets_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_self_destruct_sprite_grid(Machine &m) {
    position_self_destruct_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374C2A (near)
// Reset the sprite offsets for the self-destruct effect.
void reset_self_destruct_sprite_offsets_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4c42;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_4c50;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_4c60;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_4c70;
branch_4c42:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_4c80;
branch_4c50:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x38)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_4c80;
branch_4c60:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x70)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_4c80;
branch_4c70:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0xA8)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_4c80;
branch_4c80:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void reset_self_destruct_sprite_offsets(Machine &m) {
    reset_self_destruct_sprite_offsets_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x374F00
// Play the riddle opening battle animation.
void play_riddle_opening_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x374F00);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x29);
    m.si() = uint16_t(0x1910);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x18F0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_4f3b:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_4f4f;
    goto branch_4f3b;
branch_4f4f:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x4038);
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) ^ uint8_t(m.cx())))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.cx()));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_riddle_opening_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.si() = uint16_t(0x4010);
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_opening_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x10));
    m.out8(uint8_t(0x6), m.al());
branch_4f86:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_4f9b;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_4f86;
branch_4f9b:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x12);
    if (m.flags() & emu::ZF)
        goto branch_4fc1;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_4fb5;
    goto branch_4fbf;
branch_4fb5:;
    m.ax() = uint16_t(0x3C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_4fbf;
branch_4fbf:;
    goto branch_4f4f;
branch_4fc1:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x13));
    m.si() = uint16_t(0x4010);
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_opening_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_riddle_opening_animation(Machine &m) {
    play_riddle_opening_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x374FE7 (near)
// Initialize the tile attributes for the riddle opening sprites.
void prepare_riddle_opening_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x1E30);
branch_4ff9:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5005;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_4ff9;
branch_5005:;
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_5010:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_501c;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_5010;
branch_501c:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x10));
branch_5021:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_503b;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5021;
branch_503b:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_riddle_opening_sprites(Machine &m) {
    prepare_riddle_opening_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375041 (near)
// Position the riddle opening sprite grid using the current effect offsets.
void position_riddle_opening_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_504e:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_505a;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_504e;
branch_505a:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_riddle_opening_sprite_grid(Machine &m) {
    position_riddle_opening_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37507D (near)
// Select the riddle opening effect origin for the current battle target.
void position_riddle_opening_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    goto branch_5093;
branch_5093:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_50f2;
branch_50f2:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_riddle_opening_effect(Machine &m) {
    position_riddle_opening_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375100
// Play the riddle flash battle animation.
void play_riddle_flash_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x375100);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x50);
    m.si() = uint16_t(0x9200);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x91E0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_513b:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_514f;
    goto branch_513b;
branch_514f:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_riddle_flash_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x0));
    m.si() = uint16_t(0x4060);
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_flash_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_517a:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_518f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_517a;
branch_518f:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_51b3;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_51a9;
    goto branch_51b1;
branch_51a9:;
    m.ax() = uint16_t(0x74);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_51b1:;
    goto branch_514f;
branch_51b3:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x4));
    m.si() = uint16_t(0x4060);
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_flash_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_riddle_flash_animation(Machine &m) {
    play_riddle_flash_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3751D9 (near)
// Initialize the tile attributes for the riddle flash sprites.
void prepare_riddle_flash_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x9C00);
branch_51eb:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_51f8;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_51eb;
branch_51f8:;
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_5201:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_520d;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_5201;
branch_520d:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x64));
branch_5212:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_522c;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5212;
branch_522c:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_riddle_flash_sprites(Machine &m) {
    prepare_riddle_flash_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375232 (near)
// Position the riddle flash sprite grid using the current effect offsets.
void position_riddle_flash_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_523d:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5249;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_523d;
branch_5249:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_riddle_flash_sprite_grid(Machine &m) {
    position_riddle_flash_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37526C (near)
// Select the riddle flash effect origin for the current battle target.
void position_riddle_flash_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    goto branch_5282;
branch_5282:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_52e1;
branch_52e1:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_riddle_flash_effect(Machine &m) {
    position_riddle_flash_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3752F0
// Play the riddle fade battle animation.
void play_riddle_fade_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3752F0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x18);
    m.si() = uint16_t(0xA010);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x9FF0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_532b:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_533f;
    goto branch_532b;
branch_533f:;
    m.bx() = uint16_t(0x0);
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x40B4);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_534f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_535b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x9));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_534f;
branch_535b:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_5369;
    computed::push(m, m.si());
    {
        const uint16_t callback_cs = m.cs();
        prepare_riddle_fade_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::pop(m));
branch_5369:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.set_ah(uint8_t(0x9));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_5376;
    m.set_ah(uint8_t(uint8_t(m.bx())));
branch_5376:;
    computed::push(m, m.si());
    m.si() = uint16_t(0x40A0);
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_fade_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x9);
    if (!(m.flags() & emu::ZF))
        goto branch_535b;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x48));
    m.out8(uint8_t(0x6), m.al());
branch_5396:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_53ab;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_5396;
branch_53ab:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_53d0;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_53c3;
branch_53c3:;
    m.ax() = uint16_t(0x3D);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_53cd;
branch_53cd:;
    goto branch_533f;
branch_53d0:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
branch_53d2:;
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.set_ah(uint8_t(0x9));
    {
        const uint16_t callback_cs = m.cs();
        position_riddle_fade_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.cx()), 0x1)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x9);
    if (!(m.flags() & emu::ZF))
        goto branch_53d2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_riddle_fade_animation(Machine &m) {
    play_riddle_fade_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3753FD (near)
// Initialize the tile attributes for the riddle fade sprites.
void prepare_riddle_fade_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xA310);
branch_540f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_541b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x12));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_540f;
branch_541b:;
    m.set_ah(uint8_t(0x9));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_5424:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5430;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_5424;
branch_5430:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x9));
branch_5435:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_544f;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5435;
branch_544f:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_riddle_fade_sprites(Machine &m) {
    prepare_riddle_fade_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375455 (near)
// Position the riddle fade sprite grid using the current effect offsets.
void position_riddle_fade_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0x9));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_5462:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_546e;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5462;
branch_546e:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_riddle_fade_sprite_grid(Machine &m) {
    position_riddle_fade_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375490
// Play the hypnotic gaze battle animation.
void play_hypnotic_gaze_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x375490);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x17);
    m.si() = uint16_t(0x1EB0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x1E90);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x4161);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
branch_54d5:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_54d5;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(0x0));
branch_5506:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x4146);
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) ^ uint8_t(m.cx())))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.cx()));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_hypnotic_gaze_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.si() = uint16_t(0x4110);
    {
        const uint16_t callback_cs = m.cs();
        position_hypnotic_gaze_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x10));
    m.out8(uint8_t(0x6), m.al());
branch_553d:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5552;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_553d;
branch_5552:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1A);
    if (m.flags() & emu::ZF)
        goto branch_5576;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_556c;
    goto branch_5574;
branch_556c:;
    m.ax() = uint16_t(0x59);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_5574:;
    goto branch_5506;
branch_5576:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x13));
    m.si() = uint16_t(0x4110);
    {
        const uint16_t callback_cs = m.cs();
        position_hypnotic_gaze_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_hypnotic_gaze_animation(Machine &m) {
    play_hypnotic_gaze_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x37559C (near)
// Initialize the tile attributes for the hypnotic gaze sprites.
void prepare_hypnotic_gaze_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x2190);
branch_55ae:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_55ba;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_55ae;
branch_55ba:;
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_55c5:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_55d1;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_55c5;
branch_55d1:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x10));
branch_55d6:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_55f0;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_55d6;
branch_55f0:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_hypnotic_gaze_sprites(Machine &m) {
    prepare_hypnotic_gaze_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3755F6 (near)
// Position the hypnotic gaze sprite grid using the current effect offsets.
void position_hypnotic_gaze_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_5603:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_560f;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5603;
branch_560f:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_hypnotic_gaze_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_hypnotic_gaze_sprite_grid(Machine &m) {
    position_hypnotic_gaze_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375632 (near)
// Select the hypnotic gaze effect origin for the current battle target.
void position_hypnotic_gaze_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5646;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_5648;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_5652;
branch_5646:;
    goto branch_5670;
branch_5648:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_568e;
    goto branch_569e;
branch_5652:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5660;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5670;
    goto branch_567e;
branch_5660:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_56cf;
branch_5670:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_56cf;
branch_567e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_56cf;
branch_568e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_56cf;
branch_569e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_56cf;
branch_56cf:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_hypnotic_gaze_effect(Machine &m) {
    position_hypnotic_gaze_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3756E0
// Play the hypnotic gaze portraits battle animation.
void play_hypnotic_gaze_portraits_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3756E0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x23);
    m.si() = uint16_t(0xA370);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xA350);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(0x1));
branch_571e:;
    m.set_al(uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    m.set_ah(uint8_t(uint8_t(m.bx())));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) ^ 0x1))));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(uint8_t(m.bx())));
    {
        const uint16_t callback_cs = m.cs();
        prepare_hypnotic_gaze_portraits_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x0));
    m.si() = uint16_t(0x42B0);
    {
        const uint16_t callback_cs = m.cs();
        position_hypnotic_gaze_portraits_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_5753:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_5768;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_5753;
branch_5768:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_57cd;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5792;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_579c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_579c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_57b3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_57b3;
    goto branch_57ca;
branch_5792:;
    m.ax() = uint16_t(0x59);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_57ca;
branch_579c:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    m.wr8(m.ss(), uint16_t(kFormationKind), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        apply_formation_portrait_tint1_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_57ca;
branch_57b3:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    m.wr8(m.ss(), uint16_t(kFormationKind), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        copy_formation_palette_next_call_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_57ca;
branch_57ca:;
    goto branch_571e;
branch_57cd:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        position_hypnotic_gaze_portraits_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_battle_palette_display_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_57f3:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_57f3;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_hypnotic_gaze_portraits_animation(Machine &m) {
    play_hypnotic_gaze_portraits_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x375817 (near)
// Initialize the tile attributes for the hypnotic gaze portraits sprites.
void prepare_hypnotic_gaze_portraits_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xA7D0);
branch_5829:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5836;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5829;
branch_5836:;
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_583f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_584b;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_583f;
branch_584b:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x64));
branch_5850:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_586a;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5850;
branch_586a:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_hypnotic_gaze_portraits_sprites(Machine &m) {
    prepare_hypnotic_gaze_portraits_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375870 (near)
// Position the hypnotic gaze portraits sprite grid using the current effect offsets.
void position_hypnotic_gaze_portraits_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_587d:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5889;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_587d;
branch_5889:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        reset_hypnotic_gaze_sprite_offsets_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_hypnotic_gaze_portraits_sprite_grid(Machine &m) {
    position_hypnotic_gaze_portraits_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3758AC (near)
// Reset the sprite offsets for the hypnotic gaze effect.
void reset_hypnotic_gaze_sprite_offsets_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_58c0;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_58c2;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_58cc;
branch_58c0:;
    goto branch_58ea;
branch_58c2:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5908;
    goto branch_5918;
branch_58cc:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_58da;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_58ea;
    goto branch_58f8;
branch_58da:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_5928;
branch_58ea:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_5928;
branch_58f8:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_5928;
branch_5908:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_5928;
branch_5918:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_5928;
branch_5928:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void reset_hypnotic_gaze_sprite_offsets(Machine &m) {
    reset_hypnotic_gaze_sprite_offsets_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375930
// Play the song battle animation.
void play_song_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x375930);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDA));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x100);
    m.si() = uint16_t(0x8B50);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x8B30);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_595f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_596b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_595f;
branch_596b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_song_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xAB50);
branch_598a:;
    m.si() = uint16_t(0xAB50);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_5991:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_599d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_5991;
branch_599d:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_59a3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_59a3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_59c9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_59de;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_59c9;
branch_59de:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_5a0f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5a00;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_5a0a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_5a0c;
    goto branch_5a0c;
branch_5a00:;
    m.ax() = uint16_t(0x3E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_5a0c;
branch_5a0a:;
    goto branch_5a0c;
branch_5a0c:;
    goto branch_598a;
branch_5a0f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_song_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_song_animation(Machine &m) {
    play_song_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x375A31 (near)
// Select the song effect origin for the current battle target.
void position_song_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_5a39;
    goto branch_5b0d;
branch_5a39:;
    goto branch_5a88;
branch_5a88:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5b2e;
branch_5b0d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_5b31;
branch_5b2e:;
    position_effect_sprite_grid_call_core(m);
branch_5b31:;
    return;
}
void position_song_effect(Machine &m) {
    position_song_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375B40
// Play the entangle battle animation.
void play_entangle_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x375B40);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDA));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xC9);
    m.si() = uint16_t(0xB5F0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xB4B0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_5b6f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5b7b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5b6f;
branch_5b7b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_entangle_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xCF10);
branch_5b9a:;
    m.si() = uint16_t(0xCF10);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_5ba1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5bad;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_5ba1;
branch_5bad:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_5bb3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_5bb3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_5bd9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_5bee;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_5bd9;
branch_5bee:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_5c1f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5c10;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_5c1a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_5c1c;
    goto branch_5c1c;
branch_5c10:;
    m.ax() = uint16_t(0x3F);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_5c1c;
branch_5c1a:;
    goto branch_5c1c;
branch_5c1c:;
    goto branch_5b9a;
branch_5c1f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_entangle_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_entangle_animation(Machine &m) {
    play_entangle_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x375C41 (near)
// Select the entangle effect origin for the current battle target.
void position_entangle_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_5c49;
    goto branch_5d1b;
branch_5c49:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5c59;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_5c5b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_5c66;
branch_5c59:;
    goto branch_5c96;
branch_5c5b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5cd9;
    goto branch_5cfa;
branch_5c66:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5c74;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5c96;
    goto branch_5cb8;
branch_5c74:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5d3c;
branch_5c96:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5d3c;
branch_5cb8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5d3c;
branch_5cd9:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5d3c;
branch_5cfa:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5d3c;
branch_5d1b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_5d3f;
branch_5d3c:;
    position_effect_sprite_grid_call_core(m);
branch_5d3f:;
    return;
}
void position_entangle_effect(Machine &m) {
    position_entangle_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x375D40
// Play the web battle animation.
void play_web_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x375D40);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xCC);
    m.si() = uint16_t(0x21F0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x21D0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_5d6f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5d7b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_5d6f;
branch_5d7b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_web_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x3B70);
branch_5d9a:;
    m.si() = uint16_t(0x3B70);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_5da1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5dad;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_5da1;
branch_5dad:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_5db3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_5db3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_5dd9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_5dee;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_5dd9;
branch_5dee:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF);
    if (m.flags() & emu::ZF)
        goto branch_5e14;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_5e08;
    goto branch_5e12;
branch_5e08:;
    m.ax() = uint16_t(0x40);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_5e12;
branch_5e12:;
    goto branch_5d9a;
branch_5e14:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_web_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_web_animation(Machine &m) {
    play_web_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x375E36 (near)
// Select the web effect origin for the current battle target.
void position_web_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_5e3e;
    goto branch_5f10;
branch_5e3e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5e4e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_5e50;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_5e5b;
branch_5e4e:;
    goto branch_5e8b;
branch_5e50:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5ece;
    goto branch_5eef;
branch_5e5b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_5e69;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_5e8b;
    goto branch_5ead;
branch_5e69:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5f31;
branch_5e8b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5f31;
branch_5ead:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5f31;
branch_5ece:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5f31;
branch_5eef:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_5f31;
branch_5f10:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_5f34;
branch_5f31:;
    position_effect_sprite_grid_call_core(m);
branch_5f34:;
    return;
}
void position_web_effect(Machine &m) {
    position_web_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376140
// Play the liquid jet battle animation.
void play_liquid_jet_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x376140);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xFE);
    m.si() = uint16_t(0x7850);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x7750);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_616f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_617b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_616f;
branch_617b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_liquid_jet_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x9810);
branch_619a:;
    m.si() = uint16_t(0x9810);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_61a1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_61ad;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_61a1;
branch_61ad:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_61b3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_61b3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_61d9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_61ee;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_61d9;
branch_61ee:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x11);
    if (m.flags() & emu::ZF)
        goto branch_6223;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6214;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_621e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_6220;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_6214;
    goto branch_6220;
branch_6214:;
    m.ax() = uint16_t(0x42);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6220;
branch_621e:;
    goto branch_6220;
branch_6220:;
    goto branch_619a;
branch_6223:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_liquid_jet_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_liquid_jet_animation(Machine &m) {
    play_liquid_jet_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x376245 (near)
// Select the liquid jet effect origin for the current battle target.
void position_liquid_jet_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_624d;
    goto branch_631f;
branch_624d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_625d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_625f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_626a;
branch_625d:;
    goto branch_629a;
branch_625f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_62dd;
    goto branch_62fe;
branch_626a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6278;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_629a;
    goto branch_62bc;
branch_6278:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6340;
branch_629a:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6340;
branch_62bc:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6340;
branch_62dd:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6340;
branch_62fe:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6340;
branch_631f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_6343;
branch_6340:;
    position_effect_sprite_grid_call_core(m);
branch_6343:;
    return;
}
void position_liquid_jet_effect(Machine &m) {
    position_liquid_jet_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376350
// Play the formation tremble battle animation.
void play_formation_tremble_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x376350);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6367;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_63aa;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (!(m.flags() & emu::ZF))
        goto branch_6367;
    goto branch_63ec;
branch_6367:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationBackTileSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectTileSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationBackPaletteSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectPaletteSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationBackMapSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectMapSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationBackTileCount)));
    m.wr16(m.ss(), uint16_t(kFormationEffectTileCount), uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationBackPaletteVariant))));
    m.wr8(m.ss(), uint16_t(kFormationEffectPaletteVariant), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationBackWidth))));
    m.wr8(m.ss(), uint16_t(kFormationEffectWidth), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationBackHeight))));
    m.wr8(m.ss(), uint16_t(kFormationEffectHeight), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationBackPaletteCount))));
    m.wr8(m.ss(), uint16_t(kFormationEffectPaletteCount), uint8_t(m.al()));
    goto branch_642e;
branch_63aa:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationMiddleTileSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectTileSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationMiddlePaletteSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectPaletteSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationMiddleMapSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectMapSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationMiddleTileCount)));
    m.wr16(m.ss(), uint16_t(kFormationEffectTileCount), uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationMiddlePaletteVariant))));
    m.wr8(m.ss(), uint16_t(kFormationEffectPaletteVariant), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationMiddleWidth))));
    m.wr8(m.ss(), uint16_t(kFormationEffectWidth), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationMiddleHeight))));
    m.wr8(m.ss(), uint16_t(kFormationEffectHeight), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationMiddlePaletteCount))));
    m.wr8(m.ss(), uint16_t(kFormationEffectPaletteCount), uint8_t(m.al()));
    goto branch_642e;
branch_63ec:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontTileSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectTileSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontPaletteSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectPaletteSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontMapSource)));
    m.wr16(m.ss(), uint16_t(kFormationEffectMapSource), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontTileCount)));
    m.wr16(m.ss(), uint16_t(kFormationEffectTileCount), uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationFrontPaletteVariant))));
    m.wr8(m.ss(), uint16_t(kFormationEffectPaletteVariant), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationFrontWidth))));
    m.wr8(m.ss(), uint16_t(kFormationEffectWidth), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationFrontHeight))));
    m.wr8(m.ss(), uint16_t(kFormationEffectHeight), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationFrontPaletteCount))));
    m.wr8(m.ss(), uint16_t(kFormationEffectPaletteCount), uint8_t(m.al()));
    goto branch_642e;
branch_642e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_643e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_6440;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_644a;
branch_643e:;
    goto branch_6462;
branch_6440:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6476;
    goto branch_6480;
branch_644a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6458;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6462;
    goto branch_646c;
branch_6458:;
    m.bx() = uint16_t(0x304);
    m.wr16(m.ss(), uint16_t(kFormationFrontPosition), uint16_t(m.bx()));
    goto branch_648a;
branch_6462:;
    m.bx() = uint16_t(0x30C);
    m.wr16(m.ss(), uint16_t(kFormationFrontPosition), uint16_t(m.bx()));
    goto branch_648a;
branch_646c:;
    m.bx() = uint16_t(0x314);
    m.wr16(m.ss(), uint16_t(kFormationFrontPosition), uint16_t(m.bx()));
    goto branch_648a;
branch_6476:;
    m.bx() = uint16_t(0x307);
    m.wr16(m.ss(), uint16_t(kFormationFrontPosition), uint16_t(m.bx()));
    goto branch_648a;
branch_6480:;
    m.bx() = uint16_t(0x311);
    m.wr16(m.ss(), uint16_t(kFormationFrontPosition), uint16_t(m.bx()));
    goto branch_648a;
branch_648a:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_front_layer_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        prepare_formation_tremble_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_64a2:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    m.si() = uint16_t(0x42C0);
branch_64af:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_64bb;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_64af;
branch_64bb:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    {
        const uint16_t callback_cs = m.cs();
        position_formation_tremble_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectWidth))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectHeight))));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.set_al(uint8_t(m.al()));
    m.out8(uint8_t(0x6), m.al());
branch_64e0:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_64f5;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_64e0;
branch_64f5:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1D);
    if (m.flags() & emu::ZF)
        goto branch_6521;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6517;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_6517;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x12);
    if (m.flags() & emu::ZF)
        goto branch_6517;
    goto branch_651f;
branch_6517:;
    m.ax() = uint16_t(0x5C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_651f:;
    goto branch_64a2;
branch_6521:;
    m.wr16(m.ss(), uint16_t(kFormationFrontPosition), uint16_t(0x9000));
    {
        const uint16_t callback_cs = m.cs();
        position_formation_tremble_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_formation_tremble_animation(Machine &m) {
    play_formation_tremble_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x376547 (near)
// Initialize the tile attributes for the formation tremble sprites.
void prepare_formation_tremble_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0xE000);
    m.es() = uint16_t(m.bx());
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationEffectMapSource)));
    m.si() = uint16_t(m.bx());
    m.di() = uint16_t(0xC000);
    m.dx() = uint16_t(0x800);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectWidth))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectHeight))));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
branch_656d:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6587;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_656d;
branch_6587:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_formation_tremble_sprites(Machine &m) {
    prepare_formation_tremble_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37658D (near)
// Position the formation tremble sprite grid using the current effect offsets.
void position_formation_tremble_sprite_grid_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(0x0));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontPosition)));
    computed::push(m, m.ax());
    m.set_al(uint8_t(uint8_t(m.bx() >> 8)));
    m.set_ah(uint8_t(0x8));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x4)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), m.ah())));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(uint8_t(m.bx())));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontPosition)));
    computed::push(m, m.ax());
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(0x8));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), m.al())));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(uint8_t(m.bx())));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectWidth))));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void position_formation_tremble_sprite_grid(Machine &m) {
    position_formation_tremble_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3765E0
// Play the telekinesis battle animation.
void play_telekinesis_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3765E0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xE2);
    m.si() = uint16_t(0xAA20);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xA960);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_661b:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_662f;
    goto branch_661b;
branch_662f:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_telekinesis_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x0));
    m.si() = uint16_t(0x4300);
    {
        const uint16_t callback_cs = m.cs();
        position_telekinesis_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_665a:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_666f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_665a;
branch_666f:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_66b2;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_669d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_669d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_669d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_66a5;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_66a5;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_66a5;
    goto branch_66af;
branch_669d:;
    m.ax() = uint16_t(0x5A);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
branch_66a5:;
    m.ax() = uint16_t(0x31);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_66af;
branch_66af:;
    goto branch_662f;
branch_66b2:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x4));
    m.si() = uint16_t(0x4300);
    {
        const uint16_t callback_cs = m.cs();
        position_telekinesis_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_telekinesis_animation(Machine &m) {
    play_telekinesis_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3766D8 (near)
// Initialize the tile attributes for the telekinesis sprites.
void prepare_telekinesis_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xC660);
branch_66ea:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_66f7;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_66ea;
branch_66f7:;
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_6700:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_670c;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_6700;
branch_670c:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x64));
branch_6711:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_672b;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_6711;
branch_672b:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_telekinesis_sprites(Machine &m) {
    prepare_telekinesis_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376731 (near)
// Position the telekinesis sprite grid using the current effect offsets.
void position_telekinesis_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    m.set_ah(uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_673c:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6748;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_673c;
branch_6748:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_telekinesis_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_telekinesis_sprite_grid(Machine &m) {
    position_telekinesis_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x37676B (near)
// Select the telekinesis effect origin for the current battle target.
void position_telekinesis_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_677f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_6781;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_678b;
branch_677f:;
    goto branch_67a9;
branch_6781:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_67c7;
    goto branch_67d7;
branch_678b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6799;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_67a9;
    goto branch_67b7;
branch_6799:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6808;
branch_67a9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6808;
branch_67b7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6808;
branch_67c7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6808;
branch_67d7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6808;
branch_6808:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_telekinesis_effect(Machine &m) {
    position_telekinesis_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376810
// Play the curse eye battle animation.
void play_curse_eye_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x376810);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x25);
    m.si() = uint16_t(0xD170);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xD150);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.bx() = uint16_t(0x0);
branch_684b:;
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCEC), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.bx() + 0xDCFC), uint8_t(0x0));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_685f;
    goto branch_684b;
branch_685f:;
    m.bx() = uint16_t(0x0);
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x435E);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_686f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_687b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_686f;
branch_687b:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_688d;
    m.set_ah(uint8_t(uint8_t(m.bx())));
    computed::push(m, m.bx());
    computed::push(m, m.si());
    {
        const uint16_t callback_cs = m.cs();
        prepare_curse_eye_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
branch_688d:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.set_ah(uint8_t(0x4));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_689a;
    m.set_ah(uint8_t(uint8_t(m.dx())));
branch_689a:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_68a9;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_68af;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_68b5;
branch_68a9:;
    computed::push(m, m.si());
    m.si() = uint16_t(0x434A);
    goto branch_68bb;
branch_68af:;
    computed::push(m, m.si());
    m.si() = uint16_t(0x4340);
    goto branch_68bb;
branch_68b5:;
    computed::push(m, m.si());
    m.si() = uint16_t(0x4354);
    goto branch_68bb;
branch_68bb:;
    computed::push(m, m.bx());
    {
        const uint16_t callback_cs = m.cs();
        position_curse_eye_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x3);
    if (!(m.flags() & emu::ZF))
        goto branch_687b;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x30));
    m.out8(uint8_t(0x6), m.al());
branch_68d9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_68ee;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_68d9;
branch_68ee:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_6913;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6906;
branch_6906:;
    m.ax() = uint16_t(0x42);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6910;
branch_6910:;
    goto branch_685f;
branch_6913:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
branch_6915:;
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.set_ah(uint8_t(0x9));
    {
        const uint16_t callback_cs = m.cs();
        position_curse_eye_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.cx()), 0x1)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x9);
    if (!(m.flags() & emu::ZF))
        goto branch_6915;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_curse_eye_animation(Machine &m) {
    play_curse_eye_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x376940 (near)
// Initialize the tile attributes for the curse eye sprites.
void prepare_curse_eye_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xD610);
branch_6952:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_695e;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_6952;
branch_695e:;
    m.set_ah(uint8_t(0x10));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_6967:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6973;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_6967;
branch_6973:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x10));
branch_6978:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6992;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_6978;
branch_6992:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_curse_eye_sprites(Machine &m) {
    prepare_curse_eye_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376998 (near)
// Position the curse eye sprite grid using the current effect offsets.
void position_curse_eye_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0x10));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_69a5:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_69b1;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_69a5;
branch_69b1:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    {
        const uint16_t callback_cs = m.cs();
        position_curse_eye_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_curse_eye_sprite_grid(Machine &m) {
    position_curse_eye_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3769D4 (near)
// Select the curse eye effect origin for the current battle target.
void position_curse_eye_effect_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_69e8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_69ea;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_69f4;
branch_69e8:;
    goto branch_6a12;
branch_69ea:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6a30;
    goto branch_6a40;
branch_69f4:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6a02;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6a12;
    goto branch_6a20;
branch_6a02:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6a71;
branch_6a12:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6a71;
branch_6a20:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6a71;
branch_6a30:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6a71;
branch_6a40:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectY))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    goto branch_6a71;
branch_6a71:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void position_curse_eye_effect(Machine &m) {
    position_curse_eye_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376A80
// Play the death battle animation.
void play_death_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x376A80);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x9D);
    m.si() = uint16_t(0xA7A0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xA620);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_6aaf:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6abb;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_6aaf;
branch_6abb:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_death_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xBB40);
branch_6ada:;
    m.si() = uint16_t(0xBB40);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_6ae1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6aed;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_6ae1;
branch_6aed:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_6af3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_6af3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_6b19:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_6b2e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_6b19;
branch_6b2e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::ZF)
        goto branch_6b56;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6b48;
    goto branch_6b54;
branch_6b48:;
    m.ax() = uint16_t(0x5B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6b54;
branch_6b54:;
    goto branch_6ada;
branch_6b56:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_death_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_death_animation(Machine &m) {
    play_death_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x376B78 (near)
// Select the death effect origin for the current battle target.
void position_death_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_6b80;
    goto branch_6c52;
branch_6b80:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6b90;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_6b92;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_6b9d;
branch_6b90:;
    goto branch_6bcd;
branch_6b92:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6c10;
    goto branch_6c31;
branch_6b9d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6bab;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6bcd;
    goto branch_6bef;
branch_6bab:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6c73;
branch_6bcd:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6c73;
branch_6bef:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6c73;
branch_6c10:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6c73;
branch_6c31:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_6c73;
branch_6c52:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_6c76;
branch_6c73:;
    position_effect_sprite_grid_call_core(m);
branch_6c76:;
    return;
}
void position_death_effect(Machine &m) {
    position_death_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376C80
// Play the healing glow battle animation.
void play_healing_glow_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x376C80);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTargetMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_6cc4;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x4380);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
branch_6ca5:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_6ca5;
branch_6cc4:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x5E);
    m.si() = uint16_t(0xAEE0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xAD80);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_6cef:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6cfb;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_6cef;
branch_6cfb:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_healing_glow_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xBAA0);
branch_6d1a:;
    m.si() = uint16_t(0xBAA0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_6d21:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6d2c;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x38));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_6d21;
branch_6d2c:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x1C);
branch_6d32:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_6d32;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x1C));
    m.out8(uint8_t(0x6), m.al());
branch_6d58:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_6d6d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_6d58;
branch_6d6d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_6d97;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_6d8b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_6d95;
    goto branch_6d95;
branch_6d8b:;
    m.ax() = uint16_t(0x44);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6d95;
branch_6d95:;
    goto branch_6d1a;
branch_6d97:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_healing_glow_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_battle_palette_display_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
branch_6dd4:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
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
        goto branch_6dd4;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_healing_glow_animation(Machine &m) {
    play_healing_glow_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x376DFD (near)
// Select the healing glow effect origin for the current battle target.
void position_healing_glow_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_6e05;
    goto branch_6e9e;
branch_6e05:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6e19;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6e3b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_6e5c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_6e7d;
branch_6e19:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_6ebf;
branch_6e3b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_6ebf;
branch_6e5c:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_6ebf;
branch_6e7d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_6ebf;
branch_6e9e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_6ec2;
branch_6ebf:;
    position_effect_sprite_grid_call_core(m);
branch_6ec2:;
    return;
}
void position_healing_glow_effect(Machine &m) {
    position_healing_glow_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x376ED0
// Play the defense increase battle animation.
void play_defense_increase_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x376ED0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xDD));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x17);
    m.si() = uint16_t(0x22E0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x17);
    m.si() = uint16_t(0x2860);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x2220);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_6f01:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6f0d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_6f01;
branch_6f0d:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectMotionCounter), uint8_t(0x0));
branch_6f33:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x448C);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTravelOffset))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (!(m.flags() & emu::ZF))
        goto branch_6f54;
    m.set_al(uint8_t(0x0));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectMotionCounter))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.cx()), 0x4)));
    m.wr8(m.ss(), uint16_t(kEffectMotionCounter), uint8_t(uint8_t(m.cx())));
branch_6f54:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.wr8(m.ss(), uint16_t(kEffectTravelOffset), uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
branch_6f67:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    {
        const uint16_t callback_cs = m.cs();
        prepare_defense_increase_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx() + m.si()))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x10));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_6f89;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(uint8_t(m.bx())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectMotionCounter))));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.dx()), uint8_t(m.cx()))));
branch_6f89:;
    m.set_ah(uint8_t(uint8_t(m.dx())));
    m.set_al(uint8_t(uint8_t(m.bx())));
    computed::push(m, m.si());
    m.si() = uint16_t(0x4460);
    {
        const uint16_t callback_cs = m.cs();
        position_defense_increase_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.bx()))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_6f67;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x50));
    m.out8(uint8_t(0x6), m.al());
branch_6fae:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6fc3;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_6fae;
branch_6fc3:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x38);
    if (m.flags() & emu::ZF)
        goto branch_6ffa;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_6fe9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (m.flags() & emu::ZF)
        goto branch_6fe9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1C);
    if (m.flags() & emu::ZF)
        goto branch_6fe9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2A);
    if (m.flags() & emu::ZF)
        goto branch_6fe9;
    goto branch_6ff7;
branch_6fe9:;
    m.ax() = uint16_t(0x29);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6ff7;
branch_6ff7:;
    goto branch_6f33;
branch_6ffa:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x4));
    m.si() = uint16_t(0x4482);
    {
        const uint16_t callback_cs = m.cs();
        position_defense_increase_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_defense_increase_animation(Machine &m) {
    play_defense_increase_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x377020 (near)
// Initialize the tile attributes for the defense increase sprites.
void prepare_defense_increase_sprites_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(0x2000);
    m.es() = uint16_t(m.bx());
    m.si() = uint16_t(0x25C0);
branch_702f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_703b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x28));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_702f;
branch_703b:;
    m.set_ah(uint8_t(0x14));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.di() = uint16_t(0xC000);
branch_7044:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_7050;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    goto branch_7044;
branch_7050:;
    m.dx() = uint16_t(0x800);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x14));
branch_7055:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_706f;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_7055;
branch_706f:;
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void prepare_defense_increase_sprites(Machine &m) {
    prepare_defense_increase_sprites_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3770CC (near)
// Position the defense increase sprite grid using the current effect offsets.
void position_defense_increase_sprite_grid_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0x14));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.wr8(m.ss(), uint16_t(kEffectSpriteOffset), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ah()));
branch_70d9:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_70e5;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_70d9;
branch_70e5:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x2));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    position_offset_effect_sprite_grid_call_core(m);
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void position_defense_increase_sprite_grid(Machine &m) {
    position_defense_increase_sprite_grid_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377140
// Play the protective aura battle animation.
void play_protective_aura_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x377140);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x31);
    m.si() = uint16_t(0xBEF0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xBDB0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_716f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_717b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_716f;
branch_717b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_protective_aura_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xC510);
branch_719a:;
    m.si() = uint16_t(0xC510);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_71a1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_71ac;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x12));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_71a1;
branch_71ac:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x9);
branch_71b2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_71b2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x9));
    m.out8(uint8_t(0x6), m.al());
branch_71d8:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_71ed;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_71d8;
branch_71ed:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (m.flags() & emu::ZF)
        goto branch_7217;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_720b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_7215;
    goto branch_7215;
branch_720b:;
    m.ax() = uint16_t(0x5D);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7215;
branch_7215:;
    goto branch_719a;
branch_7217:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_protective_aura_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_protective_aura_animation(Machine &m) {
    play_protective_aura_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x377239 (near)
// Select the protective aura effect origin for the current battle target.
void position_protective_aura_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_7241;
    goto branch_72da;
branch_7241:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_7255;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_7277;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_7298;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_72b9;
branch_7255:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x78));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    goto branch_72fb;
branch_7277:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x78));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x3C));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x3C));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    goto branch_72fb;
branch_7298:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x78));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x74));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x74));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    goto branch_72fb;
branch_72b9:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x78));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0xAC));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0xAC));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    goto branch_72fb;
branch_72da:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x3));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_72fe;
branch_72fb:;
    position_effect_sprite_grid_call_core(m);
branch_72fe:;
    return;
}
void position_protective_aura_effect(Machine &m) {
    position_protective_aura_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377440
// Play the revenge sword battle animation.
void play_revenge_sword_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x377440);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x85);
    m.si() = uint16_t(0xC7C0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xC7A0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_746f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_747b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_746f;
branch_747b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_revenge_sword_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xD860);
branch_749a:;
    m.si() = uint16_t(0xD860);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_74a1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_74ad;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_74a1;
branch_74ad:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_74b3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_74b3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_74d9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_74ee;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_74d9;
branch_74ee:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x14);
    if (m.flags() & emu::ZF)
        goto branch_7532;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_751c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::ZF)
        goto branch_751c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_7526;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF);
    if (m.flags() & emu::ZF)
        goto branch_7526;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_7528;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x10);
    if (m.flags() & emu::ZF)
        goto branch_7528;
    goto branch_752f;
branch_751c:;
    m.ax() = uint16_t(0x45);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_752f;
branch_7526:;
    goto branch_752f;
branch_7528:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        shake_battle_screen_repeated_diagonally_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_752f;
branch_752f:;
    goto branch_749a;
branch_7532:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_revenge_sword_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_revenge_sword_animation(Machine &m) {
    play_revenge_sword_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x377554 (near)
// Select the revenge sword effect origin for the current battle target.
void position_revenge_sword_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_755c;
    goto branch_762e;
branch_755c:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_756c;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_756e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_7579;
branch_756c:;
    goto branch_75a9;
branch_756e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_75ec;
    goto branch_760d;
branch_7579:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_7587;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_75a9;
    goto branch_75cb;
branch_7587:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_764f;
branch_75a9:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_764f;
branch_75cb:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_764f;
branch_75ec:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_764f;
branch_760d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_764f;
branch_762e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_7652;
branch_764f:;
    position_effect_sprite_grid_call_core(m);
branch_7652:;
    return;
}
void position_revenge_sword_effect(Machine &m) {
    position_revenge_sword_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377660
// Play the glass sword shatter battle animation.
void play_glass_sword_shatter_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x377660);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x76);
    m.si() = uint16_t(0xC590);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xC570);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_768f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_769b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_768f;
branch_769b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_glass_sword_shatter_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xD450);
branch_76ba:;
    m.si() = uint16_t(0xD450);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_76c1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_76cd;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_76c1;
branch_76cd:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_76d3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_76d3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_76f9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_770e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_76f9;
branch_770e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF);
    if (m.flags() & emu::ZF)
        goto branch_773f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_7730;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_773a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_773c;
    goto branch_773c;
branch_7730:;
    m.ax() = uint16_t(0x46);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_773c;
branch_773a:;
    goto branch_773c;
branch_773c:;
    goto branch_76ba;
branch_773f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_glass_sword_shatter_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_glass_sword_shatter_animation(Machine &m) {
    play_glass_sword_shatter_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x377761 (near)
// Select the glass sword shatter effect origin for the current battle target.
void position_glass_sword_shatter_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_7769;
    goto branch_783b;
branch_7769:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_7779;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_777b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_7786;
branch_7779:;
    goto branch_77b6;
branch_777b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_77f9;
    goto branch_781a;
branch_7786:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_7794;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_77b6;
    goto branch_77d8;
branch_7794:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_785c;
branch_77b6:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_785c;
branch_77d8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_785c;
branch_77f9:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_785c;
branch_781a:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_785c;
branch_783b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_785f;
branch_785c:;
    position_effect_sprite_grid_call_core(m);
branch_785f:;
    return;
}
void position_glass_sword_shatter_effect(Machine &m) {
    position_glass_sword_shatter_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377860
// Play the decisive slash battle animation.
void play_decisive_slash_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x377860);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x70);
    m.si() = uint16_t(0xE210);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xE0D0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_788f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_789b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_788f;
branch_789b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_decisive_slash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xF010);
branch_78ba:;
    m.si() = uint16_t(0xF010);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_78c1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_78cd;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_78c1;
branch_78cd:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_78d3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_78d3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_78f9:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_790e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_78f9;
branch_790e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_793f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_7930;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_793a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_793c;
    goto branch_793c;
branch_7930:;
    m.ax() = uint16_t(0x47);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_793c;
branch_793a:;
    goto branch_793c;
branch_793c:;
    goto branch_78ba;
branch_793f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_decisive_slash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_decisive_slash_animation(Machine &m) {
    play_decisive_slash_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x377961 (near)
// Select the decisive slash effect origin for the current battle target.
void position_decisive_slash_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_7969;
    goto branch_7a3b;
branch_7969:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_7979;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_797b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_7986;
branch_7979:;
    goto branch_79b6;
branch_797b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_79f9;
    goto branch_7a1a;
branch_7986:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_7994;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_79b6;
    goto branch_79d8;
branch_7994:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_7a5c;
branch_79b6:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_7a5c;
branch_79d8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_7a5c;
branch_79f9:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_7a5c;
branch_7a1a:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_7a5c;
branch_7a3b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_7a5f;
branch_7a5c:;
    position_effect_sprite_grid_call_core(m);
branch_7a5f:;
    return;
}
void position_decisive_slash_effect(Machine &m) {
    position_decisive_slash_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377A6E
// Play the nuclear bomb battle animation.
void play_nuclear_bomb_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x377A6E);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x0)) | (uint16_t(m.in8(uint8_t(0x0 + 1))) << 8)));
    computed::push(m, m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x8)));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(0x0);
    computed::out_word(m, uint8_t(0xC), m.ax());
    m.ax() = uint16_t(0x4FDF);
    computed::out_word(m, uint8_t(0xE), m.ax());
    m.cx() = uint16_t(0x10);
branch_7a8f:;
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
    m.cx() = uint16_t(0x3);
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_7a8f;
    m.set_al(uint8_t(0xDD));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x34);
    m.si() = uint16_t(0x4480);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(0x4400);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x4B00);
    m.bx() = uint16_t(0x1);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x34);
    m.cx() = uint16_t(0x10);
    m.si() = uint16_t(0x2D40);
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
    m.si() = uint16_t(0x2D20);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x44);
    m.cx() = uint16_t(0x5D);
    m.si() = uint16_t(0x2FC0);
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
    m.si() = uint16_t(0x2F40);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0xA1);
    m.cx() = uint16_t(0x2E);
    m.si() = uint16_t(0x3DA0);
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
    m.si() = uint16_t(0x3D40);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xC200);
    m.cx() = uint16_t(0x32);
branch_7b48:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        initialize_nuclear_particle_field_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xA));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_7b48;
    m.ax() = uint16_t(0x25);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x0);
branch_7b5e:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    (void)computed::subtract<uint16_t>(m, m.cx(), 0x3C);
    if (m.flags() & emu::CF)
        goto branch_7b7f;
    m.ax() = uint16_t(m.cx());
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x3C));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x3));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0xD);
    if (m.flags() & emu::CF)
        goto branch_7b7f;
    m.ax() = uint16_t(m.cx());
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x1)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0xD));
branch_7b7f:;
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x3B60));
    m.si() = uint16_t(m.ax());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x62));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x14)) << 8));
    m.cx() = uint16_t(0x404);
    m.dx() = uint16_t(0x2A44);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_sprite_tile_grid_offset_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::pop(m));
    computed::push(m, m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x5E)));
    m.si() = uint16_t(0x2F42);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.di() = uint16_t(0xC400);
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x1A2));
    m.cx() = uint16_t(0xF);
    m.flags() &= ~emu::DF;
    computed::copy_words(m);
    m.dx() = uint16_t(0x10);
    m.si() = uint16_t(0xC200);
    m.cx() = uint16_t(0x32);
branch_7bc6:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (!(m.flags() & emu::ZF))
        goto branch_7bd5;
    {
        const uint16_t callback_cs = m.cs();
        initialize_nuclear_particle_field_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
branch_7bd5:;
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax(), true));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0008), uint16_t(m.ax()));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x3F);
    if (!(m.flags() & emu::CF))
        goto branch_7c24;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(m.si() + 0x0004))));
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.ax()));
    m.set_ah(uint8_t(computed::add<uint8_t>(m, m.ah(), 0x68)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.ah()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0002)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(m.si() + 0x0006))));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0002), uint16_t(m.ax()));
    m.set_ah(uint8_t(computed::add<uint8_t>(m, m.ah(), 0x1C)));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.ah())) << 8));
    (void)computed::subtract<uint8_t>(m, m.ah(), 0x3A);
    if (!(m.flags() & emu::CF))
        goto branch_7c24;
    computed::push(m, m.dx());
    m.ax() = uint16_t(m.dx());
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x2));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() & 0xC)));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2834));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        set_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x4));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x7C);
    if (!(m.flags() & emu::CF))
        goto branch_7c2a;
branch_7c24:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xA));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_7bc6;
branch_7c2a:;
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(uint8_t(m.dx())));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx()));
    (void)computed::subtract<uint16_t>(m, m.cx(), 0xC8);
    if (!(m.flags() & emu::CF))
        goto branch_7c4c;
    goto branch_7b5e;
branch_7c4c:;
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kRasterWaveSource), uint16_t(m.ax()));
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.flags() &= ~emu::IF;
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x3), m.al());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t(0x90);
    m.wr16(m.ds(), uint16_t(m.bx()), uint16_t(0x7EFA));
    m.wr16(m.ds(), uint16_t(m.bx() + 0x0002), uint16_t(0x7000));
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x10))));
    m.out8(uint8_t(0xB2), m.al());
    m.flags() |= emu::IF;
    m.cx() = uint16_t(0x20);
branch_7c84:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        update_nuclear_flash_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_7c84;
    m.si() = uint16_t(0xC200);
    m.cx() = uint16_t(0x20);
branch_7c9c:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    {
        const uint16_t callback_cs = m.cs();
        initialize_nuclear_particle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xA));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_7c9c;
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x200);
    m.cx() = uint16_t(0x88);
    m.si() = uint16_t(0x4DB0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(0x4D30);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x5EB0);
    m.bx() = uint16_t(0x1);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x96);
branch_7ce2:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        update_nuclear_particles_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t callback_cs = m.cs();
        rotate_nuclear_blast_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t(0x3);
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_7ce2;
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    m.cx() = uint16_t(0x10);
branch_7d0a:;
    computed::push(m, m.cx());
    {
        const uint16_t callback_cs = m.cs();
        rotate_nuclear_blast_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        brighten_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_7d0a;
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_set_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kRasterWaveSource), uint16_t(m.ax()));
    m.wr8(m.ss(), uint16_t(kRasterWaveOffset), uint8_t(m.al()));
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kRasterWavePhase), uint8_t(m.al()));
    m.flags() &= ~emu::IF;
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x3), m.al());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t(0x90);
    m.wr16(m.ds(), uint16_t(m.bx()), uint16_t(0x7F50));
    m.wr16(m.ds(), uint16_t(m.bx() + 0x0002), uint16_t(0x7000));
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x10))));
    m.out8(uint8_t(0xB2), m.al());
    m.flags() |= emu::IF;
    m.cx() = uint16_t(0x100);
branch_7d88:;
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(kRasterWavePhase), uint8_t(uint8_t(m.cx())));
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kRasterWaveOffset),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kRasterWaveOffset)), m.al())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRasterWaveOffset))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3F))));
    if (!(m.flags() & emu::ZF))
        goto branch_7da8;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_7da8:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_7d88;
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xEF))));
    m.out8(uint8_t(0xB2), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x8);
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.ax() = uint16_t(0x400);
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(0x3C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
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
void play_nuclear_bomb_animation(Machine &m) {
    play_nuclear_bomb_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x377DDB (near)
// Randomize a nuclear blast particle's coordinates and lifetime.
void initialize_nuclear_particle_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.ax()));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x40));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.wr16(m.ss(), uint16_t(m.si() + 0x0002), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x3F)));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0008), uint16_t(m.ax()));
    return;
}
void initialize_nuclear_particle(Machine &m) {
    initialize_nuclear_particle_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377DFE (near)
// Advance nuclear blast particles, respawn expired particles, and position their sprites.
void update_nuclear_particles_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0xC200);
    m.cx() = uint16_t(0x20);
    m.dx() = uint16_t(0x0);
branch_7e10:;
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x78);
    if (!(m.flags() & emu::CF))
        goto branch_7e60;
    m.wr16(m.ss(), uint16_t(m.si() + 0x0008),
           uint16_t(computed::increment<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.si() + 0x0008)), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_7e1e;
    {
        const uint16_t callback_cs = m.cs();
        initialize_nuclear_particle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_7e1e:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x8);
    if (!(m.flags() & emu::CF))
        goto branch_7e5a;
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    computed::push(m, m.si());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0002)));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.set_ah(uint8_t(0x20));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4360));
    m.cx() = uint16_t(0x404);
    m.ax() = uint16_t(m.dx());
    m.dx() = uint16_t(0x2CA1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_sprite_tile_grid_offset_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x10));
branch_7e5a:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xA));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_7e10;
branch_7e60:;
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(uint8_t(m.dx())));
    m.out8(uint8_t(0x6), m.al());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void update_nuclear_particles(Machine &m) {
    update_nuclear_particles_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377E6D (near)
// Initialize the nuclear blast particles and wave offsets.
void initialize_nuclear_particle_field_core(Machine &m) {
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.bx() = uint16_t(m.cx());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_alternate_music_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1, true));
    m.cx() = uint16_t(0xFF);
    {
        const int32_t product = int32_t(int16_t(m.ax())) * int16_t(m.cx());
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(uint32_t(product) >> 16);
        computed::flag(m, emu::CF | emu::OF, product < -32768 || product > 32767);
    }
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, 0, m.ax()));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x6, true));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0004), uint16_t(m.ax()));
    m.set_al(uint8_t(uint8_t(m.bx())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.ax() = m.rd16(m.cs(), uint16_t(0xcd7 + m.al() * 2));
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1, true));
    m.cx() = uint16_t(0xFF);
    {
        const int32_t product = int32_t(int16_t(m.ax())) * int16_t(m.cx());
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(uint32_t(product) >> 16);
        computed::flag(m, emu::CF | emu::OF, product < -32768 || product > 32767);
    }
    m.wr16(m.ss(), uint16_t(m.si() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, 0, m.ax()));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x6, true));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0006), uint16_t(m.ax()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_byte_call_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(0x0));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x40));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0008), uint16_t(m.ax()));
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void initialize_nuclear_particle_field(Machine &m) {
    initialize_nuclear_particle_field_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377EC0 (near)
// Rotate the first nuclear blast palette's colors.
void rotate_nuclear_blast_palette_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(0xC41C);
    m.di() = uint16_t(0xC41E);
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.di())));
    m.cx() = uint16_t(0xE);
    m.flags() |= emu::DF;
    computed::copy_words(m);
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.dx()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void rotate_nuclear_blast_palette(Machine &m) {
    rotate_nuclear_blast_palette_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x377EE1 (near)
// Advance the nuclear flash palette and randomized wave effect.
void update_nuclear_flash_palette_core(Machine &m) {
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(0xC47C);
    m.di() = uint16_t(0xC47E);
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.di())));
    m.cx() = uint16_t(0x3E);
    m.flags() |= emu::DF;
    computed::copy_words(m);
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.dx()));
    return;
}
void update_nuclear_flash_palette(Machine &m) {
    update_nuclear_flash_palette_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x378130
// Play the spell casting battle animation.
void play_spell_casting_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x378130);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xD8));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x6C);
    m.si() = uint16_t(0xEA90);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xE990);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_815f:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_816b;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_815f;
branch_816b:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_spell_casting_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xF810);
branch_818a:;
    m.si() = uint16_t(0xF810);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_8191:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_819c;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x24));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_8191;
branch_819c:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x12);
branch_81a2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_81a2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x12));
    m.out8(uint8_t(0x6), m.al());
branch_81c8:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_81dd;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_81c8;
branch_81dd:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_8203;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_81f7;
    goto branch_8201;
branch_81f7:;
    m.ax() = uint16_t(0x4E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8201;
branch_8201:;
    goto branch_818a;
branch_8203:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_spell_casting_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_spell_casting_animation(Machine &m) {
    play_spell_casting_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x378225 (near)
// Select the spell casting effect origin for the current battle target.
void position_spell_casting_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_824b;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x58));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x58));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    goto branch_826c;
branch_824b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_826f;
branch_826c:;
    position_effect_sprite_grid_call_core(m);
branch_826f:;
    return;
}
void position_spell_casting_effect(Machine &m) {
    position_spell_casting_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x378270
// Play the full healing battle animation.
void play_full_healing_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x378270);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x4610);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
branch_828d:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_828d;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x88);
    m.si() = uint16_t(0x140);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_82d7:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_82e3;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_82d7;
branch_82e3:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_full_healing_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x1240);
branch_8302:;
    m.si() = uint16_t(0x1240);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_8309:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8314;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x38));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_8309;
branch_8314:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x1C);
branch_831a:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_831a;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x1C));
    m.out8(uint8_t(0x6), m.al());
branch_8340:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_8355;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_8340;
branch_8355:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_837f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_8373;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_837d;
    goto branch_837d;
branch_8373:;
    m.ax() = uint16_t(0x44);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_837d;
branch_837d:;
    goto branch_8302;
branch_837f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_full_healing_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_battle_palette_display_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
branch_83bc:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
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
        goto branch_83bc;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_full_healing_animation(Machine &m) {
    play_full_healing_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3783E5 (near)
// Select the full healing effect origin for the current battle target.
void position_full_healing_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_83ed;
    goto branch_8486;
branch_83ed:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8401;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8423;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_8444;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_8465;
branch_8401:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_84a7;
branch_8423:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_84a7;
branch_8444:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_84a7;
branch_8465:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_84a7;
branch_8486:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_84aa;
branch_84a7:;
    position_effect_sprite_grid_call_core(m);
branch_84aa:;
    return;
}
void position_full_healing_effect(Machine &m) {
    position_full_healing_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3784B0
// Play the cloud spell opening battle animation.
void play_cloud_spell_opening_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3784B0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF2);
    m.si() = uint16_t(0x1570);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x1550);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_cloud_spell_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x33B0);
branch_84f9:;
    m.si() = uint16_t(0x33B0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_8500:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_850c;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_8500;
branch_850c:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_8512:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8512;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_8538:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_854d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_8538;
branch_854d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_8573;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8567;
    goto branch_8571;
branch_8567:;
    m.ax() = uint16_t(0x5B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8571;
branch_8571:;
    goto branch_84f9;
branch_8573:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_cloud_spell_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_cloud_spell_opening_animation(Machine &m) {
    play_cloud_spell_opening_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x378595 (near)
// Select the cloud spell opening effect origin for the current battle target.
void position_cloud_spell_opening_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_859d;
    goto branch_866f;
branch_859d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_85ad;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_85af;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_85ba;
branch_85ad:;
    goto branch_85ea;
branch_85af:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_862d;
    goto branch_864e;
branch_85ba:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_85c8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_85ea;
    goto branch_860c;
branch_85c8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8690;
branch_85ea:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8690;
branch_860c:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8690;
branch_862d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8690;
branch_864e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8690;
branch_866f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_8693;
branch_8690:;
    position_effect_sprite_grid_call_core(m);
branch_8693:;
    return;
}
void position_cloud_spell_opening_effect(Machine &m) {
    position_cloud_spell_opening_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3786A0
// Play the cloud spell flash battle animation.
void play_cloud_spell_flash_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3786A0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x34);
    m.si() = uint16_t(0x3950);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x3930);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_cloud_spell_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x3FD0);
branch_86e9:;
    m.si() = uint16_t(0x3FD0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_86f0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_86fc;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_86f0;
branch_86fc:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_8702:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8702;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_8728:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_873d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_8728;
branch_873d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF);
    if (m.flags() & emu::ZF)
        goto branch_8763;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8757;
    goto branch_8761;
branch_8757:;
    m.ax() = uint16_t(0x5C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8761;
branch_8761:;
    goto branch_86e9;
branch_8763:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_cloud_spell_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_cloud_spell_flash_animation(Machine &m) {
    play_cloud_spell_flash_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x378785 (near)
// Select the cloud spell flash effect origin for the current battle target.
void position_cloud_spell_flash_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_878d;
    goto branch_885f;
branch_878d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_879d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_879f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_87aa;
branch_879d:;
    goto branch_87da;
branch_879f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_881d;
    goto branch_883e;
branch_87aa:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_87b8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_87da;
    goto branch_87fc;
branch_87b8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8880;
branch_87da:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8880;
branch_87fc:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8880;
branch_881d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8880;
branch_883e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8880;
branch_885f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_8883;
branch_8880:;
    position_effect_sprite_grid_call_core(m);
branch_8883:;
    return;
}
void position_cloud_spell_flash_effect(Machine &m) {
    position_cloud_spell_flash_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3788B0
// Play the thunder spell opening battle animation.
void play_thunder_spell_opening_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3788B0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x8E);
    m.si() = uint16_t(0x4C70);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x4C50);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_thunder_spell_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x5E30);
branch_88f9:;
    m.si() = uint16_t(0x5E30);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_8900:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_890c;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_8900;
branch_890c:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_8912:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8912;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_8938:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_894d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_8938;
branch_894d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_8973;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8967;
    goto branch_8971;
branch_8967:;
    m.ax() = uint16_t(0x95);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8971;
branch_8971:;
    goto branch_88f9;
branch_8973:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_thunder_spell_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_thunder_spell_opening_animation(Machine &m) {
    play_thunder_spell_opening_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x378995 (near)
// Select the thunder spell opening effect origin for the current battle target.
void position_thunder_spell_opening_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_899d;
    goto branch_8a6f;
branch_899d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_89ad;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_89af;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_89ba;
branch_89ad:;
    goto branch_89ea;
branch_89af:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8a2d;
    goto branch_8a4e;
branch_89ba:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_89c8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_89ea;
    goto branch_8a0c;
branch_89c8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8a90;
branch_89ea:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8a90;
branch_8a0c:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8a90;
branch_8a2d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8a90;
branch_8a4e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8a90;
branch_8a6f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_8a93;
branch_8a90:;
    position_effect_sprite_grid_call_core(m);
branch_8a93:;
    return;
}
void position_thunder_spell_opening_effect(Machine &m) {
    position_thunder_spell_opening_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x378AA0
// Play the thunder spell flash battle animation.
void play_thunder_spell_flash_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x378AA0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xDA);
    m.si() = uint16_t(0x2C00);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x2BE0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_thunder_spell_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x4740);
branch_8ae9:;
    m.si() = uint16_t(0x4740);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_8af0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8afc;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_8af0;
branch_8afc:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_8b02:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8b02;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_8b28:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_8b3d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_8b28;
branch_8b3d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x29);
    if (m.flags() & emu::ZF)
        goto branch_8b82;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8b73;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_8b7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::ZF)
        goto branch_8b7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x12);
    if (m.flags() & emu::ZF)
        goto branch_8b7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x18);
    if (m.flags() & emu::ZF)
        goto branch_8b7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1E);
    if (m.flags() & emu::ZF)
        goto branch_8b7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x24);
    if (m.flags() & emu::ZF)
        goto branch_8b7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x28);
    if (m.flags() & emu::ZF)
        goto branch_8b7d;
    goto branch_8b7f;
branch_8b73:;
    m.ax() = uint16_t(0x21);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8b7f;
branch_8b7d:;
    goto branch_8b7f;
branch_8b7f:;
    goto branch_8ae9;
branch_8b82:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_thunder_spell_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_thunder_spell_flash_animation(Machine &m) {
    play_thunder_spell_flash_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x378BA4 (near)
// Select the thunder spell flash effect origin for the current battle target.
void position_thunder_spell_flash_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_8bac;
    goto branch_8c7e;
branch_8bac:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8bbc;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_8bbe;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_8bc9;
branch_8bbc:;
    goto branch_8bf9;
branch_8bbe:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8c3c;
    goto branch_8c5d;
branch_8bc9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8bd7;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8bf9;
    goto branch_8c1b;
branch_8bd7:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8c9f;
branch_8bf9:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8c9f;
branch_8c1b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8c9f;
branch_8c3c:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8c9f;
branch_8c5d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8c9f;
branch_8c7e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_8ca2;
branch_8c9f:;
    position_effect_sprite_grid_call_core(m);
branch_8ca2:;
    return;
}
void position_thunder_spell_flash_effect(Machine &m) {
    position_thunder_spell_flash_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x378CB0
// Play the cold breath battle animation.
void play_cold_breath_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x378CB0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xE4);
    m.si() = uint16_t(0x6830);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x6810);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_cold_breath_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x84B0);
branch_8cf9:;
    m.si() = uint16_t(0x84B0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_8d00:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8d0c;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_8d00;
branch_8d0c:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_8d12:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8d12;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_8d38:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_8d4d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_8d38;
branch_8d4d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x17);
    if (m.flags() & emu::ZF)
        goto branch_8d73;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8d67;
    goto branch_8d71;
branch_8d67:;
    m.ax() = uint16_t(0x95);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8d71;
branch_8d71:;
    goto branch_8cf9;
branch_8d73:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_cold_breath_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_cold_breath_animation(Machine &m) {
    play_cold_breath_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x378D95 (near)
// Select the cold breath effect origin for the current battle target.
void position_cold_breath_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_8d9d;
    goto branch_8e6f;
branch_8d9d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8dad;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_8daf;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_8dba;
branch_8dad:;
    goto branch_8dea;
branch_8daf:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8e2d;
    goto branch_8e4e;
branch_8dba:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8dc8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8dea;
    goto branch_8e0c;
branch_8dc8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8e90;
branch_8dea:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8e90;
branch_8e0c:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8e90;
branch_8e2d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8e90;
branch_8e4e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_8e90;
branch_8e6f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_8e93;
branch_8e90:;
    position_effect_sprite_grid_call_core(m);
branch_8e93:;
    return;
}
void position_cold_breath_effect(Machine &m) {
    position_cold_breath_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x378EA0
// Play the fire spell opening battle animation.
void play_fire_spell_opening_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x378EA0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x52);
    m.si() = uint16_t(0x7750);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x7730);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_fire_spell_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0x8190);
branch_8ee9:;
    m.si() = uint16_t(0x8190);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_8ef0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8efc;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_8ef0;
branch_8efc:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_8f02:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8f02;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_8f28:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_8f3d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_8f28;
branch_8f3d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB);
    if (m.flags() & emu::ZF)
        goto branch_8f63;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8f57;
    goto branch_8f61;
branch_8f57:;
    m.ax() = uint16_t(0x5B);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8f61;
branch_8f61:;
    goto branch_8ee9;
branch_8f63:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_fire_spell_opening_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_fire_spell_opening_animation(Machine &m) {
    play_fire_spell_opening_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x378F85 (near)
// Select the fire spell opening effect origin for the current battle target.
void position_fire_spell_opening_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_8f8d;
    goto branch_905f;
branch_8f8d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8f9d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_8f9f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_8faa;
branch_8f9d:;
    goto branch_8fda;
branch_8f9f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_901d;
    goto branch_903e;
branch_8faa:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8fb8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8fda;
    goto branch_8ffc;
branch_8fb8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9080;
branch_8fda:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9080;
branch_8ffc:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9080;
branch_901d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9080;
branch_903e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9080;
branch_905f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_9083;
branch_9080:;
    position_effect_sprite_grid_call_core(m);
branch_9083:;
    return;
}
void position_fire_spell_opening_effect(Machine &m) {
    position_fire_spell_opening_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x379090
// Play the fire spell flash battle animation.
void play_fire_spell_flash_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x379090);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xFF);
    m.si() = uint16_t(0x8B10);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x8AF0);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_fire_spell_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xAAF0);
branch_90d9:;
    m.si() = uint16_t(0xAAF0);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_90e0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_90ec;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_90e0;
branch_90ec:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_90f2:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_90f2;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_9118:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_912d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_9118;
branch_912d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_9153;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9147;
    goto branch_9151;
branch_9147:;
    m.ax() = uint16_t(0x57);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9151;
branch_9151:;
    goto branch_90d9;
branch_9153:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_fire_spell_flash_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_fire_spell_flash_animation(Machine &m) {
    play_fire_spell_flash_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x379175 (near)
// Select the fire spell flash effect origin for the current battle target.
void position_fire_spell_flash_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_917d;
    goto branch_924f;
branch_917d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_918d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_918f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_919a;
branch_918d:;
    goto branch_91ca;
branch_918f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_920d;
    goto branch_922e;
branch_919a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_91a8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_91ca;
    goto branch_91ec;
branch_91a8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9270;
branch_91ca:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9270;
branch_91ec:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9270;
branch_920d:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9270;
branch_922e:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9270;
branch_924f:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_9273;
branch_9270:;
    position_effect_sprite_grid_call_core(m);
branch_9273:;
    return;
}
void position_fire_spell_flash_effect(Machine &m) {
    position_fire_spell_flash_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x379280
// Play the beam rifle battle animation.
void play_beam_rifle_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x379280);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x93);
    m.si() = uint16_t(0x9830);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0x9770);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_92af:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_92bb;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_92af;
branch_92bb:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_beam_rifle_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xAA90);
branch_92da:;
    m.si() = uint16_t(0xAA90);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_92e1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_92ed;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_92e1;
branch_92ed:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_92f3:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_92f3;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_9319:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_932e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_9319;
branch_932e:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1B);
    if (m.flags() & emu::ZF)
        goto branch_935f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9350;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_935a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_935c;
    goto branch_935c;
branch_9350:;
    m.ax() = uint16_t(0x31);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_935c;
branch_935a:;
    goto branch_935c;
branch_935c:;
    goto branch_92da;
branch_935f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_beam_rifle_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_beam_rifle_animation(Machine &m) {
    play_beam_rifle_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x379381 (near)
// Select the beam rifle effect origin for the current battle target.
void position_beam_rifle_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_9389;
    goto branch_945b;
branch_9389:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9399;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_939b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_93a6;
branch_9399:;
    goto branch_93d6;
branch_939b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9419;
    goto branch_943a;
branch_93a6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_93b4;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_93d6;
    goto branch_93f8;
branch_93b4:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_947c;
branch_93d6:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_947c;
branch_93f8:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_947c;
branch_9419:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_947c;
branch_943a:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_947c;
branch_945b:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_947f;
branch_947c:;
    position_effect_sprite_grid_call_core(m);
branch_947f:;
    return;
}
void position_beam_rifle_effect(Machine &m) {
    position_beam_rifle_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x379480
// Play the status cure battle animation.
void play_status_cure_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x379480);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x46F0);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
branch_949d:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_949d;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x97);
    m.si() = uint16_t(0xB270);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xB130);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_94e7:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_94f3;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_94e7;
branch_94f3:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_status_cure_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xC550);
branch_9512:;
    m.si() = uint16_t(0xC550);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_9519:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9524;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x38));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_9519;
branch_9524:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x1C);
branch_952a:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_952a;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x1C));
    m.out8(uint8_t(0x6), m.al());
branch_9550:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_9565;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_9550;
branch_9565:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_958f;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_9583;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_958d;
    goto branch_958d;
branch_9583:;
    m.ax() = uint16_t(0x44);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_958d;
branch_958d:;
    goto branch_9512;
branch_958f:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_status_cure_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_battle_palette_display_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
branch_95cc:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
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
        goto branch_95cc;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_status_cure_animation(Machine &m) {
    play_status_cure_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x3795F5 (near)
// Select the status cure effect origin for the current battle target.
void position_status_cure_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_95fd;
    goto branch_9696;
branch_95fd:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9611;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9633;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_9654;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_9675;
branch_9611:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x74));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_96b7;
branch_9633:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x74));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_96b7;
branch_9654:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x74));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_96b7;
branch_9675:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x74));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_96b7;
branch_9696:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_96ba;
branch_96b7:;
    position_effect_sprite_grid_call_core(m);
branch_96ba:;
    return;
}
void position_status_cure_effect(Machine &m) {
    position_status_cure_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x3796C0
// Play the shield battle animation.
void play_shield_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x3796C0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x47D0);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
    m.di() = uint16_t(0xC600);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
branch_96dd:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
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
        goto branch_96dd;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDE));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x65);
    m.si() = uint16_t(0xCCB0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xCB70);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectPaletteVariant))));
branch_9727:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9733;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_9727;
branch_9733:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_shield_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xD950);
branch_9752:;
    m.si() = uint16_t(0xD950);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_9759:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9764;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x38));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_9759;
branch_9764:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x1C);
branch_976a:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_976a;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x1C));
    m.out8(uint8_t(0x6), m.al());
branch_9790:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_97a5;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_9790;
branch_97a5:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1B);
    if (m.flags() & emu::ZF)
        goto branch_97cf;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_97c3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_97cd;
    goto branch_97cd;
branch_97c3:;
    m.ax() = uint16_t(0x9E);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_97cd;
branch_97cd:;
    goto branch_9752;
branch_97cf:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_shield_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_layer_target_palette_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_battle_palette_display_core(m);
        if (m.cs() != 0x6000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x8);
branch_980c:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x7);
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
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
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
        goto branch_980c;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_shield_animation(Machine &m) {
    play_shield_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x379835 (near)
// Select the shield effect origin for the current battle target.
void position_shield_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_983d;
    goto branch_98d6;
branch_983d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9851;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9873;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_9894;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_98b5;
branch_9851:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_98f7;
branch_9873:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x38));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_98f7;
branch_9894:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_98f7;
branch_98b5:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0xA8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    goto branch_98f7;
branch_98d6:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_98fa;
branch_98f7:;
    position_effect_sprite_grid_call_core(m);
branch_98fa:;
    return;
}
void position_shield_effect(Machine &m) {
    position_shield_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
// rom: 0x379AE0
// Play the scripture closing battle animation.
void play_scripture_closing_animation_core(Machine &m) {
    const AnimationScope animation(m, 0x379AE0);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(0x0));
    m.set_al(uint8_t(0xDA));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xD0);
    m.si() = uint16_t(0xD570);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.si() = uint16_t(0xD550);
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(0x1);
    m.di() = uint16_t(0xC400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        position_scripture_closing_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x2000);
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(0x800);
    m.si() = uint16_t(0xEF70);
branch_9b29:;
    m.si() = uint16_t(0xEF70);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
branch_9b30:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9b3c;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC8));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_9b30;
branch_9b3c:;
    m.di() = uint16_t(0xC000);
    m.cx() = uint16_t(0x64);
branch_9b42:;
    m.ax() = uint16_t(m.rd16(m.es(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | 0x2000)));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9b42;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
branch_9b68:;
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectTicks))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_9b7d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(m.al()));
    goto branch_9b68;
branch_9b7d:;
    m.wr8(m.ss(), uint16_t(kEffectTicks), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEffectFrame))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_9b9b;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kEffectFrame), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9b97;
    goto branch_9b99;
branch_9b97:;
    goto branch_9b99;
branch_9b99:;
    goto branch_9b29;
branch_9b9b:;
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t callback_cs = m.cs();
        position_scripture_closing_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
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
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x64));
    m.out8(uint8_t(0x6), m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_scripture_closing_animation(Machine &m) {
    play_scripture_closing_animation_core(m);
    if (m.cs() == 0x7000)
        m.retf();
}
// rom: 0x379BBD (near)
// Select the scripture closing effect origin for the current battle target.
void position_scripture_closing_effect_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_9bc5;
    goto branch_9c97;
branch_9bc5:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_enemy_group_count))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9bd5;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_9bd7;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_9be2;
branch_9bd5:;
    goto branch_9c12;
branch_9bd7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9c55;
    goto branch_9c76;
branch_9be2:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRam_target_group))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9bf0;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9c12;
    goto branch_9c34;
branch_9bf0:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9cb8;
branch_9c12:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x48));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9cb8;
branch_9c34:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x88));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9cb8;
branch_9c55:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x20));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9cb8;
branch_9c76:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x70));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kEffectHeight))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    goto branch_9cb8;
branch_9c97:;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x90));
    m.wr8(m.ss(), uint16_t(kEffectY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectRowY), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectHeight), uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    hide_effect_sprite_grid_call_core(m);
    goto branch_9cbb;
branch_9cb8:;
    position_effect_sprite_grid_call_core(m);
branch_9cbb:;
    return;
}
void position_scripture_closing_effect(Machine &m) {
    position_scripture_closing_effect_core(m);
    if (m.cs() == 0x7000)
        m.ret();
}
} // namespace sawgod::game
