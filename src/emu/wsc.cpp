// WonderSwan Color system. See wsc.h.
#include "emu/wsc.h"
#include "engine/cpu_scope.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sawgod::emu {

namespace {

constexpr int kFrameEndLine = 145; // reference engine ends its frame when this line begins

// interrupt status bits
constexpr int IRQ_KEY = 1;
constexpr int IRQ_LINE = 4;
constexpr int IRQ_VTIMER = 5;
constexpr int IRQ_VBLANK = 6;
constexpr int IRQ_HTIMER = 7;

uint16_t grey12(int shade) {
    uint16_t v = uint16_t(15 - shade);
    return v << 8 | v << 4 | v;
}

} // namespace

Wsc::Wsc(std::vector<uint8_t> rom_data, size_t sram_size) : rom(std::move(rom_data)), sram(sram_size, 0) {
    reset();
}

void Wsc::reset() {
    scene_hidden_palettes = 0;
    scene_hidden_sprite_prefix = 0;
    scene_hidden_sprite_min_y = 0;
    scene_hidden_sprite_palettes = 0;
    scene_keep_field_npcs = false;
    scene_window_palettes = 0;
    scene_window_tile_min = 0;
    second_scene_raster.reset();
    reset_scene_underlay();
    // A machine at power-on has drawn nothing yet: its pictures are black, not the last frame before the
    // reset, which the host would otherwise show again without its presentation.
    fb_.fill(0);
    for (auto &picture : screen2_underlay_)
        picture.fill(0);
    for (auto &picture : scene_fb_)
        picture.fill(0);
    held_second_.reset();
    if (second_)
        second_->fb.fill(0);
    ram.fill(0);
    cell_pixels.clear();
    if (raster_text_)
        raster_text_->clear();
    std::fill(scene_text_.begin(), scene_text_.end(), uint8_t(0));
    for (auto &layers : layers_)
        if (layers)
            layers->valid.fill(0);
    std::fill(ram.begin() + 0xFE00, ram.end(), 0xFF); // colour palette
    resync_second();
    pass_mem_ = ram.data();
    io.fill(0);
    // I/O state at the first instruction, as reference engine sets it up without a boot ROM
    io[0x16] = 0x9E; // last line: 158 (159 lines per frame)
    io[0x17] = 0x9B;
    io[0x1A] = 0x0C;
    io[0x91] = 0x80;
    io[0x9E] = 0x03;
    io[0xA0] = 0x87;
    io[0xBE] = 0x83;
    io[0xC0] = io[0xC1] = io[0xC2] = io[0xC3] = 0xFF;
    htimer_ = vtimer_ = 0;
    sprite_count_ = 0;
    terrain_cards = {};
    terrain_oam_cards_ = {};
    terrain_cards_ = {};
    scanline = 127;
    line_cycle = 232;
    frame_count = 0;
    prev_keys_ = 0;
    line_parked_ = false;
    keypad_frame_ = ~uint64_t(0);

    apu.reset();
    cpu.reset();
    // registers as left by the boot ROM (reference engine's high-level start-up)
    cpu.s.r[AX] = 0xFF87;
    cpu.s.r[BX] = 0x0043;
    cpu.s.r[CX] = 0x0004;
    cpu.s.r[DX] = 0x0001;
    cpu.s.r[SP] = 0x2000;
    cpu.s.r[SI] = 0x0435;
    cpu.s.r[DI] = 0x040B;
    cpu.s.seg[DS] = 0xFE00;
    cpu.s.flags = SF | PF;
}

// ---------------------------------------------------------------------------------------------------------------
// memory map

uint8_t Wsc::read8(uint32_t addr) {
    addr &= 0xFFFFF;
    if (on_bus)
        on_bus(cpu.fetching ? 'F' : 'R', addr);
    switch (addr >> 16) {
    case 0:
        if (!color_mode() && addr >= 0x4000)
            return 0x90;
        return ram[addr];
    case 1:
        return sram.empty() ? 0x90 : sram[((uint32_t(io[0xC1]) << 16) | (addr & 0xFFFF)) % sram.size()];
    case 2:
        return rom[rom_offset((uint32_t(io[0xC2]) << 16) | (addr & 0xFFFF))];
    case 3:
        return rom[rom_offset((uint32_t(io[0xC3]) << 16) | (addr & 0xFFFF))];
    default:
        return rom[rom_offset((uint32_t(io[0xC0]) << 20) | addr)];
    }
}

void Wsc::write8(uint32_t addr, uint8_t value) {
    addr &= 0xFFFFF;
    if (on_bus)
        on_bus('W', addr);
    switch (addr >> 16) {
    case 0:
        if (!color_mode() && addr >= 0x4000)
            return;
        ram[addr] = value;
        if (second_ && !second_->mask[addr]) [[unlikely]]
            second_->ram[addr] = value;
        if (!cell_pixels.empty()) [[unlikely]]
            cell_pixels.erase(uint16_t(addr & ~1u)); // any write to the cell ends its host pixels
        break;
    case 1:
        if (!sram.empty())
            sram[((uint32_t(io[0xC1]) << 16) | (addr & 0xFFFF)) % sram.size()] = value;
        break;
    default:
        break; // ROM
    }
}

int64_t Wsc::rom_offset_of(uint32_t linear) const {
    linear &= 0xFFFFF;
    switch (linear >> 16) {
    case 0:
    case 1:
        return -1;
    case 2:
        return rom_offset((uint32_t(io[0xC2]) << 16) | (linear & 0xFFFF));
    case 3:
        return rom_offset((uint32_t(io[0xC3]) << 16) | (linear & 0xFFFF));
    default:
        return rom_offset((uint32_t(io[0xC0]) << 20) | linear);
    }
}

void Wsc::set_native(uint32_t rom_offset, Native fn) { natives_[rom_offset] = std::move(fn); }

// ---------------------------------------------------------------------------------------------------------------
// I/O

uint8_t Wsc::in8(uint16_t port) {
    if (on_bus)
        on_bus('I', port);
    if (port >= 0x100) {
        // bits 0-8 decide: SoC ports repeat above 0x100; anything else is open bus
        if ((port & 0x1FF) <= 0xB7 && port < 0x200)
            return in8(port & 0xFF);
        return 0x90;
    }
    if (apu.handles_read(uint8_t(port)))
        return apu.read(uint8_t(port));
    switch (port) {
    case 0x02:
        return uint8_t(scanline);
    case 0xA8:
        return htimer_ & 0xFF;
    case 0xA9:
        return htimer_ >> 8;
    case 0xAA:
        return vtimer_ & 0xFF;
    case 0xAB:
        return vtimer_ >> 8;
    case 0xB0: {
        uint8_t status = io[0xB4];
        int hi = 0;
        for (int i = 7; i >= 0; --i)
            if (status & (1 << i)) {
                hi = i;
                break;
            }
        return (io[0xB0] & 0xF8) | hi;
    }
    case 0xB5: {
        uint8_t sel = io[0xB5] & 0x70, rows = 0;
        if (sel & 0x10)
            rows |= (keys_ >> 8) & 0x0F;
        if (sel & 0x20)
            rows |= (keys_ >> 4) & 0x0F;
        if (sel & 0x40)
            rows |= keys_ & 0x0F;
        return sel | rows;
    }
    default:
        return io[port];
    }
}

