// Presentation-only GB audit capture. No changes to game RAM or art.
#pragma once
#include "emu/wsc.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace sawgod::tools {
// Independent collector provenance, observed from native notifications and palette RAM.
// This does not consult HostPicture's retained pixels or its fade implementation.
class NativePictureFade {
    int depth_ = 0, peak_ = 0, owner_ = 0, submission_ = 0;
    uint64_t epoch_ = 0;
    static int brightness(const emu::Wsc &w) {
        int result = 0;
        for (int palette = 0; palette < 16; ++palette)
            for (int color = 0; color < 16; ++color) {
                const size_t at = size_t(0xFE00 + palette * 32 + color * 2);
                const int rgb = w.ram[at] | w.ram[at + 1] << 8;
                for (int shift : {0, 4, 8})
                    result = std::max(result, rgb >> shift & 15);
            }
        return result;
    }

  public:
    void observe(const emu::Wsc &w, int action) {
        if (action == 3) {
            if (!depth_) {
                peak_ = std::max(1, brightness(w));
                epoch_ = w.presentation_epoch;
            }
            ++depth_;
        } else if (action == 4)
            depth_ = std::max(0, depth_ - 1);
        else if (action == 1 || action == 2 || action == 5)
            submission_ = action;
    }
    std::pair<int, int> level(const emu::Wsc &w, int owner) {
        const bool complete =
            (!w.presentation_epoch ||
             (w.presentation_ready == w.presentation_epoch &&
              std::all_of(w.presentation_raster.begin(), w.presentation_raster.end(),
                          [&](uint64_t epoch) { return epoch == w.presentation_epoch; }))) &&
            !(submission_ == 2 && owner);
        if (!depth_ && peak_ &&
            (brightness(w) >= peak_ || (complete && (epoch_ != w.presentation_epoch || owner_ != owner))))
            peak_ = 0;
        owner_ = owner;
        return peak_ && owner ? std::pair{std::min(peak_, brightness(w)), peak_} : std::pair{15, 15};
    }
};

