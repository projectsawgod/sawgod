#include "engine/screen.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sawgod {

namespace {

// What the stock screen shows for black and white, how much color survives, and the share of the previous
// picture still visible one game frame (13.25 ms) later.
constexpr float kBlack[3] = {50, 58, 64};
constexpr float kWhite[3] = {206, 212, 198};
constexpr float kSaturation = 0.65f;
constexpr float kContrastGamma = 0.9f; // < 1 lifts the midtones a little (the washed-out look)
constexpr int kPersist = 96;           // of 256

// The WSC has 12-bit color, so a 4096-entry table covers every pixel (keyed by each channel's top nibble).
const std::array<uint32_t, 4096> &table(float amount) {
    static std::array<uint32_t, 4096> t{};
    static float built = -1;
    if (built != amount) {
        built = amount;
        for (int k = 0; k < 4096; ++k) {
            float c[3] = {float((k >> 8) & 15) / 15, float((k >> 4) & 15) / 15, float(k & 15) / 15};
            const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
            uint32_t px = 0;
            for (int i = 0; i < 3; ++i) {
                const float v =
                    std::pow(std::clamp(l + kSaturation * (c[i] - l), 0.0f, 1.0f), kContrastGamma);
                const float lcd = kBlack[i] + (kWhite[i] - kBlack[i]) * v;
                px |= uint32_t(std::lround(c[i] * 255 + amount * (lcd - c[i] * 255))) << (16 - 8 * i);
            }
            t[size_t(k)] = px;
        }
    }
    return t;
}

} // namespace

void fstn_color(std::vector<uint32_t> &rgb, float amount) {
    const auto &t = table(std::clamp(amount, 0.0f, 1.0f));
    for (uint32_t &p : rgb)
        p = t[((p >> 12) & 0xF00) | ((p >> 8) & 0xF0) | ((p >> 4) & 0xF)];
}

FstnWash fstn_wash(float amount) {
    const float a = std::clamp(amount, 0.0f, 1.0f);
    FstnWash w;
    for (int i = 0; i < 3; ++i) {
        w.scale[size_t(i)] = 1 - a + a * (kWhite[i] - kBlack[i]) / 255;
        w.offset[size_t(i)] = a * kBlack[i] / 255;
    }
    return w;
}

void fstn_persist(std::vector<uint32_t> &shown, const std::vector<uint32_t> &frame) {
    if (shown.size() != frame.size()) {
        shown = frame;
        return;
    }
    for (size_t i = 0; i < frame.size(); ++i) {
        uint32_t out = 0;
        for (int s = 0; s < 24; s += 8) {
            const int a = int((shown[i] >> s) & 0xFF), b = int((frame[i] >> s) & 0xFF);
            out |= uint32_t(b + ((a - b) * kPersist) / 256) << s;
        }
        shown[i] = out;
    }
}

// Screen Mode IPS: the look of an IPS-modded WSC, multiplied over the picture as two 1-D masks laid out in
// output pixels: one across (stretched over the picture's height) and one down (stretched over its width).
// Each pixel is split across into red, green and blue stripes over a dark line one output pixel wide at its
// bottom edge (pixels under 3 output pixels get a plain grid, a line at the right and bottom edges). The
// lines sit exactly on the game pixels' edges, with partial coverage at non-whole scales (supersampled), so
// they never beat against the picture's pixels. Strength: the look at 6x (2/3 strength) at every size.
// The stripes take a fixed share of each pixel, so their strength is
// fixed; a 1-pixel line covers 1/scale of a pixel, so its strength scales with the pixel size to darken the
// same share as at 6x (a third of 6x's at 2x, full from 9x).
constexpr float kIpsStrength = 2.0f / 3.0f; // the look at 6x
// The stripes alone, lighter (so the IPS filter doesn't duck the whites too far): white keeps ~91%.
constexpr float kIpsStripeStrength = 0.4f;
float ips_strength(float scale) { return std::clamp(kIpsStrength * scale / 6.0f, 0.0f, 1.0f); }

// The mask along one axis: n output pixels over `pixels` game pixels; `across`: the horizontal one.
std::vector<uint32_t> ips_mask(int n, int pixels, bool across, float amount) {
    const float p = float(n) / float(pixels); // output pixels per game pixel
    const float line_strength = ips_strength(p);
    const bool stripes = p >= 3;
    constexpr int kSub = 16;
    std::vector<uint32_t> m(static_cast<size_t>(n));
    for (int j = 0; j < n; ++j) {
        float rgb[3] = {0, 0, 0}; // the mask's average over this output pixel
        for (int i = 0; i < kSub; ++i) {
            const float u = (float(j) + (float(i) + 0.5f) / kSub) / p; // in game pixels
            const float t = u - std::floor(u);                         // within the game pixel
            const bool line = t >= 1 - 1 / p;
            for (int c = 0; c < 3; ++c) {
                float v = 1;
                if (stripes && across)
                    v = std::min(2, int(t * 3)) == c ? 1.0f : 170 / 255.0f;
                else if (stripes)
                    v = line ? 170 / 255.0f : 1.0f;
                else if (line)
                    v = 140 / 255.0f;
                rgb[c] += v;
            }
        }
        uint32_t px = 0xFF000000u;
        for (int c = 0; c < 3; ++c) {
            const float strength = stripes && across ? kIpsStripeStrength : line_strength;
            const float v = 1 - std::clamp(amount, 0.f, 1.f) * strength * (1 - rgb[c] / kSub);
            px |= uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)) << (16 - 8 * c);
        }
        m[size_t(j)] = px;
    }
    return m;
}

std::vector<uint32_t> ips_picture(const std::vector<uint32_t> &rgb, int w, int h, int scale) {
    const int W = w * scale, H = h * scale;
    const std::vector<uint32_t> across = ips_mask(W, w, true), down = ips_mask(H, h, false);
    std::vector<uint32_t> out(size_t(W) * size_t(H));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint32_t p = rgb[size_t(y / scale) * size_t(w) + size_t(x / scale)];
            uint32_t o = 0;
            for (int s = 0; s < 24; s += 8) {
                const uint32_t v = ((p >> s) & 0xFF) * ((across[size_t(x)] >> s) & 0xFF) / 255 *
                                   ((down[size_t(y)] >> s) & 0xFF) / 255;
                o |= v << s;
            }
            out[size_t(y) * size_t(W) + size_t(x)] = o;
        }
    return out;
}

} // namespace sawgod
