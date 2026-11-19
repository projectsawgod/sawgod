#include "game/world_card_lettering.h"
#include "emu/wsc.h"
#include "game/gb_intro.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sawgod::game {
namespace {
struct Png {
    const unsigned char *bytes;
    size_t size;
};
struct BakedPart {
    Png png;
    std::array<int, 4> ink;
};
#include "world_card_lettering.inc"
} // namespace
const RgbaImage &WorldCardLettering::image(int card, bool enhanced) {
    static const auto images = [] {
        std::array<RgbaImage, 8> result;
        for (size_t i = 0; i < result.size(); ++i) {
            const auto &png = cards[i];
            auto &im = result[i];
            const int scale = i % 2 ? 8 : 1;
            if (!png.size || !decode_rgba_png({png.bytes, png.bytes + png.size}, &im, nullptr) ||
                im.width != 224 * scale || im.height != 144 * scale)
                im = {}; // A missing/bad picture simply leaves the native card alone.
        }
        return result;
    }();
    static const RgbaImage empty;
    return card >= 1 && card <= 4 ? images[size_t((card - 1) * 2 + int(enhanced))] : empty;
}
const WorldCardLettering::Part &WorldCardLettering::part(int card, bool enhanced, int index) {
    static const auto images = [] {
        std::array<Part, 32> result;
        for (size_t i = 0; i < result.size(); ++i) {
            const auto &baked = parts[i];
            auto &p = result[i];
            const int scale = (i / 4) % 2 ? 8 : 1;
            RgbaImage full;
            if (!baked.png.size ||
                !decode_rgba_png({baked.png.bytes, baked.png.bytes + baked.png.size}, &full, nullptr) ||
                full.width != 224 * scale || full.height != 144 * scale)
                continue;
            p.ink = baked.ink;
            int left = full.width, top = full.height, right = 0, bottom = 0;
            for (int y = 0; y < full.height; ++y)
                for (int x = 0; x < full.width; ++x)
                    if (full.pixels[size_t(y * full.width + x)] >> 24) {
                        left = std::min(left, x);
                        top = std::min(top, y);
                        right = std::max(right, x + 1);
                        bottom = std::max(bottom, y + 1);
                    }
            if (right <= left || bottom <= top)
                continue;
            p.x = left;
            p.y = top;
            p.image.width = right - left;
            p.image.height = bottom - top;
            for (int y = top; y < bottom; ++y)
                p.image.pixels.insert(p.image.pixels.end(), full.pixels.begin() + y * full.width + left,
                                      full.pixels.begin() + y * full.width + right);
        }
        return result;
    }();
    static const Part empty;
    return card >= 1 && card <= 4 && index >= 0 && index < 4
               ? images[size_t(((card - 1) * 2 + int(enhanced)) * 4 + index)]
               : empty;
}
void WorldCardLettering::attach(emu::Wsc &w) {
    subscription_.reset();
    generation_ = Session::of(w).generation();
    card_ = 0;
    edges_.fill(0);
    subscription_ = Session::of(w).hooks().subscribe(
        hooks::Event::IntroPresentation, [this](Machine &m, const hooks::Args &a) {
            auto &w = m.wsc();
            const auto generation = Session::of(w).generation();
            if (generation_ != generation) {
                card_ = 0;
                edges_.fill(0);
                generation_ = generation;
            }
            switch (IntroStage(a.a)) {
            case IntroStage::WorldCard:
                card_ = a.b;
                edges_.fill(0);
                fade_ = 0;
                break;
            case IntroStage::WorldCardRow:
                if (a.b >= 0 && a.b < 32)
                    edges_[size_t(a.b)] = std::clamp(a.c, 0, 255);
                break;
            case IntroStage::Fade:
                fade_ = std::clamp(a.b, 0, 15);
                break;
            case IntroStage::Off:
                card_ = 0;
                edges_.fill(0);
                break;
            default:
                break;
            }
        });
}
bool WorldCardLettering::active(emu::Wsc &w) const {
    return generation_ == Session::of(w).generation() && card_ >= 1 && card_ <= 4;
}
float WorldCardLettering::opacity(emu::Wsc &w) const {
    if (!active(w) || !Session::of(w).text().proportional())
        return 0; // Japanese retains the original picture.
    return float(fade_) / 15;
}
std::vector<WorldCardLettering::Rect> WorldCardLettering::slices(bool enhanced, int index) const {
    const auto &p = part(card_, enhanced, index);
    std::vector<Rect> result;
    if (p.image.pixels.empty())
        return result;
    const int cell = 48 + index * 32;
    // The native window hides x >= edge (SCR2 outside-window mode). All 32 rows retain
    // their original RNG order; no sorting, new randomness, frame counter, or independent clock.
    for (int y = p.y; y < p.y + p.image.height; ++y) {
        // Each English pixel follows its own game scanline; outer glow clamps to 48/79.
        const int row = std::clamp(y / (enhanced ? 8 : 1) - 48, 0, 31);
        const int edge = edges_[size_t(row)] - cell;
        if (edge <= 0)
            continue;
        const int right =
            edge >= 32 ? p.x + p.image.width
                       : std::clamp(p.ink[0] + edge * (p.ink[2] - p.ink[0]) / 32, p.x, p.x + p.image.width);
        if (right <= p.x)
            continue;
        if (!result.empty() && result.back().y + result.back().h == y && result.back().w == right - p.x)
            ++result.back().h;
        else
            result.push_back({p.x, y, right - p.x, 1});
    }
    return result;
}
void WorldCardLettering::composite(emu::Wsc &w, std::vector<uint32_t> &rgb, bool enhanced) const {
    const float alpha = opacity(w);
    const int width = enhanced ? 1792 : 224, height = enhanced ? 1152 : 144;
    if (alpha <= 0 || rgb.size() != size_t(width * height))
        return;
    for (int index = 0; index < 4; ++index) {
        const auto &p = part(card_, enhanced, index);
        for (const auto &r : slices(enhanced, index))
            for (int y = r.y; y < r.y + r.h; ++y)
                for (int x = r.x; x < r.x + r.w; ++x) {
                    const uint32_t src = p.image.pixels[size_t((y - p.y) * p.image.width + x - p.x)];
                    auto &dst = rgb[size_t(y * width + x)];
                    const unsigned a = unsigned(std::lround(float(src >> 24) * alpha));
                    if (!a)
                        continue;
                    const auto channel = [&](int shift) {
                        return (((src >> shift & 255) * a + (dst >> shift & 255) * (255 - a) + 127) / 255)
                               << shift;
                    };
                    dst = channel(16) | channel(8) | channel(0);
                }
    }
}
} // namespace sawgod::game