void Wsc::out8(uint16_t port, uint8_t value) {
    if (on_bus)
        on_bus('O', port);
    if (port >= 0x100) {
        if ((port & 0x1FF) <= 0xB7 && port < 0x200)
            out8(port & 0xFF, value);
        return;
    }
    switch (port) {
    case 0x02:
        return; // read-only
    case 0x48:
        io[port] = value;
        if (value & 0x80)
            run_gdma();
        return;
    case 0xA0:
        io[port] = 0x82 | (value & 0x0C) | ((io[port] | value) & 1);
        return;
    case 0xA4:
    case 0xA5:
        io[port] = value;
        htimer_ = io[0xA4] | (io[0xA5] << 8);
        return;
    case 0xA6:
    case 0xA7:
        io[port] = value;
        vtimer_ = io[0xA6] | (io[0xA7] << 8);
        return;
    case 0x91: // sound output control: bit 7 (headphones connected) is read-only; reference engine reports
               // them present
        io[port] = 0x80 | (value & 0x0F);
        return;
    case 0xB4:
        return; // status is read-only
    case 0xB6:
        io[0xB4] &= ~value;
        return;
    case 0xB5:
        io[port] = value & 0x70;
        return;
    default:
        io[port] = value;
        if (port >= 0x80 && port <= 0x9F)
            apu.write(uint8_t(port), value);
        return;
    }
}

void Wsc::request_irq(int bit) {
    if (io[0xB2] & (1 << bit))
        io[0xB4] |= 1 << bit;
}

void Wsc::run_gdma() {
    uint32_t src = (io[0x40] | (io[0x41] << 8) | (io[0x42] << 16)) & 0xFFFFE;
    uint16_t dst = (io[0x44] | (io[0x45] << 8)) & 0xFFFE;
    uint16_t len = (io[0x46] | (io[0x47] << 8)) & 0xFFFE;
    bool dec = io[0x48] & 0x40;
    if (len != 0 && !((src >> 16) == 1)) {
        // 5 cycles plus 2 per word from internal RAM; reference engine charges 4 per word from the cartridge
        const int per_word = src < 0x10000 ? 2 : 4;
        stall_ += 5;
        while (len) {
            uint8_t lo = read8(src), hi = read8(src + 1);
            ram[dst] = lo;
            ram[uint16_t(dst + 1)] = hi;
            if (second_) [[unlikely]] {
                if (!second_->mask[dst])
                    second_->ram[dst] = lo;
                if (!second_->mask[uint16_t(dst + 1)])
                    second_->ram[uint16_t(dst + 1)] = hi;
            }
            if (!cell_pixels.empty()) [[unlikely]]
                cell_pixels.erase(uint16_t(dst & ~1u));
            src = dec ? src - 2 : src + 2;
            dst = dec ? dst - 2 : dst + 2;
            len -= 2;
            stall_ += per_word;
        }
    }
    io[0x40] = src & 0xFF;
    io[0x41] = (src >> 8) & 0xFF;
    io[0x42] = (src >> 16) & 0x0F;
    io[0x44] = dst & 0xFF;
    io[0x45] = dst >> 8;
    io[0x46] = len & 0xFF;
    io[0x47] = len >> 8;
    io[0x48] &= 0x7F;
}

// ---------------------------------------------------------------------------------------------------------------
// timing

void Wsc::end_line() {
    if (render_lcd && scanline < kHeight)
        render_line(scanline);
    // horizontal blank timer: counts down once per line
    if ((io[0xA2] & 0x01) && htimer_) {
        if (--htimer_ == 0) {
            request_irq(IRQ_HTIMER);
            if (io[0xA2] & 0x02)
                htimer_ = io[0xA4] | (io[0xA5] << 8);
        }
    }
    scanline = scanline >= io[0x16] ? 0 : scanline + 1;
    start_line();
}

void Wsc::start_line() {
    if (scanline == io[0x03])
        request_irq(IRQ_LINE);
    if (scanline == kHeight) {
        if (second_ && !second_visible) [[unlikely]] {
            second_->frame_ram = second_->ram;
            second_->frame_io = io;
            second_->frame_sprites = second_->sprites;
            second_->frame_kept = true;
        }
        request_irq(IRQ_VBLANK);
        if ((io[0xA2] & 0x04) && vtimer_) {
            if (--vtimer_ == 0) {
                request_irq(IRQ_VTIMER);
                if (io[0xA2] & 0x08)
                    vtimer_ = io[0xA6] | (io[0xA7] << 8);
            }
        }
        presentation_oam = presentation_ready == presentation_epoch &&
                                   (!second_ || presentation_shadow == presentation_epoch)
                               ? presentation_epoch
                               : 0;
        // latch the sprite table for the next frame
        uint32_t base = uint32_t(io[0x04] & (color_mode() ? 0x3F : 0x1F)) << 9;
        int first = io[0x05] & 0x7F;
        terrain_cards_ = terrain_oam_cards_;
        for (auto &card : terrain_cards_)
            if (card.first >= 0)
                card.first = (card.first - first) & 0x7F;
        sprite_count_ = std::min<int>(io[0x06], 128);
        for (int i = 0; i < sprite_count_; ++i)
            for (int b = 0; b < 4; ++b) {
                sprites_[i * 4 + b] = ram[(base + ((first + i) & 0x7F) * 4 + b) & 0xFFFF];
                if (second_) [[unlikely]]
                    second_->sprites[size_t(i * 4 + b)] =
                        second_->ram[(base + ((first + i) & 0x7F) * 4 + b) & 0xFFFF];
            }
    }
    if (scanline == kFrameEndLine)
        frame_done_ = true;
}

void Wsc::begin_frame() {
    if (keys_ & ~prev_keys_)
        request_irq(IRQ_KEY);
    prev_keys_ = keys_;
    frame_done_ = false;
}

bool Wsc::tick(bool natives, bool advance) {
    if (host_paused)
        return frame_done_;
    uint8_t pending = io[0xB4] & io[0xB2];
    if (pending && cpu.s.halted && (!irq_gate || irq_gate()))
        cpu.wake();
    int c;
    if (pending && cpu.accepts_irq() && (!irq_gate || irq_gate())) {
        line_parked_ = false; // dispatch the handler now; the wait will recheck after IRET
        uint8_t vector = in8(0xB0);
        c = cpu.interrupt(vector);
        if (on_irq)
            on_irq(*this);
    } else if (cpu.s.halted || line_parked_) {
        c = kCyclesPerLine - line_cycle; // nothing happens until the next line event
        cpu.cycles += c;
    } else {
        if (on_instruction)
            on_instruction(*this);
        c = -1;
        if (natives && !natives_.empty()) {
            int64_t ro = rom_offset_of(cpu.pc());
            auto it = ro >= 0 ? natives_.find(uint32_t(ro)) : natives_.end();
            if (it != natives_.end()) {
                c = it->second(*this);
                // c < 0: declined (a trampoline that isn't the innermost suspended native's): interpret it,
                // with no trace of the attempt in the cycle count or the prefetch queue
                if (c >= 0) {
                    cpu.flush_queue();
                    cpu.cycles += uint64_t(c);
                }
            }
        }
        if (c < 0)
            c = cpu.step();
    }
    c += stall_;
    cpu.cycles += stall_;
    stall_ = 0;
    if (!advance && !cpu.s.halted && !line_parked_)
        return frame_done_;
    line_cycle += c;
    if (audio_clock_enabled)
        apu.run(c);
    while (line_cycle >= kCyclesPerLine) {
        line_cycle -= kCyclesPerLine;
        end_line();
        line_parked_ = false;
    }
    return frame_done_;
}

