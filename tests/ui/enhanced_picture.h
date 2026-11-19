#pragma once
#include "engine/hd_menus.h"
#include "engine/menu_fill.h"
#include "pilot_support.h"
#include <cmath>
#include <cstring>

namespace ui_pilot {
inline constexpr int enhanced_scale = 3;
inline constexpr int enhanced_width = 224 * enhanced_scale, enhanced_height = 144 * enhanced_scale;

inline int luminance(uint32_t p) {
    return int(299 * (p >> 16 & 255) + 587 * (p >> 8 & 255) + 114 * (p & 255)) / 1000;
}

// Check the finished output, not the source palette: a surviving pointer must have a recognizable
// luminance edge. Hue differences alone cannot satisfy this rule. Coordinates are native units.
inline bool visible_shape(const std::vector<uint32_t> &output, const std::vector<uint32_t> &ink) {
    std::set<uint32_t> mask(ink.begin(), ink.end());
    int edges = 0, clear = 0;
    for (auto at : mask) {
        const int x = int(at % 224), y = int(at / 224);
        if (x < 0 || x >= 224 || y < 0 || y >= 144)
            continue;
        for (auto [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || nx >= 224 || ny < 0 || ny >= 144 || mask.contains(uint32_t(ny * 224 + nx)))
                continue;
            ++edges;
            const auto sample = [&](int xx, int yy) {
                return output[size_t((yy * enhanced_scale + 1) * enhanced_width + xx * enhanced_scale + 1)];
            };
            clear += std::abs(luminance(sample(x, y)) - luminance(sample(nx, ny))) >= 32;
        }
    }
    return clear >= 3 && clear * 4 >= edges;
}

// Compare the borders and a blank fill band of cards/plates, excluding their different captions/art.
// The normalized horizontal positions also work for enemy plates with different caption widths.
inline bool distinct_windows(const std::vector<uint32_t> &output, const SDL_Rect &a, const SDL_Rect &b) {
    int different = 0, samples = 0;
    for (int row : {0, 1, 2, 3, a.h - 4, a.h - 3, a.h - 2, a.h - 1})
        for (int column = 2; column < 30; ++column) {
            const auto sample = [&](const SDL_Rect &r) {
                const int x = r.x * 3 + column * (r.w * 3 - 1) / 31;
                const int y = (r.y + std::min(row, r.h - 1)) * 3 + 1;
                return output[size_t(y * enhanced_width + x)];
            };
            ++samples;
            different += std::abs(luminance(sample(a)) - luminance(sample(b))) >= 24;
        }
    return different * 8 >= samples;
}

inline void enhanced_rules(const std::vector<uint32_t> &output, const std::vector<uint32_t> &native,
                           const emu::Wsc::UiText &text, const std::vector<game::Chrome::Draw> &draws,
                           std::vector<std::string> &errors) {
    require(output.size() == size_t(enhanced_width * enhanced_height), "wrong Enhanced output size");
    bool lost_text = false;
    for (size_t at = 0; at < native.size(); ++at) {
        if (text.visible(at, native.data()) != emu::Wsc::UiText::Stroke)
            continue;
        for (int y = 0; y < enhanced_scale; ++y)
            for (int x = 0; x < enhanced_scale; ++x)
                lost_text |= output[(at / 224 * enhanced_scale + size_t(y)) * enhanced_width +
                                    at % 224 * enhanced_scale + size_t(x)] != native[at];
    }
    if (lost_text)
        errors.push_back("Enhanced text strokes clipped or overwritten at output scale");
    for (const auto &d : draws)
        if (d.kind == game::Chrome::Draw::Kind::Cursor && d.pointer && !visible_shape(output, d.ink))
            errors.push_back("Enhanced selection lacks a visible luminance/shape pointer at " +
                             std::to_string(d.x) + "," + std::to_string(d.y));
}

// The same output-resolution passes and shared cache, in the same order as main.cpp. A surface-backed
// renderer exercises HdMenus' analytic software fallback without opening a window or creating a GPU.
class EnhancedPicture {
    SDL_Surface *surface_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    sawgod::MenuFill fill_;
    sawgod::HdMenus text_;
    sawgod::UiDrawCache inputs_;

