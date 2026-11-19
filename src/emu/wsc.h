// WonderSwan Color system: memory map, cartridge mapper, I/O ports, display timing and rendering,
// interrupts, timers, DMA and keypad, around the V30MZ core.
//
// Written from the WSdev wiki (ref/docs/wsdev-wiki). The start-up state matches the reference engine without
// a boot ROM (the oracle our goldens come from): execution starts at FFFF:0000 on line 127, and a "frame"
// ends when line 145 begins, as the reference engine counts them.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "emu/apu.h"
#include "emu/frame_layers.h"
#include "emu/v30mz.h"

namespace sawgod::emu {

// Keypad bits (the conventional scan mask): Y1-Y4 in bits 8-11, X1-X4 in bits 4-7, B A Start in 3-1.
enum Key : uint16_t {
    KEY_START = 1 << 1,
    KEY_A = 1 << 2,
    KEY_B = 1 << 3,
    KEY_X1 = 1 << 4, // up
    KEY_X2 = 1 << 5, // right
    KEY_X3 = 1 << 6, // down
    KEY_X4 = 1 << 7, // left
    KEY_Y1 = 1 << 8,
    KEY_Y2 = 1 << 9,
    KEY_Y3 = 1 << 10,
    KEY_Y4 = 1 << 11,
};

class Wsc : public Bus {
  public:
    static constexpr int kWidth = 224;
    static constexpr int kHeight = 144;
    static constexpr int kCyclesPerLine = 256;

    Wsc(std::vector<uint8_t> rom, size_t sram_size);

    void reset();
    // Runs until the next frame boundary (the start of line 145).
    void run_frame();
    // A host decision may suspend a native in the middle of a frame. No CPU, raster, keypad or
    // audio time passes until its owner resumes it; run_frame then finishes that same frame.
    bool host_paused = false;
    // Host Battle Speed runs extra game frames without advancing the driver or sound chip.
    // Playback control only; the native/default clock and machine snapshots stay unchanged.
    bool audio_clock_enabled = true;

    void set_keys(uint16_t keys) { keys_ = keys; }

