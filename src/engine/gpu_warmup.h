// Compile the renderer's shader/blend/target variants before entering the audio/frame loop.
#pragma once
#include "engine/presentation_blend.h"
#include <array>
namespace sawgod {
inline bool warm_gpu_state(SDL_Renderer *renderer, SDL_GPURenderState *state, const void *uniforms,
                           Uint32 size, bool influence = false) {
    if (!SDL_GetGPURendererDevice(renderer))
        return true;
    auto *old = SDL_GetRenderTarget(renderer);
    auto *source = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 2, 2);
    const std::array<Uint32, 4> pixels{};
    bool ok = source && SDL_UpdateTexture(source, nullptr, pixels.data(), 8);
    const auto maximum = influence_blend();
    for (auto format : {SDL_PIXELFORMAT_XRGB8888, SDL_PIXELFORMAT_RGBA32, SDL_PIXELFORMAT_RGBA64_FLOAT}) {
        if ((format == SDL_PIXELFORMAT_RGBA64_FLOAT) != influence)
            continue;
        auto props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, format);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, 2);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, 2);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
        auto *target = SDL_CreateTextureWithProperties(renderer, props);
        SDL_DestroyProperties(props);
        ok = target && SDL_SetRenderTarget(renderer, target) && ok;
        for (SDL_BlendMode blend :
             {SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_MOD, terrain_veil_blend(), maximum}) {
            if ((blend == maximum) != influence)
                continue;
            ok = SDL_SetTextureBlendMode(source, blend) && ok;
            if (state && size)
                ok = SDL_SetGPURenderStateFragmentUniforms(state, 0, uniforms, size) && ok;
            ok = SDL_SetGPURenderState(renderer, state) &&
                 SDL_RenderTexture(renderer, source, nullptr, nullptr) && ok;
        }
        // This is deliberately startup-only. Wait once so resources and driver work are ready.
        SDL_SetGPURenderState(renderer, nullptr);
        auto *done = ok ? SDL_RenderReadPixels(renderer, nullptr) : nullptr;
        ok = done && ok;
        SDL_DestroySurface(done);
        SDL_SetRenderTarget(renderer, old);
        SDL_DestroyTexture(target);
    }
    SDL_DestroyTexture(source);
    return ok;
}
inline bool warm_gpu_defaults(SDL_Renderer *renderer) {
    if (!SDL_GetGPURendererDevice(renderer))
        return true;
    auto *old = SDL_GetRenderTarget(renderer);
    SDL_FColor color{};
    SDL_GetRenderDrawColorFloat(renderer, &color.r, &color.g, &color.b, &color.a);
    SDL_BlendMode old_blend;
    SDL_GetRenderDrawBlendMode(renderer, &old_blend);
    const SDL_FColor white{1, 1, 1, 1};
    const SDL_Vertex vertices[] = {{{0, 0}, white, {0, 0}}, {{2, 0}, white, {1, 0}}, {{0, 2}, white, {0, 1}}};
    bool ok = true;
    SDL_SetGPURenderState(renderer, nullptr);
    for (auto format : {SDL_PIXELFORMAT_XRGB8888, SDL_PIXELFORMAT_RGBA32}) {
        auto *target = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, 2, 2);
        ok = target && SDL_SetRenderTarget(renderer, target) && ok;
        for (SDL_BlendMode blend :
             {SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD,
              SDL_BLENDMODE_MUL, SDL_BLENDMODE_BLEND_PREMULTIPLIED, mask_alpha_blend()}) {
            SDL_SetRenderDrawBlendMode(renderer, blend);
            ok = SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0) && ok;
            ok = SDL_RenderLine(renderer, 0, 0, 1, 1) && ok;
            for (auto source_format : {SDL_PIXELFORMAT_XRGB8888, SDL_PIXELFORMAT_ARGB8888}) {
                auto *source = SDL_CreateTexture(renderer, source_format, SDL_TEXTUREACCESS_STATIC, 2, 2);
                const std::array<Uint32, 4> pixels{};
                ok = source && SDL_UpdateTexture(source, nullptr, pixels.data(), 8) &&
                     SDL_SetTextureBlendMode(source, blend) &&
                     SDL_RenderTexture(renderer, source, nullptr, nullptr) && ok;
                SDL_DestroyTexture(source);
            }
        }
        auto *done = ok ? SDL_RenderReadPixels(renderer, nullptr) : nullptr;
        ok = done && ok;
        SDL_DestroySurface(done);
        SDL_SetRenderTarget(renderer, old);
        SDL_DestroyTexture(target);
    }
    SDL_SetRenderDrawBlendMode(renderer, old_blend);
    SDL_SetRenderDrawColorFloat(renderer, color.r, color.g, color.b, color.a);
    return ok;
}
} // namespace sawgod
