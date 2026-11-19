#include "engine/menu_fill.h"
#include "engine/presentation_diagnostics.h"
#include <algorithm>
#include <cmath>

namespace sawgod {
bool MenuFill::draw(SDL_Renderer *renderer, const SDL_FRect &dst, const emu::Wsc::UiFill &fill,
                    std::span<const uint32_t> picture, float weight, const emu::Wsc::UiText *text,
                    const UiDrawCache *prepared) {
    const auto start = SDL_GetTicksNS();
    quads = 0;
    if (weight <= 0 || fill.windows.empty() || picture.size() != fill.owner.size()) {
        cpu_ns = SDL_GetTicksNS() - start;
        return true;
    }
    if (prepared ? cache_.accept(*prepared)
                 : cache_.update(dst, &fill, text, picture, std::clamp(weight, 0.f, 1.f))) {
        ++rebuilds;
        vertices_.clear();
        const auto edge_x = [&](int x) { return std::floor(dst.x + x * dst.w / 224); };
        const auto edge_y = [&](int y) { return std::floor(dst.y + y * dst.h / 144); };
        const auto beneath = [&](size_t at) {
            if (text && text->bars.visible(at, picture.data()) && text->bars.pixels[at].shadow)
                return text->bars.pixels[at].beneath;
            return text ? text->pixels[at].beneath : picture[at];
        };
        const auto shadow = [&](size_t at) {
            return text &&
                   (text->visible(at, picture.data()) == emu::Wsc::UiText::Shadow ||
                    (text->bars.visible(at, picture.data()) && text->bars.pixels[at].shadow)) &&
                   fill.visible(at, beneath(at));
        };
        const auto visible = [&](size_t at) {
            return fill.visible(at, shadow(at) ? beneath(at) : picture[at]);
        };
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 224;) {
                const auto owner = visible(size_t(y * 224 + x));
                if (!owner) {
                    ++x;
                    continue;
                }
                const size_t at = size_t(y * 224 + x);
                const bool under_text = shadow(at);
                const auto &window = fill.windows[owner - 1];
                // A translucent window's quad color carries the picture beneath it (below), so a run may
                // only span pixels with that same picture; otherwise the run's first pixel smears across it.
                const bool translucent = window.alpha < 100;
                const int left = x++;
                while (x < 224 && visible(size_t(y * 224 + x)) == owner &&
                       shadow(size_t(y * 224 + x)) == under_text &&
                       (!under_text || beneath(size_t(y * 224 + x)) == beneath(at)) &&
                       (!translucent || fill.beneath[size_t(y * 224 + x)] == fill.beneath[at]))
                    ++x;
                const float l = edge_x(left), r = edge_x(x), t = edge_y(y), b = edge_y(y + 1);
                if (r <= l || b <= t || (window.alpha == 0 && !under_text))
                    continue;
                const auto color = [&](float output_y) {
                    // Interpolate at the rounded output edges, not at native row centers. Adjacent quads
                    // therefore share both their boundary and color, even at fractional scales.
                    const float top = edge_y(window.top), bottom = edge_y(window.bottom);
                    const float f =
                        bottom > top ? std::clamp((output_y - top) / (bottom - top), 0.f, 1.f) : 0;
                    const auto channel = [&](int shift) {
                        const float light = float(window.light >> shift & 255);
                        const float gradient = light + (float(window.dark >> shift & 255) - light) * f;
                        // Replace the tile with gradient-over-the-original-background once. Blending
                        // again over the already translucent tile would make an 80% plate 96% opaque.
                        return (gradient * float(window.alpha) / 100 +
                                float(fill.beneath[at] >> shift & 255) * (1 - float(window.alpha) / 100)) /
                               255;
                    };
                    return SDL_FColor{channel(16), channel(8), channel(0), std::clamp(weight, 0.f, 1.f)};
                };
                const auto ct = color(t), cb = color(b);
                const SDL_Vertex tl{{l, t}, ct, {}}, tr{{r, t}, ct, {}}, bl{{l, b}, cb, {}},
                    br{{r, b}, cb, {}};
                vertices_.insert(vertices_.end(), {tl, tr, bl, tr, br, bl});
                ++quads;
            }
    }
    if (auto &t = presentation_diagnostics::trace(); t.enabled()) {
        presentation_diagnostics::Hash h;
        for (const auto &v : vertices_) {
            h.add(v.position.x);
            h.add(v.position.y);
            h.add(v.color.r);
            h.add(v.color.g);
            h.add(v.color.b);
            h.add(v.color.a);
            h.add(v.tex_coord.x);
            h.add(v.tex_coord.y);
        }
        t.write("MenuFill.geometry", h);
    }
    if (presentation_diagnostics::cpu_only())
        return true;
    quads = vertices_.size() / 6;
    SDL_BlendMode previous;
    bool ok = SDL_GetRenderDrawBlendMode(renderer, &previous);
    if (ok) {
        ok = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        if (ok && !vertices_.empty())
            ok = SDL_RenderGeometry(renderer, nullptr, vertices_.data(), int(vertices_.size()), nullptr, 0);
        ok = SDL_SetRenderDrawBlendMode(renderer, previous) && ok;
    }
    cpu_ns = SDL_GetTicksNS() - start;
    return ok;
}
} // namespace sawgod
