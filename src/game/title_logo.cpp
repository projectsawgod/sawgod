#include "game/title_logo.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace sawgod::game {
namespace {
constexpr unsigned char kKanjiPng[] = {
#include "title_kanji_png.inc"
};
constexpr int W = TitleLogo::kWidth, H = TitleLogo::kHeight, S = TitleLogo::kScale;
constexpr float kGradientSigma = 3.0f; // native pixels
// The strokes' glow, about one native pixel wide and smooth, in the color of the stroke beside it, and the
// letters' thin dark gray drop shadow.
constexpr float kStrokeGlowSigma = S / 2.f; // canvas pixels
constexpr float kStrokeGlowStrength = 0.8f;
constexpr float kLetterShadow[3] = {56, 56, 56};
constexpr int kLetterShadowOffset = S / 2;    // canvas pixels, down and right
constexpr float kLetterShadowSigma = S / 4.f; // canvas pixels

// A W x H plane of floats.
using Plane = std::vector<float>;

float at(const Plane &p, int x, int y) {
    return p[size_t(std::clamp(y, 0, H - 1) * W + std::clamp(x, 0, W - 1))];
}

// Separable Gaussian, weights renormalized at the borders.
Plane blur_sized(const Plane &p, float sigma, const int W, const int H) {
    const int r = int(std::ceil(sigma * 3));
    std::vector<float> k(size_t(2 * r + 1));
    for (int i = -r; i <= r; ++i)
        k[size_t(i + r)] = std::exp(-float(i * i) / (2 * sigma * sigma));
    Plane tmp(p.size()), out(p.size());
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float sum = 0, weight = 0;
            for (int i = -r; i <= r; ++i)
                if (x + i >= 0 && x + i < W) {
                    sum += p[size_t(y * W + x + i)] * k[size_t(i + r)];
                    weight += k[size_t(i + r)];
                }
            tmp[size_t(y * W + x)] = sum / weight;
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float sum = 0, weight = 0;
            for (int i = -r; i <= r; ++i)
                if (y + i >= 0 && y + i < H) {
                    sum += tmp[size_t((y + i) * W + x)] * k[size_t(i + r)];
                    weight += k[size_t(i + r)];
                }
            out[size_t(y * W + x)] = sum / weight;
        }
    return out;
}
Plane blur(const Plane &p, float sigma) { return blur_sized(p, sigma, W, H); }

} // namespace

const RgbaImage &TitleLogo::kanji() {
    static const RgbaImage image = [] {
        RgbaImage out;
        std::string error;
        if (!decode_rgba_png(std::vector<uint8_t>(std::begin(kKanjiPng), std::end(kKanjiPng)), &out, &error))
            std::fprintf(stderr, "title logo: %s\n", error.c_str());
        return out;
    }();
    return image;
}