void Wsc::run_frame() {
    if (host_paused)
        return;
    if (!frame_in_progress_) {
        begin_frame();
        frame_in_progress_ = true;
    }
    while (!frame_done_ && !host_paused)
        tick();
    if (host_paused)
        return;
    frame_in_progress_ = false;
    ++frame_count;
    static std::FILE *hash_log = [] {
        const char *path = std::getenv("SAWGOD_RASTER_HASH");
        return path && *path ? std::fopen(path, "w") : nullptr;
    }();
    if (hash_log) [[unlikely]] {
        std::fprintf(hash_log, "%llu %016llx\n", (unsigned long long)frame_count,
                     (unsigned long long)raster_hash());
        std::fflush(hash_log);
    }
}

uint64_t Wsc::raster_hash() const {
    uint64_t h = 1469598103934665603ull;
    const auto bytes = [&](const void *data, size_t size) {
        const auto *p = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i)
            h = (h ^ p[i]) * 1099511628211ull;
    };
    const auto value = [&](const auto &v) { bytes(&v, sizeof v); };
    static_assert(sizeof(RasterPixel) == 14, "RasterPixel must have no padding to hash its bytes");
    bytes(fb_.data(), sizeof fb_);
    bytes(screen2_underlay_.data(), sizeof screen2_underlay_);
    bytes(scene_fb_.data(), sizeof scene_fb_);
    for (const auto &layers : layers_) {
        value(uint8_t(layers ? 1 : 0));
        if (layers) {
            bytes(layers->planes.data(), sizeof layers->planes);
            bytes(layers->registers.data(), sizeof layers->registers);
            bytes(layers->valid.data(), sizeof layers->valid);
        }
    }
    value(uint8_t(second_ ? 1 : 0));
    if (second_)
        bytes(second_->fb.data(), sizeof second_->fb);
    value(uint8_t(raster_text_ ? 1 : 0));
    if (raster_text_)
        for (const auto &p : raster_text_->pixels) {
            value(p.kind), value(p.color), value(p.beneath), value(p.casts_shadow), value(p.shadow_color);
            value(p.corner), value(p.corner_scene);
        }
    bytes(scene_text_.data(), scene_text_.size());
    bytes(ram.data(), ram.size());
    bytes(io.data(), io.size());
    return h;
}

// ---------------------------------------------------------------------------------------------------------------
// display

uint16_t Wsc::palette_color(int pal, int idx) const {
    const uint16_t c = raw_palette_color(pal, idx);
    if (!pass_second_ || !upper_palette_greys || pal < 8 || !color_mode()) [[likely]]
        return c;
    return gb_grey(c);
}

uint16_t Wsc::presentation_palette_color(int palette, int index, bool second, bool sprite) {
    const auto *memory = pass_mem_;
    const bool old_second = pass_second_, old_sprite = pass_sprite_;
    pass_second_ = second && bool(second_);
    pass_sprite_ = sprite;
    pass_mem_ = pass_second_ ? second_->ram.data() : ram.data();
    const auto color = palette_color(palette, index);
    pass_mem_ = memory;
    pass_second_ = old_second;
    pass_sprite_ = old_sprite;
    return color;
}

uint16_t Wsc::faded(uint16_t color, Fade fade) {
    uint16_t shown = 0;
    for (int ch = 0; ch < 3; ++ch) {
        const int t = color >> (8 - ch * 4) & 15;
        const int v = fade.kind == Fade::Cap     ? std::min(t, int(fade.k))
                      : fade.kind == Fade::Floor ? std::max(t, int(fade.k))
                                                 : std::clamp(t + fade.k, 0, 15);
        shown |= uint16_t(v << (8 - ch * 4));
    }
    return shown;
}

uint16_t Wsc::raw_palette_color(int pal, int idx) const {
    if (color_mode()) {
        // (a background-only swap leaves sprite palettes to the grey snap: the hand cursor and party icons
        // share the window palette's colors, which its swap turns into black text)
        if (pass_second_ && !palette_swaps.empty() && line_swap_[size_t(pal)] >= 0 &&
            (!pass_sprite_ || palette_swaps[size_t(line_swap_[size_t(pal)])].sprites)) [[unlikely]] {
            const uint16_t to = palette_swaps[size_t(line_swap_[size_t(pal)])].to[size_t(idx)];
            return faded(to, line_fade_[size_t(pal)]);
        }
        int a = 0xFE00 + pal * 32 + idx * 2;
        return (pass_mem_[a] | (pass_mem_[a + 1] << 8)) & 0x0FFF;
    }
    uint16_t p = io[0x20 + pal * 2] | (io[0x21 + pal * 2] << 8);
    int lut = (p >> (idx * 4)) & 7;
    return grey12((io[0x1C + lut / 2] >> ((lut & 1) * 4)) & 15);
}

std::vector<uint32_t> Wsc::framebuffer_rgb(ColorMode mode, bool second, bool scene,
                                           bool without_screen2) const {
    CpuScope cpu_scope(second ? "framebuffer_rgb GB" : "framebuffer_rgb WSC", frame_count);
    const auto &fb = scene             ? scene_fb_[second && second_ ? 1 : 0]
                     : without_screen2 ? screen2_underlay_[second && second_ ? 1 : 0]
                                       : framebuffer(second);
    std::vector<uint32_t> out(fb.size());
    for (size_t i = 0; i < fb.size(); ++i) {
        uint32_t r = (fb[i] >> 8) & 15, g = (fb[i] >> 4) & 15, b = fb[i] & 15;
        uint32_t R, G, B;
        if (mode == ColorMode::Lcd) { // fitted to reference engine's screenshots (gray n -> 16n)
            R = 13 * r + 2 * g + b;
            G = 12 * g + 4 * b;
            B = 3 * r + 2 * g + 11 * b;
        } else {
            R = r * 17;
            G = g * 17;
            B = b * 17;
        }
        out[i] = R << 16 | G << 8 | B;
    }
    // Seed ordinary and clean-scene bases before Chrome writes. Helper reads after composition
    // must preserve the host record; native-only reads may still change color mode during handoff.
    if (ui_text && !second && !without_screen2 && !ui_text->composed) {
        ui_text->clear();
        if (raster_text_) {
            const auto rgb = [mode](uint32_t c) {
                const uint32_t r = c >> 8 & 15, g = c >> 4 & 15, b = c & 15;
                return mode == ColorMode::Lcd
                           ? (13 * r + 2 * g + b) << 16 | (12 * g + 4 * b) << 8 | (3 * r + 2 * g + 11 * b)
                           : r * 17 << 16 | g * 17 << 8 | b * 17;
            };
            for (size_t at = 0; at < out.size(); ++at) {
                auto p = raster_text_->pixels[at];
                // The clean scene keeps only text it still draws itself, not a hidden window's glyph whose
                // color the backdrop beneath happens to share.
                if (p.kind != UiText::None && p.color == fb[at] && (!scene || scene_text_[at])) {
                    p.color = rgb(p.color);
                    p.beneath = rgb(p.beneath);
                    ui_text->pixels[at] = p;
                }
            }
        }
    }
    return out;
}