class GbAudit {
    struct Hit {
        int x = 0, y = 0;
        bool ui_only = true;
        std::array<uint32_t, 64> pixels{};
        std::vector<std::pair<int, int>> spans;
        void add(int frame) {
            if (!spans.empty() && spans.back().second == frame)
                return;
            if (!spans.empty() && spans.back().second + 1 == frame)
                spans.back().second = frame;
            else
                spans.emplace_back(frame, frame);
        }
    };
    // map, host picture, battle animation entry, kind (0=color, 1=unchanged shape), pixels
    using Key = std::tuple<int, int, int, int, std::array<uint32_t, 64>>;
    std::map<Key, Hit> hits_;
    std::map<std::tuple<int, int, int>, Hit> coverage_;
    std::string dir_;
    int every_;
    std::map<std::tuple<int, int, int, bool>, int> sprites_;
    static std::array<int, 64> shape(const std::array<uint32_t, 64> &p) {
        std::array<uint32_t, 64> colors{};
        std::array<int, 64> out{};
        int count = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            int at = 0;
            while (at < count && colors[size_t(at)] != p[i])
                ++at;
            if (at == count)
                colors[size_t(count++)] = p[i];
            out[i] = at;
        }
        return out;
    }
    static bool palette_only(const std::array<uint32_t, 64> &g, const std::array<uint32_t, 64> &w) {
        // A WSC shade may collapse into another GB shade; backgrounds may also invert.
        // Requiring one consistent output per source color tests geometry without requiring a bijection.
        std::array<uint32_t, 16> from{}, to{};
        int count = 0;
        for (size_t i = 0; i < g.size(); ++i) {
            int at = 0;
            while (at < count && from[size_t(at)] != w[i])
                ++at;
            if (at == count) {
                if (count == 16)
                    return false;
                from[size_t(count)] = w[i];
                to[size_t(count++)] = g[i];
            } else if (to[size_t(at)] != g[i])
                return false;
        }
        return count >= 2;
    }

  public:
    explicit GbAudit(const std::string &dir, int every) : dir_(dir), every_(every) {
        std::filesystem::create_directories(dir_);
    }
    void sprite(int map, int set, int pose, bool replaced) { ++sprites_[{map, set, pose, replaced}]; }
    void frame(int frame, int map, int owner, int animation, const std::vector<uint32_t> &gb,
               const std::vector<uint32_t> &wsc, const std::array<uint8_t, 224 * 144> *ui_coverage = nullptr,
               std::pair<int, int> fade = {15, 15}) {
        if ((frame - 1) % every_)
            return;
        coverage_[{map, owner, animation}].add(frame);
        for (int y = 0; y < 144; y += 8)
            for (int x = 0; x < 224; x += 8) {
                std::array<uint32_t, 64> g{}, w{}, grey{};
                bool color = false, ui_only = true;
                for (int j = 0; j < 8; ++j)
                    for (int i = 0; i < 8; ++i) {
                        const int t = j * 8 + i, at = (y + j) * 224 + x + i;
                        uint32_t c = gb[at] & 0xFFFFFF;
                        const uint32_t v = wsc[at] & 0xFFFFFF;
                        // A legitimate host fade has exactly the four GB shades multiplied by
                        // the native level. Recover those exact shades before the ordinary art
                        // and palette checks; chromatic or other neutral output still fails.
                        // Black has no recoverable geometry and is checked as the visible black.
                        if (fade.first > 0 && fade.first < fade.second)
                            for (uint32_t shade : {0u, 85u, 170u, 255u})
                                if (c == (shade * unsigned(fade.first) / unsigned(fade.second)) * 0x010101) {
                                    c = shade * 0x010101;
                                    break;
                                }
                        g[t] = c;
                        w[t] = v;
                        if (c != 0xFFFFFF && (!ui_coverage || !(*ui_coverage)[size_t(at)]))
                            ui_only = false;
                        const auto q = emu::Wsc::gb_grey(
                            uint16_t(((v >> 12) & 0xF00) | ((v >> 8) & 0xF0) | ((v >> 4) & 15)));
                        grey[t] = uint32_t(q & 15) * 17 * 0x010101;
                        color |= c != 0 && c != 0x555555 && c != 0xAAAAAA && c != 0xFFFFFF;
                    }
                const bool varied = std::any_of(g.begin(), g.end(), [&](auto c) { return c != g[0]; });
                // Consistent color remapping catches palette-only changes, including inverted glyphs.
                // Quantized equality also catches WSC colors merged by the sprite greys treatment.
                const bool unchanged = !color && varied && (g == grey || palette_only(g, w));
                for (int kind = 0; kind < 2; ++kind)
                    if (kind ? unchanged : color) {
                        auto key_pixels = g;
                        if (kind ==
                            0) { // group palette fades of the same artwork, retaining all affected frames
                            const auto ids = shape(g);
                            for (size_t i = 0; i < g.size(); ++i)
                                key_pixels[i] = uint32_t(ids[i]);
                        }
                        auto &h = hits_[{map, owner, animation, kind, key_pixels}];
                        if (h.spans.empty()) {
                            h.x = x;
                            h.y = y;
                            h.pixels = g;
                        }
                        h.ui_only &= ui_only;
                        h.add(frame);
                    }
            }
    }
    void finish() const {
        const auto path = dir_ + "/audit.tsv";
        FILE *f = std::fopen(path.c_str(), "w");
        if (!f)
            throw std::runtime_error("cannot write " + path);
        std::fprintf(f, "kind\tmap\towner\tanimation\tx\ty\tsurface\trgb\tframes\tdetector\n");
        auto spans = [&](const Hit &h) {
            for (auto [a, b] : h.spans)
                std::fprintf(f, "%d-%d,", a, b);
            std::fprintf(f, "\tpalette-map-v2\n");
        };
        for (const auto &[k, h] : hits_) {
            const auto &[map, owner, animation, kind, px] = k;
            std::fprintf(f, "%s\t%d\t%d\t%d\t%d\t%d\t", kind ? "shape" : "color", map, owner, animation, h.x,
                         h.y);
            std::fprintf(f, "%s\t", h.ui_only ? "ui" : "scene");
            for (auto c : h.pixels)
                std::fprintf(f, "%06x", c);
            std::fputc('\t', f);
            spans(h);
        }
        for (const auto &[k, h] : coverage_) {
            const auto &[map, owner, animation] = k;
            std::fprintf(f, "coverage\t%d\t%d\t%d\t0\t0\tscene\t\t", map, owner, animation);
            spans(h);
        }
        if (std::fclose(f) != 0)
            throw std::runtime_error("cannot finish " + path);
        f = std::fopen((dir_ + "/sprites.tsv").c_str(), "w");
        if (!f)
            throw std::runtime_error("cannot write sprite audit");
        std::fprintf(f, "map\tset\tpose\treplaced\tcalls\n");
        for (const auto &[key, calls] : sprites_) {
            const auto &[map, set, pose, replaced] = key;
            std::fprintf(f, "%d\t%d\t%d\t%d\t%d\n", map, set, pose, int(replaced), calls);
        }
        if (std::fclose(f) != 0)
            throw std::runtime_error("cannot finish sprite audit");
    }
};
} // namespace sawgod::tools
