#include "engine/startup_timing.h"
#include "engine/hd_menus.h"
#include "engine/cpu_scope.h"
#include "engine/presentation_blend.h"
#include "engine/presentation_diagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sawgod {
namespace {
// Integral of a unit box convolved with N(0, .45^2), evaluated at a pixel center.
float box(float x) {
    constexpr float s = .45f * 1.41421356237f;
    return .5f * (std::erf(x / s) - std::erf((x - 1) / s));
}
void quad(std::vector<SDL_Vertex> &v, float l, float t, float r, float b, SDL_FColor c,
          bool textured = false) {
    if (r <= l || b <= t)
        return;
    const SDL_Vertex tl{{l, t}, c, {0, 0}}, tr{{r, t}, c, {textured ? 1.f : 0.f, 0}},
        bl{{l, b}, c, {0, textured ? 1.f : 0.f}}, br{{r, b}, c, {textured ? 1.f : 0.f, textured ? 1.f : 0.f}};
    v.insert(v.end(), {tl, tr, bl, tr, br, bl});
}
// Integral of a Gaussian CDF: lets fractional output pixels use area coverage, including blur tails.
float gaussian_integral(float x, float sigma) {
    constexpr float inv_sqrt_2pi = .3989422804f;
    return .5f * x * (1 + std::erf(x / (sigma * 1.41421356237f))) +
           sigma * inv_sqrt_2pi * std::exp(-.5f * x * x / (sigma * sigma));
}
float blurred_span(float a, float b, float left, float right, float sigma) {
    const auto integral = [&](float x) {
        return gaussian_integral(x - left, sigma) - gaussian_integral(x - right, sigma);
    };
    return std::max(0.f, integral(b) - integral(a));
}
SDL_FColor color(uint32_t c, float alpha) {
    return {float(c >> 16 & 255) / 255, float(c >> 8 & 255) / 255, float(c & 255) / 255, alpha};
}
} // namespace
void HdMenus::shutdown() {
    SDL_DestroyTexture(splat_);
    for (auto &[ink, texture] : masks_)
        SDL_DestroyTexture(texture);
    masks_.clear();
    for (auto *texture : spare_masks_)
        SDL_DestroyTexture(texture);
    spare_masks_.clear();
    for (auto &[key, shadow] : bar_shadows_)
        SDL_DestroyTexture(shadow.texture);
    bar_shadows_.clear();
    splat_ = nullptr;
    cache_.invalidate();
    width_ = height_ = 0;
}
void HdMenus::prepare(SDL_Renderer *renderer) {
    sawgod::startup::Stage timing("HD menu atlases");
    int width = 0, height = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height) || (width == width_ && height == height_))
        return;
    shutdown();
    width_ = width;
    height_ = height;
    software_ = std::strcmp(SDL_GetRendererName(renderer), "software") == 0;
    // Text uses two inks (neutral, and dark sage for HP numbers). Allocate their masks before any menu
    // opens rather than in the frame that first shows one.
    for (int i = 0; i < 2; ++i)
        if (auto *mask = new_mask(renderer))
            spare_masks_.push_back(mask);
}
SDL_Texture *HdMenus::new_mask(SDL_Renderer *renderer) {
    if (!spare_masks_.empty()) {
        auto *mask = spare_masks_.back();
        spare_masks_.pop_back();
        return mask;
    }
    auto *mask = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                   software_ ? SDL_TEXTUREACCESS_STREAMING : SDL_TEXTUREACCESS_TARGET, width_,
                                   height_);
    if (!mask)
        return nullptr;
    SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(mask, SDL_SCALEMODE_NEAREST);
    if (!software_) {
        // A target's first use pays its own setup; pay it with the allocation.
        auto *target = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, mask)) {
            SDL_RenderClear(renderer);
            SDL_SetRenderTarget(renderer, target);
        }
    }
    return mask;
}
bool HdMenus::draw(SDL_Renderer *renderer, const SDL_FRect &dst, const emu::Wsc::UiText &text,
                   const emu::Wsc::UiFill *fill, std::span<const uint32_t> picture, float weight,
                   std::span<const uint32_t> blended, const UiDrawCache *prepared) {
    CpuScope cpu_scope("HdMenus::draw");
    const auto start = SDL_GetTicksNS();
    cpu_ns = 0;
    strokes = 0;
    draws = 0;
    if (presentation_diagnostics::fresh())
        shutdown();
    prepare(renderer);
    if (!width_)
        return false;
    const int width = width_, height = height_;
    if (weight <= 0 || dst.w <= 0 || dst.h <= 0 || picture.size() != text.pixels.size())
        return true;
    weight = std::clamp(weight, 0.f, 1.f);
    const float sx = dst.w / 224, sy = dst.h / 144;
    const auto ex = [&](int x) { return std::floor(dst.x + x * sx); };
    const auto ey = [&](int y) { return std::floor(dst.y + y * sy); };
    const bool rebuild =
        prepared ? cache_.accept(*prepared) : cache_.update(dst, fill, &text, picture, weight, blended);
    if (rebuild) {
        ++rebuilds;
        for (auto &[ink, vertices] : splats_)
            vertices.clear();
        for (auto &[key, shadow] : bar_shadows_)
            shadow.vertices.clear();
        restore_.clear();
        glyphs_.clear();
        corners_.clear();
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 224; ++x) {
                const size_t at = size_t(y * 224 + x);
                const auto kind = text.visible(at, picture.data());
                if (kind == emu::Wsc::UiText::None)
                    continue;
                const auto &p = text.pixels[at];
                if (kind == emu::Wsc::UiText::FrameCorner) {
                    if (!p.corner || p.corner > text.corners.size())
                        continue;
                    const auto &corner = text.corners[p.corner - 1];
                    const float left = ex(x), right = ex(x + 1), top = ey(y), bottom = ey(y + 1);
                    if (right <= left || bottom <= top)
                        continue;
                    // Integrate the native pipe's rounded join at output resolution. Straight ends retain
                    // their floor-rounded native edges, including fractional window scales.
                    for (int oy = int(top); oy < int(bottom); ++oy)
                        for (int ox = int(left); ox < int(right); ++ox) {
                            std::array<float, 3> rgb{};
                            for (int yy = 0; yy < 4; ++yy)
                                for (int xx = 0; xx < 4; ++xx) {
                                    const float u0 =
                                        x - corner.x + (ox + (xx + .5f) / 4 - left) / (right - left);
                                    const float u = corner.mirror ? 7 - u0 : u0;
                                    const float v =
                                        y - corner.y + (oy + (yy + .5f) / 4 - top) / (bottom - top);
                                    const float distance = v < 4.5f   ? 2.5f - u
                                                           : u > 2.5f ? v - 4.5f
                                                                      : std::hypot(u - 2.5f, v - 4.5f);
                                    const bool stroke = distance >= 1.5f && distance < 2.5f;
                                    // The black bevel belongs to the left tab's right edge only. A right
                                    // tab's left edge meets the page with no black, as its native slice
                                    // (white, then gray beyond the bend) shows.
                                    const bool bevel =
                                        !corner.mirror && distance >= .5f && distance < 1.5f && u < 2.5f;
                                    const bool shadow = distance >= 2.5f && distance < 3.5f;
                                    const auto ink = stroke  ? corner.stroke
                                                     : bevel ? corner.bevel
                                                             : corner.shadow;
                                    const float f =
                                        std::clamp((oy + (yy + .5f) / 4 - ey(corner.top)) /
                                                       std::max(1.f, ey(corner.bottom) - ey(corner.top)),
                                                   0.f, 1.f);
                                    for (int i = 0; i < 3; ++i) {
                                        const int shift = 16 - i * 8;
                                        const float light = float(corner.light >> shift & 255);
                                        const float background =
                                            p.corner_scene
                                                ? float(p.beneath >> shift & 255)
                                                : light + (float(corner.dark >> shift & 255) - light) * f;
                                        rgb[size_t(i)] +=
                                            ((stroke || bevel || shadow) ? float(ink >> shift & 255)
                                                                         : background) /
                                            16 / 255;
                                    }
                                }
                            quad(corners_, float(ox), float(oy), float(ox + 1), float(oy + 1),
                                 {rgb[0], rgb[1], rgb[2], weight});
                        }
                    continue;
                }
                if (kind == emu::Wsc::UiText::Shadow) {
                    // Menu Fill has already replaced these shadows with its gradient over the beneath color.
                    if (fill && fill->visible(at, p.beneath))
                        continue;
                    quad(restore_, ex(x), ey(y), ex(x + 1), ey(y + 1), color(p.beneath, weight));
                } else {
                    if (p.casts_shadow) {
                        quad(splats_[p.shadow_color], dst.x + (x - 1.25f) * sx, dst.y + (y - 1.25f) * sy,
                             dst.x + (x + 3.75f) * sx, dst.y + (y + 3.75f) * sy, {1, 1, 1, 1}, true);
                        ++strokes;
                    }
                    // Same shared-edge rounding as ProtectedRegions. Only the shadow fades: glyphs stay
                    // exact.
                    quad(glyphs_, ex(x), ey(y), ex(x + 1), ey(y + 1),
                         color(blended.size() == picture.size() ? blended[at] : p.color, 1));
                }
            }
        const auto &bars = text.bars;
        for (size_t i = 0; i < bars.rectangles.size(); ++i) {
            const auto &bar = bars.rectangles[i];
            const float l = ex(bar.x), r = ex(bar.x + bar.width), t = ey(bar.y), b = ey(bar.y + bar.height);
            if (r <= l || b <= t)
                continue;
            // The bar's floor-rounded edges are integers. Rasterize its separable, area-integrated blur
            // once per box size/scale, at output resolution, including the outside-only cutout.
            const int pad_x = int(std::ceil(3 * sx)), pad_y = int(std::ceil(3 * sy));
            const int tw = int(r - l) + 2 * pad_x, th = int(b - t) + 2 * pad_y;
            auto &shadow = bar_shadows_[{r - l, b - t, sx, sy}];
            if (!shadow.texture) {
                std::vector<float> xs(size_t(tw), 0), ys(size_t(th), 0);
                for (int x = 0; x < tw; ++x)
                    xs[size_t(x)] = blurred_span(float(x), float(x + 1), pad_x + .5f * sx,
                                                 pad_x + r - l + .5f * sx, .35f * sx);
                for (int y = 0; y < th; ++y)
                    ys[size_t(y)] = blurred_span(float(y), float(y + 1), pad_y + .5f * sy,
                                                 pad_y + b - t + .5f * sy, .35f * sy);
                std::vector<uint32_t> pixels(size_t(tw * th), 0x111111);
                for (int y = 0; y < th; ++y)
                    for (int x = 0; x < tw; ++x) {
                        if (x >= pad_x && x < tw - pad_x && y >= pad_y && y < th - pad_y)
                            continue;
                        const float a = .85f * std::clamp(xs[size_t(x)] * ys[size_t(y)], 0.f, 1.f);
                        if (a > .0001f)
                            pixels[size_t(y * tw + x)] |= uint32_t(std::lround(a * 255)) << 24;
                    }
                shadow.texture =
                    SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
                if (!shadow.texture || !SDL_UpdateTexture(shadow.texture, nullptr, pixels.data(), tw * 4) ||
                    !SDL_SetTextureBlendMode(shadow.texture, SDL_BLENDMODE_BLEND) ||
                    !SDL_SetTextureScaleMode(shadow.texture, SDL_SCALEMODE_NEAREST)) {
                    SDL_DestroyTexture(shadow.texture);
                    shadow.texture = nullptr;
                    cache_.invalidate();
                    return false;
                }
            }
            // Native runs both remove the hard shadow and clip the blur to surviving bar provenance.
            // Their floor-rounded boundaries exactly match the old output-pixel ownership test.
            std::map<std::pair<float, float>, size_t> above;
            for (int y = std::max(0, bar.y - 4); y < std::min(144, bar.y + bar.height + 4); ++y) {
                std::map<std::pair<float, float>, size_t> row;
                for (int x = std::max(0, bar.x - 4); x < std::min(224, bar.x + bar.width + 4);) {
                    const size_t at = size_t(y * 224 + x);
                    if (bars.visible(at, picture.data()) != i + 1) {
                        ++x;
                        continue;
                    }
                    const auto &p = bars.pixels[at];
                    const bool restore = p.shadow && !(fill && fill->visible(at, p.beneath));
                    const int first = x++;
                    while (x < std::min(224, bar.x + bar.width + 4)) {
                        const size_t next = size_t(y * 224 + x);
                        const auto &q = bars.pixels[next];
                        if (bars.visible(next, picture.data()) != i + 1 || q.beneath != p.beneath ||
                            (q.shadow && !(fill && fill->visible(next, q.beneath))) != restore)
                            break;
                        ++x;
                    }
                    if (restore)
                        quad(restore_, ex(first), ey(y), ex(x), ey(y + 1), color(p.beneath, weight));
                    const float left = std::max(ex(first), l - pad_x), right = std::min(ex(x), r + pad_x),
                                top = std::max(ey(y), t - pad_y), bottom = std::min(ey(y + 1), b + pad_y);
                    if (right <= left || bottom <= top)
                        continue;
                    const auto key = std::pair{left, right};
                    const auto previous = above.find(key);
                    size_t first_vertex = shadow.vertices.size();
                    if (previous != above.end() && shadow.vertices[previous->second + 2].position.y == top) {
                        first_vertex = previous->second;
                        for (size_t j : {2u, 4u, 5u})
                            shadow.vertices[first_vertex + j].position.y = bottom;
                    } else {
                        quad(shadow.vertices, left, top, right, bottom, {1, 1, 1, bar.opacity});
                    }
                    row[key] = first_vertex;
                    for (size_t j = first_vertex; j < first_vertex + 6; ++j) {
                        auto &v = shadow.vertices[j];
                        v.tex_coord = {(v.position.x - (l - pad_x)) / tw, (v.position.y - (t - pad_y)) / th};
                    }
                }
                above = std::move(row);
            }
        }
        // Palette fades and layout changes must not accumulate full-output masks or old bar sizes:
        // an unused ink's mask returns to the spares, and the next ink to need one redraws it.
        for (auto it = masks_.begin(); it != masks_.end();) {
            if (splats_[it->first].empty()) {
                spare_masks_.push_back(it->second);
                it = masks_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = splats_.begin(); it != splats_.end();)
            if (it->second.empty())
                it = splats_.erase(it);
            else
                ++it;
        for (auto it = bar_shadows_.begin(); it != bar_shadows_.end();) {
            if (it->second.vertices.empty()) {
                SDL_DestroyTexture(it->second.texture);
                it = bar_shadows_.erase(it);
            } else {
                ++it;
            }
        }
        cached_strokes_ = strokes;
    }
    if (auto &t = presentation_diagnostics::trace(); t.enabled()) {
        presentation_diagnostics::Hash h;
        const auto vertices = [&](const auto &vs) {
            h.add(vs.size());
            for (const auto &v : vs) {
                h.add(v.position.x);
                h.add(v.position.y);
                h.add(v.color.r);
                h.add(v.color.g);
                h.add(v.color.b);
                h.add(v.color.a);
                h.add(v.tex_coord.x);
                h.add(v.tex_coord.y);
            }
        };
        vertices(restore_);
        vertices(glyphs_);
        vertices(corners_);
        for (const auto &[ink, vs] : splats_)
            if (!vs.empty()) {
                h.add(ink);
                vertices(vs);
            }
        for (const auto &[key, bar] : bar_shadows_)
            if (!bar.vertices.empty()) {
                h.add(key);
                vertices(bar.vertices);
            }
        t.write("HdMenus.geometry", h);
    }
    if (presentation_diagnostics::cpu_only())
        return true;
    strokes = cached_strokes_;
    bool has_edges = false;
    for (const auto &[key, shadow] : bar_shadows_)
        has_edges |= !shadow.vertices.empty();
    if (restore_.empty() && corners_.empty() && !strokes && !has_edges) {
        cpu_ns = SDL_GetTicksNS() - start;
        return true;
    }
    SDL_BlendMode previous;
    Uint8 red, green, blue, alpha;
    if (!SDL_GetRenderDrawBlendMode(renderer, &previous) ||
        !SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha))
        return false;
    bool ok = true;
    ok = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) && ok;
    if (ok && !restore_.empty()) {
        ok = SDL_RenderGeometry(renderer, nullptr, restore_.data(), int(restore_.size()), nullptr, 0);
        ++draws;
    }
    if (ok && !corners_.empty()) {
        ok = SDL_RenderGeometry(renderer, nullptr, corners_.data(), int(corners_.size()), nullptr, 0);
        ++draws;
    }
    for (const auto &[key, shadow] : bar_shadows_) {
        if (ok && !shadow.vertices.empty()) {
            ok = SDL_SetTextureAlphaModFloat(shadow.texture, weight) &&
                 SDL_RenderGeometry(renderer, shadow.texture, shadow.vertices.data(),
                                    int(shadow.vertices.size()), nullptr, 0);
            ++draws;
        }
    }
    // Each ink shares one mask pass: ordinary text stays neutral, HP numbers use dark sage.
    for (const auto &[shadow_color, splats] : splats_) {
        if (!ok)
            break;
        if (splats.empty())
            continue;
        auto *&mask = masks_[shadow_color];
        if (!mask) {
            mask = new_mask(renderer);
            if (!mask) {
                masks_.erase(shadow_color);
                cache_.invalidate();
                return false;
            }
        }
        if (rebuild) {
            if (software_) {
                // SDL's software backend has no custom additive-alpha blend. The same analytic splats
                // provide a deterministic fallback; live GPU rendering uses one batched geometry draw.
                coverage_.assign(size_t(width * height), 0);
                pixels_.resize(coverage_.size());
                // Many glyph pixels share a column or row. Evaluate their analytic blur kernel once
                // per position, not an erf for every output pixel of every splat. Keep the same
                // floating-point operations and accumulation order so the fallback picture is exact.
                struct Kernel {
                    int first;
                    std::vector<float> values;
                };
                std::map<float, Kernel> columns, rows;
                const auto kernel = [](auto &cache, float start, float scale, int extent) -> const Kernel & {
                    auto [it, inserted] = cache.try_emplace(start);
                    if (inserted) {
                        auto &k = it->second;
                        k.first = std::max(0, int(std::floor(start)));
                        const int end = std::min(extent, int(std::ceil(start + 5 * scale)));
                        const float origin = start + 2 * scale;
                        for (int p = k.first; p < end; ++p)
                            k.values.push_back(box((float(p) + .5f - origin) / scale));
                    }
                    return it->second;
                };
                for (size_t i = 0; i < splats.size(); i += 6) {
                    const auto &xs = kernel(columns, splats[i].position.x, sx, width);
                    const auto &ys = kernel(rows, splats[i].position.y, sy, height);
                    for (size_t y = 0; y < ys.values.size(); ++y) {
                        const float cy = ys.values[y] * 1.125f;
                        for (size_t x = 0; x < xs.values.size(); ++x)
                            coverage_[size_t((ys.first + int(y)) * width + xs.first + int(x))] +=
                                xs.values[x] * cy;
                    }
                }
                for (size_t i = 0; i < pixels_.size(); ++i)
                    pixels_[i] =
                        uint32_t(std::lround(std::min(1.f, coverage_[i]) * 255)) << 24 | shadow_color;
                ok = SDL_UpdateTexture(mask, nullptr, pixels_.data(), width * 4);
            } else {
                if (!splat_) {
                    constexpr int n = 128;
                    std::vector<uint32_t> pixels(n * n);
                    for (int y = 0; y < n; ++y)
                        for (int x = 0; x < n; ++x) {
                            const float a = box(-2 + (float(x) + .5f) * 5 / n) *
                                            box(-2 + (float(y) + .5f) * 5 / n) * 1.125f;
                            pixels[size_t(y * n + x)] = uint32_t(std::lround(a * 255)) << 24 | 0xFFFFFF;
                        }
                    splat_ =
                        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, n, n);
                    if (!splat_)
                        return false;
                    ok = SDL_UpdateTexture(splat_, nullptr, pixels.data(), n * 4) &&
                         SDL_SetTextureScaleMode(splat_, SDL_SCALEMODE_LINEAR) &&
                         SDL_SetTextureBlendMode(splat_, mask_alpha_blend());
                }
                if (!ok) {
                    SDL_DestroyTexture(splat_);
                    splat_ = nullptr;
                }
                auto *target = SDL_GetRenderTarget(renderer);
                if (ok)
                    ok = SDL_SetRenderTarget(renderer, mask);
                if (ok)
                    ok = SDL_SetRenderDrawColor(renderer, Uint8(shadow_color >> 16), Uint8(shadow_color >> 8),
                                                Uint8(shadow_color), 0) &&
                         SDL_RenderClear(renderer);
                if (ok)
                    ok = SDL_RenderGeometry(renderer, splat_, splats.data(), int(splats.size()), nullptr, 0);
                ++draws;
                ok = SDL_SetRenderTarget(renderer, target) && ok;
            }
        }
        // Keep blur tails inside the game picture, including letterboxed/fractional layouts.
        const SDL_FRect game{ex(0), ey(0), ex(224) - ex(0), ey(144) - ey(0)};
        ok = SDL_SetTextureAlphaModFloat(mask, weight) && SDL_RenderTexture(renderer, mask, &game, &game);
        ++draws;
    }
    if (ok && strokes) {
        ok = SDL_RenderGeometry(renderer, nullptr, glyphs_.data(), int(glyphs_.size()), nullptr, 0);
        ++draws;
    }
    ok = SDL_SetRenderDrawBlendMode(renderer, previous) && ok;
    ok = SDL_SetRenderDrawColor(renderer, red, green, blue, alpha) && ok;
    cpu_ns = SDL_GetTicksNS() - start;
    if (!ok)
        cache_.invalidate();
    return ok;
}
} // namespace sawgod
