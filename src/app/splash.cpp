// The splash (splash.h) and the logos it and the ROM screen draw.
#include "app/splash.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "app/rom_screen.h"
#include "engine/input.h"

namespace sawgod::app {

namespace {

// assets/ui/sawgod_logo.png (light lettering) and sawgod_logo_on_white.png (black), compiled in by
// CMakeLists.txt
constexpr unsigned char kOnBlackPng[] = {
#include "rom_screen_logo.inc"
};
constexpr unsigned char kOnWhitePng[] = {
#include "splash_logo.inc"
};
// assets/ui/app_icon_256.png (tools/release/app_icons.py), compiled in the same way
constexpr unsigned char kAppIconPng[] = {
#include "app_icon.inc"
};

} // namespace

std::vector<uint32_t> load_logo(Logo which, int *w, int *h) {
    const bool white = which == Logo::OnWhite;
    SDL_Surface *png = SDL_LoadPNG_IO(white ? SDL_IOFromConstMem(kOnWhitePng, sizeof kOnWhitePng)
                                            : SDL_IOFromConstMem(kOnBlackPng, sizeof kOnBlackPng),
                                      true);
    SDL_Surface *argb = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    std::vector<uint32_t> px;
    if (argb) {
        *w = argb->w;
        *h = argb->h;
        px.resize(size_t(*w) * size_t(*h));
        for (int y = 0; y < *h; ++y)
            std::memcpy(&px[size_t(y) * size_t(*w)],
                        static_cast<const uint8_t *>(argb->pixels) + size_t(y) * size_t(argb->pitch),
                        size_t(*w) * 4);
    } else {
        SDL_Log("logo: %s", SDL_GetError());
    }
    SDL_DestroySurface(argb);
    SDL_DestroySurface(png);
    return px;
}

void set_window_icon(SDL_Window *window) {
#if defined(__ANDROID__) || defined(__APPLE__)
    (void)window;
    (void)kAppIconPng;
#else
    SDL_Surface *icon = SDL_LoadPNG_IO(SDL_IOFromConstMem(kAppIconPng, sizeof kAppIconPng), true);
    if (!icon || !SDL_SetWindowIcon(window, icon))
        SDL_Log("window icon: %s", SDL_GetError());
    SDL_DestroySurface(icon);
#endif
}

SDL_FRect splash_logo_rect(int out_w, int out_h, int logo_w, int logo_h) {
    if (out_w <= 0 || out_h <= 0 || logo_w <= 0 || logo_h <= 0)
        return {};
    const float s =
        std::min(float(out_w) * kSplashFill / float(logo_w), float(out_h) * kSplashFill / float(logo_h));
    const float w = float(logo_w) * s, h = float(logo_h) * s;
    return {(float(out_w) - w) / 2, (float(out_h) - h) / 2, w, h};
}

SDL_Rect splash_pixel_rect(int logo_w, int logo_h) {
    const SDL_FRect r = splash_logo_rect(SplashPixels::kWidth, SplashPixels::kHeight, logo_w, logo_h);
    return {int(std::lround(r.x)), int(std::lround(r.y)), int(std::lround(r.w)), int(std::lround(r.h))};
}

int splash_mosaic_width(float k, int pixel_w, float scale) {
    if (k >= 1 || scale <= 1)
        return pixel_w;
    const float stepped =
        std::floor(std::max(k, 0.0f) * float(kSplashMosaicSteps)) / float(kSplashMosaicSteps);
    return std::max(pixel_w, int(std::lround(float(pixel_w) * std::pow(scale, 1 - stepped))));
}

SplashPixels splash_pixels(const std::vector<uint32_t> &logo_argb, int logo_w, int logo_h) {
    SplashPixels out;
    out.rgb.assign(size_t(SplashPixels::kWidth) * SplashPixels::kHeight, 0xFFFFFF);
    const SDL_Rect r = splash_pixel_rect(logo_w, logo_h);
    const int x0 = r.x, y0 = r.y, w = r.w, h = r.h;
    const std::vector<uint32_t> px = scale_rom_screen_logo(logo_argb, logo_w, logo_h, w, h); // premultiplied
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (x0 + x < 0 || y0 + y < 0 || x0 + x >= SplashPixels::kWidth || y0 + y >= SplashPixels::kHeight)
                continue;
            const uint32_t p = px[size_t(y) * size_t(w) + size_t(x)], white = 255 - (p >> 24);
            out.rgb[size_t(y0 + y) * SplashPixels::kWidth + size_t(x0 + x)] =
                ((p >> 16 & 0xFF) + white) << 16 | ((p >> 8 & 0xFF) + white) << 8 | ((p & 0xFF) + white);
        }
    return out;
}

namespace {

// The logo averaged down to w x h, straight alpha, as a texture (nearest: a mosaic step; else linear).
SDL_Texture *logo_texture(SDL_Renderer *renderer, const std::vector<uint32_t> &logo, int logo_w, int logo_h,
                          int w, int h, bool nearest) {
    std::vector<uint32_t> px = scale_rom_screen_logo(logo, logo_w, logo_h, w, h);
    for (uint32_t &p : px) { // back to straight alpha
        const uint32_t a = p >> 24;
        const auto ch = [&](int shift) {
            return a ? std::min(255u, ((p >> shift & 0xFF) * 255 + a / 2) / a) : 0u;
        };
        p = a << 24 | ch(16) << 16 | ch(8) << 8 | ch(0);
    }
    SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    }
    return t;
}

} // namespace