// Palette index of pixel (x, y) of a tile (0-1023), or 0 (transparent/colour 0).
int Wsc::tile_pixel(int tile, int x, int y, bool hflip, bool vflip) const {
    if (hflip)
        x = 7 - x;
    if (vflip)
        y = 7 - y;
    uint8_t mode = color_mode() ? io[0x60] & 0x60 : 0;
    if (!color_mode())
        tile &= 511;
    switch (mode) {
    case 0x40: { // 4bpp planar
        int a = 0x4000 + tile * 32 + y * 4;
        int bit = 7 - x, v = 0;
        for (int p = 0; p < 4; ++p)
            v |= ((pass_mem_[a + p] >> bit) & 1) << p;
        return v;
    }
    case 0x60: { // 4bpp packed
        if (glyph_index_[size_t(tile & 1023)] >= 0) [[unlikely]] {
            const uint8_t *t = &pass_mem_[0x4000 + tile * 32];
            for (const GlyphOverride &g : *glyph_list_[size_t(glyph_index_[size_t(tile & 1023)])])
                if (std::equal(g.match.begin(), g.match.end(), t)) {
                    const uint8_t s = g.show[size_t(y * 4 + x / 2)];
                    return (x & 1) ? (s & 15) : (s >> 4);
                }
        }
        uint8_t b = pass_mem_[0x4000 + tile * 32 + y * 4 + x / 2];
        return (x & 1) ? (b & 15) : (b >> 4);
    }
    case 0x20: { // 2bpp packed
        uint8_t b = pass_mem_[0x2000 + tile * 16 + y * 2 + x / 4];
        return (b >> ((3 - (x & 3)) * 2)) & 3;
    }
    default: { // 2bpp planar
        int a = 0x2000 + tile * 16 + y * 2;
        int bit = 7 - x;
        return ((pass_mem_[a] >> bit) & 1) | (((pass_mem_[a + 1] >> bit) & 1) << 1);
    }
    }
}

void Wsc::reset_scene_underlay() {
    for (auto &cells : scene_underlay_)
        cells.clear();
}

// Both pictures read their own tilemap at the same address: the GB picture's words live in the shadow RAM.
void Wsc::note_scene_underlay(uint16_t address) {
    for (int picture = 0; picture < (second_ ? 2 : 1); ++picture) {
        const uint8_t *mem = picture ? second_->ram.data() : ram.data();
        const uint16_t entry = uint16_t(mem[address] | mem[uint16_t(address + 1)] << 8);
        // Only cells outside the window palettes are remembered. Effect art that shares those palettes
        // (bank37 backgrounds below the UI tiles) is shown while present but never pasted back later.
        if (scene_window_palettes >> (entry >> 9 & 15) & 1)
            continue;
        auto &cells = scene_underlay_[size_t(picture)];
        if (cells.empty())
            cells.resize(0x8000);
        cells[address >> 1] = entry | 0x10000u;
    }
}

void Wsc::render_line(int y) {
    if (y == 0 && on_scene_frame)
        on_scene_frame(*this);
    presentation_raster[size_t(y)] =
        presentation_ready == presentation_epoch && presentation_oam == presentation_epoch
            ? presentation_epoch
            : 0;
    render_pass(y, ram.data(), fb_.data(), sprites_.data(), false);
    const bool hidden = scene_hidden_palettes || scene_hidden_sprite_prefix || scene_hidden_sprite_palettes;
    if (hidden) {
        pass_scene_ = true;
        render_pass(y, ram.data(), scene_fb_[0].data(), sprites_.data(), false);
        pass_scene_ = false;
    } else { // keep the scene picture whole: a layout that turns on mid-frame shows these lines, not stale
             // ones
        std::copy_n(fb_.data() + y * kWidth, kWidth, scene_fb_[0].data() + y * kWidth);
    }
    if (second_) [[unlikely]] {
        if (second_visible) {
            if (y == 0)
                second_stale_ = false; // this frame draws every line before anyone reads the picture
            render_second_line(y);
        } else {
            // Not shown: only the per-line palette-swap tracking, exactly as render_pass would run it.
            if (!held_second_ && !palette_swaps.empty() && color_mode() &&
                palette_inputs_changed(second_->ram.data()))
                update_second_swaps(second_->ram.data());
            second_stale_ = true;
        }
    }
    if (layers_[1] && !scene_hidden_palettes && !scene_hidden_sprite_prefix &&
        !scene_hidden_sprite_palettes) {
        for (int second = 0; second < (second_ && layers_[3] ? 2 : 1); ++second) {
            auto &a = *layers_[size_t(second * 2)], &b = *layers_[size_t(second * 2 + 1)];
            for (int layer = 0; layer < FrameLayers::Count; ++layer)
                std::copy_n(a.planes[size_t(layer)].begin() + y * kWidth, kWidth,
                            b.planes[size_t(layer)].begin() + y * kWidth);
            b.registers[size_t(y)] = a.registers[size_t(y)];
            b.valid[size_t(y)] = 1;
        }
    }
    pass_mem_ = ram.data();
    pass_second_ = false;
}

// One line of the second picture, ordinary and clean-scene, as render_line draws the first.
void Wsc::render_second_line(int y) {
    render_pass(y, second_->ram.data(), second_->fb.data(), second_->sprites.data(), true);
    if (scene_hidden_palettes || scene_hidden_sprite_prefix || scene_hidden_sprite_palettes) {
        pass_scene_ = true;
        render_pass(y, second_->ram.data(), scene_fb_[1].data(), second_->sprites.data(), true);
        pass_scene_ = false;
    } else {
        std::copy_n(second_->fb.data() + y * kWidth, kWidth, scene_fb_[1].data() + y * kWidth);
    }
    pass_mem_ = ram.data();
    pass_second_ = false;
}

void Wsc::refresh_second() {
    CpuScope cpu_scope("Wsc::refresh_second", frame_count);
    if (!second_ || !second_stale_)
        return;
    // The last frame's picture: from the state its lines ended in, else (none kept yet) the current state.
    const bool kept = second_->frame_kept;
    if (kept) {
        std::swap(second_->ram, second_->frame_ram);
        std::swap(io, second_->frame_io);
        std::swap(second_->sprites, second_->frame_sprites);
    }
    for (int y = 0; y < kHeight; ++y)
        render_second_line(y);
    if (kept) {
        std::swap(second_->ram, second_->frame_ram);
        std::swap(io, second_->frame_io);
        std::swap(second_->sprites, second_->frame_sprites);
    }
    second_stale_ = false;
}

void Wsc::copy_presentation_to(Wsc &destination) const {
    destination.ram = ram;
    destination.io = io;
    destination.sram = sram;
    destination.sprites_ = sprites_;
    destination.sprite_count_ = sprite_count_;
    destination.terrain_cards_ = terrain_cards_;
    destination.frame_count = frame_count;
}

void Wsc::capture_layers(bool on, bool second) {
    if (on && !raster_text_) {
        raster_text_ = std::make_unique<UiText>();
        scene_text_.assign(size_t(kWidth * kHeight), 0);
    }
    if (!on) {
        raster_text_.reset();
        scene_text_.clear();
    }
    for (size_t i = 0; i < layers_.size(); ++i) {
        const bool wanted = on && (second || i < 2);
        if (wanted && !layers_[i])
            layers_[i] = std::make_unique<FrameLayers>();
        else if (!wanted)
            layers_[i].reset();
    }
}

void Wsc::set_second(bool on) {
    swap_cache_valid_ = false;
    if (on && !second_) {
        for (size_t i = 2; i < layers_.size(); ++i)
            if (layers_[i])
                layers_[i]->valid.fill(0);
        second_ = std::make_unique<Second>();
        second_->ram = ram;
        second_->fb = fb_;
        second_->sprites = sprites_;
    } else if (!on) {
        held_second_.reset();
        second_motion_y.clear();
        second_scene_image.clear();
        second_scene_above_host = false;
        second_scene_raster.reset();
        second_.reset();
    }
}

void Wsc::forget_second_swaps() {
    line_swap_.fill(-1);
    line_fade_.fill({});
    swap_cache_valid_ = false;
}

