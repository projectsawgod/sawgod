// Custom presentation blend modes shared by their draws and startup pipeline warm-up.
#pragma once
#include <SDL3/SDL.h>
namespace sawgod {
inline SDL_BlendMode influence_blend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM,
                                      SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM);
}
inline SDL_BlendMode mask_alpha_blend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                      SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}
inline SDL_BlendMode terrain_veil_blend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                      SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE,
                                      SDL_BLENDOPERATION_ADD);
}
} // namespace sawgod
