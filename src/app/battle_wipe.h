// The native battle-entry raster applied to the last complete Enhanced field picture.
#pragma once
#include "emu/wsc.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace sawgod::app {
class BattleWipe {
  public:
    void release() {
        SDL_DestroyTexture(picture_);
        picture_ = nullptr;
        map_ = -1;
    }
    bool available(uint64_t generation, int map) const {
        return picture_ && generation_ == generation && map_ == map;
    }
    bool begin(SDL_Renderer *renderer, int width, int height, uint64_t generation, int map,
               const SDL_FRect &picture) {
        const bool fresh = !picture_ || width_ != width || height_ != height;
        if (fresh) {
            release();
            picture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, width,
                                         height);
            if (!picture_)
                return false;
            SDL_SetTextureBlendMode(picture_, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(picture_, SDL_SCALEMODE_NEAREST);
            width_ = width;
            height_ = height;
        }
        target_ = SDL_GetRenderTarget(renderer);
        if (!SDL_SetRenderTarget(renderer, picture_))
            return false;
        {
            // The game rectangle can shrink or move without reallocating this retained target.
            // Clear every redraw, or end() copies the old picture back into the new black bars.
            SDL_Color previous{};
            SDL_GetRenderDrawColor(renderer, &previous.r, &previous.g, &previous.b, &previous.a);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            const bool cleared = SDL_RenderClear(renderer);
            SDL_SetRenderDrawColor(renderer, previous.r, previous.g, previous.b, previous.a);
            if (!cleared) {
                SDL_SetRenderTarget(renderer, target_);
                return false;
            }
        }
        picture_rect_ = picture;
        generation_ = generation;
        map_ = map;
        return true;
    }
    void end(SDL_Renderer *renderer) {
        SDL_SetRenderTarget(renderer, target_);
        SDL_RenderTexture(renderer, picture_, nullptr, nullptr);
    }
    // Native SCR2 covers the outside of the closing window. SCR1's four scroll pairs
    // shift interleaved rows; use the latched registers, including the first partial raster.
    void draw(SDL_Renderer *renderer, const emu::Wsc &w, const SDL_FRect &dst, float weight) {
        if (!picture_ || weight <= 0)
            return;
        const auto *layers = w.frame_layers();
        if (!layers)
            return;
        SDL_SetTextureBlendMode(picture_, weight < 1 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
        SDL_SetTextureAlphaModFloat(picture_, weight);
        const auto wrap = [](int n, int size) { return (n % size + size) % size; };
        // Sample the rectangle actually retained, including whole-pixel scaling or a moved picture.
        const float scale = picture_rect_.w / 224;
        const float left = picture_rect_.x, top = picture_rect_.y;
        for (int y = 0; y < 144; ++y) {
            if (!layers->valid[size_t(y)])
                continue;
            const auto &registers = layers->registers[size_t(y)];
            const int sy = wrap(y + int8_t(registers[0x11]), 144);
            for (int x = 0; x < 224;) {
                const auto covered = [&](int xx) {
                    const auto &p = layers->planes[emu::FrameLayers::Screen2][size_t(y * 224 + xx)];
                    return (p.flags & (emu::RasterPixel::Present | emu::RasterPixel::Window)) ==
                           (emu::RasterPixel::Present | emu::RasterPixel::Window);
                };
                if (covered(x)) {
                    ++x;
                    continue;
                }
                const int start = x, sx = wrap(x + int8_t(registers[0x10]), 224);
                while (x < 224 && !covered(x) && sx + x - start < 224)
                    ++x;
                const SDL_FRect source{left + float(sx) * scale, top + float(sy) * scale,
                                       float(x - start) * scale, scale};
                const float l = std::floor(dst.x + float(start) * dst.w / 224);
                const float r = std::floor(dst.x + float(x) * dst.w / 224);
                const float t = std::floor(dst.y + float(y) * dst.h / 144);
                const float b = std::floor(dst.y + float(y + 1) * dst.h / 144);
                const SDL_FRect destination{l, t, r - l, b - t};
                SDL_RenderTexture(renderer, picture_, &source, &destination);
            }
        }
        SDL_SetTextureAlphaModFloat(picture_, 1);
        SDL_SetTextureBlendMode(picture_, SDL_BLENDMODE_NONE);
    }

  private:
    SDL_Texture *picture_ = nullptr, *target_ = nullptr;
    int width_ = 0, height_ = 0, map_ = -1;
    SDL_FRect picture_rect_{};
    uint64_t generation_ = 0;
};
} // namespace sawgod::app