    // 224x144 pixels, 12-bit 0x0RGB as the LCD receives them
    const std::array<uint16_t, kWidth * kHeight> &framebuffer(bool second = false) const {
        return second && second_ ? second_->fb : fb_;
    }
    enum class ColorMode {
        Raw,
        Lcd
    }; // Raw: each 4-bit channel * 17; LCD: reference engine's LCD color matrix
    // The frame as 0x00RRGGBB.
    std::vector<uint32_t> framebuffer_rgb(ColorMode mode, bool second = false, bool scene = false,
                                          bool without_screen2 = false) const;
    // without_screen2 selects this picture beneath SCR2, retaining sprites/host cells.
    // Captured on the native raster; presentation only, excluded from machine snapshots.
    // Optional presentation pass: omit these palettes from tile layers and a prefix of sprite entries,
    // without changing machine memory or the ordinary framebuffer. The host can compose a new UI over this
    // scene.
    uint16_t scene_hidden_palettes = 0;
    uint8_t scene_hidden_sprite_prefix = 0;
    // Prefix sprites whose top is above this line stay visible (0 = hide the whole prefix): the game reuses
    // those slots for battle animations, which must never be cut.
    uint8_t scene_hidden_sprite_min_y = 0;
    uint16_t scene_hidden_sprite_palettes = 0; // bit per sprite palette (8-15)
    bool scene_keep_field_npcs = false;        // the hidden palettes spare field NPC sprites
    // Native windows that replace SCR1 cells (bit per tile palette). While nonzero, each picture keeps the
    // last SCR1 entry outside these palettes per tilemap cell, read from the game's own tilemap on every
    // ordinary frame; the clean scene pass draws that entry where it hides a window cell, so the backdrop a
    // window replaced shows through. Presentation only; reset_scene_underlay() forgets every cell.
    uint16_t scene_window_palettes = 0;
    // Lower tile art can share window palettes. A game-owned allocation boundary, sampled with
    // the other scene settings; applies to both hiding and remembering covered cells.
    uint16_t scene_window_tile_min = 0;
    // Presentation consumer samples all screen requests before row zero. No machine state changes.
    std::function<void(Wsc &)> on_scene_frame;
    void reset_scene_underlay();
    // Allocate only while a presentation consumer needs provenance. Both ordinary and clean
    // scene passes are captured independently, and the second picture's too unless `second` is false
    // (the app reads only the first picture's layers; recording the second's costs ~0.4 ms a frame).
    void capture_layers(bool on, bool second = true);
    // Deterministic function-only runs may freeze LCD pixels while keeping CPU, timers, IRQs,
    // keypad and audio timing unchanged. Presentation control; not part of a machine snapshot.
    bool render_lcd = true;
    // Host handoff provenance. Zero disables tracking. Excluded from native snapshots.
    uint64_t presentation_epoch = 0, presentation_ready = 0, presentation_shadow = 0;
    std::array<uint64_t, kHeight> presentation_raster{};
    uint64_t presentation_oam = 0;
    const FrameLayers *frame_layers(bool second = false, bool scene = false) const {
        return layers_[size_t((second && second_ ? 2 : 0) + (scene ? 1 : 0))].get();
    }
    // Borrow the completed picture's memory during host composition. The caller
    // must finish drawing before changing the second-picture allocation.
    std::span<const uint8_t> presentation_memory(bool second = false) const {
        return second && second_ ? std::span<const uint8_t>(second_->ram) : std::span<const uint8_t>(ram);
    }
    // Read-only latched OAM for host cameras, including active cards outside the ordinary raster.
    std::span<const uint8_t> presentation_sprites() const {
        return {sprites_.data(), size_t(sprite_count_) * 4};
    }
    // Copy only renderer inputs at a raster boundary. The destination is never executed.
    // No CPU, timers, keys, audio, or game state is changed in the source machine.
    void copy_presentation_to(Wsc &destination) const;
    // A caller-owned mask during host picture composition; Chrome records every UI write,
    // including fully transparent fills and pictures whose colors equal the scene.
    std::array<uint8_t, kWidth * kHeight> *ui_coverage = nullptr;
    // Presentation-only terrain masking, staged by the field sprite writers. One record for the
    // party and each NPC slot; first is its top-left OAM entry, word identifies its DMA tile buffer.
    // Latch with the OAM so a terrain transition cannot affect the preceding picture. Not saved in
    // machine snapshots (game/terrain_veil.h); it never changes RAM, OAM, RNG or frame timing.
    struct TerrainCard {
        int first = -1;
        uint16_t word = 0;
        bool veiled = false;
    };
    std::array<TerrainCard, 17> terrain_cards{};
    // Publish alongside the native staging-buffer -> OAM copy, then latch at the hardware boundary.
    void publish_terrain_cards() { terrain_oam_cards_ = terrain_cards; }
    const auto &presentation_terrain_cards() const { return terrain_cards_; }
    // Whether displayed OAM entry `index` belongs to a field NPC (cards 1-16; four entries each).
    bool field_npc_sprite(int index) const {
        for (size_t i = 1; i < terrain_cards_.size(); ++i)
            if (terrain_cards_[i].first >= 0 && index >= terrain_cards_[i].first &&
                index < terrain_cards_[i].first + 4)
                return true;
        return false;
    }
    // Bar edges are presentation provenance, carried with UiText through held HostPictures.
    struct UiBars {
        struct Rect {
            int x, y, width, height;
            float opacity = 1; // native palette fade, also applied to Enhanced's soft shadow
            bool operator==(const Rect &) const = default;
        };
        struct Pixel {
            uint32_t owner = 0, color = 0, beneath = 0;
            bool shadow = false;
            bool operator==(const Pixel &) const = default;
        };
        std::vector<Rect> rectangles;
        // Includes the blur halo. Chrome invalidates later writes even when their color is identical.
        std::vector<Pixel> pixels = std::vector<Pixel>(kWidth * kHeight);
        void clear() {
            rectangles.clear();
            std::fill(pixels.begin(), pixels.end(), Pixel{});
        }
        uint32_t visible(size_t at, const uint32_t *picture) const {
            return pixels[at].color == picture[at] ? pixels[at].owner : 0;
        }
    };
    // Caller-owned text provenance. Records count only while their last written color survives.
    // Shadow underlays and glyph colors are presentation data, excluded from machine snapshots.
    struct UiText {
        enum Kind : uint8_t { None, Stroke, Shadow, FrameCorner };
        struct Pixel {
            Kind kind = None;
            uint32_t color = 0, beneath = 0;
            bool casts_shadow = true;         // intentionally shadowless labels/captions stay shadowless
            uint32_t shadow_color = 0x111111; // output-resolution shadow, independent of the stroke
            uint32_t corner = 0; // FrameCorner: index plus one in corners; ordinary writes invalidate it
            bool corner_scene = false; // speaker cutout: beneath is the scene rather than the window gradient
            bool operator==(const Pixel &) const = default;
        };
        std::vector<Pixel> pixels = std::vector<Pixel>(kWidth * kHeight);
        UiBars bars;
        // Concave tab joins use the native frame's palette, with an output-resolution rounded stroke.
        struct Corner {
            int x, y;
            bool mirror;
            int top, bottom;
            uint32_t light, dark, stroke, bevel, shadow;
            bool operator==(const Corner &) const = default;
        };
        std::vector<Corner> corners;
        bool composed = false; // Chrome wrote this picture; native helper reads must preserve its record
        void clear() {
            std::fill(pixels.begin(), pixels.end(), Pixel{});
            bars.clear();
            corners.clear();
            composed = false;
        }
        // What `picture` still shows of the record at `at`. A hard shadow lies right of and/or below its
        // stroke and counts only while such a stroke is shown: hidden text (ink the color of its background)
        // keeps its shadow.
        Kind visible(size_t at, const uint32_t *picture) const {
            const auto &p = pixels[at];
            if (p.color != picture[at] || p.kind != Shadow)
                return p.color == picture[at] ? p.kind : None;
            const size_t x = at % kWidth;
            const auto shown = [&](size_t from) {
                return pixels[from].kind == Stroke && pixels[from].color == picture[from];
            };
            const bool left = x > 0 && shown(at - 1), up = at >= size_t(kWidth) && shown(at - kWidth),
                       diagonal = x > 0 && at >= size_t(kWidth) + 1 && shown(at - kWidth - 1);
            return left || up || diagonal ? Shadow : None;
        }
    };
    UiText *ui_text = nullptr;
    // Window indices plus one; native pixels remain unchanged.
    struct UiFill {
        struct Window {
            int top, bottom;
            uint32_t light, dark;
            int alpha;
            bool operator==(const Window &) const = default;
        };
        std::vector<Window> windows;
        std::vector<uint32_t> owner = std::vector<uint32_t>(kWidth * kHeight);
        std::vector<uint32_t> color = std::vector<uint32_t>(kWidth * kHeight);
        std::vector<uint32_t> beneath = std::vector<uint32_t>(kWidth * kHeight); // before translucent fill
        void clear() {
            windows.clear();
            std::fill(owner.begin(), owner.end(), 0);
        }
        uint32_t visible(size_t at, uint32_t pixel) const {
            return owner[at] && color[at] == pixel ? owner[at] : 0;
        }
    };
    UiFill *ui_fill = nullptr;
    // Palette for a host-composed tile, with the second view's current swap/fade/grey treatment.
    uint16_t presentation_palette_color(int palette, int index, bool second, bool sprite);
    // The nearest GB grey of a 12-bit color: the second view's palettes 8-15 with upper_palette_greys.
    static uint16_t gb_grey(uint16_t c) {
        const int lum = (30 * (c >> 8 & 15) + 59 * (c >> 4 & 15) + 11 * (c & 15)) / 100;
        return lum >= 13 ? 0xFFF : lum >= 8 ? 0xAAA : lum >= 3 ? 0x555 : 0x000;
    }

