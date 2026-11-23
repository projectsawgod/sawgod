#include "app/picture_draw.h"

#include "engine/overlay.h"

namespace sawgod::app {

Rect GameRect(int out_w, int out_h, bool whole_scale) {
    float s = std::max(0.25f, std::min(float(out_w) / kNativeWidth, float(out_h) / kNativeHeight));
    if (whole_scale && s >= 1)
        s = std::floor(s);
    const float w = std::round(kNativeWidth * s), h = std::round(kNativeHeight * s);
    return {std::floor((out_w - w) / 2), std::floor((out_h - h) / 2), w, h, s};
}

Rect MenuRect(int out_w, int out_h, bool whole_scale) {
    const Rect g = GameRect(out_w, out_h, whole_scale);
    const float s = g.scale / 2;
    return {std::round(g.x + 16 * g.scale), std::round(g.y + 16 * g.scale),
            std::round(ui::Overlay::kWidth * s), std::round(ui::Overlay::kHeight * s), s};
}

std::vector<uint32_t> BlurredBackdrop(const std::vector<uint32_t> &rgb, int w, int h, int r) {
    std::vector<float> ch[3];
    for (int c = 0; c < 3; ++c) {
        ch[c].resize(rgb.size());
        for (size_t i = 0; i < rgb.size(); ++i)
            ch[c][i] = float((rgb[i] >> (16 - 8 * c)) & 0xFF);
    }
    std::vector<float> tmp(rgb.size());
    auto pass = [&](std::vector<float> &v, bool horizontal) {
        const int n = horizontal ? w : h, lines = horizontal ? h : w;
        for (int l = 0; l < lines; ++l)
            for (int i = 0; i < n; ++i) {
                float sum = 0;
                for (int k = -r; k <= r; ++k) {
                    const int j = std::clamp(i + k, 0, n - 1);
                    sum += horizontal ? v[size_t(l) * w + j] : v[size_t(j) * w + l];
                }
                (horizontal ? tmp[size_t(l) * w + i] : tmp[size_t(i) * w + l]) = sum / float(2 * r + 1);
            }
        v.swap(tmp);
    };
    for (auto &v : ch)
        for (int p = 0; p < 3; ++p) {
            pass(v, true);
            pass(v, false);
        }
    std::vector<uint32_t> out(rgb.size());
    for (size_t i = 0; i < out.size(); ++i) {
        uint32_t px = 0;
        for (int c = 0; c < 3; ++c)
            px |= uint32_t(std::clamp(ch[c][i] * 0.9f + 0.5f, 0.0f, 255.0f)) << (16 - 8 * c);
        out[i] = px;
    }
    return out;
}

} // namespace sawgod::app