void Wsc::hold_second_picture(bool on) {
    if (on && !held_second_)
        refresh_second();
    if (!on) {
        held_second_.reset();
    } else if (second_ && !held_second_) {
        held_second_ = std::make_unique<HeldSecond>();
        held_second_->pixels[0] = second_->fb;
        held_second_->pixels[1] = scene_fb_[1];
        for (size_t pass = 0; pass < 2; ++pass)
            if (layers_[2 + pass])
                held_second_->layers[pass] = std::make_unique<FrameLayers>(*layers_[2 + pass]);
    }
}

void Wsc::move_second_picture(std::vector<uint32_t> &pixels) const {
    if (pixels.size() != kWidth * kHeight || frame_count <= second_motion_start)
        return;
    const uint64_t frame = frame_count - second_motion_start - 1;
    if (frame >= second_motion_y.size())
        return;
    const int offset = second_motion_y[size_t(frame)];
    const auto before = pixels;
    for (int y = std::max(0, second_motion_split - offset); y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const int source = y + offset;
            pixels[size_t(y * kWidth + x)] = source >= second_motion_split && source < kHeight
                                                 ? before[size_t(source * kWidth + x)]
                                                 : 0xFFFFFF;
        }
}

void Wsc::shadow_write8(uint16_t addr, uint8_t value) {
    if (!second_)
        return;
    second_->ram[addr] = value;
    second_->mask[addr] = true;
}

void Wsc::shadow_release(uint16_t addr, size_t bytes) {
    for (size_t i = 0; second_ && i < bytes; ++i) {
        const uint16_t a = uint16_t(addr + i);
        second_->mask[a] = false;
        second_->ram[a] = ram[a];
    }
}

bool Wsc::palette_inputs_changed(const uint8_t *mem) {
    bool changed = !swap_cache_valid_ || swap_target_ != palette_swap_target ||
                   swap_sources_.size() != palette_swaps.size() ||
                   std::memcmp(swap_colors_.data(), mem + 0xFE00, swap_colors_.size()) != 0;
    if (palette_swap_target)
        changed |= std::memcmp(swap_target_colors_.data(), mem + palette_swap_target,
                               swap_target_colors_.size()) != 0;
    if (!changed)
        for (size_t i = 0; i < palette_swaps.size(); ++i)
            if (swap_sources_[i] != palette_swaps[i].from) {
                changed = true;
                break;
            }
    if (!changed)
        return false;
    std::copy_n(mem + 0xFE00, swap_colors_.size(), swap_colors_.begin());
    if (palette_swap_target)
        std::copy_n(mem + palette_swap_target, swap_target_colors_.size(), swap_target_colors_.begin());
    swap_target_ = palette_swap_target;
    swap_sources_.resize(palette_swaps.size());
    for (size_t i = 0; i < palette_swaps.size(); ++i)
        swap_sources_[i] = palette_swaps[i].from;
    swap_cache_valid_ = true;
    return true;
}

// Which palette swap (and fade of it) each palette shows on the second picture. Kept per line even while the
// second picture is not drawn (second_visible false), so drawing it again matches drawing it all along.
void Wsc::update_second_swaps(const uint8_t *mem) {
    const auto color = [&](uint16_t base, int p, int c) {
        return uint16_t((mem[size_t(base + p * 32 + c * 2)] | mem[size_t(base + p * 32 + c * 2 + 1)] << 8) &
                        0x0FFF);
    };
    const auto equals = [&](uint16_t base, int p, const std::array<uint16_t, 16> &from) {
        for (int c = 0; c < 16; ++c)
            if (color(base, p, c) != from[size_t(c)])
                return false;
        return true;
    };
    for (int p = 0; p < 16; ++p) {
        int bound = -1;
        for (size_t i = 0; i < palette_swaps.size() && bound < 0; ++i)
            if (equals(0xFE00, p, palette_swaps[i].from) ||
                (palette_swap_target && equals(palette_swap_target, p, palette_swaps[i].from)))
                bound = int(i);
        if (bound < 0 && line_swap_[size_t(p)] >= 0 && size_t(line_swap_[size_t(p)]) < palette_swaps.size())
            bound = line_swap_[size_t(p)]; // still a fade of it?
        Fade fade{};
        if (bound >= 0) {
            // the 45 channel values (15 colors, R G B; color 0 is transparent) from and shown
            const auto &from = palette_swaps[size_t(bound)].from;
            int f[45], v[45];
            for (int c = 1; c < 16; ++c)
                for (int ch = 0; ch < 3; ++ch) {
                    f[(c - 1) * 3 + ch] = from[size_t(c)] >> (8 - ch * 4) & 15;
                    v[(c - 1) * 3 + ch] = color(0xFE00, p, c) >> (8 - ch * 4) & 15;
                }
            // shifted: clamp(from + k) (stepping down to black or up to white)
            int lo = -15, hi = 15;
            for (int i = 0; i < 45 && lo <= hi; ++i) {
                if (v[i] == 0)
                    hi = std::min(hi, -f[i]);
                else if (v[i] == 15)
                    lo = std::max(lo, 15 - f[i]);
                else
                    lo = std::max(lo, v[i] - f[i]), hi = std::min(hi, v[i] - f[i]);
            }
            bool fits = lo <= hi;
            if (fits) {
                fade = {Fade::Shift, int8_t(lo > 0 ? lo : hi < 0 ? hi : 0)};
            } else { // capped: min(from, k) (stepping up from black); floored: max(from, k) (down from
                     // white)
                for (const auto kind : {Fade::Cap, Fade::Floor}) {
                    int k = -1;
                    bool ok = true;
                    for (int i = 0; i < 45 && ok; ++i)
                        if (v[i] != f[i]) {
                            ok = (kind == Fade::Cap ? v[i] < f[i] : v[i] > f[i]) && (k < 0 || k == v[i]);
                            k = v[i];
                        }
                    for (int i = 0; i < 45 && ok && k >= 0; ++i)
                        if (v[i] == f[i])
                            ok = kind == Fade::Cap ? f[i] <= k : f[i] >= k;
                    if (ok && k >= 0) {
                        fade = {kind, int8_t(k)};
                        fits = true;
                        break;
                    }
                }
            }
            if (!fits)
                bound = -1;
        }
        line_swap_[size_t(p)] = int8_t(bound);
        line_fade_[size_t(p)] = fade;
    }
}