    // Bus
    uint8_t read8(uint32_t addr) override;
    void write8(uint32_t addr, uint8_t value) override;
    uint8_t in8(uint16_t port) override;
    void out8(uint16_t port, uint8_t value) override;

    // Called before every instruction when set (tracing/debugging).
    std::function<void(const Wsc &)> on_instruction;
    // Called on every bus access when set: kind 'F' (instruction fetch), 'R', 'W', 'I' (port in), 'O' (port
    // out).
    std::function<void(char kind, uint32_t addr)> on_bus;
    // Called when the CPU has taken a hardware interrupt (registers already at the handler), like the
    // reference engine's irq event.
    std::function<void(const Wsc &)> on_irq;

    V30MZ cpu{*this};
    std::array<uint8_t, 0x10000> ram{};
    std::vector<uint8_t> rom;
    std::vector<uint8_t> sram;
    std::array<uint8_t, 0x100> io{};
    Apu apu{io, ram};

    // Host pixels for screen-map cells (the port's variable-width text, game/text_render.h): while the map
    // entry at `addr` (an internal-RAM address, as the screen layers read it) still equals `entry`, the
    // cell shows `px` (8x8 palette indices, row-major; 0 = the tile's transparent index) instead of its
    // tile, with the entry's palette, and `over` on top of it where nonzero (what hangs into the cell from
    // the text row above: descenders and their shadow). Any write to the cell's entry (even of the same
    // value) ends the override. Not part of the machine's state (the original never sets any): empty unless
    // the port draws such text.
    struct CellPixels {
        uint16_t entry = 0;
        std::array<uint8_t, 64> px{}, over{}, beneath{}, ink{};
    };
    std::unordered_map<uint16_t, CellPixels> cell_pixels;

