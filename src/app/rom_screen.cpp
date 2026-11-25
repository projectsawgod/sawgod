#include "app/rom_screen.h"

#include <algorithm>

namespace sawgod::app {

namespace {

#include "engine/font_silver.inc"

struct SilverGlyph {
    uint32_t cp;
    int advance, x, y, w, h, first;
};
constexpr SilverGlyph kGlyphs[] = {SILVER_GLYPHS_TEXT};
constexpr uint32_t kRows[] = {SILVER_ROWS_TEXT};

constexpr uint32_t kWhite = 0xFFFFFF, kGrey = 0x9A9A9A, kDim = 0x5A5A5A, kHot = 0x2E2E2E, kWarn = 0xFFD27A;

const SilverGlyph *glyph(uint32_t cp) {
    const auto *end = std::end(kGlyphs);
    const auto *g = std::lower_bound(std::begin(kGlyphs), end, cp,
                                     [](const SilverGlyph &a, uint32_t c) { return a.cp < c; });
    return g != end && g->cp == cp ? g : nullptr;
}

std::vector<uint32_t> code_points(const std::string &s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        const int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        uint32_t cp = n == 1 ? c : n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
        for (int k = 1; k < n && i + size_t(k) < s.size(); ++k)
            cp = cp << 6 | (uint8_t(s[i + size_t(k)]) & 0x3F);
        out.push_back(cp);
        i += size_t(n);
    }
    return out;
}

struct Canvas {
    std::vector<uint32_t> px = std::vector<uint32_t>(size_t(kRomScreenW * kRomScreenH), 0);
    void set(int x, int y, uint32_t c) {
        if (x >= 0 && y >= 0 && x < kRomScreenW && y < kRomScreenH)
            px[size_t(y * kRomScreenW + x)] = c;
    }
    void fill(const RomScreenRect &r, uint32_t c, int radius) {
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x)
                if (!corner_cut(r, x, y, radius))
                    set(x, y, c);
    }
    // whether (x, y) lies outside the rounded corner of radius `rad`
    static bool corner_cut(const RomScreenRect &r, int x, int y, int rad) {
        const int dx = x < r.x + rad ? r.x + rad - x : x >= r.x + r.w - rad ? x - (r.x + r.w - rad - 1) : 0;
        const int dy = y < r.y + rad ? r.y + rad - y : y >= r.y + r.h - rad ? y - (r.y + r.h - rad - 1) : 0;
        return dx && dy && dx * dx + dy * dy > rad * rad;
    }
    // A 1 px rounded outline; `dash` > 0 draws it dotted (dash pixels on, dash off along each side).
    void outline(const RomScreenRect &r, uint32_t c, int rad, int dash) {
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) {
                if (corner_cut(r, x, y, rad))
                    continue;
                const bool rim = x == r.x || y == r.y || x == r.x + r.w - 1 || y == r.y + r.h - 1 ||
                                 corner_cut(r, x - 1, y, rad) || corner_cut(r, x + 1, y, rad) ||
                                 corner_cut(r, x, y - 1, rad) || corner_cut(r, x, y + 1, rad);
                if (!rim)
                    continue;
                const bool top_bottom = y == r.y || y == r.y + r.h - 1;
                const bool sides = x == r.x || x == r.x + r.w - 1;
                const int t = top_bottom && !sides ? x : sides && !top_bottom ? y : x + y;
                if (dash <= 0 || (t / dash) % 2 == 0)
                    set(x, y, c);
            }
    }
    // every line 1 px below its chosen position (the default baseline for new layouts)
    void text(int x, int y, const std::string &s, uint32_t c) {
        silver_draw(px, kRomScreenW, kRomScreenH, x, y + 1, s, c);
    }
    void centered(int y, const std::string &s, uint32_t c) {
        text((kRomScreenW - silver_width(s)) / 2, y, s, c);
    }
    void centered_in(const RomScreenRect &r, int y, const std::string &s, uint32_t c) {
        text(r.x + (r.w - silver_width(s)) / 2, y, s, c);
    }
};

void pair(Canvas &cv, int y, const rom_text::Pair &p, uint32_t c) {
    cv.centered(y, p.en, c);
    cv.centered(y + 15, p.ja, c);
}

} // namespace

int silver_width(const std::string &utf8) {
    int w = 0;
    for (uint32_t cp : code_points(utf8))
        if (const SilverGlyph *g = glyph(cp))
            w += g->advance;
    return w;
}

void silver_draw(std::vector<uint32_t> &canvas, int width, int height, int x, int y, const std::string &utf8,
                 uint32_t color) {
    for (uint32_t cp : code_points(utf8)) {
        const SilverGlyph *g = glyph(cp);
        if (!g)
            continue;
        for (int r = 0; r < g->h; ++r)
            for (int c = 0; c < g->w; ++c)
                if (kRows[g->first + r] & (0x80000000u >> c)) {
                    const int px = x + g->x + c, py = y + g->y + r;
                    if (px >= 0 && py >= 0 && px < width && py < height)
                        canvas[size_t(py * width + px)] = color;
                }
        x += g->advance;
    }
}