  public:
    EnhancedPicture() {
        surface_ = SDL_CreateSurface(enhanced_width, enhanced_height, SDL_PIXELFORMAT_XRGB8888);
        require(surface_ != nullptr, SDL_GetError());
        renderer_ = SDL_CreateSoftwareRenderer(surface_);
        require(renderer_ != nullptr, SDL_GetError());
        texture_ =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 224, 144);
        require(texture_ && SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST), SDL_GetError());
    }
    ~EnhancedPicture() {
        text_.shutdown();
        SDL_DestroyTexture(texture_);
        SDL_DestroyRenderer(renderer_);
        SDL_DestroySurface(surface_);
    }
    std::vector<uint32_t> draw(const std::vector<uint32_t> &native, const emu::Wsc::UiFill &fill,
                               const emu::Wsc::UiText &text) {
        const SDL_FRect dst{0, 0, float(enhanced_width), float(enhanced_height)};
        require(SDL_UpdateTexture(texture_, nullptr, native.data(), 224 * 4) &&
                    SDL_RenderTexture(renderer_, texture_, nullptr, &dst),
                SDL_GetError());
        inputs_.update(dst, &fill, &text, native, 1.f, native);
        require(fill_.draw(renderer_, dst, fill, native, 1.f, &text, &inputs_) &&
                    text_.draw(renderer_, dst, text, &fill, native, 1.f, native, &inputs_) &&
                    SDL_RenderPresent(renderer_),
                SDL_GetError());
        std::vector<uint32_t> output(size_t(enhanced_width * enhanced_height));
        for (int y = 0; y < enhanced_height; ++y)
            std::memcpy(output.data() + y * enhanced_width,
                        static_cast<const uint8_t *>(surface_->pixels) + y * surface_->pitch,
                        size_t(enhanced_width * 4));
        for (auto &p : output)
            p &= 0xFFFFFF;
        return output;
    }
};

template <class Result, class Picture>
void append_enhanced(
    emu::Wsc &w, Result source, const std::filesystem::path &out, std::vector<Result> &results,
    Picture picture,
    const std::function<void(const std::vector<uint32_t> &, const std::vector<game::Chrome::Draw> &,
                             std::vector<std::string> &)> &selection = {}) {
    if (source.variant != "wsc-en")
        return;
    const auto ram = w.ram;
    const auto io = w.io;
    const auto sram = w.sram;
    const auto cpu = w.cpu.s;
    const auto frame = w.frame_count;
    const auto cycles = w.cpu.cycles;
    const auto framebuffer = w.framebuffer();
    const auto second_framebuffer = w.framebuffer(true);
    emu::Wsc::UiText text;
    emu::Wsc::UiFill fill;
    struct Restore {
        emu::Wsc &w;
        emu::Wsc::UiText *text;
        emu::Wsc::UiFill *fill;
        decltype(game::Chrome::observe) observer;
        ~Restore() {
            w.ui_text = text;
            w.ui_fill = fill;
            game::Chrome::observe = std::move(observer);
        }
    } restore{w, w.ui_text, w.ui_fill, game::Chrome::observe};
    w.ui_text = &text;
    w.ui_fill = &fill;
    std::vector<game::Chrome::Draw> draws;
    game::Chrome::observe = [&](const auto &d) { draws.push_back(d); };
    const auto native = picture();
    game::Chrome::observe = {};
    // Keep the renderer local: SDL_Quit in the Options/fidelity pilots must never outlive its textures.
    EnhancedPicture presenter;
    const auto output = presenter.draw(native, fill, text);
    source.variant = "enh-en";
    source.file.clear();
    enhanced_rules(output, native, text, draws, source.errors);
    if (selection)
        selection(output, draws, source.errors);
    require(w.ram == ram && w.io == io && w.sram == sram && w.frame_count == frame &&
                w.cpu.cycles == cycles && w.framebuffer() == framebuffer &&
                w.framebuffer(true) == second_framebuffer && std::memcmp(&cpu, &w.cpu.s, sizeof cpu) == 0,
            "Enhanced rendering mutated machine state");
    if (!out.empty()) {
        source.file = source.id + "--enh-en.ppm";
        std::ofstream f(out / source.file, std::ios::binary);
        f << "P6\n" << enhanced_width << ' ' << enhanced_height << "\n255\n";
        for (auto p : output) {
            f.put(char(p >> 16));
            f.put(char(p >> 8));
            f.put(char(p));
        }
        require(bool(f), "cannot write Enhanced picture");
    }
    results.push_back(std::move(source));
}
} // namespace ui_pilot