void Wsc::render_pass(int y, const uint8_t *mem, uint16_t *fb, const uint8_t *sprites, bool second) {
    if (second && held_second_) {
        const size_t pass = pass_scene_ ? 1 : 0, at = size_t(y * kWidth);
        const uint64_t frame = frame_count - second_motion_start;
        const int offset = frame < second_motion_y.size() ? second_motion_y[size_t(frame)] : 0;
        const int source_y = y >= second_motion_split - offset ? y + offset : y;
        const bool blank = source_y < 0 || source_y >= kHeight;
        const size_t source = blank ? 0 : size_t(source_y * kWidth);
        if (blank)
            std::fill_n(fb + at, kWidth, uint16_t(0xFFF));
        else
            std::copy_n(held_second_->pixels[pass].begin() + source, kWidth, fb + at);
        if (auto *layers = layers_[2 + pass].get()) {
            const auto *held = held_second_->layers[pass].get();
            for (size_t plane = 0; plane < FrameLayers::Count; ++plane) {
                auto out = layers->planes[plane].begin() + at;
                if (held && !blank)
                    std::copy_n(held->planes[plane].begin() + source, kWidth, out);
                else
                    std::fill_n(out, kWidth, RasterPixel{});
            }
            if (blank)
                std::fill_n(layers->planes[FrameLayers::Backdrop].begin() + at, kWidth,
                            RasterPixel{0xFFF, 0, 0, 0, 0, 255, RasterPixel::Present});
            if (held) {
                layers->registers[size_t(y)] = held->registers[size_t(y)];
                layers->valid[size_t(y)] = held->valid[size_t(y)];
            }
        }
        second_scene_line(y, fb + at, layers_[2 + pass].get());
        return;
    }
    pass_mem_ = mem;
    pass_second_ = second;
    uint16_t *out = &fb[y * kWidth];
    auto *layers = layers_[size_t((second ? 2 : 0) + (pass_scene_ ? 1 : 0))].get();
    if (layers) {
        // The backdrop line is rewritten in full below; the other planes start empty: one block copy of a
        // prepared empty line each (RasterPixel{} is not all-zero bytes: sprite = 255).
        static const std::array<RasterPixel, kWidth> empty_line{};
        for (size_t plane = FrameLayers::Backdrop + 1; plane < FrameLayers::Count; ++plane)
            std::memcpy(&layers->planes[plane][size_t(y * kWidth)], empty_line.data(), sizeof empty_line);
        std::copy_n(io.begin(), 24, layers->registers[size_t(y)].begin());
        layers->valid[size_t(y)] = 1;
    }
    auto record = [&](int layer, int x, uint16_t color, int tile, int pal, int index, uint16_t cell,
                      int sprite, uint8_t flags) {
        if (layers)
            layers->planes[size_t(layer)][size_t(y * kWidth + x)] = {color,
                                                                     uint16_t(tile),
                                                                     cell,
                                                                     uint8_t(pal),
                                                                     uint8_t(index),
                                                                     uint8_t(sprite),
                                                                     uint8_t(flags | RasterPixel::Present)};
    };
    auto *text = !second && !pass_scene_ && raster_text_ ? raster_text_.get() : nullptr;
    // scene_text_ follows the ordinary record, then the clean-scene pass (drawn after it) rewrites the line.
    uint8_t *scene_text = !second && raster_text_ ? scene_text_.data() + y * kWidth : nullptr;
    if (text) {
        static const std::array<UiText::Pixel, kWidth> empty_text{};
        std::copy_n(empty_text.begin(), kWidth, text->pixels.begin() + y * kWidth);
    }
    if (scene_text)
        std::fill_n(scene_text, kWidth, uint8_t(0));
    // Native tile rows repeat across the scanline (empty/window/ground tiles in
    // particular). Decode a row once; reset validity each pass so mid-frame writes
    // and the separate GB memory always take effect immediately.
    uint8_t rows[8192][8];
    uint64_t row_valid[128]{};
    // One tile row's eight decoded indices (unflipped), decoding it on first use this pass.
    const auto tile_row = [&](int tile, int row, bool vflip) -> const uint8_t * {
        if (vflip)
            row = 7 - row;
        const size_t key = size_t(tile * 8 + row);
        const uint64_t bit = uint64_t(1) << (key & 63);
        if (!(row_valid[key >> 6] & bit)) {
            row_valid[key >> 6] |= bit;
            if (color_mode() && (io[0x60] & 0x60) == 0x60) {
                // Packed rows share a glyph match across all eight pixels. Resolve it once, with
                // this pass's memory; the next scanline still sees every mid-raster tile write.
                const uint8_t *bytes = mem + 0x4000 + tile * 32;
                if (glyph_index_[size_t(tile)] >= 0)
                    for (const auto &glyph : *glyph_list_[size_t(glyph_index_[size_t(tile)])])
                        if (std::equal(glyph.match.begin(), glyph.match.end(), bytes)) {
                            bytes = glyph.show.data();
                            break;
                        }
                for (int i = 0; i < 4; ++i) {
                    const auto packed = bytes[row * 4 + i];
                    rows[key][size_t(i * 2)] = packed >> 4;
                    rows[key][size_t(i * 2 + 1)] = packed & 15;
                }
            } else {
                for (int i = 0; i < 8; ++i)
                    rows[key][size_t(i)] = uint8_t(tile_pixel(tile, i, row, false, false));
            }
        }
        return rows[key];
    };
    const auto pixel = [&](int tile, int x, int row, bool hflip, bool vflip) {
        return int(tile_row(tile, row, vflip)[size_t(hflip ? 7 - x : x)]);
    };
    const bool color4 = color_mode() && (io[0x60] & 0x40);
    auto transparent = [&](int pal, int idx) { return idx == 0 && (color4 || (pal & 4)); };
    if (second && !palette_swaps.empty() && color_mode() && palette_inputs_changed(mem)) [[unlikely]]
        update_second_swaps(mem);
    // Resolve each tile-layer color once per scanline. GB fade/gray conversion is
    // identical for every pixel with this palette/index; sprites have separate rules.
    uint16_t colors[256];
    std::fill_n(colors, 256, uint16_t(0xFFFF));
    const auto tile_color = [&](int pal, int idx) {
        auto &color = colors[size_t(pal * 16 + idx)];
        if (color == 0xFFFF)
            color = palette_color(pal, idx);
        return color;
    };
    // background colour
    uint16_t bg;
    if (color_mode())
        bg = tile_color(io[0x01] >> 4, io[0x01] & 15);
    else
        bg = grey12((io[0x1C + (io[0x01] & 7) / 2] >> ((io[0x01] & 1) * 4)) & 15);
    std::fill(out, out + kWidth, bg);
    if (layers) // the whole backdrop line at once, as record() would write each pixel
        std::fill_n(
            layers->planes[FrameLayers::Backdrop].begin() + y * kWidth, kWidth,
            RasterPixel{bg, 0, 0, uint8_t(io[1] >> 4), uint8_t(io[1] & 15), 255, RasterPixel::Present});

    int image_palette = -1; // screen 1 as a picture (screen1_image), faded like the palette bound to it
    if (second && screen1_image_swap >= 0 && !palette_swaps.empty() &&
        screen1_image.size() == size_t(kWidth) * 144 && y < 144) [[unlikely]]
        for (int p = 0; p < 16 && image_palette < 0; ++p)
            if (line_swap_[size_t(p)] == screen1_image_swap)
                image_palette = p;
    const uint8_t ctrl = io[0x00];
    const uint8_t mask = color_mode() ? 0x0F : 0x07;
    // The GB picture's walkers on see-through terrain (second_bg_zero_palette): which screen-1 pixels hide
    // their lower half.
    uint8_t s1_cover[kWidth];
    uint8_t *cover = second && (ctrl & 0x04) ? s1_cover : nullptr;
    if (cover)
        std::fill_n(s1_cover, kWidth, uint8_t(0));
    auto draw_screen = [&](int n, uint16_t *dst, uint8_t *prio, bool use_window) {
        uint16_t base = uint16_t((n == 0 ? io[0x07] & mask : (io[0x07] >> 4) & mask) << 11);
        int sx = io[0x10 + n * 2], sy = io[0x11 + n * 2];
        int py = (y + sy) & 255;
        bool win_outside = ctrl & 0x10;
        bool in_y = y >= io[0x09] && y <= io[0x0B];
        int column = -1, tile = 0, pal = 0;
        uint16_t a = 0, cell = 0;
        const CellPixels *host = nullptr;
        // The current cell's tile row, looked up once per cell rather than per pixel.
        const uint8_t *cell_row = nullptr;
        int cell_row_tile = -1, cell_row_cell = -1;
        for (int x = 0; x < kWidth; ++x) {
            if (use_window) {
                bool inside = in_y && x >= io[0x08] && x <= io[0x0A];
                if (inside == win_outside)
                    continue;
            }
            int px = (x + sx) & 255;
            if (column != (px >> 3)) {
                // Tile attributes and host glyph lookup are shared by all eight pixels.
                column = px >> 3;
                a = base + ((py >> 3) * 32 + column) * 2;
                cell = mem[a] | (mem[a + 1] << 8);
                tile = (cell & 0x1FF) | (color_mode() && (cell & 0x2000) ? 0x200 : 0);
                pal = (cell >> 9) & 15;
                host = nullptr;
                if (!cell_pixels.empty()) [[unlikely]] {
                    const auto it = cell_pixels.find(a);
                    if (it != cell_pixels.end() && it->second.entry == cell)
                        host = &it->second;
                }
                // The first picture's ordinary pass sees every visible cell each frame (both layers: an
                // oversized boss's portrait on SCR2 reaches the rows a native window replaces).
                if (scene_window_palettes && !second && !pass_scene_) [[unlikely]]
                    note_scene_underlay(a);
            }
            if (pass_scene_ && tile >= scene_window_tile_min && (scene_hidden_palettes & (1u << pal))) {
                // A native window replaced this cell: draw the entry it covered (the backdrop on SCR1, an
                // oversized boss's portrait on SCR2), if one was seen.
                const auto &cells = scene_underlay_[second ? 1 : 0];
                const uint32_t under = scene_window_palettes && !cells.empty() ? cells[a >> 1] : 0;
                if (!under || (((under & 511) | (color_mode() && (under & 0x2000) ? 512 : 0)) >=
                                   scene_window_tile_min &&
                               (scene_hidden_palettes >> (under >> 9 & 15) & 1)))
                    continue;
                cell = uint16_t(under);
                tile = (cell & 0x1FF) | (color_mode() && (cell & 0x2000) ? 0x200 : 0);
                pal = (cell >> 9) & 15;
                host = nullptr;
            }
            int idx;
            if (host) [[unlikely]] {
                const int i = (py & 7) * 8 + (px & 7);
                idx = host->over[i] ? host->over[i] : host->px[i];
            } else {
                if (tile != cell_row_tile || cell != cell_row_cell) {
                    cell_row = tile_row(tile, py & 7, cell & 0x8000);
                    cell_row_tile = tile;
                    cell_row_cell = cell;
                }
                idx = cell_row[size_t(cell & 0x4000 ? 7 - (px & 7) : px & 7)];
            }
            if (transparent(pal, idx) ||
                (n == 1 && image_palette >= 0 && (screen2_hidden_palettes >> pal & 1)))
                continue;
            if (text || scene_text) {
                // Only explicit host text is recorded: a field tile with the same index/color
                // is artwork. Japanese host UI is recorded by Chrome at composition time.
                const int i = (py & 7) * 8 + (px & 7);
                const bool glyph = host && (host->over[i] || host->ink[i]) && (idx == 1 || idx == 2);
                if (scene_text)
                    scene_text[x] = glyph;
                if (text) {
                    auto &p = text->pixels[size_t(y * kWidth + x)];
                    p = {};
                    if (glyph) {
                        const int background = host->over[i] ? host->px[i] : host->beneath[i];
                        p = {idx == 2 ? UiText::Stroke : UiText::Shadow, tile_color(pal, idx),
                             transparent(pal, background) ? dst[x] : tile_color(pal, background)};
                    }
                }
            }
            dst[x] = tile_color(pal, idx);
            if (n == 0 && cover)
                cover[x] = !(pal == second_bg_zero_palette && idx == 1);
            if (layers)
                record(n == 0 ? FrameLayers::Screen1 : FrameLayers::Screen2, x, dst[x], tile, pal, idx, a,
                       255,
                       uint8_t((host ? RasterPixel::HostText : 0) | (cell & 0x4000 ? RasterPixel::HFlip : 0) |
                               (cell & 0x8000 ? RasterPixel::VFlip : 0) |
                               (use_window ? RasterPixel::Window : 0)));
            if (prio)
                prio[x] = 1;
        }
    };

    if ((ctrl & 0x01) && image_palette >= 0) [[unlikely]] {
        for (int x = 0; x < kWidth; ++x) {
            out[x] = faded(screen1_image[size_t(y * kWidth + x)], line_fade_[size_t(image_palette)]);
            record(FrameLayers::Screen1, x, out[x], 0, image_palette, 0, 0, 255, 0);
        }
    } else if (ctrl & 0x01) {
        draw_screen(0, out, nullptr, false);
    }

    // sprites: first 32 on this line; the earliest in the table is in front
    uint16_t spr_col[kWidth];
    uint8_t spr_on[kWidth] = {}, spr_front[kWidth] = {};
    RasterPixel spr_meta[kWidth]{};
    if (ctrl & 0x04) {
        // GB picture: rebuild each veiled walker's lower half (game/terrain_veil.h: the card's top-left
        // word, +1 to its right, +2/+3 below) and drop the native lower entries, which come and go.
        uint8_t rebuilt[17 * 2 * 4];
        int rebuilt_count = 0, rebuilt_first[17 * 2];
        uint64_t replaced[2] = {};
        if (cover)
            for (const auto &card : terrain_cards_) {
                const int at = card.first * 4;
                if (!card.veiled || card.first < 0 || at + 7 >= sprite_count_ * 4)
                    continue;
                const uint16_t word = uint16_t(sprites[at] | sprites[at + 1] << 8);
                if (word != card.word || uint16_t(sprites[at + 4] | sprites[at + 5] << 8) != word + 1 ||
                    sprites[at + 6] != sprites[at + 2] || sprites[at + 7] != uint8_t(sprites[at + 3] + 8))
                    continue;
                for (int q = 2; q < 4; ++q) {
                    const uint16_t lower = uint16_t(word + q);
                    const int native = card.first + q;
                    if (native < sprite_count_ &&
                        uint16_t(sprites[native * 4] | sprites[native * 4 + 1] << 8) == lower &&
                        sprites[native * 4 + 2] == uint8_t(sprites[at + 2] + 8))
                        replaced[native >> 6] |= uint64_t(1) << (native & 63);
                    uint8_t *e = &rebuilt[rebuilt_count * 4];
                    e[0] = uint8_t(lower);
                    e[1] = uint8_t(lower >> 8);
                    e[2] = uint8_t(sprites[at + 2] + 8);
                    e[3] = uint8_t(sprites[at + 3] + (q - 2) * 8);
                    rebuilt_first[rebuilt_count++] = card.first;
                }
            }
        int found[32], n = 0;
        for (int i = 0; i < sprite_count_ && n < 32; ++i) {
            int sy = sprites[i * 4 + 2];
            if (((y - sy) & 0xFF) < 8 && !(replaced[i >> 6] >> (i & 63) & 1))
                found[n++] = i;
        }
        bool win = ctrl & 0x08;
        bool in_y = y >= io[0x0D] && y <= io[0x0F];
        // Back to front: the native entries, then (in front of them, behind the field's covering pixels) the
        // rebuilt lower halves.
        for (int k = n - 1; k >= -rebuilt_count; --k) {
            const bool lower = k < 0;
            if (lower && ((y - rebuilt[(-k - 1) * 4 + 2]) & 0xFF) >= 8)
                continue;
            const int index = lower ? rebuilt_first[-k - 1] : found[k];
            const uint8_t *s = lower ? &rebuilt[(-k - 1) * 4] : &sprites[found[k] * 4];
            uint16_t attr = s[0] | (s[1] << 8);
            int sy = s[2], sx = s[3];
            int pal = 8 + ((attr >> 9) & 7);
            const int top = sy >= 0xF0 ? sy - 256 : sy;
            // Palette 15 is also the field's eighth NPC palette (chests, parked vehicles): in field dialogue
            // a hidden window palette never takes a field NPC's sprite with it.
            if (pass_scene_ && ((index < scene_hidden_sprite_prefix && top >= scene_hidden_sprite_min_y) ||
                                ((scene_hidden_sprite_palettes >> pal & 1) &&
                                 !(scene_keep_field_npcs && field_npc_sprite(index)))))
                continue;
            bool outside = attr & 0x1000;
            for (int dx = 0; dx < 8; ++dx) {
                int x = (sx + dx) & 0xFF;
                if (x >= kWidth)
                    continue;
                if (win) {
                    bool inside = in_y && x >= io[0x0C] && x <= io[0x0E];
                    if (inside == outside)
                        continue;
                }
                if (lower && cover[x])
                    continue;
                int idx = pixel(attr & 0x1FF, dx, (y - sy) & 7, attr & 0x4000, attr & 0x8000);
                if (transparent(pal, idx))
                    continue;
                pass_sprite_ = true;
                spr_col[x] = palette_color(pal, idx);
                pass_sprite_ = false;
                spr_on[x] = 1;
                spr_front[x] = (attr & 0x2000) ? 1 : 0;
                if (layers)
                    spr_meta[x] = {spr_col[x],
                                   uint16_t(attr & 0x1FF),
                                   0,
                                   uint8_t(pal),
                                   uint8_t(idx),
                                   uint8_t(index),
                                   uint8_t(RasterPixel::Present | (attr & 0x4000 ? RasterPixel::HFlip : 0) |
                                           (attr & 0x8000 ? RasterPixel::VFlip : 0) |
                                           (win ? RasterPixel::Window : 0)),
                                   int16_t(sx >= 248 ? sx - 256 : sx),
                                   int16_t(sy >= 248 ? sy - 256 : sy)};
            }
        }
    }
    for (int x = 0; x < kWidth; ++x)
        if (spr_on[x] && !spr_front[x]) {
            out[x] = spr_col[x];
            if (text)
                text->pixels[size_t(y * kWidth + x)] = {};
            if (layers)
                layers->planes[spr_front[x] ? FrameLayers::SpriteFront : FrameLayers::SpriteBehind]
                              [size_t(y * kWidth + x)] = spr_meta[x];
        }
    // Keep the actual raster beneath SCR2 for host composition; never alter the native picture.
    auto *underlay = !pass_scene_ ? screen2_underlay_[second ? 1 : 0].data() + y * kWidth : nullptr;
    if (underlay)
        std::copy_n(out, kWidth, underlay);
    if (ctrl & 0x02)
        draw_screen(1, out, nullptr, ctrl & 0x20);
    for (int x = 0; x < kWidth; ++x)
        if (spr_on[x] && spr_front[x]) {
            out[x] = spr_col[x];
            if (text)
                text->pixels[size_t(y * kWidth + x)] = {};
            if (underlay)
                underlay[x] = out[x];
            if (layers)
                layers->planes[spr_front[x] ? FrameLayers::SpriteFront : FrameLayers::SpriteBehind]
                              [size_t(y * kWidth + x)] = spr_meta[x];
        }
    for (const HostCell &c : host_cells) { // over everything
        if (y < c.y || y >= c.y + 8)
            continue;
        const int tile = (c.word & 0x1FF) | (color_mode() && (c.word & 0x2000) ? 0x200 : 0);
        const int pal = (c.word >> 9) & 15;
        for (int dx = 0; dx < 8; ++dx) {
            const int x = c.x + dx;
            if (x < 0 || x >= kWidth)
                continue;
            const int idx = tile_pixel(tile, dx, y - c.y, c.word & 0x4000, c.word & 0x8000);
            if (!transparent(pal, idx)) {
                const bool glyph = tile >= 0x100 && tile < 0x1D5 && (idx == 1 || idx == 2);
                if (scene_text)
                    scene_text[x] = glyph;
                if (text) {
                    auto &p = text->pixels[size_t(y * kWidth + x)];
                    p = {};
                    if (glyph)
                        p = {idx == 2 ? UiText::Stroke : UiText::Shadow, tile_color(pal, idx), out[x]};
                }
                out[x] = tile_color(pal, idx);
                if (underlay)
                    underlay[x] = out[x];
                record(FrameLayers::Host, x, out[x], tile, pal, idx, 0, 255, RasterPixel::HostText);
            }
        }
    }
    if (second)
        second_scene_line(y, out, layers);
}