RomScreenRect rom_screen_logo() { return {130, 12, 188, 72}; } // renders/logo/sawgod_logo_dark@4x.png's shape
RomScreenRect rom_screen_box() { return {96, 121, 256, 46}; }  // vertically centered
RomScreenRect rom_screen_yes() { return {150, 166, 64, 34}; }
RomScreenRect rom_screen_no() { return {234, 166, 64, 34}; }

std::vector<uint32_t> scale_rom_screen_logo(const std::vector<uint32_t> &argb, int sw, int sh, int dw,
                                            int dh) {
    std::vector<uint32_t> out(size_t(std::max(dw, 0)) * size_t(std::max(dh, 0)), 0);
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || argb.size() < size_t(sw) * size_t(sh))
        return out;
    const double fx = double(sw) / dw, fy = double(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        const double y0 = y * fy, y1 = y0 + fy;
        for (int x = 0; x < dw; ++x) {
            const double x0 = x * fx, x1 = x0 + fx;
            double a = 0, r = 0, g = 0, b = 0;
            // every source pixel the destination pixel covers, weighted by the covered area (premultiplied)
            for (int sy = int(y0); sy < sh && sy < y1; ++sy) {
                const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (int sx = int(x0); sx < sw && sx < x1; ++sx) {
                    const double w = wy * (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0));
                    const uint32_t p = argb[size_t(sy) * size_t(sw) + size_t(sx)];
                    const double pa = double(p >> 24) * w;
                    a += pa;
                    r += pa * double(p >> 16 & 0xFF);
                    g += pa * double(p >> 8 & 0xFF);
                    b += pa * double(p & 0xFF);
                }
            }
            const double area = fx * fy;
            const auto ch = [&](double v) {
                return uint32_t(std::clamp(v / area / 255.0 + 0.5, 0.0, 255.0));
            };
            out[size_t(y) * size_t(dw) + size_t(x)] =
                uint32_t(std::clamp(a / area + 0.5, 0.0, 255.0)) << 24 | ch(r) << 16 | ch(g) << 8 | ch(b);
        }
    }
    return out;
}

std::vector<uint32_t> draw_rom_screen(const RomScreenState &s) {
    Canvas cv;
    using Mode = RomScreenState::Mode;
    if (s.mode == Mode::Working) {
        if (s.message) { // setup failed
            pair(cv, 100, *s.message, kWarn);
            if (!s.detail.empty())
                cv.centered(134, s.detail, kGrey);
            return cv.px;
        }
        pair(cv, 104, s.rebuilding ? rom_text::kRebuilding : rom_text::kSettingUp, kWhite);
        const RomScreenRect bar{124, 146, 200, 10};
        cv.outline(bar, kWhite, 0, 0);
        const int fill = int(float(bar.w - 4) * std::clamp(s.progress, 0.0f, 1.0f));
        cv.fill({bar.x + 2, bar.y + 2, fill, bar.h - 4}, kWhite, 0);
        return cv.px;
    }
    if (s.mode == Mode::Found) {
        pair(cv, 118, rom_text::kFoundRom, kWhite);
        const RomScreenRect yes = rom_screen_yes(), no = rom_screen_no();
        for (int i = 0; i < 2; ++i) {
            const RomScreenRect r = i == 0 ? yes : no;
            const bool hot = s.found_choice == i;
            if (hot)
                cv.fill(r, kHot, 3);
            cv.outline(r, hot ? kWhite : kGrey, 3, 0);
            const rom_text::Pair &p = i == 0 ? rom_text::kYes : rom_text::kNo;
            cv.centered_in(r, r.y + 1, p.en, kWhite);
            cv.centered_in(r, r.y + 16, p.ja, kWhite);
        }
        return cv.px;
    }
    // an error under the box, above the footer
    if (s.message) {
        cv.centered(182, std::string("! ") + s.message->en, kWarn);
        cv.centered(197, std::string("! ") + s.message->ja, kWarn);
        if (!s.expected_sha1.empty()) {
            cv.centered(215, std::string(rom_text::kExpectedSha1.en) + " / " + rom_text::kExpectedSha1.ja,
                        kGrey);
            cv.centered(230, s.expected_sha1, kGrey);
        }
    }
    // One box: click it for the picker, or drop a file on it. Dotted at rest; hovered, focused or with a file
    // dragged over it, filled with a solid border and a second ring (visible without relying on color).
    const RomScreenRect box = rom_screen_box();
    const bool hot = s.box_hot || s.drop_hot;
    if (hot)
        cv.fill(box, kHot, 6);
    cv.outline(box, kWhite, 6, hot ? 0 : 2);
    if (hot)
        cv.outline({box.x - 2, box.y - 2, box.w + 4, box.h + 4}, kWhite, 7, 0);
    cv.centered_in(box, box.y + 7, rom_text::kChooseOrDrop.en, kWhite);
    cv.centered_in(box, box.y + 22, rom_text::kChooseOrDrop.ja, kWhite);
    cv.centered(252, rom_text::kWanted.en, kDim);
    cv.centered(267, rom_text::kWanted.ja, kDim);
    return cv.px;
}

} // namespace sawgod::app
