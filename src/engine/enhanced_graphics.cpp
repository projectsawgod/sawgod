#include "engine/enhanced_graphics.h"
#include "engine/cpu_scope.h"
#include "engine/gpu_pass_timer.h"
#include "engine/gpu_warmup.h"
#include "engine/pixel_upload.h"
#include "engine/presentation_diagnostics.h"
#include "engine/shaders/enhanced.generated.h"
#include "engine/startup_timing.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace sawgod {
bool EnhancedGraphics::init(SDL_Renderer *renderer, SDL_Texture *scene, SDL_Texture *ownership,
                            SDL_Texture *water, const EnhancedGraphics *shared) {
    startup::Stage timing("Enhanced scene passes");
    CpuScope cpu_scope("EnhancedGraphics::init");
    shutdown();
    auto *device = SDL_GetGPURendererDevice(renderer);
    if (!device)
        return SDL_SetError("Enhanced graphics requires the GPU renderer");
    renderer_ = renderer;
    const auto formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 12;
    info.num_uniform_buffers = 1;
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = shaders::enhanced_spirv;
        info.code_size = sizeof(shaders::enhanced_spirv);
    } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = shaders::enhanced_dxil;
        info.code_size = sizeof(shaders::enhanced_dxil);
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = reinterpret_cast<const Uint8 *>(shaders::enhanced_msl);
        info.code_size = sizeof(shaders::enhanced_msl) - 1;
    } else {
        shutdown();
        return SDL_SetError("Enhanced shader format unavailable");
    }
    if (shared && shared != this && shared->renderer_ == renderer && shared->shader_) {
        shader_ = shared->shader_;
        startup::mark("Enhanced shader/pipeline reuse");
    } else if (auto *shader = SDL_CreateGPUShader(device, &info)) {
        shader_ = std::shared_ptr<SDL_GPUShader>(
            shader, [device](SDL_GPUShader *value) { SDL_ReleaseGPUShader(device, value); });
    }
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearest_ = SDL_CreateGPUSampler(device, &sampler);
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    linear_ = SDL_CreateGPUSampler(device, &sampler);
    const auto texture = [&](int access, int w, int h) {
        auto *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TextureAccess(access), w, h);
        if (t)
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
        return t;
    };
    external_ = scene && ownership && water;
    if (!influence_.init(renderer, 224, 144, 4, !external_)) {
        shutdown();
        return false;
    }
    // A small periodic sprite sheet replaces water's per-fragment trigonometry. Gutters wrap
    // spatial filtering and adjacent phases interpolate, so motion never jumps at an atlas seam.
    constexpr int side = 64, stride = side + 2, phases = 32;
    std::vector<uint32_t> pattern(size_t(stride) * stride * phases);
    constexpr float tau = 6.2831853f;
    const auto sat = [](float v) { return std::clamp(v, 0.f, 1.f); };
    for (int f = 0; f < phases; ++f)
        for (int y = 0; y < stride; ++y)
            for (int x = 0; x < stride; ++x) {
                const float px = float((x - 1 + side) % side) * 4, py = float((y - 1 + side) % side) * 4,
                            t = float(f) * 30;
                const float wave = std::sin(px * tau / 64 + py * tau / 32 - t * tau / 480);
                const float wobble = std::sin(px * tau / 64 + t * tau / 480) * 1.6f +
                                     std::sin(px * tau / 32 + py * tau / 64) * 0.9f +
                                     std::sin(px * tau / 128 - py * tau / 32) * 0.7f;
                float band =
                    std::pow(sat(std::sin(py * tau / 16 + wave * 0.8f + wobble - t * tau / 320)), 12);
                band *= sat(std::sin(px * tau / 64 + py * tau / 128 + t * tau / 960) * 1.4f + 0.35f);
                const float shift = std::sin(py * tau / 16 - t * tau / 480);
                // Sparse authored-looking glints live in the same repeating sheet, with no scene RNG.
                const bool glint = ((int(px) / 4 * 37 + int(py) / 4 * 101) % 127) == 0;
                const float sparkle = glint ? std::pow(sat(std::sin(t * tau / 240 + px * tau / 32)), 16) : 0;
                const unsigned r = unsigned(std::lround(sat(band * 0.6f + sparkle) * 255));
                const unsigned g = unsigned(std::lround((shift * 0.5f + 0.5f) * 255));
                pattern[size_t((f * stride + y) * stride + x)] = r << 16 | g << 8;
            }
    water_pattern_ = texture(SDL_TEXTUREACCESS_STATIC, stride, stride * phases);
    if (!water_pattern_ || !SDL_UpdateTexture(water_pattern_, nullptr, pattern.data(), stride * 4))
        return false;
    SDL_SetTextureScaleMode(water_pattern_, SDL_SCALEMODE_LINEAR);
    source_ = external_ ? scene : texture(SDL_TEXTUREACCESS_STREAMING, 224, 144);
    original_ = external_ ? scene : texture(SDL_TEXTUREACCESS_STREAMING, 224, 144);
    gb_ = texture(SDL_TEXTUREACCESS_STREAMING, 224, 144);
    water_ = external_ ? water : texture(SDL_TEXTUREACCESS_STREAMING, 224, 144);
    mask_ = external_ ? ownership : texture(SDL_TEXTUREACCESS_STREAMING, 224, 144);
    float width = 672, height = 432;
    if (external_)
        SDL_GetTextureSize(scene, &width, &height);
    for (size_t i = 0; i < 3; ++i)
        targets_[i] = texture(SDL_TEXTUREACCESS_TARGET, 112, 72);
    targets_[3] = texture(SDL_TEXTUREACCESS_TARGET, external_ ? 448 : 672, external_ ? 288 : 432);
    if (external_)
        SDL_SetTextureScaleMode(targets_[3], SDL_SCALEMODE_LINEAR);
    targets_[4] = texture(SDL_TEXTUREACCESS_TARGET, 448, 288);
    SDL_SetTextureScaleMode(targets_[4], SDL_SCALEMODE_LINEAR);
    targets_[5] = texture(SDL_TEXTUREACCESS_TARGET, int(width), int(height));
    if (!shader_ || !nearest_ || !linear_ || !source_ || !original_ || !gb_ || !mask_ || !water_ ||
        std::any_of(targets_.begin(), targets_.end(), [](auto *t) { return !t; })) {
        shutdown();
        return false;
    }
    const auto gpu_texture = [](SDL_Texture *t) {
        return static_cast<SDL_GPUTexture *>(SDL_GetPointerProperty(
            SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    };
    for (size_t index = 0; index < 7; ++index) {
        const size_t i = index >= 5 ? index + 1 : index;
        // A pass must not bind the target it writes, even via an unused sampler.
        SDL_GPUTextureSamplerBinding bindings[] = {
            {gpu_texture(mask_), nearest_},
            {gpu_texture(i == 6 ? targets_[3]
                                : (i == 7 ? targets_[2] : (i == 0 || i == 4 ? gb_ : targets_[i - 1]))),
             linear_},
            {gpu_texture(gb_), nearest_},
            {gpu_texture(original_), nearest_},
            {gpu_texture(water_), nearest_},
            {gpu_texture(influence_.texture(0)), linear_},
            {gpu_texture(influence_.texture(1)), linear_},
            {gpu_texture(influence_.texture(2)), linear_},
            {gpu_texture(influence_.texture(3)), linear_},
            {gpu_texture(water_pattern_), linear_},
            {gpu_texture(i == 6 ? targets_[4] : gb_), linear_}};
        SDL_GPURenderStateCreateInfo state{};
        state.fragment_shader = shader_.get();
        state.num_sampler_bindings = 11;
        state.sampler_bindings = bindings;
        states_[i] = SDL_CreateGPURenderState(renderer_, &state);
        if (!states_[i]) {
            shutdown();
            return false;
        }
    }
    if (!warm_gpu_state(renderer_, states_[0], &uniforms_, sizeof(uniforms_))) {
        shutdown();
        return false;
    }
    SDL_Log("Enhanced graphics: %dx%d GPU scene passes ready", int(width), int(height));
    return true;
}
bool EnhancedGraphics::upload(const std::vector<uint32_t> &wsc, const std::vector<uint32_t> &gb,
                              const uint32_t *mask, const EnhancedParameters &p) {
    if (!ready() || wsc.size() != 224 * 144 || gb.size() != wsc.size() ||
        (!p.background.empty() && p.background.size() != wsc.size()) ||
        (!p.water.empty() && p.water.size() != wsc.size()))
        return false;
    if (auto &t = presentation_diagnostics::trace(); t.enabled()) {
        t.range(external_ ? "Mode7.picture" : "Enhanced.picture", wsc);
        t.range(external_ ? "Mode7.gb" : "Enhanced.gb", gb);
        t.range(external_ ? "Mode7.mask" : "Enhanced.mask", std::span(mask, wsc.size()));
        t.range(external_ ? "Mode7.water" : "Enhanced.water", p.water);
        t.range(external_ ? "Mode7.background" : "Enhanced.background", p.background);
        t.range(external_ ? "Mode7.actors" : "Enhanced.actors", p.actors);
        presentation_diagnostics::Hash h;
        h.add(p.lights.size());
        for (const auto &l : p.lights) {
            h.add(l.position);
            h.add(l.emission);
            h.add(l.beam);
            h.add(l.type);
            h.add(l.extent);
            h.add(l.source_pixels);
        }
        t.write(external_ ? "Mode7.lights" : "Enhanced.lights", h);
    }
    last_upload_full = !external_ && cached_source_.empty() && cached_original_.empty() &&
                       cached_water_.empty() && cached_mask_.empty();
    ui_regions.update(mask, !external_);
    if (!external_)
        sharp_ui_regions_.update(mask);
    influence_parameters_ = p;
    uniforms_ = {};
    uniforms_.palette_fade = {p.palette_range[0], p.palette_range[1], 0, 0};
    uniforms_.battle[0] = external_ ? 0.f : p.battle_lighting.weight;
    for (size_t g = 0; g < 3; ++g)
        uniforms_.battle[g + 1] = p.battle_lighting.selection[g];
    uniforms_.battle_groups = p.battle_lighting.groups;
    uniforms_.metadata[0] = external_ ? 1.f : 0.f;
    uniforms_.metadata[1] =
        std::any_of(p.lights.begin(), p.lights.end(), [](const PointLight &l) { return l.type < 6; }) ? 1.f
                                                                                                      : 0.f;
    uniforms_.metadata[2] =
        std::any_of(p.lights.begin(), p.lights.end(), [](const PointLight &l) { return l.type == 6; }) ? 1.f
                                                                                                       : 0.f;
    uniforms_.control = {0, p.snap ? 1.0f : 0.0f, p.grade, p.bloom};
    uniforms_.effect_grid = {p.effect_grid[0], p.effect_grid[1], p.effect_origin[0], p.effect_origin[1]};
    uniforms_.scene = {p.shadows ? 1.0f : 0.0f, p.gb_weight, float(std::min<size_t>(p.actors.size(), 20)), 0};
    // Full scenes use the exact mockup lift. Fade exposure reaches zero with
    // scene light; UI brightness cannot keep a black battlefield illuminated.
    const auto &scene_pixels = p.background.empty() ? wsc : p.background;
    unsigned light = 0;
    for (size_t i = 0; i < scene_pixels.size(); ++i)
        if (!(mask[i] & 0xFF0000))
            for (unsigned shift : {0u, 8u, 16u})
                light = std::max(light, (scene_pixels[i] >> shift) & 255u);
    uniforms_.scene[3] = std::min(1.0f, float(light) / 191.25f);
    std::copy_n(p.actors.begin(), std::min<size_t>(p.actors.size(), 20), uniforms_.actors.begin());
    uniforms_.water = {
        p.water_strength, float(p.frame % 30720), p.effect_weight,
        std::any_of(p.lights.begin(), p.lights.end(), [](const PointLight &l) { return l.type == 7; })
            ? 1.0f
            : 0.0f};
    for (const auto &l : p.lights)
        if (l.type == 7) {
            // Each native animation has one table-selected gloomy target, shared by all its patches.
            uniforms_.dark_color = l.emission;
            break;
        }
    uniforms_.lighting = {float(std::min<size_t>(p.lights.size(), 256)), p.light_strength,
                          float(p.beam_style), float(p.frame % 30720)};
    for (size_t i = 0; i < std::min<size_t>(p.lights.size(), 256); ++i) {
        light_positions_[i] = p.lights[i].position;
        light_positions_[i][3] = float(p.lights[i].type);
        if ((p.lights[i].type == 6 || p.lights[i].type == 7) &&
            (p.lights[i].source_pixels[0] || p.lights[i].source_pixels[1]))
            light_positions_[i][3] += 2;
        light_colors_[i] = p.lights[i].emission;
        light_beams_[i] = p.lights[i].beam;
        if (p.lights[i].type != 4) {
            light_beams_[i][0] = p.lights[i].extent[0];
            light_beams_[i][1] = p.lights[i].extent[1];
        }
        if (light_positions_[i][3] >= 8) {
            light_beams_[i][2] = std::bit_cast<float>(p.lights[i].source_pixels[0]);
            light_beams_[i][3] = std::bit_cast<float>(p.lights[i].source_pixels[1]);
        }
    }
    if (auto &t = presentation_diagnostics::trace(); t.enabled()) {
        presentation_diagnostics::Hash h;
        h.add(uniforms_.control);
        h.add(uniforms_.scene);
        h.add(uniforms_.actors);
        h.add(uniforms_.lighting);
        h.add(uniforms_.water);
        h.add(uniforms_.metadata);
        h.add(uniforms_.dark_color);
        h.add(uniforms_.effect_grid);
        h.add(uniforms_.battle);
        h.add(uniforms_.battle_groups);
        h.add(uniforms_.palette_fade);
        t.write(external_ ? "Mode7.uniforms" : "Enhanced.uniforms", h);
    }
    // A zero-weight endpoint is invisible. Initialize its texture once, then upload the current
    // picture before the very first nonzero fade frame; no visible frame is deferred.
    const bool gb_needed = p.gb_weight > 0 || cached_gb_.empty();
    if (external_)
        return !gb_needed || upload_pixels(gb_, gb.data(), 224, 144, cached_gb_);
    restoration_coverage(mask, upload_mask_.data(), upload_mask_.size(), true);
    const auto &source = p.background.empty() ? wsc : p.background;
    const uint32_t *water = p.water.empty() ? dry_.data() : p.water.data();
    const auto same = [](const auto &previous, const uint32_t *pixels, size_t size) {
        return previous.size() == size && std::memcmp(previous.data(), pixels, size * sizeof(uint32_t)) == 0;
    };
    // Smooth can upload the same completed battle picture on consecutive refreshes. Exact byte
    // and uniform comparisons also catch menu/settings changes and effect-only clock updates.
    auto scene_uniforms = uniforms_;
    scene_uniforms.battle = {};
    scene_uniforms.battle_groups = {}; // these change only the final output-resolution draw
    const bool unchanged =
        !presentation_diagnostics::fresh() && !dirty_ &&
        std::memcmp(&scene_uniforms, &cached_uniforms_, sizeof(scene_uniforms)) == 0 &&
        std::memcmp(light_positions_.data(), cached_positions_.data(), sizeof(light_positions_)) == 0 &&
        std::memcmp(light_colors_.data(), cached_colors_.data(), sizeof(light_colors_)) == 0 &&
        std::memcmp(light_beams_.data(), cached_beams_.data(), sizeof(light_beams_)) == 0 &&
        same(cached_source_, source.data(), source.size()) &&
        same(cached_original_, wsc.data(), wsc.size()) &&
        (!gb_needed || same(cached_gb_, gb.data(), gb.size())) && same(cached_water_, water, dry_.size()) &&
        same(cached_mask_, upload_mask_.data(), upload_mask_.size());
    if (unchanged)
        return true;
    dirty_ = true;
    cached_uniforms_ = scene_uniforms;
    cached_positions_ = light_positions_;
    cached_colors_ = light_colors_;
    cached_beams_ = light_beams_;
    return upload_pixels(source_, source.data(), 224, 144, cached_source_) &&
           upload_pixels(original_, wsc.data(), 224, 144, cached_original_) &&
           (!gb_needed || upload_pixels(gb_, gb.data(), 224, 144, cached_gb_)) &&
           upload_pixels(water_, water, 224, 144, cached_water_) &&
           upload_pixels(mask_, upload_mask_.data(), 224, 144, cached_mask_);
}
bool EnhancedGraphics::build_influences() {
    CpuScope cpu_scope("EnhancedGraphics::build_influences");
    GpuPassTimer gpu_time(renderer_, "influence");
    influence_.work = {};
    InfluenceBuffer::Parameters u;
    u.grid = {uniforms_.effect_grid[0], uniforms_.effect_grid[1], 0, 0};
    u.options = {0, uniforms_.lighting[2], uniforms_.lighting[3], uniforms_.control[1]};
    for (int plane = 0; plane < 4; ++plane) {
        const bool active =
            plane == 3 ? uniforms_.scene[0] > 0.5 && uniforms_.scene[2] > 0
                       : uniforms_.lighting[1] > 0 &&
                             std::any_of(influence_parameters_.lights.begin(),
                                         influence_parameters_.lights.end(), [&](const PointLight &l) {
                                             return plane == 0 ? l.type < 6
                                                               : (plane == 1 ? l.type == 6 : l.type >= 6);
                                         });
        if (!active && !influence_active_[size_t(plane)])
            continue;
        influence_active_[size_t(plane)] = active;
        if (!influence_.begin(plane))
            return false;
        bool ok = true;
        if (plane < 3 && uniforms_.lighting[1] > 0) {
            for (size_t i = 0; i < std::min<size_t>(256, influence_parameters_.lights.size()); ++i) {
                const auto &l = influence_parameters_.lights[i];
                if ((plane == 0 && l.type >= 6) || (plane > 0 && l.type < 6) || (plane == 1 && l.type == 7))
                    continue;
                if (!influence_.light(l, u, plane)) {
                    ok = false;
                    break;
                }
            }
        } else if (plane == 3 && uniforms_.scene[0] > 0.5) {
            for (size_t i = 0; i < size_t(uniforms_.scene[2]); ++i) {
                u.position = uniforms_.actors[i];
                const float x = std::abs(u.position[2]) * 1.582f, y = std::abs(u.position[3]) * 1.582f;
                if (!influence_.shadow(u, {u.position[0] - x, u.position[1] - y, 2 * x, 2 * y})) {
                    ok = false;
                    break;
                }
            }
        }
        ok = influence_.end() && ok;
        if (!ok)
            return false;
    }
    influence_work = influence_.work;
    return true;
}
SDL_Texture *EnhancedGraphics::render(bool force_redraw) {
    CpuScope cpu_scope("EnhancedGraphics::render");
    if (!ready())
        return nullptr;
    last_work = {};
    influence_work = {};
    // Flat scenes only change on upload. Smooth pacing still composites every refresh, but
    // repeated battle/dialogue pictures need no repeat extraction, blur, or scene work.
    if (!presentation_diagnostics::fresh() && !force_redraw && !external_ && !dirty_)
        return targets_[3];
    if (!build_influences())
        return nullptr;
    if (presentation_diagnostics::cpu_only()) {
        dirty_ = false;
        return targets_[3];
    }
    auto *old = SDL_GetRenderTarget(renderer_);
    bool ok = true;
    for (size_t i = 0; i < 4 && ok; ++i) {
        if (i < 3 && (uniforms_.control[3] <= 0 || uniforms_.scene[3] <= 0))
            continue;
        const char *names[]{"bloom-extract", "bloom-x", "bloom-y", "effect-composite"};
        GpuPassTimer gpu_time(renderer_, names[i]);
        const auto start = timing_enabled ? SDL_GetTicksNS() : 0;
        uniforms_.control[0] = i == 2 ? 4.f : (i == 3 ? 2.f : float(i));
        ok = SDL_SetRenderTarget(renderer_, targets_[i]) &&
             SDL_SetGPURenderStateFragmentUniforms(states_[i], 0, &uniforms_, sizeof(uniforms_)) &&
             SDL_SetGPURenderState(renderer_, states_[i]) &&
             SDL_RenderTexture(renderer_, source_, nullptr, nullptr);
        if (ok) {
            float w = 0, h = 0;
            SDL_GetTextureSize(targets_[i], &w, &h);
            const auto pixels = uint64_t(w) * uint64_t(h);
            last_work.shaded_pixels += pixels;
            if (i < 3)
                last_work.bloom_pixels += pixels;
            ++last_work.passes;
        }
        if (timing_enabled)
            pass_ns[i] += SDL_GetTicksNS() - start;
    }
    if (ok && external_) {
        {
            GpuPassTimer gpu_time(renderer_, "effect-multiply-layer");
            uniforms_.control[0] = 7;
            ok = SDL_SetRenderTarget(renderer_, targets_[4]) &&
                 SDL_SetGPURenderStateFragmentUniforms(states_[7], 0, &uniforms_, sizeof(uniforms_)) &&
                 SDL_SetGPURenderState(renderer_, states_[7]) &&
                 SDL_RenderTexture(renderer_, source_, nullptr, nullptr);
            last_work.shaded_pixels += 448u * 288u;
            ++last_work.passes;
        }
        GpuPassTimer gpu_time(renderer_, "effect-layer-finish");
        uniforms_.control[0] = 6;
        ok = SDL_SetRenderTarget(renderer_, targets_[5]) &&
             SDL_SetGPURenderStateFragmentUniforms(states_[6], 0, &uniforms_, sizeof(uniforms_)) &&
             SDL_SetGPURenderState(renderer_, states_[6]) &&
             SDL_RenderTexture(renderer_, source_, nullptr, nullptr);
        float w = 0, h = 0;
        SDL_GetTextureSize(targets_[5], &w, &h);
        last_work.shaded_pixels += uint64_t(w) * uint64_t(h);
        ++last_work.passes;
    }
    ok = SDL_SetGPURenderState(renderer_, nullptr) && ok;
    ok = SDL_SetRenderTarget(renderer_, old) && ok;
    if (ok)
        dirty_ = false;
    return ok ? targets_[external_ ? 5 : 3] : nullptr;
}
bool EnhancedGraphics::restore_ui(SDL_Texture *ordinary, const SDL_FRect &destination) {
    GpuPassTimer gpu_time(renderer_, "ui-restore");
    if (ui_regions.empty()) {
        ui_regions.pixels = ui_regions.draws = 0;
        return true;
    }
    const auto start = timing_enabled ? SDL_GetTicksNS() : 0;
    uniforms_.control[0] = 3;
    const bool ok = SDL_SetGPURenderStateFragmentUniforms(states_[4], 0, &uniforms_, sizeof(uniforms_)) &&
                    SDL_SetTextureBlendMode(ordinary, SDL_BLENDMODE_BLEND) &&
                    SDL_SetGPURenderState(renderer_, states_[4]) &&
                    ui_regions.draw(renderer_, destination, [&] {
                        return SDL_RenderTexture(renderer_, ordinary, nullptr, &destination);
                    });
    SDL_SetGPURenderState(renderer_, nullptr);
    SDL_SetTextureBlendMode(ordinary, SDL_BLENDMODE_NONE);
    if (timing_enabled)
        ui_ns += SDL_GetTicksNS() - start;
    return ok;
}
bool EnhancedGraphics::present(SDL_Texture *scaled_scene, SDL_Texture *ordinary, const SDL_FRect &dst,
                               bool restore_only) {
    CpuScope cpu_scope("EnhancedGraphics::present");
    GpuPassTimer gpu_time(renderer_, uniforms_.battle[0] > 0 ? "battle-lighting-final" : "final-scene-ui");
    if (!restore_only)
        final_work = {};
    ui_regions.pixels = ui_regions.draws = 0;
    if (!ready() || external_ || !scaled_scene || !ordinary)
        return false;
    const int width = int(dst.w), height = int(dst.h);
    if (!states_[5] || ordinary != finish_ordinary_ || width != finish_width_ || height != finish_height_) {
        SDL_DestroyGPURenderState(states_[5]);
        states_[5] = nullptr;
        const auto gpu = [](SDL_Texture *t) {
            return static_cast<SDL_GPUTexture *>(SDL_GetPointerProperty(
                SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
        };
        SDL_ScaleMode scale{};
        SDL_GetTextureScaleMode(ordinary, &scale);
        SDL_GPUTextureSamplerBinding bindings[] = {
            {gpu(mask_), nearest_},
            {gpu(gb_), linear_},
            {gpu(gb_), nearest_},
            {gpu(ordinary), scale == SDL_SCALEMODE_LINEAR ? linear_ : nearest_},
            {gpu(water_), nearest_},
            {gpu(influence_.texture(0)), linear_},
            {gpu(influence_.texture(1)), linear_},
            {gpu(influence_.texture(2)), linear_},
            {gpu(influence_.texture(3)), linear_},
            {gpu(water_pattern_), linear_},
            {gpu(gb_), linear_}};
        SDL_GPURenderStateCreateInfo state{};
        state.fragment_shader = shader_.get();
        state.num_sampler_bindings = 11;
        state.sampler_bindings = bindings;
        states_[5] = SDL_CreateGPURenderState(renderer_, &state);
        if (!states_[5])
            return false;
        finish_ordinary_ = ordinary;
        finish_width_ = width;
        finish_height_ = height;
    }
    uniforms_.control[0] = restore_only ? 5.25f : 5.f;
    const bool ok =
        SDL_SetGPURenderStateFragmentUniforms(states_[5], 0, &uniforms_, sizeof(uniforms_)) &&
        SDL_SetGPURenderState(renderer_, states_[5]) &&
        (restore_only
             ? sharp_ui_regions_.draw(
                   renderer_, dst, [&] { return SDL_RenderTexture(renderer_, scaled_scene, nullptr, &dst); })
             : SDL_RenderTexture(renderer_, scaled_scene, nullptr, &dst));
    SDL_SetGPURenderState(renderer_, nullptr);
    if (ok) {
        final_work.shaded_pixels +=
            restore_only ? sharp_ui_regions_.pixels : uint64_t(width) * uint64_t(height);
        ++final_work.passes;
    }
    return ok;
}
void EnhancedGraphics::shutdown() {
    if (!renderer_)
        return;
    SDL_SetGPURenderState(renderer_, nullptr);
    auto *device = SDL_GetGPURendererDevice(renderer_);
    for (auto &s : states_) {
        if (s)
            SDL_DestroyGPURenderState(s);
        s = nullptr;
    }
    shader_.reset();
    if (nearest_)
        SDL_ReleaseGPUSampler(device, nearest_);
    if (linear_)
        SDL_ReleaseGPUSampler(device, linear_);
    for (auto &t : targets_) {
        SDL_DestroyTexture(t);
        t = nullptr;
    }
    if (!external_) {
        SDL_DestroyTexture(source_);
        SDL_DestroyTexture(original_);
        SDL_DestroyTexture(mask_);
        SDL_DestroyTexture(water_);
    }
    SDL_DestroyTexture(water_pattern_);
    water_pattern_ = nullptr;
    influence_.shutdown();
    influence_active_ = {};
    SDL_DestroyTexture(gb_);
    finish_ordinary_ = nullptr;
    finish_width_ = finish_height_ = 0;
    cached_source_.clear();
    cached_original_.clear();
    cached_gb_.clear();
    cached_water_.clear();
    cached_mask_.clear();
    external_ = false;
    dirty_ = true;
    source_ = original_ = gb_ = mask_ = water_ = nullptr;
    nearest_ = linear_ = nullptr;
    renderer_ = nullptr;
}
} // namespace sawgod