void Wsc::second_scene_line(int y, uint16_t *out, FrameLayers *layers) {
    const size_t at = size_t(y * kWidth);
    if (second_scene_image.size() < at + kWidth)
        return;
    const uint64_t frame = frame_count - second_motion_start;
    const int offset = frame < second_motion_y.size() ? second_motion_y[size_t(frame)] : 0;
    if (offset > 0 && y >= second_motion_split - offset)
        return; // the original scroll brings the message frame above the split
    if (second_scene_raster) {
        const auto &mapping = *second_scene_raster;
        // Use the same live scanline registers as the native tile planes, including IRQ changes.
        // The style image replaces their art, not their scroll, window or display control.
        for (int x = 0; x < kWidth; ++x) {
            out[x] = 0xFFF;
            for (int plane = 0; plane < 2; ++plane) {
                if (mapping.y[size_t(plane)] < 0 || !(io[0] & (1 << plane)))
                    continue;
                if (plane == 1 && (io[0] & 0x20)) {
                    const bool inside = x >= io[8] && x <= io[10] && y >= io[9] && y <= io[11];
                    if (inside == bool(io[0] & 0x10))
                        continue;
                }
                const int sx = (x + io[size_t(0x10 + plane * 2)]) & 255;
                if (mapping.split >= 0 &&
                    ((plane == 0 && sx >= mapping.split) || (plane == 1 && sx < mapping.split)))
                    continue;
                const int sy = (y + io[size_t(0x11 + plane * 2)] - mapping.y[size_t(plane)]) & 255;
                if (sx < kWidth && size_t(sy * kWidth + sx) < second_scene_image.size()) {
                    const auto color = second_scene_image[size_t(sy * kWidth + sx)];
                    if (color != 0xFFF) // the white GB battlefield is transparent between native planes
                        out[x] = color;
                }
            }
        }
    } else {
        std::copy_n(second_scene_image.begin() + at, kWidth, out);
    }
    if (!layers)
        return;
    for (size_t plane = 0; plane < (second_scene_above_host ? FrameLayers::Count : FrameLayers::Host);
         ++plane)
        std::fill_n(layers->planes[plane].begin() + at, kWidth, RasterPixel{});
    for (int x = 0; x < kWidth; ++x) {
        layers->planes[FrameLayers::Screen1][at + size_t(x)] = {
            out[x], 0, 0, 0, 0, 255, RasterPixel::Present};
        const auto &host = layers->planes[FrameLayers::Host][at + size_t(x)];
        if (!second_scene_above_host && (host.flags & RasterPixel::Present))
            out[x] = host.color; // optional host HP labels remain above the owned battlefield
    }
    layers->valid[size_t(y)] = 1;
}

} // namespace sawgod::emu
