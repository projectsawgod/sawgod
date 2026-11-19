// Where the game picture and the menu sit in the window, and the small texture helpers the presenter draws
// them with: the sharp scaler, the LCD grid masks and the menu backdrops.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/presentation_diagnostics.h"
#include "engine/screen.h"
#include "engine/version.h"

namespace sawgod::app {

// The game picture: as large as fits, keeping the WSC's shape, centered (whole pixels).
struct Rect {
    float x, y, w, h;
    float scale; // output pixels per game pixel (MenuRect: per menu unit)
};
Rect GameRect(int out_w, int out_h);

// Enhanced scene passes stop growing here; UI and the final presentation keep the output size.
inline constexpr int kEnhancedSceneScale = 5;
inline std::array<int, 2> enhanced_scene_size(int width, int height) {
    if (width <= 224 * kEnhancedSceneScale && height <= 144 * kEnhancedSceneScale)
        return {width, height};
    const int scale = std::clamp(std::min(width / 224, height / 144), 1, kEnhancedSceneScale);
    return {224 * scale, 144 * scale};
}

// The menu: the game picture less 2 tiles (16 game pixels) on every side, so it is the same size relative
// to the game at every window size and in fullscreen; one menu unit is half a game pixel.
Rect MenuRect(int out_w, int out_h);

// Draws a pixel-art texture (nearest-neighbour sampling) into dst with crisp pixels at any scale: directly
// at a whole scale, else "sharp bilinear" -- enlarged by nearest-neighbour to the next whole scale in a
// scratch texture, then smoothly down to dst, so only the pixel edges are softened.
struct SharpScaler {
    SDL_Texture *big = nullptr;
    int k = 0, w = 0, h = 0;
    void draw(SDL_Renderer *r, SDL_Texture *src, int src_w, int src_h, const SDL_FRect &dst, float scale,
              float opacity = 1, bool display = true) {
        const int whole = int(std::lround(scale));
        if (std::fabs(scale - float(whole)) < 1e-3f && whole >= 1) {
            if (display)
                SDL_RenderTexture(r, src, nullptr, &dst);
            return;
        }
        const int want = std::max(1, int(std::ceil(scale)));
        if (!big || k != want || w != src_w || h != src_h) {
            if (big)
                SDL_DestroyTexture(big);
            big = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, src_w * want,
                                    src_h * want);
            SDL_SetTextureScaleMode(big, SDL_SCALEMODE_LINEAR);
            k = want;
            w = src_w;
            h = src_h;
        }
        SDL_Texture *target = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, big);
        SDL_RenderTexture(r, src, nullptr, nullptr);
        SDL_SetRenderTarget(r, target);
        if (!display)
            return;
        SDL_SetTextureBlendMode(big, opacity < 1 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
        SDL_SetTextureAlphaModFloat(big, opacity);
        SDL_RenderTexture(r, big, nullptr, &dst);
        SDL_SetTextureAlphaModFloat(big, 1);
        SDL_SetTextureBlendMode(big, SDL_BLENDMODE_NONE);
    }
};