RgbaImage TitleLogo::build(const std::vector<uint32_t> &native) {
    // The crop, its white lettering (the kanji line and the TM mark) and the strokes.
    std::array<Plane, 3> color;
    for (auto &c : color)
        c.assign(size_t(W * H), 0);
    Plane stroke(size_t(W * H));
    std::vector<uint8_t> unknown(size_t(W * H)), art_dark(size_t(W * H));
    Plane art_luma(size_t(W * H));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint32_t px = native[size_t((kY + y) * 224 + kX + x)];
            const int r = int(px >> 16 & 255), g = int(px >> 8 & 255), b = int(px & 255);
            const int lo = std::min({r, g, b}), hi = std::max({r, g, b});
            const size_t i = size_t(y * W + x);
            color[0][i] = float(r), color[1][i] = float(g), color[2][i] = float(b);
            stroke[i] = hi > 40 ? 1.f : 0.f;
            art_dark[i] = hi <= 40;
            art_luma[i] = float(hi);
            const float white =
                std::clamp((lo - 110) / 90.f, 0.f, 1.f) * std::clamp(1 - (hi - lo) / 90.f, 0.f, 1.f);
            const bool tm = y >= 75 && x >= 148;
            // The kanji line and the TM mark come off; the strokes under them are painted in from around.
            if ((y >= 62 && y < 82 && x >= 15 && x < 162 && lo > 80 && hi - lo < 60) || (tm && white > 0))
                unknown[i] = 1;
        }
    {
        auto grown = unknown; // one pixel around the lettering too: its antialiasing
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (unknown[size_t(y * W + x)])
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (x + dx >= 0 && x + dx < W && y + dy >= 0 && y + dy < H)
                                grown[size_t((y + dy) * W + x + dx)] = 1;
        unknown = std::move(grown);
    }
    const auto painted = unknown;
    for (bool any = true; any;) { // paint in from the known neighbors, ring by ring
        any = false;
        auto next = unknown;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                if (!unknown[i])
                    continue;
                float sum[4] = {}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if ((dx || dy) && nx >= 0 && nx < W && ny >= 0 && ny < H &&
                            !unknown[size_t(ny * W + nx)]) {
                            const size_t j = size_t(ny * W + nx);
                            sum[0] += color[0][j], sum[1] += color[1][j], sum[2] += color[2][j],
                                sum[3] += stroke[j];
                            ++n;
                        }
                    }
                if (n > 0) {
                    color[0][i] = sum[0] / n, color[1][i] = sum[1] / n, color[2][i] = sum[2] / n;
                    stroke[i] = sum[3] / n;
                    next[i] = 0;
                    any = true;
                }
            }
        unknown = std::move(next);
    }
    // Smooth gradients: the strokes' colors averaged inside the strokes. The art's darker rim pixels take the
    // color of the stroke inside them and never tint it (averaged in, they turn the oranges brown).
    Plane luma(size_t(W * H)), near(size_t(W * H));
    std::vector<uint8_t> interior(size_t(W * H)); // not on the strokes' edge: the art's rim can't be here
    {
        for (size_t i = 0; i < luma.size(); ++i)
            luma[i] = std::max({color[0][i], color[1][i], color[2][i]});
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float m = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        m = std::max(m, at(luma, x + dx, y + dy));
                near[size_t(y * W + x)] = m;
            }
        Plane body(size_t(W * H)); // stroke pixels at the stroke's own brightness
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                interior[i] = at(stroke, x - 1, y) > 0 && at(stroke, x + 1, y) > 0 &&
                              at(stroke, x, y - 1) > 0 && at(stroke, x, y + 1) > 0;
                body[i] = stroke[i] > 0 && (interior[i] || luma[i] >= 0.85f * near[i]) ? stroke[i] : 0.f;
            }
        std::array<Plane, 3> smooth;
        const Plane weight = blur(body, kGradientSigma);
        for (int ch = 0; ch < 3; ++ch) {
            Plane weighted(size_t(W * H));
            for (size_t i = 0; i < weighted.size(); ++i)
                weighted[i] = color[size_t(ch)][i] * body[i];
            weighted = blur(weighted, kGradientSigma);
            smooth[size_t(ch)].resize(weighted.size());
            for (size_t i = 0; i < weighted.size(); ++i)
                smooth[size_t(ch)][i] = weight[i] > 0.001f ? weighted[i] / weight[i] : color[size_t(ch)][i];
        }
        // Outside the strokes, carry the nearest stroke color out so the upscaled edge never pulls in black.
        const Plane reach = blur(stroke, 1.5f);
        for (int ch = 0; ch < 3; ++ch) {
            Plane weighted(size_t(W * H));
            for (size_t i = 0; i < weighted.size(); ++i)
                weighted[i] = smooth[size_t(ch)][i] * stroke[i];
            weighted = blur(weighted, 1.5f);
            for (size_t i = 0; i < weighted.size(); ++i)
                color[size_t(ch)][i] = stroke[i] > 0       ? smooth[size_t(ch)][i]
                                       : reach[i] > 0.001f ? weighted[i] / reach[i]
                                                           : 0;
        }
    }
    // The shape is the art's, pixel for pixel: each pixel is the smoothed color at its own brightness against
    // the stroke around it, so the art's dark rim stays as drawn. Under the lettering, a pixel the art leaves
    // dark stays dark and the rest are painted in.
    Plane coverage(size_t(W * H));
    for (size_t i = 0; i < coverage.size(); ++i)
        coverage[i] = art_dark[i]
                          ? (painted[i] ? 0.f : std::clamp(art_luma[i] / std::max(near[i], 1.f), 0.f, 1.f))
                      : painted[i] || interior[i] ? std::max(stroke[i], float(interior[i]))
                                                  : std::clamp(luma[i] / std::max(near[i], 1.f), 0.f, 1.f);
    // What can't reach the canvas's border without crossing a stroke is a gap inside the letters: no glow
    // there.
    std::vector<uint8_t> outside(coverage.size());
    {
        std::vector<int> open;
        const auto visit = [&](int x, int y) {
            const size_t i = size_t(y * W + x);
            if (x >= 0 && x < W && y >= 0 && y < H && !outside[i] && coverage[i] < 0.5f)
                outside[i] = 1, open.push_back(int(i));
        };
        for (int x = 0; x < W; ++x)
            visit(x, 0), visit(x, H - 1);
        for (int y = 0; y < H; ++y)
            visit(0, y), visit(W - 1, y);
        while (!open.empty()) {
            const int i = open.back();
            open.pop_back();
            const int x = i % W, y = i / W;
            visit(x - 1, y), visit(x + 1, y), visit(x, y - 1), visit(x, y + 1);
        }
    }
    // Nor in a slit, stroke on both sides.
    std::vector<uint8_t> between(coverage.size());
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool left = false, right = false;
            for (int d = 1; d <= 4; ++d)
                left = left || at(coverage, x - d, y) >= 0.5f,
                right = right || at(coverage, x + d, y) >= 0.5f;
            between[size_t(y * W + x)] = left && right;
        }
    const RgbaImage &letters = kanji();
    RgbaImage out;
    out.width = W * S, out.height = H * S;
    out.pixels.resize(size_t(out.width) * size_t(out.height));
    // The glow: the strokes' pixels softened at full resolution, kept out of the gaps and slits.
    Plane stroke_glow(out.pixels.size());
    for (int Y = 0; Y < out.height; ++Y)
        for (int X = 0; X < out.width; ++X)
            stroke_glow[size_t(Y) * size_t(out.width) + size_t(X)] = coverage[size_t((Y / S) * W + X / S)];
    stroke_glow = blur_sized(stroke_glow, kStrokeGlowSigma, out.width, out.height);
    Plane glow_open(coverage.size());
    for (size_t i = 0; i < glow_open.size(); ++i)
        glow_open[i] = outside[i] && !between[i] ? 1.f : 0.f;
    const auto bilinear = [](const Plane &p, float sx, float sy) {
        const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
        const float fx = sx - float(x0), fy = sy - float(y0);
        return (at(p, x0, y0) * (1 - fx) + at(p, x0 + 1, y0) * fx) * (1 - fy) +
               (at(p, x0, y0 + 1) * (1 - fx) + at(p, x0 + 1, y0 + 1) * fx) * fy;
    };
    Plane letter_shadow(out.pixels.size());
    if (letters.width == out.width && letters.height == out.height) {
        for (int y = kLetterShadowOffset; y < out.height; ++y)
            for (int x = kLetterShadowOffset; x < out.width; ++x)
                letter_shadow[size_t(y) * size_t(out.width) + size_t(x)] =
                    float(letters.pixels[size_t(y - kLetterShadowOffset) * size_t(out.width) +
                                         size_t(x - kLetterShadowOffset)] >>
                          24) /
                    255;
        letter_shadow = blur_sized(letter_shadow, kLetterShadowSigma, out.width, out.height);
    }
    for (int Y = 0; Y < out.height; ++Y) {
        // The last eight native rows fade into the native picture under the canvas.
        const float fade = std::clamp((H * S - 0.5f - Y) / float(8 * S), 0.f, 1.f);
        for (int X = 0; X < out.width; ++X) {
            // The strokes stay on the WSC's own pixels; their glow is smooth, behind them.
            const size_t native_i = size_t((Y / S) * W + X / S);
            const float edge = coverage[native_i];
            const float sx = (X + 0.5f) / S - 0.5f, sy = (Y + 0.5f) / S - 0.5f;
            const float glow = std::min(stroke_glow[size_t(Y) * size_t(out.width) + size_t(X)] * 2, 1.f) *
                               kStrokeGlowStrength * bilinear(glow_open, sx, sy);
            float rgb[3];
            for (int ch = 0; ch < 3; ++ch)
                rgb[ch] = std::clamp(color[size_t(ch)][native_i], 0.f, 255.f) * edge +
                          std::clamp(bilinear(color[size_t(ch)], sx, sy), 0.f, 255.f) * glow * (1 - edge);
            // The re-lettered kanji line and TM mark in white over it.
            const size_t li = size_t(Y) * size_t(letters.width) + size_t(X);
            float white = letters.pixels.empty() || X >= letters.width || Y >= letters.height
                              ? 0.f
                              : float(letters.pixels[li] >> 24) / 255;
            // The letters' shadow goes over the strokes, the letters over it.
            const float cast_by_letters = letter_shadow[size_t(Y) * size_t(out.width) + size_t(X)];
            for (int ch = 0; ch < 3; ++ch)
                rgb[ch] += (kLetterShadow[ch] - rgb[ch]) * cast_by_letters;
            for (float &v : rgb)
                v += (255 - v) * white;
            out.pixels[size_t(Y) * size_t(out.width) + size_t(X)] =
                uint32_t(std::lround(fade * 255)) << 24 | uint32_t(std::lround(rgb[0])) << 16 |
                uint32_t(std::lround(rgb[1])) << 8 | uint32_t(std::lround(rgb[2]));
        }
    }
    return out;
}
} // namespace sawgod::game