    // Host cells (presentation overlays): screen-map words drawn over everything at screen
    // pixel (x, y), with the game's tiles and palettes as the pass sees them (so the GB picture gets the GB
    // font). Not part of the machine's state.
    struct HostCell {
        int16_t x = 0, y = 0;
        uint16_t word = 0;
    };
    std::vector<HostCell> host_cells;

    // Glyph overrides (presentation: the m5x7 digits, game/text_render.h): while a 4bpp tile's 32 bytes, as
    // the pass sees them, equal `match`, it is drawn as `show` instead. Per tile, several (the WSC picture's
    // glyph and the GB picture's). Not part of the machine's state.
    struct GlyphOverride {
        std::array<uint8_t, 32> match{}, show{};
    };
    void set_glyph_overrides(std::unordered_map<int, std::vector<GlyphOverride>> o) {
        glyph_overrides_ = std::move(o);
        glyph_index_.fill(-1);
        glyph_list_.clear();
        for (auto &[tile, list] : glyph_overrides_)
            if (tile >= 0 && tile < 1024) {
                glyph_index_[size_t(tile)] = int16_t(glyph_list_.size());
                glyph_list_.push_back(&list);
            }
    }

    // The second picture (presentation: the GFX swap's GB view; game/gfx_swap.h). While on, every line is
    // drawn twice: from the machine's RAM into framebuffer(), and from a shadow copy of it into
    // framebuffer(true) with the display options below. The shadow follows every RAM write except at the
    // bytes set with shadow_write8 (released with shadow_release); the game never sees the shadow.
    void set_second(bool on);
    bool second() const { return second_ != nullptr; }
    // Presentation control (not machine state): false while nothing shows the second picture (Graphics
    // fully WSC or Enhanced). Its pixels are then not drawn, saving about a third of the raster cost; its
    // palette-swap tracking still runs every line. refresh_second() draws a complete picture from the
    // current state when one is needed again: anything keeping a copy of the second picture calls it first.
    bool second_visible = true;
    void refresh_second();
    // Forget which palette swap (and fade of it) each palette shows on the second picture. A binding follows
    // a palette through fades, including through black; a loaded save must not continue the title's fade
    // (the GB title stayed bound to the field's fading-in palettes).
    void forget_second_swaps();
    // Retain the completed second picture (both ordinary and clean-scene passes) while the
    // native machine draws a presentation that has no counterpart in that picture's style.
    // Display-only, including its raster provenance; never part of a machine snapshot.
    void hold_second_picture(bool on);
    // Optional second-picture motion, one offset per native frame, below a screen split.
    // Host windows apply the same motion after composing over the clean-scene pass.
    std::vector<int> second_motion_y;
    uint64_t second_motion_start = 0;
    int second_motion_split = 80;
    void move_second_picture(std::vector<uint32_t> &pixels) const;
    // A style-owned scene at the top of the second picture (whole rows, RGB444).
    // Independent of native palette identity and screen/OAM/tile reuse.
    std::vector<uint16_t> second_scene_image;
    // Full-page presentations also cover host glyph cells; battle backdrops retain HP labels.
    bool second_scene_above_host = false;
    // Optional native tile-plane scroll/window mapping for a style-owned scene. The image's
    // screen-space row zero corresponds to these tile-plane Y origins; -1 omits that plane.
    // No mapping keeps the fixed image; two omitted planes blank it. Presentation only.
    struct SceneRaster {
        std::array<int, 2> y{-1, -1};
        int split = -1; // mirrored source: SCR1 owns x < split, SCR2 owns x >= split
    };
    std::optional<SceneRaster> second_scene_raster;
    void shadow_write8(uint16_t addr, uint8_t value);
    void shadow_write16(uint16_t addr, uint16_t value) {
        shadow_write8(addr, uint8_t(value));
        shadow_write8(uint16_t(addr + 1), uint8_t(value >> 8));
    }
    uint8_t shadow_read8(uint16_t addr) const { return second_ ? second_->ram[addr] : ram[addr]; }
    void shadow_release(uint16_t addr, size_t bytes);
    bool shadowed(uint16_t addr) const { return second_ && second_->mask[addr]; }