// A finished scene is scaled only after every scene effect; all host overlays follow this draw.
struct SceneScaler {
    SDL_Texture *target = nullptr;
    SharpScaler scaler;
    int w = 0, h = 0;
    bool prepare(SDL_Renderer *renderer, int width, int height) {
        if (target && w == width && h == height)
            return true;
        SDL_DestroyTexture(target);
        target =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
        w = width;
        h = height;
        return target && SDL_SetTextureScaleMode(target, SDL_SCALEMODE_NEAREST) &&
               SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
    }
    SDL_Texture *scaled(SDL_Renderer *renderer, const SDL_FRect &dst, float opacity = 1) {
        const float scale = dst.w / w;
        const bool whole = std::abs(scale - std::round(scale)) < 0.001f;
        if (whole) {
            SDL_SetTextureAlphaModFloat(target, opacity);
            SDL_SetTextureBlendMode(target, opacity < 1 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
        }
        scaler.draw(renderer, target, w, h, dst, scale, opacity);
        SDL_SetTextureAlphaModFloat(target, 1);
        SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
        return whole ? target : scaler.big;
    }
    void release() {
        SDL_DestroyTexture(target);
        SDL_DestroyTexture(scaler.big);
        target = scaler.big = nullptr;
    }
};

// The two mask textures, rebuilt when the filter or the picture's size changes.
struct LcdGrid {
    SDL_Texture *across = nullptr, *down = nullptr;
    int w = 0, h = 0;
    float strength = -1;
    void release() {
        if (across)
            SDL_DestroyTexture(across);
        if (down)
            SDL_DestroyTexture(down);
        across = down = nullptr;
    }
    void draw(SDL_Renderer *r, const SDL_FRect &dst, float amount = 1) {
        const int dw = int(dst.w), dh = int(dst.h);
        if (dw < 1 || dh < 1 || ips_strength(float(dw) / kNativeWidth) <= 0)
            return;
        if (presentation_diagnostics::fresh() || !across || w != dw || h != dh || strength != amount) {
            release();
            auto make = [&](const std::vector<uint32_t> &m, int tw, int th) {
                SDL_Texture *t =
                    SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
                SDL_UpdateTexture(t, nullptr, m.data(), tw * 4);
                SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_MOD);
                return t;
            };
            across = make(ips_mask(dw, kNativeWidth, true, amount), dw, 1);
            down = make(ips_mask(dh, kNativeHeight, false, amount), 1, dh);
            w = dw;
            h = dh;
            strength = amount;
        }
        SDL_RenderTexture(r, across, nullptr, &dst);
        SDL_RenderTexture(r, down, nullptr, &dst);
    }
};

// The menu's backdrop over a zoomed flat field: the live zoomed picture, so the camera stays where it is.
// Drawn into `full`, halved a few times and scaled back smoothly: about the blur and the darkening of
// BlurredBackdrop.
struct ZoomBackdrop {
    SDL_Texture *full = nullptr;
    std::array<SDL_Texture *, 4> half{};
    int w = 0, h = 0, levels = 0;
    void release() {
        SDL_DestroyTexture(full);
        for (auto *&t : half) {
            SDL_DestroyTexture(t);
            t = nullptr;
        }
        full = nullptr;
        w = h = 0;
    }
    bool begin(SDL_Renderer *r, int width, int height, float scale) {
        const int wanted =
            std::clamp(int(std::lround(std::log2(std::max(1.0f, scale)))), 1, int(half.size()));
        if (!full || w != width || h != height || levels != wanted) {
            release();
            const auto make = [&](int tw, int th) {
                auto *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET,
                                            std::max(1, tw), std::max(1, th));
                if (t)
                    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
                return t;
            };
            full = make(width, height);
            levels = wanted;
            bool ok = full != nullptr;
            for (int i = 0; i < levels; ++i)
                ok = (half[size_t(i)] = make(width >> (i + 1), height >> (i + 1))) && ok;
            if (!ok) {
                release();
                return false;
            }
            w = width;
            h = height;
        }
        return SDL_SetRenderTarget(r, full);
    }
    void end(SDL_Renderer *r, SDL_Texture *target, const SDL_FRect &dst) {
        SDL_Texture *source = full;
        for (int i = 0; i < levels; ++i) {
            SDL_SetRenderTarget(r, half[size_t(i)]);
            SDL_RenderTexture(r, source, nullptr, nullptr);
            source = half[size_t(i)];
        }
        SDL_SetRenderTarget(r, target);
        SDL_SetTextureColorModFloat(source, 0.9f, 0.9f, 0.9f);
        SDL_RenderTexture(r, source, nullptr, &dst);
    }
};

// The menu's backdrop: the paused frame, blurred (three box passes ~ a Gaussian, radius `r` game pixels)
// and darkened a little so the menu stands out where the scene shows around or through it.
std::vector<uint32_t> BlurredBackdrop(const std::vector<uint32_t> &rgb, int w, int h, int r);

} // namespace sawgod::app
