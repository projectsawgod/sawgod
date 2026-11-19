#include "engine/fast_forward_vfx.h"
#include "engine/gpu_warmup.h"
#include "engine/shaders/fast_forward.generated.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sawgod {
float FastForwardEnvelope::level(uint64_t now) const {
    const float elapsed = float(now >= changed_ ? now - changed_ : 0);
    return held_ ? std::min(1.f, from_ + elapsed / kEntryNs)
                 : std::max(0.f, from_ * (1.f - elapsed / kExitNs));
}
float FastForwardEnvelope::update(bool enabled, bool held, uint64_t now) {
    if (!enabled || now < last_) {
        held_ = false;
        from_ = 0;
        changed_ = now;
    }
    last_ = now;
    held = enabled && held;
    if (held != held_) {
        from_ = level(now);
        changed_ = now;
        held_ = held;
    }
    return level(now);
}
namespace {
float mod(float v, float d) { return v - std::floor(v / d) * d; }
int channel(uint32_t p, int shift) { return (p >> shift) & 255; }
} // namespace
void fast_forward_pixels(std::vector<uint32_t> &px, const std::vector<uint32_t> &previous, int w, int h,
                         SDL_FRect g, const uint32_t *ui, float amount, float gb, double seconds) {
    if (amount <= 0 || g.w <= 0 || g.h <= 0 || px.size() != size_t(w * h))
        return;
    const auto clean = px;
    const float t = float(seconds), s = std::clamp(amount, 0.f, 1.f);
    gb = std::clamp(gb, 0.f, 1.f);
    const auto sample = [&](int x, int y) { return clean[size_t(y * w + std::clamp(x, 0, w - 1))]; };
    for (int yy = 0; yy < h; ++yy) {
        const float y = (yy + 0.5f - g.y) * 144 / g.h;
        if (y < 0 || y >= 144)
            continue;
        float band = 0;
        for (float offset : {0.f, 47.f, 99.f})
            band += std::max(0.f, 1 - mod(y + t * 24 + offset, 144) / 2);
        const int displacement = int(std::round(band * std::sin(t * 21) * s * g.w / 224));
        const float scan = std::max(0.f, 1 - mod(y + t * 28, 144) / 7);
        for (int xx = 0; xx < w; ++xx) {
            const float x = (xx + 0.5f - g.x) * 224 / g.w;
            if (x < 0 || x >= 224 || (ui && (ui[size_t(int(y) * 224 + int(x))] & 0xFF0000)))
                continue;
            const size_t at = size_t(yy * w + xx);
            const int sx = std::clamp(xx + displacement, int(std::max(0.f, g.x)),
                                      std::min(w - 1, int(std::ceil(g.x + g.w)) - 1));
            const uint32_t p = sample(sx, yy), left = sample(sx - int(std::round(2 * g.w / 224)), yy),
                           right = sample(sx + int(std::round(2 * g.w / 224)), yy);
            const uint32_t phase = uint32_t(std::floor(seconds * 30));
            const float noise =
                float((uint32_t(xx) * 1664525u + uint32_t(yy) * 1013904223u + phase * 374761393u) & 65535u) /
                    65535 -
                0.5f;
            uint32_t result = clean[at] & 0xFF000000;
            for (int shift : {16, 8, 0}) {
                const float base = float(channel(p, shift));
                float a = base;
                if (shift == 16)
                    a += .16f * s * (channel(left, shift) - base);
                if (shift == 0)
                    a += .16f * s * (channel(right, shift) - base);
                a += band * (noise * 25 + 5) * s;
                const float raw = float(channel(clean[at], shift));
                const float old =
                    float(channel(previous.size() == clean.size() ? previous[at] : clean[at], shift));
                const float d = (raw * (1 - .19f * s) + old * .19f * s) * (1 - .05f * scan * s);
                result |= uint32_t(std::clamp(std::lround(a * (1 - gb) + d * gb), 0l, 255l)) << shift;
            }
            px[at] = result;
        }
    }
}