    // Display options of the second picture:
    // - palette swaps (the GB windows, monsters, backdrop): a palette whose 16 colors equal `from` (or whose
    //   fade target, at RAM `palette_swap_target` + 32 * palette, does) shows as `to`, and keeps showing it,
    //   faded the same way, while it fades (every channel of every color shifted by one step, or capped or
    //   floored at one level: the game's fade routines);
    // - a picture for screen 1 (the GB title): while a palette is bound to palette_swaps[screen1_image_swap],
    //   screen 1 shows `screen1_image` (224 x 144 colors) faded like it, and screen-2 cells in
    //   `screen2_hidden_palettes` (bits) aren't drawn;
    // - palettes 8-15 (sprites; icons and windows) in the GB's four greys.
    struct PaletteSwap {
        std::array<uint16_t, 16> from{}, to{};
        bool sprites = true; // false: background palettes only (sprites with these colors keep the grey snap)
    };
    std::vector<PaletteSwap> palette_swaps;
    uint16_t palette_swap_target = 0; // 0: none
    std::vector<uint16_t> screen1_image;
    int screen1_image_swap = -1;
    uint16_t screen2_hidden_palettes = 0;
    bool upper_palette_greys = false;
    // - walkers on see-through terrain (veiled terrain cards) as on the GB: their lower half drawn on every
    //   frame, only over screen-1 pixels that are the GB's background color 0 (the GB's per-sprite
    //   BG-over-OBJ priority), where the WSC code lists it on every other frame. Color 0 is index 1 of
    //   `second_bg_zero_palette` (the GFX swap's field greys); any other opaque screen-1 pixel covers.
    int second_bg_zero_palette = -1;

    uint64_t frame_count = 0;
    int scanline = 0;
    int line_cycle = 0;

    // Machine state for differential tests: CPU, RAM, I/O, SRAM, timers, and the frame position, sound
    // state and display so a test can run through whole frames (tick) and still restore exactly.
    struct Snapshot {
        V30MZ::Snapshot cpu;
        std::array<uint8_t, 0x10000> ram;
        std::array<uint8_t, 0x100> io;
        std::vector<uint8_t> sram;
        uint16_t htimer, vtimer;
        uint64_t frame_count;
        int scanline, line_cycle, stall;
        bool frame_done, frame_in_progress;
        bool line_parked;
        uint64_t keypad_frame;
        uint16_t keys, prev_keys;
        Apu::State apu;
        std::array<uint8_t, 128 * 4> sprites;
        int sprite_count;
        std::array<uint16_t, kWidth * kHeight> fb;
    };
    Snapshot snapshot() const {
        return {cpu.snapshot(),
                ram,
                io,
                sram,
                htimer_,
                vtimer_,
                frame_count,
                scanline,
                line_cycle,
                stall_,
                frame_done_,
                frame_in_progress_,
                line_parked_,
                keypad_frame_,
                keys_,
                prev_keys_,
                apu.state(),
                sprites_,
                sprite_count_,
                fb_};
    }
    void restore(const Snapshot &p) {
        terrain_cards = {};
        terrain_oam_cards_ = {};
        terrain_cards_ = {};
        if (raster_text_)
            raster_text_->clear();
        std::fill(scene_text_.begin(), scene_text_.end(), uint8_t(0));
        for (auto &layers : layers_)
            if (layers)
                layers->valid.fill(0);
        cpu.restore(p.cpu);
        ram = p.ram;
        io = p.io;
        sram = p.sram;
        htimer_ = p.htimer;
        vtimer_ = p.vtimer;
        frame_count = p.frame_count;
        scanline = p.scanline;
        line_cycle = p.line_cycle;
        stall_ = p.stall;
        frame_done_ = p.frame_done;
        frame_in_progress_ = p.frame_in_progress;
        line_parked_ = p.line_parked;
        keypad_frame_ = p.keypad_frame;
        keys_ = p.keys;
        prev_keys_ = p.prev_keys;
        apu.set_state(p.apu);
        sprites_ = p.sprites;
        sprite_count_ = p.sprite_count;
        fb_ = p.fb;
        resync_second();
    }
    void resync_second() { // the shadow follows the RAM again (except its own bytes)
        if (second_)
            for (size_t a = 0; a < ram.size(); ++a)
                if (!second_->mask[a])
                    second_->ram[a] = ram[a];
    }

