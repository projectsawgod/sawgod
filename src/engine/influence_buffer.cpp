#include "engine/influence_buffer.h"
#include "engine/cpu_scope.h"
#include "engine/falloff_field.h"
#include "engine/gpu_warmup.h"
#include "engine/influence_stamp.h"
#include "engine/presentation_diagnostics.h"
#include "engine/shaders/influence.generated.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace sawgod {
bool InfluenceBuffer::init(SDL_Renderer *renderer, int width, int height, int planes, bool spell_patches) {
    shutdown();
    renderer_ = renderer;
    width_ = width;
    height_ = height;
    auto *device = SDL_GetGPURendererDevice(renderer);
    if (!device)
        return false;
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = info.num_uniform_buffers = 1;
    if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = shaders::influence_spirv;
        info.code_size = sizeof(shaders::influence_spirv);
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = shaders::influence_dxil;
        info.code_size = sizeof(shaders::influence_dxil);
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = reinterpret_cast<const Uint8 *>(shaders::influence_msl);
        info.code_size = sizeof(shaders::influence_msl) - 1;
    }
    if (!info.code_size)
        return SDL_SetError("Influence shader format unavailable");
    shader_ = SDL_CreateGPUShader(device, &info);
    SDL_GPURenderStateCreateInfo state{};
    state.fragment_shader = shader_;
    if (shader_)
        state_ = SDL_CreateGPURenderState(renderer, &state);
    dummy_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
    const uint32_t white = 0xFFFFFFFF;
    if (!state_ || !dummy_ || !SDL_UpdateTexture(dummy_, nullptr, &white, 4))
        return false;
    const auto maximum = influence_blend();
    if (!SDL_SetTextureBlendMode(dummy_, maximum))
        return false;
    for (int i = 0; i < planes; ++i) {
        auto props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA64_FLOAT);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
        // Numerical fields, not linear-light images. Vertex COLOR also carries exact native anchors.
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
        targets_[size_t(i)] = SDL_CreateTextureWithProperties(renderer, props);
        SDL_DestroyProperties(props);
        if (!targets_[size_t(i)])
            return false;
        SDL_SetTextureBlendMode(targets_[size_t(i)], SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(targets_[size_t(i)], SDL_SCALEMODE_LINEAR);
    }
    for (int i = 0; i < planes; ++i)
        if (!begin(i) || !end())
            return false;
    if (planes == 4 && spell_patches) {
        // The game's spell radii are 4 and 8. One bounded texture pool replaces driver allocation
        // on every newly animated mask, while retaining each texture's exact size and UV mapping.
        falloff_cache_.reserve(256);
        patch_textures_.reserve(512);
        for (float radius : {4.f, 8.f}) {
            if (falloff_pixels({}, radius).empty())
                return false;
            for (int i = 0; i < 256; ++i) {
                auto *texture = new_patch_texture(radius);
                if (!texture)
                    return false;
                patch_textures_.push_back({radius, texture, true});
            }
        }
    }
    Parameters p;
    p.extent[2] = 4;
    if (!warm_gpu_state(renderer_, state_, &p, sizeof(p), true)) {
        shutdown();
        return false;
    }
    work = {};
    return true;
}
bool InfluenceBuffer::begin(int plane) {
    presentation_diagnostics::trace().influence_begin(this, plane);
    if (presentation_diagnostics::fresh()) {
        for (auto &entry : falloff_cache_)
            release_patch_texture(entry.texture);
        falloff_cache_.clear();
        for (auto &entry : stamps_)
            SDL_DestroyTexture(entry.texture);
        stamps_.clear();
    }
    for (auto &entry : falloff_cache_)
        entry.vertices.clear();
    for (auto &entry : stamps_)
        entry.vertices.clear();
    if (presentation_diagnostics::cpu_only())
        return true;
    old_ = SDL_GetRenderTarget(renderer_);
    if (!SDL_SetGPURenderState(renderer_, nullptr) || !SDL_SetRenderTarget(renderer_, texture(plane)))
        return false;
    SDL_FColor old_color{};
    SDL_GetRenderDrawColorFloat(renderer_, &old_color.r, &old_color.g, &old_color.b, &old_color.a);
    SDL_SetRenderDrawColorFloat(renderer_, 0, 0, 0, 0);
    const bool ok = SDL_RenderClear(renderer_);
    SDL_SetRenderDrawColorFloat(renderer_, old_color.r, old_color.g, old_color.b, old_color.a);
    work.clear_pixels += uint64_t(width_) * height_;
    ++work.passes;
    return ok;
}
bool InfluenceBuffer::quad(Parameters &p, const SDL_FRect &b, SDL_Texture *falloff) {
    // Expand a snapped support by one cell; shader still enforces the exact analytic support.
    const float pad_x = p.options[3] > 0.5 ? (p.camera[0] > 0 ? p.grid[2] / p.grid[0] : 224 / p.grid[0]) : 0;
    const float pad_y = p.options[3] > 0.5 ? (p.camera[0] > 0 ? p.grid[2] / p.grid[1] : 144 / p.grid[1]) : 0;
    const float x0 = std::max(0.f, std::floor((b.x - pad_x) * width_ / 224));
    const float y0 = std::max(0.f, std::floor((b.y - pad_y) * height_ / 144));
    const float x1 = std::min(float(width_), std::ceil((b.x + b.w + pad_x) * width_ / 224));
    const float y1 = std::min(float(height_), std::ceil((b.y + b.h + pad_y) * height_ / 144));
    if (x1 <= x0 || y1 <= y0)
        return true;
    const SDL_FColor c{1, 1, 1, 1};
    const SDL_Vertex v[] = {{{x0, y0}, c, {x0 / width_, y0 / height_}},
                            {{x1, y0}, c, {x1 / width_, y0 / height_}},
                            {{x1, y1}, c, {x1 / width_, y1 / height_}},
                            {{x0, y1}, c, {x0 / width_, y1 / height_}}};
    const int indices[] = {0, 1, 2, 0, 2, 3};
    work.pixels += uint64_t(x1 - x0) * uint64_t(y1 - y0);
    ++work.quads;
    return presentation_diagnostics::cpu_only() ||
           (SDL_SetGPURenderStateFragmentUniforms(state_, 0, &p, sizeof(p)) &&
            SDL_SetGPURenderState(renderer_, state_) &&
            SDL_RenderGeometry(renderer_, falloff ? falloff : dummy_, v, 4, indices, 6));
}
InfluenceBuffer::FalloffPatch *InfluenceBuffer::patch_falloff(const std::array<uint32_t, 2> &mask,
                                                              float radius) {
    CpuScope cpu_scope("InfluenceBuffer::patch_falloff");
    const int halo = int(std::ceil(radius)) + 1;
    for (auto &entry : falloff_cache_)
        if (entry.mask == mask && entry.radius == radius) {
            entry.used = ++falloff_clock_;
            return &entry;
        }
    const int side = (8 + 2 * halo) * 3;
    auto half_pixels = falloff_pixels(mask, radius);
    if (half_pixels.empty())
        return nullptr;
    ++work.patch_builds;
    // Never recycle a patch whose geometry is still waiting to be submitted in this plane.
    auto oldest = falloff_cache_.end();
    if (falloff_cache_.size() >= 256) {
        for (auto i = falloff_cache_.begin(); i != falloff_cache_.end(); ++i)
            if (i->vertices.empty() && (oldest == falloff_cache_.end() || i->used < oldest->used))
                oldest = i;
        if (oldest != falloff_cache_.end())
            release_patch_texture(oldest->texture);
    }
    SDL_Texture *texture = nullptr;
    for (auto &entry : patch_textures_)
        if (entry.available && entry.radius == radius) {
            entry.available = false;
            texture = entry.texture;
            break;
        }
    if (!texture) {
        texture = new_patch_texture(radius);
        ++work.patch_allocations;
    }
    if (!texture || !SDL_UpdateTexture(texture, nullptr, half_pixels.data(), side * 8)) {
        release_patch_texture(texture);
        if (oldest != falloff_cache_.end())
            falloff_cache_.erase(oldest);
        return nullptr;
    }
    presentation_diagnostics::trace().texture(texture, half_pixels);
    if (oldest != falloff_cache_.end()) {
        *oldest = {mask, radius, texture, ++falloff_clock_, {}};
        return &*oldest;
    }
    falloff_cache_.push_back({mask, radius, texture, ++falloff_clock_, {}});
    return &falloff_cache_.back();
}
SDL_Texture *InfluenceBuffer::new_patch_texture(float radius) {
    const int side = (8 + 2 * (int(std::ceil(radius)) + 1)) * 3;
    auto props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA64_FLOAT);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, side);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, side);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
    auto *texture = SDL_CreateTextureWithProperties(renderer_, props);
    SDL_DestroyProperties(props);
    if (!texture)
        return nullptr;
    SDL_BlendMode maximum;
    SDL_GetTextureBlendMode(dummy_, &maximum);
    if (!SDL_SetTextureBlendMode(texture, maximum) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    return texture;
}
void InfluenceBuffer::release_patch_texture(SDL_Texture *texture) {
    if (!texture)
        return;
    for (auto &entry : patch_textures_)
        if (entry.texture == texture) {
            entry.available = true;
            return;
        }
    SDL_DestroyTexture(texture);
}
bool InfluenceBuffer::light(const PointLight &light, Parameters p, int plane) {
    p.position = light.position;
    p.position[3] = float(light.type);
    p.emission = light.emission;
    p.extent = light.type == 4 ? light.beam : std::array<float, 4>{light.extent[0], light.extent[1], 0, 0};
    p.options[0] = float(plane);
    const float radius = light.type < 6 ? std::min(light.position[2], 32.f) : light.position[2];
    if (light.type >= 6 && (light.source_pixels[0] || light.source_pixels[1])) {
        p.position[3] += 2;
        const std::array<float, 2> origin{std::floor(light.position[0] / 8) * 8 + 0.5f,
                                          std::floor(light.position[1] / 8) * 8 + 0.5f};
        const float pad = p.options[3] > 0.5f ? 1.f : 0.f;
        const SDL_FRect bounds{origin[0] - radius, origin[1] - radius, 7 + 2 * radius, 7 + 2 * radius};
        if (p.grid[0] == 224 && p.grid[1] == 144 &&
            (bounds.x - pad >= 224 || bounds.y - pad >= 144 || bounds.x + bounds.w + pad <= 0 ||
             bounds.y + bounds.h + pad <= 0))
            return true;
        // A mask's sampled falloff replaces every emitting-pixel disc and its fragment math.
        // Fully occupied masks share one cache entry too, so their patches can join the same batch.
        auto *patch = patch_falloff(light.source_pixels, radius);
        if (!patch)
            return false;
        const int halo = int(std::ceil(radius)) + 1;
        p.extent[2] = 2;
        p.scroll = {origin[0] - 0.5f - halo, origin[1] - 0.5f - halo, float(8 + 2 * halo),
                    float(8 + 2 * halo)};
        if (p.grid[0] != 224 || p.grid[1] != 144)
            return quad(p, bounds, patch->texture); // arbitrary mod grids retain their exact snap mapping
        // Batch equal masks. Color carries numerical emission; UV carries patch-local native position.
        // Patch origins and texture sides are integral, so local snapping equals native screen snapping.
        const float x0 = std::max(0.f, std::floor((bounds.x - pad) * width_ / 224));
        const float y0 = std::max(0.f, std::floor((bounds.y - pad) * height_ / 144));
        const float x1 = std::min(float(width_), std::ceil((bounds.x + bounds.w + pad) * width_ / 224));
        const float y1 = std::min(float(height_), std::ceil((bounds.y + bounds.h + pad) * height_ / 144));
        if (x1 <= x0 || y1 <= y0)
            return true;
        auto rgb = light.emission;
        if (plane == 1) {
            rgb[0] = (13 * light.emission[0] + 2 * light.emission[1] + light.emission[2]) / 17;
            rgb[1] = (12 * light.emission[1] + 4 * light.emission[2]) / 17;
            rgb[2] = (3 * light.emission[0] + 2 * light.emission[1] + 11 * light.emission[2]) / 17;
        }
        const SDL_FColor color{rgb[0], rgb[1], rgb[2], light.type == 7 ? -rgb[3] : rgb[3]};
        const auto vertex = [&](float x, float y) {
            return SDL_Vertex{{x, y},
                              color,
                              {(x * 224 / width_ - p.scroll[0]) / p.scroll[2],
                               (y * 144 / height_ - p.scroll[1]) / p.scroll[3]}};
        };
        const SDL_Vertex v[]{vertex(x0, y0), vertex(x1, y0), vertex(x1, y1), vertex(x0, y1)};
        for (int i : {0, 1, 2, 0, 2, 3})
            patch->vertices.push_back(v[i]);
        work.pixels += uint64_t(x1 - x0) * uint64_t(y1 - y0);
        ++work.quads;
        patch_parameters_ = p;
        patch_parameters_.extent[2] = 3;
        return true;
    }
    const float ex = light.type == 4 ? 0 : light.extent[0], ey = light.type == 4 ? 0 : light.extent[1];
    auto rgb = light.emission;
    if (plane < 2) {
        rgb[0] = (13 * light.emission[0] + 2 * light.emission[1] + light.emission[2]) / 17;
        rgb[1] = (12 * light.emission[1] + 4 * light.emission[2]) / 17;
        rgb[2] = (3 * light.emission[0] + 2 * light.emission[1] + 11 * light.emission[2]) / 17;
    }
    SDL_FRect bounds{light.position[0] - ex - radius, light.position[1] - ey - radius, 2 * (ex + radius),
                     2 * (ey + radius)};
    if (light.type == 4) {
        // Rotate the cached upward shaft without evaluating the beam at every scene pixel.
        // Current mod directions are cardinal; horizontal shafts swap their bounds/UV axes below.
        const float side = radius * 2;
        bounds = {light.position[0] - radius, light.position[1] - radius, side, side};
    }
    // Projected source lists retain a safety margin. Invisible sources must not generate/cache
    // numerical textures just because a map has newly entered that margin.
    if (bounds.x >= 224 || bounds.y >= 144 || bounds.x + bounds.w <= 0 || bounds.y + bounds.h <= 0)
        return true;
    auto *sprite = stamp({float(light.type), radius, ex, ey, float(p.options[1]), 0});
    if (!sprite)
        return false;
    patch_parameters_ = p;
    const size_t first = sprite->vertices.size();
    if (!stamp_quad(*sprite, bounds, {rgb[0], rgb[1], rgb[2], light.type == 7 ? -rgb[3] : rgb[3]}))
        return false;
    if (light.type == 4) {
        const float dx = light.beam[0], dy = light.beam[1];
        for (size_t i = first; i < sprite->vertices.size(); ++i) {
            auto &uv = sprite->vertices[i].tex_coord;
            const float x = uv.x - 0.5f, y = uv.y - 0.5f;
            uv = {0.5f + (-dy * x + dx * y), 0.5f + (-dx * x - dy * y)};
        }
    }
    return true;
}
bool InfluenceBuffer::shadow(Parameters p, const SDL_FRect &) {
    const bool hard = p.position[3] < 0;
    auto *sprite = stamp({-1, float(hard), 0, 0, 0, 0});
    if (!sprite)
        return false;
    float x = p.position[0], y = p.position[1], rx = std::abs(p.position[2]), ry = std::abs(p.position[3]);
    if (p.camera[0] > 0) {
        float actor_scale = 0;
        x -= p.scroll[0];
        y -= p.scroll[1];
        if (p.grid[2] > 0) {
            x = (x - p.scroll[2]) * p.grid[2] / p.grid[0];
            y = (y - p.scroll[3]) * p.grid[2] / p.grid[1];
            rx *= p.grid[2] / p.grid[0];
            ry *= p.grid[2] / p.grid[1];
            actor_scale = p.grid[2];
        } else {
            const float base = y - 79, scale = p.camera[0] / (p.camera[0] * p.camera[3] - base * p.camera[1]);
            x = 112 + (x - 112) * scale;
            y = 79 + base * p.camera[2] * scale;
            rx *= scale;
            ry *= p.camera[2] * scale;
            actor_scale = scale * p.grid[0];
        }
        // Match the upright card's lattice, then move to its actual opaque feet in screen space.
        // Projecting the padding onto the tilted ground would leave it detached from the card.
        if (p.grid[2] == std::floor(p.grid[2])) {
            actor_scale = std::max(1.f, std::floor(actor_scale + 0.5f));
            x = std::floor(x * p.grid[0] + 0.5f) / p.grid[0];
            y = std::floor(y * p.grid[1] + 0.5f) / p.grid[1];
        }
        y += p.grid[3] * actor_scale / p.grid[1];
    }
    // A console-style screen-space ellipse follows the projected foot continuously.
    const float reach = std::sqrt(2.5f);
    patch_parameters_ = p;
    return stamp_quad(*sprite, {x - rx * reach, y - ry * reach, rx * 2 * reach, ry * 2 * reach},
                      {1, 1, 1, 1});
}
InfluenceBuffer::Stamp *InfluenceBuffer::stamp(const std::array<float, 6> &key) {
    CpuScope cpu_scope("InfluenceBuffer::stamp");
    for (auto &s : stamps_)
        if (s.key == key) {
            s.used = ++falloff_clock_;
            return &s;
        }
    const bool profile = CpuScope::enabled();
    const Uint64 raster_start = profile ? SDL_GetTicksNS() : 0;
    const auto image = build_influence_stamp(key);
    const int side = image.side, phases = image.phases;
    const auto &half = image.pixels;
    if (half.empty())
        return nullptr;
    const bool beam = int(key[0]) == 4 && key[4] > 1.5f;
    const Uint64 build_end = profile ? SDL_GetTicksNS() : 0;
    auto props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA64_FLOAT);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, side);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, side * phases);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
    auto *texture = SDL_CreateTextureWithProperties(renderer_, props);
    SDL_DestroyProperties(props);
    if (!texture)
        return nullptr;
    SDL_BlendMode maximum;
    SDL_GetTextureBlendMode(dummy_, &maximum);
    if (!SDL_SetTextureBlendMode(texture, maximum) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) ||
        !SDL_UpdateTexture(texture, nullptr, half.data(), side * 8)) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    presentation_diagnostics::trace().texture(texture, half);
    if (profile)
        SDL_Log("GPU stamp type=%.0f radius=%.3f extent=%.3f,%.3f style=%.0f side=%d phases=%d build=%.3f "
                "texture=%.3f ms",
                key[0], key[1], key[2], key[3], key[4], side, phases, double(build_end - raster_start) / 1e6,
                double(SDL_GetTicksNS() - build_end) / 1e6);
    // Never evict a texture with vertices still waiting in this pass.
    if (stamps_.size() >= 256) {
        auto oldest = stamps_.end();
        for (auto i = stamps_.begin(); i != stamps_.end(); ++i)
            if (i->vertices.empty() && (oldest == stamps_.end() || i->used < oldest->used))
                oldest = i;
        if (oldest != stamps_.end()) {
            SDL_DestroyTexture(oldest->texture);
            *oldest = {key, texture, ++falloff_clock_, beam, {}};
            return &*oldest;
        }
    }
    stamps_.push_back({key, texture, ++falloff_clock_, beam, {}});
    return &stamps_.back();
}
bool InfluenceBuffer::stamp_quad(Stamp &s, const SDL_FRect &b, const SDL_FColor &color) {
    const float x0 = std::max(0.f, std::floor(b.x * width_ / 224)),
                y0 = std::max(0.f, std::floor(b.y * height_ / 144));
    const float x1 = std::min(float(width_), std::ceil((b.x + b.w) * width_ / 224)),
                y1 = std::min(float(height_), std::ceil((b.y + b.h) * height_ / 144));
    if (x1 <= x0 || y1 <= y0)
        return true;
    const auto vertex = [&](float x, float y) {
        return SDL_Vertex{{x, y}, color, {(x * 224 / width_ - b.x) / b.w, (y * 144 / height_ - b.y) / b.h}};
    };
    const SDL_Vertex v[]{vertex(x0, y0), vertex(x1, y0), vertex(x1, y1), vertex(x0, y1)};
    for (int i : {0, 1, 2, 0, 2, 3})
        s.vertices.push_back(v[i]);
    work.pixels += uint64_t(x1 - x0) * uint64_t(y1 - y0);
    ++work.quads;
    return true;
}
bool InfluenceBuffer::end() {
    CpuScope cpu_scope("InfluenceBuffer::end");
    if (auto &t = presentation_diagnostics::trace(); t.enabled()) {
        std::vector<uint64_t> batches;
        const auto batch = [&](const auto &entry) {
            if (entry.vertices.empty())
                return;
            presentation_diagnostics::Hash h;
            h.add(t.texture(entry.texture));
            for (const auto &v : entry.vertices) {
                h.add(v.position.x);
                h.add(v.position.y);
                h.add(v.color.r);
                h.add(v.color.g);
                h.add(v.color.b);
                h.add(v.color.a);
                h.add(v.tex_coord.x);
                h.add(v.tex_coord.y);
            }
            batches.push_back(h.value);
        };
        for (const auto &s : stamps_)
            batch(s);
        for (const auto &s : falloff_cache_)
            batch(s);
        // Maximum-blended batches commute; cache insertion order is not a picture input.
        std::sort(batches.begin(), batches.end());
        presentation_diagnostics::Hash h;
        h.range(batches);
        t.influence_end(this, h);
    }
    if (presentation_diagnostics::cpu_only())
        return true;
    bool batches_ok = true, bound = false;
    Parameters previous{};
    const auto bind = [&](const Parameters &p) {
        // SDL flushes queued uses of a state before every uniform setter, even for identical bytes.
        // Most masks/stamps share these uniforms: retain the binding across their texture batches.
        if (!bound || std::memcmp(&previous, &p, sizeof(p))) {
            if (!SDL_SetGPURenderStateFragmentUniforms(state_, 0, &p, sizeof(p)))
                return false;
            previous = p;
        }
        if (!bound) {
            if (!SDL_SetGPURenderState(renderer_, state_))
                return false;
            bound = true;
        }
        return true;
    };
    for (auto &s : stamps_) {
        if (s.vertices.empty())
            continue;
        auto p = patch_parameters_;
        p.extent[2] = 4;
        p.extent[3] = s.beam ? 1.f : 0.f;
        if (!bind(p) ||
            !SDL_RenderGeometry(renderer_, s.texture, s.vertices.data(), int(s.vertices.size()), nullptr, 0))
            batches_ok = false;
    }
    for (auto &entry : falloff_cache_) {
        if (entry.vertices.empty())
            continue;
        const float side = float(8 + 2 * (int(std::ceil(entry.radius)) + 1));
        patch_parameters_.scroll = {0, 0, side, side};
        if (!bind(patch_parameters_) || !SDL_RenderGeometry(renderer_, entry.texture, entry.vertices.data(),
                                                            int(entry.vertices.size()), nullptr, 0))
            batches_ok = false;
    }
    const bool ok = SDL_SetGPURenderState(renderer_, nullptr) && SDL_SetRenderTarget(renderer_, old_);
    old_ = nullptr;
    return ok && batches_ok;
}
void InfluenceBuffer::shutdown() {
    if (!renderer_)
        return;
    SDL_SetGPURenderState(renderer_, nullptr);
    SDL_DestroyGPURenderState(state_);
    state_ = nullptr;
    if (shader_)
        SDL_ReleaseGPUShader(SDL_GetGPURendererDevice(renderer_), shader_);
    shader_ = nullptr;
    for (const auto &entry : falloff_cache_)
        release_patch_texture(entry.texture);
    for (const auto &entry : patch_textures_)
        SDL_DestroyTexture(entry.texture);
    patch_textures_.clear();
    for (const auto &s : stamps_)
        SDL_DestroyTexture(s.texture);
    stamps_.clear();
    falloff_cache_.clear();
    falloff_clock_ = 0;
    SDL_DestroyTexture(dummy_);
    dummy_ = nullptr;
    for (auto *t : targets_)
        SDL_DestroyTexture(t);
    targets_ = {};
    renderer_ = nullptr;
}
} // namespace sawgod
