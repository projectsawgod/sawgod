#include "app/world_card_overlay.h"
#include "engine/cpu_scope.h"
#include "engine/presentation_diagnostics.h"
#include "game/world_card_lettering.h"
namespace sawgod::app {
WorldCardOverlay::~WorldCardOverlay() { shutdown(); }
void WorldCardOverlay::shutdown() {
    for (auto &texture : textures_) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}
bool WorldCardOverlay::prepare(SDL_Renderer *renderer) {
    // part() decodes/trims the shared authored sheets once. Upload every card before any can appear.
    for (int card = 1; card <= 4; ++card)
        for (int resolution = 0; resolution < 2; ++resolution)
            for (int index = 0; index < 4; ++index) {
                auto &texture = textures_[size_t(((card - 1) * 2 + resolution) * 4 + index)];
                const auto &im = game::WorldCardLettering::part(card, resolution != 0, index).image;
                if (texture || im.pixels.empty())
                    continue;
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                            im.width, im.height);
                if (!texture || !SDL_UpdateTexture(texture, nullptr, im.pixels.data(), im.width * 4) ||
                    !SDL_SetTextureScaleMode(texture,
                                             resolution ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST) ||
                    !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND))
                    return false;
            }
    return true;
}
void WorldCardOverlay::draw(SDL_Renderer *renderer, const game::WorldCardLettering &cards, emu::Wsc &w,
                            const SDL_FRect &destination, const std::array<float, 3> &weights) {
    CpuScope cpu_scope("WorldCardOverlay::draw");
    if (presentation_diagnostics::fresh())
        shutdown();
    const float alpha = cards.opacity(w);
    if (alpha <= 0)
        return;
    for (int resolution = 0; resolution < 2; ++resolution) {
        // Graphics weights are GB, WSC, Enhanced. GB keeps its own typed page.
        const float a = alpha * weights[size_t(resolution + 1)];
        if (a <= 0)
            continue;
        for (int index = 0; index < 4; ++index) {
            auto &texture = textures_[size_t(((cards.card() - 1) * 2 + resolution) * 4 + index)];
            const auto &part = game::WorldCardLettering::part(cards.card(), resolution != 0, index);
            const auto &im = part.image;
            if (im.pixels.empty())
                continue;
            if (!texture) {
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                            im.width, im.height);
                if (!texture)
                    continue;
                SDL_UpdateTexture(texture, nullptr, im.pixels.data(), im.width * 4);
                SDL_SetTextureScaleMode(texture, resolution ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            }
            SDL_SetTextureAlphaModFloat(texture, a);
            const float width = resolution ? 1792.0f : 224.0f, height = resolution ? 1152.0f : 144.0f;
            for (const auto &r : cards.slices(resolution != 0, index)) {
                const SDL_FRect source{float(r.x - part.x), float(r.y - part.y), float(r.w), float(r.h)};
                const SDL_FRect dest{destination.x + r.x * destination.w / width,
                                     destination.y + r.y * destination.h / height,
                                     r.w * destination.w / width, r.h * destination.h / height};
                SDL_RenderTexture(renderer, texture, &source, &dest);
            }
        }
    }
}
} // namespace sawgod::app