    // One step of the machine: a hardware interrupt, a native routine or one instruction, then the line and
    // frame events its cycles cross. `natives` = false interprets everything (the original code). Returns
    // true when the step ended the frame (then call begin_frame before the next step, after ++frame_count).
    // run_frame() is begin_frame + tick until the frame ends.
    // `advance` = false: the step takes no machine time (no line/frame events, timers or sound): differential
    // tests of frame-spanning routines let time pass only inside the frame waits, as native code takes none.
    bool tick(bool natives = true, bool advance = true);
    void begin_frame();
    bool frame_done() const { return frame_done_; }
    // Drop a pending DMA stall: in frame-spanning tests native code runs in frozen time, so a DMA it starts
    // stalls nothing (as the original's own instructions' stalls are dropped by tick(.., false)).
    void discard_stall() { stall_ = 0; }
    // Native frame points that poll an I/O port directly (wait_visible_scanline) rather than wait on a
    // flag an interrupt sets (wait_vblank) park this way instead of halting: tick() fast-forwards to the
    // next line boundary exactly as it does for a halted CPU, then clears the flag so the native's
    // trampoline is dispatched again on the following tick() and can recheck its condition. A pending
    // interrupt still preempts it (same priority as the original's busy-poll loop with IF enabled); no
    // hardware register is read, written or reprogrammed to make this happen.
    void park_until_next_line() { line_parked_ = true; }
    bool line_parked() const { return line_parked_; }
    // The native keypad entry scans immediately once per frame. Repeated polls hand control back to
    // the scheduler until begin_frame supplies the next input, rather than spinning in native code.
    bool begin_keypad_scan() {
        if (keypad_frame_ == frame_count)
            return false;
        keypad_frame_ = frame_count;
        return true;
    }
    // When set, hardware interrupts are only taken while it returns true (differential tests of routines
    // that span frames defer them to the frame waits, where native code can be interrupted too).
    std::function<bool()> irq_gate;

    // ROM offset of a CPU linear address in the cartridge ROM windows, or -1.
    int64_t rom_offset_of(uint32_t linear) const;

    // Native routines (src/game): called instead of interpreting the code at a ROM offset. The routine does
    // the whole call, including the return; it returns the cycles to charge.
    using Native = std::function<int(Wsc &)>;
    void set_native(uint32_t rom_offset, Native fn);
    void clear_natives() { natives_.clear(); }

  private:
    std::unordered_map<uint32_t, Native> natives_;
    uint16_t keys_ = 0;
    uint16_t prev_keys_ = 0;
    bool frame_done_ = false;
    bool frame_in_progress_ = false;
    int stall_ = 0;            // cycles the CPU is stalled for (DMA)
    bool line_parked_ = false; // see park_until_next_line()
    uint64_t keypad_frame_ = ~uint64_t(0);

    // timers
    uint16_t htimer_ = 0, vtimer_ = 0;
    // sprite table latched on line 144
    std::array<TerrainCard, 17> terrain_oam_cards_{}, terrain_cards_{};
    std::array<uint8_t, 128 * 4> sprites_{};
    int sprite_count_ = 0;