bool FastForwardVfx::resources(SDL_Renderer *r, int w, int h) {
    if (renderer_ == r && w == width_ && h == height_ && clean_[0] && clean_[1])
        return true;
    shutdown();
    renderer_ = r;
    width_ = w;
    height_ = h;
    for (auto &t : clean_) {
        t = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!t) {
            shutdown();
            return false;
        }
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    }
    auto *device =
        std::strcmp(SDL_GetRendererName(r), SDL_GPU_RENDERER) == 0 ? SDL_GetGPURendererDevice(r) : nullptr;
    if (!device)
        return true;
    mask_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 224, 144);
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 3;
    info.num_uniform_buffers = 1;
    const auto formats = SDL_GetGPUShaderFormats(device);
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = shaders::fast_forward_spirv;
        info.code_size = sizeof(shaders::fast_forward_spirv);
    } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = shaders::fast_forward_dxil;
        info.code_size = sizeof(shaders::fast_forward_dxil);
    } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && sizeof(shaders::fast_forward_msl) > 1) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = reinterpret_cast<const Uint8 *>(shaders::fast_forward_msl);
        info.code_size = sizeof(shaders::fast_forward_msl) - 1;
    }
    if (info.code)
        shader_ = SDL_CreateGPUShader(device, &info);
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(device, &sampler);
    auto gpu = [](SDL_Texture *t) {
        return static_cast<SDL_GPUTexture *>(SDL_GetPointerProperty(
            SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    };
    if (shader_ && sampler_ && mask_)
        for (int i = 0; i < 2; ++i) {
            SDL_GPUTextureSamplerBinding bindings[] = {{gpu(clean_[1 - i]), sampler_},
                                                       {gpu(mask_), sampler_}};
            SDL_GPURenderStateCreateInfo state{};
            state.fragment_shader = shader_;
            state.num_sampler_bindings = 2;
            state.sampler_bindings = bindings;
            states_[i] = SDL_CreateGPURenderState(r, &state);
        }
    if (!states_[0] || !states_[1]) {
        for (auto &state : states_) {
            if (state)
                SDL_DestroyGPURenderState(state);
            state = nullptr;
        }
        SDL_Log("Fast-Forward shader unavailable; using software tracking/persistence pass");
    }
    const Uniforms u{{0, 0, 2, 2}, {0.5f, 0, 0, 0}, {2, 2, 0, 0}};
    if (states_[0] && !warm_gpu_state(r, states_[0], &u, sizeof(u)))
        return false;
    return true;
}
bool FastForwardVfx::begin(SDL_Renderer *r, int w, int h) {
    if (!resources(r, w, h))
        return false;
    caller_ = SDL_GetRenderTarget(r);
    return SDL_SetRenderTarget(r, clean_[current_]);
}
bool FastForwardVfx::end(SDL_FRect g, const uint32_t *ui, float amount, float gb, double seconds) {
    // Initialize history with this clean picture, never an uninitialized texture.
    if (!history_) {
        SDL_SetRenderTarget(renderer_, clean_[1 - current_]);
        SDL_RenderTexture(renderer_, clean_[current_], nullptr, nullptr);
    }
    // A pane that just closed must not leave ghost text in GB's prior-frame trail.
    protected_ui_.resize(224 * 144);
    for (size_t i = 0; i < protected_ui_.size(); ++i)
        protected_ui_[i] =
            ui[i] | (history_ && gb > 0 && previous_ui_.size() == protected_ui_.size() ? previous_ui_[i] : 0);
    previous_ui_.assign(ui, ui + 224 * 144);
    bool ok = true;
    if (states_[current_] && amount > 0) {
        SDL_SetRenderTarget(renderer_, caller_);
        Uniforms u{
            {g.x, g.y, g.w, g.h}, {amount, gb, float(seconds), 0}, {float(width_), float(height_), 0, 0}};
        ok = SDL_UpdateTexture(mask_, nullptr, protected_ui_.data(), 224 * 4) &&
             SDL_SetGPURenderStateFragmentUniforms(states_[current_], 0, &u, sizeof(u)) &&
             SDL_SetGPURenderState(renderer_, states_[current_]) &&
             SDL_RenderTexture(renderer_, clean_[current_], nullptr, nullptr);
        SDL_SetGPURenderState(renderer_, nullptr);
    } else if (!states_[current_] && amount > 0) {
        SDL_SetRenderTarget(renderer_, clean_[current_]);
        SDL_Surface *surface = SDL_RenderReadPixels(renderer_, nullptr);
        SDL_Surface *rgb = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888) : nullptr;
        ok = rgb != nullptr;
        if (rgb) {
            std::vector<uint32_t> pixels(size_t(width_ * height_));
            for (int y = 0; y < height_; ++y)
                std::memcpy(pixels.data() + y * width_, static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch,
                            size_t(width_) * 4);
            auto clean = pixels;
            fast_forward_pixels(pixels, previous_, width_, height_, g, protected_ui_.data(), amount, gb,
                                seconds);
            previous_ = std::move(clean);
            if (!software_)
                software_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, width_, height_);
            SDL_SetRenderTarget(renderer_, caller_);
            ok = software_ && SDL_UpdateTexture(software_, nullptr, pixels.data(), width_ * 4) &&
                 SDL_RenderTexture(renderer_, software_, nullptr, nullptr);
        }
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
    } else {
        previous_.clear();
        SDL_SetRenderTarget(renderer_, caller_);
        ok = SDL_RenderTexture(renderer_, clean_[current_], nullptr, nullptr);
    }
    if (!ok) {
        SDL_SetRenderTarget(renderer_, caller_);
        SDL_RenderTexture(renderer_, clean_[current_], nullptr, nullptr);
        SDL_Log("Fast-Forward presentation failed: %s", SDL_GetError());
    }
    current_ = 1 - current_;
    history_ = true;
    return ok;
}
void FastForwardVfx::shutdown() {
    for (auto &state : states_) {
        if (state)
            SDL_DestroyGPURenderState(state);
        state = nullptr;
    }
    auto *device = renderer_ && std::strcmp(SDL_GetRendererName(renderer_), SDL_GPU_RENDERER) == 0
                       ? SDL_GetGPURendererDevice(renderer_)
                       : nullptr;
    if (shader_ && device)
        SDL_ReleaseGPUShader(device, shader_);
    if (sampler_ && device)
        SDL_ReleaseGPUSampler(device, sampler_);
    shader_ = nullptr;
    sampler_ = nullptr;
    for (auto &t : clean_) {
        SDL_DestroyTexture(t);
        t = nullptr;
    }
    SDL_DestroyTexture(mask_);
    SDL_DestroyTexture(software_);
    mask_ = software_ = caller_ = nullptr;
    renderer_ = nullptr;
    width_ = height_ = current_ = 0;
    history_ = false;
    previous_.clear();
    previous_ui_.clear();
    protected_ui_.clear();
}
void draw_fast_forward_indicator(SDL_Renderer *r, SDL_FRect box, float s, bool gb) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, gb ? 255 : 0, gb ? 255 : 0, gb ? 255 : 0, gb ? 255 : 160);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, gb ? 0 : 255, gb ? 0 : 255, gb ? 0 : 255, 255);
    // Two solid right-facing arrows, y+3 (the new-layout baseline includes its +1).
    for (int arrow = 0; arrow < 2; ++arrow)
        for (int y = 0; y < 7; ++y) {
            const SDL_FRect row{box.x + (3 + arrow * 6) * s, box.y + (3 + y) * s,
                                float(4 - std::abs(3 - y)) * s, s};
            SDL_RenderFillRect(r, &row);
        }
}
} // namespace sawgod
