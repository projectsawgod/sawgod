#include "app/title_logo_overlay.h"
#include "engine/cpu_scope.h"
#include "engine/presentation_diagnostics.h"
#include "game/title_logo.h"
#include <algorithm>

namespace sawgod::app {
using game::TitleLogo;
namespace {
float level(uint32_t px) { return float(std::max({px >> 16 & 255, px >> 8 & 255, px & 255})); }
// The WSC logo holds this long once the title's prompt is up: 1.5 s after the kanji finish fading in, which
// the prompt follows by about half a second.
constexpr float kHoldSeconds = 1.0f;
constexpr float kFadeInSeconds = 2.5f;
} // namespace

TitleLogoOverlay::~TitleLogoOverlay() { shutdown(); }
void TitleLogoOverlay::shutdown() {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    uploaded_rows_ = 0;
}
bool TitleLogoOverlay::prepare(SDL_Renderer *renderer) {
    if (texture_)
        return true;
    texture_ =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                          TitleLogo::kWidth * TitleLogo::kScale, TitleLogo::kHeight * TitleLogo::kScale);
    uploaded_rows_ = 0;
    return texture_ && SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR) &&
           SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
}
bool TitleLogoOverlay::reset_renderer(SDL_Renderer *renderer) {
    shutdown();
    if (!prepare(renderer))
        return false;
    if (!image_.pixels.empty()) {
        if (!SDL_UpdateTexture(texture_, nullptr, image_.pixels.data(), image_.width * 4))
            return false;
        uploaded_rows_ = image_.height;
    }
    return true;
}

void TitleLogoOverlay::observe(bool title, const std::vector<uint32_t> &native, uint64_t now_ns,
                               bool settled) {
    CpuScope cpu_scope("TitleLogoOverlay::observe");
    if (building_.valid() &&
        (settled || building_.wait_for(std::chrono::seconds(0)) == std::future_status::ready))
        image_ = building_.get();
    settled_ = settled;
    const float dt = last_ns_ ? float(now_ns - last_ns_) / 1e9f : 0;
    last_ns_ = now_ns;
    title_ = title && native.size() == 224 * 144;
    if (!title_) {
        still_ = 0;
        held_ = 0;
        last_.clear();
    } else {
        held_ += dt;
        std::vector<uint32_t> logo;
        logo.reserve(size_t(TitleLogo::kWidth * TitleLogo::kHeight));
        for (int y = 0; y < TitleLogo::kHeight; ++y)
            for (int x = 0; x < TitleLogo::kWidth; ++x)
                logo.push_back(native[size_t((TitleLogo::kY + y) * 224 + TitleLogo::kX + x)]);
        still_ = logo == last_ ? still_ + 1 : 0;
        last_ = std::move(logo);
        if (image_.pixels.empty() && !building_.valid() && (still_ >= 8 || settled)) {
            // The first still, bright logo: a fade in progress changes every frame, and black holds still
            // too.
            const auto brightest = std::max_element(
                last_.begin(), last_.end(), [](uint32_t a, uint32_t b) { return level(a) < level(b); });
            if (level(*brightest) >= 200) {
                // The native logo remains on screen during its one-second hold. Build its identical
                // replacement from a private copy while that hold runs; no worker touches game/SDL state.
                if (settled)
                    image_ = TitleLogo::build(native);
                else
                    building_ = std::async(std::launch::async, [pixels = native] {
                        CpuScope build_scope("TitleLogo::build");
                        return TitleLogo::build(pixels);
                    });
                reference_ = size_t(brightest - last_.begin());
                reference_level_ = level(*brightest);
            }
        }
        if (reference_level_ > 0)
            brightness_ = std::clamp(level(last_[reference_]) / reference_level_, 0.f, 1.f);
    }
    // In gently over kFadeInSeconds and out over a quarter second, so it never pops over the native logo.
    ready_ = title_ && !image_.pixels.empty() &&
             (settled || (held_ >= kHoldSeconds && uploaded_rows_ == image_.height));
    const float target = ready_ ? 1.f : 0.f;
    opacity_ = settled             ? target
               : target > opacity_ ? std::min(target, opacity_ + dt / kFadeInSeconds)
                                   : std::max(target, opacity_ - dt * 4);
}

void TitleLogoOverlay::draw(SDL_Renderer *renderer, const SDL_FRect &destination, float enhanced_weight) {
    CpuScope cpu_scope("TitleLogoOverlay::draw");
    const float alpha = opacity_ * opacity_ * (3 - 2 * opacity_) * enhanced_weight; // eased at both ends
    if (image_.pixels.empty())
        return;
    float width = 0, height = 0;
    if (texture_ && (!SDL_GetTextureSize(texture_, &width, &height) || int(width) != image_.width ||
                     int(height) != image_.height))
        shutdown();
    if (!texture_ && !prepare(renderer))
        return;
    // Upload bounded bands even when the worker finishes after the native hold. Visibility starts only
    // after the final band; a late build must never force a large upload onto a playing frame.
    // A single settled screenshot is offline and can upload the entire image.
    if (uploaded_rows_ < image_.height) {
        const int rows =
            settled_ ? image_.height - uploaded_rows_ : std::min(96, image_.height - uploaded_rows_);
        const SDL_Rect band{0, uploaded_rows_, image_.width, rows};
        if (!SDL_UpdateTexture(texture_, &band,
                               image_.pixels.data() + size_t(uploaded_rows_) * size_t(image_.width),
                               image_.width * 4))
            return;
        uploaded_rows_ += rows;
        if (uploaded_rows_ < image_.height)
            return;
    }
    if (presentation_diagnostics::fresh() &&
        !SDL_UpdateTexture(texture_, nullptr, image_.pixels.data(), image_.width * 4))
        return;
    if (alpha <= 0)
        return;
    SDL_SetTextureAlphaModFloat(texture_, alpha);
    SDL_SetTextureColorModFloat(texture_, brightness_, brightness_, brightness_);
    const SDL_FRect dest{destination.x + TitleLogo::kX * destination.w / 224,
                         destination.y + TitleLogo::kY * destination.h / 144,
                         TitleLogo::kWidth * destination.w / 224, TitleLogo::kHeight * destination.h / 144};
    SDL_RenderTexture(renderer, texture_, nullptr, &dest);
}
} // namespace sawgod::app