    std::array<uint16_t, kWidth * kHeight> fb_{};
    std::array<std::array<uint16_t, kWidth * kHeight>, 2> screen2_underlay_{};
    std::array<std::array<uint16_t, kWidth * kHeight>, 2> scene_fb_{};
    bool pass_scene_ = false;
    // Per picture: tilemap word index -> last non-window SCR1 entry | 0x10000 (0 = none seen).
    std::array<std::vector<uint32_t>, 2> scene_underlay_;
    void note_scene_underlay(uint16_t address);
    std::array<std::unique_ptr<FrameLayers>, 4> layers_;

    bool color_mode() const { return io[0x60] & 0x80; }
    uint32_t rom_offset(uint32_t bank_addr) const { return bank_addr & uint32_t(rom.size() - 1); }

    void request_irq(int bit);
    void update_irq_levels();
    void end_line();
    void start_line();
    void run_gdma();
    void render_line(int y);
    void render_pass(int y, const uint8_t *mem, uint16_t *fb, const uint8_t *sprites, bool second);
    void render_second_line(int y);
    void update_second_swaps(const uint8_t *mem);
    bool second_stale_ = false;
    void second_scene_line(int y, uint16_t *out, FrameLayers *layers);
    struct Second {
        std::array<uint8_t, 0x10000> ram{};
        std::array<bool, 0x10000> mask{}; // shadow_write8 bytes (not following the RAM)
        std::array<uint16_t, kWidth * kHeight> fb{};
        std::array<uint8_t, 128 * 4> sprites{};
        // While it isn't drawn: its memory, registers and sprites as the last frame's lines ended, so
        // refresh_second() draws the picture that frame would have shown, wherever the game is in this one.
        decltype(ram) frame_ram{};
        decltype(io) frame_io{};
        std::array<uint8_t, 128 * 4> frame_sprites{};
        bool frame_kept = false;
    };
    std::unique_ptr<Second> second_;
    struct HeldSecond {
        std::array<std::array<uint16_t, kWidth * kHeight>, 2> pixels;
        std::array<std::unique_ptr<FrameLayers>, 2> layers;
    };
    std::unique_ptr<HeldSecond> held_second_;
    const uint8_t *pass_mem_ = nullptr; // the pass being drawn: its memory, and whether it's the second
    bool pass_second_ = false;
    mutable bool pass_sprite_ = false; // palette_color for a sprite pixel
    uint16_t palette_color(int pal, int idx) const;
    uint16_t raw_palette_color(int pal, int idx) const;
    std::array<int8_t, 16> line_swap_{
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1}; // per palette, the palette_swaps entry it shows this line (-1: none)
    struct Fade { // how a bound palette is faded from its `from` (all channels at one step or level)
        enum Kind : int8_t { Shift, Cap, Floor } kind = Shift; // from + k, min(from, k), max(from, k)
        int8_t k = 0;
    };
    // Palette matching/fade fitting is scanline-sensitive, but its inputs usually stay
    // unchanged for a whole frame. Compare the inputs before repeating the search.
    std::array<uint8_t, 512> swap_colors_{}, swap_target_colors_{};
    std::vector<std::array<uint16_t, 16>> swap_sources_;
    uint16_t swap_target_ = 0;
    bool swap_cache_valid_ = false;
    bool palette_inputs_changed(const uint8_t *mem);
    std::array<Fade, 16> line_fade_{}; // per palette
    static uint16_t faded(uint16_t color, Fade fade);
    int tile_pixel(int tile, int x, int y, bool hflip, bool vflip) const;
    std::unique_ptr<UiText> raster_text_; // native raster colors (12-bit), presentation only
    // Beside raster_text_: 1 where the clean-scene picture's last screen pixel is that same host text. A
    // native window the clean scene hides keeps its text record, so its glyphs must not show through a
    // backdrop pixel that happens to share their color (white text over white battle clouds).
    std::vector<uint8_t> scene_text_;
    std::unordered_map<int, std::vector<GlyphOverride>> glyph_overrides_;
    std::array<int16_t, 1024> glyph_index_ = [] {
        std::array<int16_t, 1024> a{};
        a.fill(-1);
        return a;
    }();
    std::vector<const std::vector<GlyphOverride> *> glyph_list_;

  public:
    // The game layer's per-machine runtime state (game::Session, src/game/session.h); opaque here. Declared
    // last so it is destroyed first, while the machine it unwinds natives against is still whole.
    std::shared_ptr<void> game_session;
};

} // namespace sawgod::emu