bool run_splash(SDL_Renderer *renderer, ui::Input &input, const ui::InputMap &map,
                const SplashScreen &screen) {
    int logo_w = 0, logo_h = 0;
    const std::vector<uint32_t> logo_px = load_logo(Logo::OnWhite, &logo_w, &logo_h);
    if (logo_px.empty())
        return true;
    const SDL_Rect cell = splash_pixel_rect(logo_w, logo_h); // the pixel logo, in game pixels
    // the logo at each width it is shown at: the sharp one (its size on screen) and each mosaic step
    std::map<std::pair<int, int>, SDL_Texture *> sizes;
    const auto sized = [&](int w, int h, bool nearest) {
        SDL_Texture *&t = sizes[{w, nearest ? -h : h}];
        if (!t)
            t = logo_texture(renderer, logo_px, logo_w, logo_h, w, h, nearest);
        return t;
    };
    // the logo at the WSC's resolution on white, in whole game pixels, in the game's Screen Mode
    const SplashPixels sp = splash_pixels(logo_px, logo_w, logo_h);
    const auto pixel_texture = [&](bool in_screen_mode) {
        SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC,
                                           SplashPixels::kWidth, SplashPixels::kHeight);
        if (t) {
            std::vector<uint32_t> shown = sp.rgb;
            if (in_screen_mode && screen.recolor)
                screen.recolor(shown);
            SDL_UpdateTexture(t, nullptr, shown.data(), SplashPixels::kWidth * 4);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_PIXELART);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        }
        return t;
    };
    SDL_Texture *pixels = pixel_texture(false), *pixels_screen = pixel_texture(true);
    const uint64_t mosaic_at = kSplashFadeInNs + kSplashSharpNs, pixel_at = mosaic_at + kSplashPixelNs;
    const uint64_t screen_at = pixel_at + kSplashPixelCleanNs,
                   black_at = screen_at + kSplashScreenFadeNs + kSplashScreenNs;
    const uint64_t end_at = black_at + kSplashBlackNs;
    // a press skips it: A held as the splash opens counts once it is let go
    bool a_was = input.button_held(map, ui::WscButton::A);
    bool open = true;
    const uint64_t start = SDL_GetTicksNS();
    for (uint64_t t = 0; open && t < end_at; t = SDL_GetTicksNS() - start) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input.handle_event(e);
            if (e.type == SDL_EVENT_QUIT)
                open = false;
        }
        input.take_menu_actions(double(SDL_GetTicksNS()) / 1e9); // presses here are the splash's
        const bool a = input.button_held(map, ui::WscButton::A);
        if (a && !a_was)
            break;
        a_was = a;

        int ow = 1, oh = 1;
        SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
        // every stage sits on the pixel logo's cells, so the logo never moves, only coarsens
        const SDL_FRect pic = screen.picture(ow, oh);
        const float scale = pic.w / float(SplashPixels::kWidth); // window pixels per game pixel
        const SDL_FRect at{pic.x + float(cell.x) * scale, pic.y + float(cell.y) * scale,
                           float(cell.w) * scale, float(cell.h) * scale};
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        if (t >= screen_at) { // the pixel logo as the game shows its picture
            if (pixels_screen)
                SDL_RenderTexture(renderer, pixels_screen, nullptr, &pic);
            if (screen.overlay)
                screen.overlay(pic);
            // fading in: the clean picture over it, going (a crossfade of the two whole pictures)
            const float k = std::min(1.0f, float(double(t - screen_at) / double(kSplashScreenFadeNs)));
            if (pixels && k < 1) {
                SDL_SetTextureAlphaModFloat(pixels, 1 - k);
                SDL_RenderTexture(renderer, pixels, nullptr, &pic);
                SDL_SetTextureAlphaModFloat(pixels, 1);
            }
        } else if (t >= pixel_at) { // the mosaic's last step, held
            if (pixels)
                SDL_RenderTexture(renderer, pixels, nullptr, &pic);
        } else if (t >= mosaic_at) {
            // the mosaic: blocks growing step by step from one window pixel to one game pixel (as the SNES's)
            const float k = float(double(t - mosaic_at) / double(kSplashPixelNs));
            const int w = splash_mosaic_width(k, cell.w, scale);
            const int h = std::max(1, int(std::lround(float(cell.h) * float(w) / float(cell.w))));
            if (SDL_Texture *tex = sized(w, h, true))
                SDL_RenderTexture(renderer, tex, nullptr, &at);
        } else {
            const int w = std::min(logo_w, int(at.w + 0.5f)), h = std::min(logo_h, int(at.h + 0.5f));
            if (SDL_Texture *tex = w > 0 && h > 0 ? sized(w, h, false) : nullptr)
                SDL_RenderTexture(renderer, tex, nullptr, &at);
        }
        // up from black at the start, down to it at the end
        const double black = t < kSplashFadeInNs ? 1 - double(t) / double(kSplashFadeInNs)
                             : t >= black_at ? std::min(1.0, double(t - black_at) / double(kSplashBlackNs))
                                             : 0;
        if (black > 0) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, uint8_t(std::lround(255 * black)));
            const SDL_FRect all{0, 0, float(ow), float(oh)};
            SDL_RenderFillRect(renderer, &all);
        }
        SDL_RenderPresent(renderer);
    }
    for (auto &[size, tex] : sizes)
        SDL_DestroyTexture(tex);
    SDL_DestroyTexture(pixels);
    SDL_DestroyTexture(pixels_screen);
    return open;
}

} // namespace sawgod::app
