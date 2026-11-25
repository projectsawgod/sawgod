#include "engine/startup_timing.h"
#include "app/mode7_renderer.h"
#include "engine/cpu_scope.h"
#include "engine/gpu_pass_timer.h"
#include "engine/gpu_warmup.h"
#include "engine/pixel_upload.h"
#include "engine/presentation_diagnostics.h"
#include "engine/screen.h"
#include "engine/shaders/mode7.generated.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace sawgod {
bool Mode7Renderer::init(SDL_Renderer *renderer) {
    sawgod::startup::Stage timing("Enhanced world scene passes");
    shutdown();
    auto *device = SDL_GetGPURendererDevice(renderer);
    if (!device)
        return SDL_SetError("Mode 7 requires SDL_GPU");
    renderer_ = renderer;
    if (!shadow_.init(renderer, 672, 432, 1))
        return false;
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 13;
    info.num_uniform_buffers = 1;
    if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = shaders::mode7_spirv;
        info.code_size = sizeof(shaders::mode7_spirv);
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = shaders::mode7_dxil;
        info.code_size = sizeof(shaders::mode7_dxil);
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = reinterpret_cast<const Uint8 *>(shaders::mode7_msl);
        info.code_size = sizeof(shaders::mode7_msl) - 1;
    } else {
        shutdown();
        return SDL_SetError("Mode 7 shader format unavailable");
    }
    shader_ = SDL_CreateGPUShader(device, &info);
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(device, &sampler);
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    linear_ = SDL_CreateGPUSampler(device, &sampler);
    const auto texture = [&](int access, int w, int h) {
        auto *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TextureAccess(access), w, h);
        if (t) {
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
        }
        return t;
    };
    world_[0] = texture(SDL_TEXTUREACCESS_STREAMING, 160, 160);
    world_[1] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 64);
    if (world_[1]) {
        SDL_SetTextureScaleMode(world_[1], SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(world_[1], SDL_BLENDMODE_NONE);
    }
    world_[2] = texture(SDL_TEXTUREACCESS_STREAMING, 16, 16);
    world_[3] = texture(SDL_TEXTUREACCESS_STREAMING, 256, 1024);
    for (size_t i = 0; i < mips_.size(); ++i)
        mips_[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 1280 >> i,
                                     1280 >> i);
    for (auto *t : mips_)
        if (t) {
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
        }
    targets_[0] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 224, 144);
    targets_[1] = texture(SDL_TEXTUREACCESS_TARGET, 224, 144);
    SDL_SetTextureScaleMode(targets_[0], SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(targets_[0], SDL_BLENDMODE_NONE);
    ui_ = texture(SDL_TEXTUREACCESS_STREAMING, 224, 144);
    scanlines_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 2, 144);
    if (!scanlines_)
        return false;
    SDL_SetTextureBlendMode(scanlines_, SDL_BLENDMODE_NONE);
    const auto gpu = [](SDL_Texture *t) {
        return static_cast<SDL_GPUTexture *>(SDL_GetPointerProperty(
            SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    };
    if (!shader_ || !sampler_ || !ui_ ||
        std::any_of(world_.begin(), world_.end(), [](auto *t) { return !t; }) ||
        std::any_of(targets_.begin(), targets_.end(), [](auto *t) { return !t; }) ||
        std::any_of(mips_.begin(), mips_.end(), [](auto *t) { return !t; })) {
        shutdown();
        return false;
    }
    // Generation states bind no pyramid target; SDL forbids target/sampler aliasing.
    for (int i = -1; i < 5; ++i) {
        std::array<SDL_GPUTextureSamplerBinding, 12> bindings{};
        for (size_t j = 0; j < 4; ++j)
            bindings[j] = {gpu(world_[j]), sampler_};
        bindings[4] = {gpu(ui_), sampler_};
        for (size_t j = 0; j < mips_.size(); ++j)
            bindings[5 + j] = {gpu(i < 0 ? mips_[j] : world_[0]), j == 0 ? sampler_ : linear_};
        bindings[10] = {gpu(i < 0 ? shadow_.texture(0) : world_[0]), sampler_};
        bindings[11] = {gpu(i < 0 ? scanlines_ : world_[0]), sampler_};
        SDL_GPURenderStateCreateInfo state{};
        state.fragment_shader = shader_;
        state.num_sampler_bindings = unsigned(bindings.size());
        state.sampler_bindings = bindings.data();
        auto *created = SDL_CreateGPURenderState(renderer, &state);
        if (i < 0)
            state_ = created;
        else
            mip_states_[size_t(i)] = created;
        if (!created) {
            shutdown();
            return false;
        }
    }
    if (!bind_scene())
        return false;
    uniforms_.control = {4, 0, 0, 0};
    uniforms_.scroll[2] = 160;
    const bool warm = warm_gpu_state(renderer_, state_, &uniforms_, sizeof(uniforms_));
    uniforms_ = {};
    if (!warm)
        shutdown();
    return warm;
}
bool Mode7Renderer::draw_mips(const SDL_Rect *visible) {
    GpuPassTimer gpu_time(renderer_, "atlas-mips");
    auto *old = SDL_GetRenderTarget(renderer_);
    const bool clipped = SDL_RenderClipEnabled(renderer_);
    SDL_Rect old_clip{};
    SDL_GetRenderClipRect(renderer_, &old_clip);
    bool ok = true;
    for (size_t i = 0; i < (visible ? 1 : mips_.size()) && ok; ++i) {
        uniforms_.control = {i == 0 ? 3.f : 4.f, 0, 0, 0};
        uniforms_.scroll[2] = float(1280 >> (i ? i - 1 : 0));
        auto *state = mip_states_[i];
        atlas_pixels +=
            visible ? uint64_t(visible->w) * uint64_t(visible->h) : uint64_t(1280 >> i) * uint64_t(1280 >> i);
        ok = SDL_SetRenderTarget(renderer_, mips_[i]) && SDL_SetRenderClipRect(renderer_, visible) &&
             SDL_SetGPURenderStateFragmentUniforms(state, 0, &uniforms_, sizeof(uniforms_)) &&
             SDL_SetGPURenderState(renderer_, state) &&
             SDL_RenderTexture(renderer_, i ? mips_[i - 1] : world_[0], nullptr, nullptr);
    }
    SDL_SetGPURenderState(renderer_, nullptr);
    SDL_SetRenderTarget(renderer_, old);
    SDL_SetRenderClipRect(renderer_, clipped ? &old_clip : nullptr);
    if (ok) {
        atlas_valid_ = visible ? *visible : SDL_Rect{0, 0, 1280, 1280};
        mips_valid_ = !visible;
    }
    return ok;
}

void Mode7Renderer::cache_palette_usage(const game::Mode7Surface &surface) {
    std::array<uint16_t, 1024> tile_colors{};
    for (size_t row = 0; row < surface.indices.size(); ++row) {
        const auto packed = surface.indices[row];
        for (unsigned shift = 0; shift < 32; shift += 4)
            tile_colors[row / 8] |= uint16_t(1u << ((packed >> shift) & 15));
    }
    palette_usage_.assign(80 * 80, {});
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 160; ++x) {
            // The resolver repeats border rows 32..63 into the top filler rows too.
            const int source_y = y < 4 ? y + 4 : y;
            const auto encoded = surface.words[size_t(source_y * 160 + x)];
            const unsigned word = ((encoded >> 16) & 255) | ((encoded >> 8) & 255) << 8;
            const unsigned tile = (word & 511) | ((word & 8192) ? 512 : 0);
            palette_usage_[size_t((y / 2) * 80 + x / 2)][(word >> 9) & 15] |= tile_colors[tile];
        }
}

bool Mode7Renderer::draw_palette_mips(const std::array<uint16_t, 16> &changed) {
    std::vector<SDL_Rect> regions;
    std::array<int, 80> previous;
    previous.fill(-1);
    for (int y = 0; y < 80; ++y) {
        std::array<int, 80> current;
        current.fill(-1);
        const auto dirty = [&](int x) {
            const auto &usage = palette_usage_[size_t(y * 80 + x)];
            for (size_t p = 0; p < changed.size(); ++p)
                if (usage[p] & changed[p])
                    return true;
            return false;
        };
        for (int x = 0; x < 80;) {
            if (!dirty(x)) {
                ++x;
                continue;
            }
            const int left = x++;
            while (x < 80 && dirty(x))
                ++x;
            const int same = previous[size_t(left)];
            if (same >= 0 && regions[size_t(same)].w == (x - left) * 16) {
                regions[size_t(same)].h += 16;
                current[size_t(left)] = same;
            } else {
                current[size_t(left)] = int(regions.size());
                regions.push_back({left * 16, y * 16, (x - left) * 16, 16});
            }
        }
        previous = current;
    }
    if (regions.empty())
        return true;
    GpuPassTimer gpu_time(renderer_, "atlas-mips");
    auto *old = SDL_GetRenderTarget(renderer_);
    const bool clipped = SDL_RenderClipEnabled(renderer_);
    SDL_Rect old_clip{};
    SDL_GetRenderClipRect(renderer_, &old_clip);
    bool ok = true;
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(regions.size() * 6);
    for (size_t level = 0; level < mips_.size() && ok; ++level) {
        vertices.clear();
        for (const auto &r : regions) {
            // All edges divide exactly at every mip: no affected reduction kernel crosses a region edge.
            const float left = float(r.x >> level), top = float(r.y >> level);
            const float right = float((r.x + r.w) >> level), bottom = float((r.y + r.h) >> level);
            const float extent = float(1280 >> level);
            const auto vertex = [&](float x, float y) {
                return SDL_Vertex{{x, y}, {1, 1, 1, 1}, {x / extent, y / extent}};
            };
            for (const auto &v : {vertex(left, top), vertex(right, top), vertex(right, bottom),
                                  vertex(left, top), vertex(right, bottom), vertex(left, bottom)})
                vertices.push_back(v);
            atlas_pixels += uint64_t(r.w >> level) * uint64_t(r.h >> level);
        }
        uniforms_.control = {level == 0 ? 3.f : 4.f, 0, 0, 0};
        uniforms_.scroll[2] = float(1280 >> (level ? level - 1 : 0));
        auto *state = mip_states_[level];
        ok = SDL_SetRenderTarget(renderer_, mips_[level]) && SDL_SetRenderClipRect(renderer_, nullptr) &&
             SDL_SetGPURenderStateFragmentUniforms(state, 0, &uniforms_, sizeof(uniforms_)) &&
             SDL_SetGPURenderState(renderer_, state) &&
             SDL_RenderGeometry(renderer_, level ? mips_[level - 1] : world_[0], vertices.data(),
                                int(vertices.size()), nullptr, 0);
    }
    SDL_SetGPURenderState(renderer_, nullptr);
    SDL_SetRenderTarget(renderer_, old);
    SDL_SetRenderClipRect(renderer_, clipped ? &old_clip : nullptr);
    if (!ok)
        mips_valid_ = false;
    return ok;
}

bool Mode7Renderer::warmup() {
    CpuScope cpu_scope("Mode7Renderer::warmup");
    cached_ui_.clear();
    if (!ready())
        return false;
    // The tiny pipeline probes compile shaders but do not touch the large atlas allocations or
    // execute their branches. First use used to stall the first field frame while music was playing.
    std::vector<uint32_t> zero(256 * 1024);
    bool ok = true;
    for (auto *t : world_) {
        float w = 0, h = 0;
        SDL_GetTextureSize(t, &w, &h);
        ok = SDL_UpdateTexture(t, nullptr, zero.data(), int(w) * 4) && ok;
    }
    ok = SDL_UpdateTexture(ui_, nullptr, zero.data(), 224 * 4) && ok;
    ok = draw_mips() && ok;
    auto *old = SDL_GetRenderTarget(renderer_);
    uniforms_ = {};
    uniforms_.screen = {float(width_) / 224, float(height_) / 144, 0, 0};
    uniforms_.camera = {500, 0, 1, 1};
    uniforms_.rectangle = {0, 0, 224, 144};
    for (int pass : {0, 2}) {
        uniforms_.control = {float(pass), 0, 3, 1};
        ok = SDL_SetRenderTarget(renderer_, targets_[size_t(pass / 2)]) &&
             SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
             SDL_SetGPURenderState(renderer_, state_) &&
             SDL_RenderTexture(renderer_, world_[0], nullptr, nullptr) && ok;
    }
    SDL_SetGPURenderState(renderer_, nullptr);
    SDL_SetRenderTarget(renderer_, old);
    ips_ = true;
    const SDL_FRect destination{0, 0, float(width_), float(height_)};
    ok = draw_field_grid(destination) && ok;
    ips_ = false;
    uniforms_ = {};
    return ok;
}

bool Mode7Renderer::draw(const game::Mode7World &world, const game::Mode7Camera &camera, bool snap,
                         const uint32_t *ui, bool ips, bool shadows, float ips_amount, float shadow_strength,
                         float output_scale) {
    CpuScope cpu_scope("Mode7Renderer::draw");
    last_upload_full = false;
    shaded_pixels = {};
    atlas_pixels = scanline_pixels = 0;
    const auto atlas_start = timing_enabled ? SDL_GetTicksNS() : 0;
    if (!ready() || world.surface().words.empty())
        return false;
    const auto &surface = world.surface();
    if (auto &t = presentation_diagnostics::trace(); t.enabled()) {
        t.range("Mode7Surface.words", surface.words);
        t.range("Mode7Surface.indices", surface.indices);
        t.range("Mode7Surface.palettes", surface.palettes);
        t.range("Mode7Surface.water", surface.water);
        // Upload bookkeeping deliberately differs in fresh mode. Keep it for diagnosis,
        // but compare effective resident contents below, not rebuild counters/identities.
        t.range("work.Mode7Surface.dirty", surface.dirty);
        presentation_diagnostics::Hash h;
        h.add(camera.tilt);
        h.add(camera.lens);
        h.add(camera.amount);
        h.add(camera.distance);
        h.add(camera.pixel_scale);
        h.add(camera.output_scale);
        h.add(camera.origin);
        h.add(world.scroll());
        h.add(snap);
        h.add(shadows);
        h.add(shadow_strength);
        h.add(ips);
        h.add(ips_amount);
        t.write("Mode7.view", h);
        t.range("Mode7.ui", std::span(ui, size_t(224 * 144)));
        presentation_diagnostics::Hash sprites;
        sprites.add(world.sprites().size());
        for (const auto &c : world.sprites()) {
            // IDs only choose a texture. Pixels and all shader inputs are the contract.
            sprites.add(c.record);
            sprites.add(c.x);
            sprites.add(c.y);
            sprites.add(c.width);
            sprites.add(c.height);
            sprites.add(c.bottom);
            sprites.add(c.extra);
            sprites.add(c.shaft_tip);
            sprites.add(c.landmark);
            sprites.add(c.front);
            sprites.add(c.veil);
            if (c.pixels)
                sprites.range(*c.pixels);
        }
        t.write("Mode7.sprites", sprites);
    }
    const bool fresh = presentation_diagnostics::fresh() || world.map() != map_ || revision_ == 0 ||
                       surface.generation != generation_;
    const bool layout = fresh || revision_ != surface.revision;
    const bool tiles = fresh || tile_revision_ != surface.tile_revision;
    const bool palettes = fresh || palette_revision_ != surface.palette_revision;
    if (auto &t = presentation_diagnostics::trace(); t.enabled()) {
        struct Resident {
            std::vector<uint32_t> words, indices, water;
            std::array<uint32_t, 256> palettes{};
        };
        static thread_local std::map<const Mode7Renderer *, Resident> residents;
        auto &r = residents[this];
        if (fresh) {
            r.words = surface.words;
            r.water = surface.water;
        } else if (layout) {
            for (size_t cell = 0; cell < surface.changed.size(); ++cell)
                if (surface.changed[cell] > revision_)
                    for (size_t q = 0; q < 4; ++q) {
                        const auto at = (cell / 80 * 2 + q / 2) * 160 + cell % 80 * 2 + q % 2;
                        r.words[at] = surface.words[at];
                    }
        }
        if (tiles)
            r.indices = surface.indices;
        if (palettes)
            r.palettes = surface.palettes;
        t.range("resident.Mode7.words", r.words);
        t.range("work.resident.Mode7.indices", r.indices); // raw upload, includes unsampled actor DMA tiles
        // Terrain never samples actor DMA tiles absent from its words. Keep raw-upload
        // differences visible above, and separately prove equality of the sampled subset.
        std::array<bool, 1024> used{};
        for (auto encoded : r.words) {
            const auto word = (encoded >> 16 & 255) | (encoded >> 8 & 255) << 8;
            used[(word & 511) | (word & 0x2000 ? 512 : 0)] = true;
        }
        presentation_diagnostics::Hash art;
        const auto *bytes = reinterpret_cast<const uint8_t *>(r.indices.data());
        for (size_t tile = 0; tile < used.size(); ++tile)
            if (used[tile]) {
                art.add(tile);
                art.data(bytes + tile * 32, 32);
            }
        t.write("resident.Mode7.sampled_indices", art);
        // Palette uploads follow the same ground dependency: compare the rows the terrain can sample.
        std::array<bool, 16> used_palettes{};
        for (auto encoded : r.words)
            used_palettes[size_t(((encoded >> 16 & 255) | (encoded >> 8 & 255) << 8) >> 9 & 15)] = true;
        presentation_diagnostics::Hash colors;
        for (size_t palette = 0; palette < used_palettes.size(); ++palette)
            if (used_palettes[palette]) {
                colors.add(palette);
                for (size_t i = 0; i < 16; ++i)
                    colors.add(r.palettes[palette * 16 + i]);
            }
        t.write("resident.Mode7.sampled_palettes", colors);
        t.range("work.resident.Mode7.palettes", r.palettes); // raw upload, includes unsampled rows
        t.range("resident.Mode7.water", r.water);
    }
    if (presentation_diagnostics::cpu_only()) {
        map_ = world.map();
        revision_ = surface.revision;
        generation_ = surface.generation;
        tile_revision_ = surface.tile_revision;
        palette_revision_ = surface.palette_revision;
        return true;
    }
    if (fresh) {
        cached_indices_.clear();
        cached_palettes_.clear();
        for (auto &[id, t] : cards_) {
            (void)id;
            SDL_DestroyTexture(t);
        }
        cards_.clear();
        if (!SDL_UpdateTexture(world_[3], nullptr, surface.water.data(), 256 * 4))
            return false;
        dry_ = std::all_of(surface.water.begin(), surface.water.end(), [](uint32_t w) { return w == 0; });
        water_cleared_ = false;
    }
    if (layout) {
        if (fresh) {
            if (!SDL_UpdateTexture(world_[0], nullptr, surface.words.data(), 160 * 4))
                return false;
        } else {
            int left = 80, right = -1, top = 80, bottom = -1;
            for (int row = 0; row < 80; ++row)
                for (int x = 0; x < 80; ++x)
                    if (surface.changed[size_t(row * 80 + x)] > revision_) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, row);
                        bottom = std::max(bottom, row);
                    }
            if (right >= left) {
                SDL_Rect r{left * 2, top * 2, (right - left + 1) * 2, (bottom - top + 1) * 2};
                if (!SDL_UpdateTexture(world_[0], &r, surface.words.data() + r.y * 160 + r.x, 160 * 4))
                    return false;
            }
        }
    }
    std::array<uint16_t, 16> changed_colors{};
    if (palettes && cached_palettes_.size() == surface.palettes.size())
        for (size_t i = 0; i < surface.palettes.size(); ++i)
            if (cached_palettes_[i] != surface.palettes[i])
                changed_colors[i / 16] |= uint16_t(1u << (i % 16));
    if (layout || tiles)
        cache_palette_usage(surface);
    if (tiles && !upload_pixels(world_[1], surface.indices.data(), 128, 64, cached_indices_))
        return false;
    if (palettes && !upload_pixels(world_[2], surface.palettes.data(), 16, 16, cached_palettes_))
        return false;
    auto *old = SDL_GetRenderTarget(renderer_);
    // At magnification the shader reads only nearest level zero. Resolve just the camera's
    // footprint, with 16 pixels of slack on each side for motion. A perspective/minified view
    // still gets the complete color pyramid, including the minimap. The shader and its
    // sampling coordinates are unchanged; clipping only omits texels it cannot read.
    const bool magnified = camera.pixel_scale >= 2 && !presentation_diagnostics::fresh();
    SDL_Rect visible{0, 0, 1280, 1280};
    SDL_Rect needed = visible;
    if (magnified) {
        const auto axis = [&](float origin, float scroll, int extent, int first, int margin) {
            const float low = origin + scroll + 128 - float(margin);
            const float high = low + float(extent) / camera.pixel_scale + float(2 * margin);
            // extend_edges repeats the two border cells outside the authored map.
            const int left = low < first ? first : std::min(1248, int(std::floor(low)));
            const int right = high > 1280 ? 1280 : std::max(first + 32, int(std::ceil(high)));
            return std::array<int, 2>{left, right};
        };
        const auto region = [&](int margin) {
            const auto x = axis(camera.origin[0], world.scroll()[0], width_, 0, margin);
            const auto y = axis(camera.origin[1], world.scroll()[1], height_, 32, margin);
            return SDL_Rect{x[0], y[0], x[1] - x[0], y[1] - y[0]};
        };
        visible = region(16);
        needed = region(2); // nearest sampling plus conservative float rounding at viewport edges
    }
    const bool covered = needed.x >= atlas_valid_.x && needed.y >= atlas_valid_.y &&
                         needed.x + needed.w <= atlas_valid_.x + atlas_valid_.w &&
                         needed.y + needed.h <= atlas_valid_.y + atlas_valid_.h;
    if (layout || tiles || palettes || (magnified ? !covered : !mips_valid_)) {
        if (!magnified && !layout && !tiles && mips_valid_) {
            if (!draw_palette_mips(changed_colors))
                return false;
        } else if (!draw_mips(magnified ? &visible : nullptr))
            return false;
    }
    map_ = world.map();
    revision_ = surface.revision;
    generation_ = surface.generation;
    tile_revision_ = surface.tile_revision;
    palette_revision_ = surface.palette_revision;
    ui_regions.update(ui);
    std::array<uint32_t, 224 * 144> coverage{};
    restoration_coverage(ui, coverage.data(), coverage.size(), false);
    if (!upload_pixels(ui_, coverage.data(), 224, 144, cached_ui_))
        return false;
    constexpr float pi = 3.14159265358979323846f;
    uniforms_.camera = {224 / (2 * std::tan(camera.lens * pi / 360)), std::sin(camera.tilt * pi / 180),
                        std::cos(camera.tilt * pi / 180), camera.distance};
    uniforms_.scroll = {world.scroll()[0], world.scroll()[1], camera.origin[0], camera.origin[1]};
    // A flat field's effects are snapped to its pixels, which would resample lines drawn here away: its
    // grid is draw_field_grid's pass over the finished picture.
    ips_ = ips;
    ips_amount_ = ips_amount;
    const float grid_scale = std::max(1.f, output_scale);
    uniforms_.raster = {grid_scale, 0, 0, 0};
    uniforms_.screen = {float(width_) / 224, float(height_) / 144,
                        ips && camera.pixel_scale == 0 ? ips_amount_ : 0.f, 0};
    if (camera.pixel_scale == 0) {
        GpuPassTimer gpu_time(renderer_, "scanlines");
        uniforms_.control = {8, 0, 0, 0};
        if (!SDL_SetRenderTarget(renderer_, scanlines_) ||
            !SDL_SetGPURenderStateFragmentUniforms(scanline_state_, 0, &uniforms_, sizeof(uniforms_)) ||
            !SDL_SetGPURenderState(renderer_, scanline_state_) ||
            !SDL_RenderTexture(renderer_, world_[0], nullptr, nullptr) ||
            !SDL_SetGPURenderState(renderer_, nullptr) || !SDL_SetRenderTarget(renderer_, old))
            return false;
        scanline_pixels = uint64_t(height_) * 2;
    }
    // Probe the reviewed world material once per actor, independent of output pixels. This is
    // the same integer lookup as tile_sample(feet+128,2), including mirroring and clamping.
    {
        GpuPassTimer gpu_time(renderer_, "projected-shadows");
        shadow_.work = {};
        const bool shadow_active =
            shadows &&
            std::any_of(world.sprites().begin(), world.sprites().end(), [&](const game::Mode7Sprite &sprite) {
                return !sprite.landmark && !sprite.layer && !world.water_at(sprite.x, sprite.y);
            });
        if ((shadow_active || shadow_active_) && !shadow_.begin(0))
            return false;
        bool shadow_ok = true;
        unsigned count = 0;
        for (const auto &sprite : world.sprites()) {
            // Script-only sprites (effects, flying things) cast no contact shadow.
            if (!shadows || sprite.landmark || sprite.layer || count++ >= 20 ||
                world.water_at(sprite.x, sprite.y))
                continue;
            InfluenceBuffer::Parameters p;
            p.position = {sprite.x, sprite.y, 6.6f, 2.2f};
            p.camera = uniforms_.camera;
            p.scroll = uniforms_.scroll;
            p.grid = {uniforms_.screen[0], uniforms_.screen[1], camera.pixel_scale, sprite.shadow_offset()};
            p.options[3] = camera.pixel_scale > 0 && snap ? 1.f : 0.f;
            const float sx = sprite.x - world.scroll()[0], sy = sprite.y - world.scroll()[1];
            const float rx = 6.6f * std::sqrt(2.5f) + 1, ry = 2.2f * std::sqrt(2.5f) + 1;
            float x0 = 224, y0 = 144, x1 = 0, y1 = 0;
            for (float x : {sx - rx, sx + rx})
                for (float y : {sy - ry, sy + ry}) {
                    const auto q = camera.project(x, y);
                    x0 = std::min(x0, q[0]);
                    x1 = std::max(x1, q[0]);
                    y0 = std::min(y0, q[1]);
                    y1 = std::max(y1, q[1]);
                }
            shadow_ok = shadow_.shadow(p, {x0 - 1, y0 - 1, x1 - x0 + 2, y1 - y0 + 2}) && shadow_ok;
        }
        if (shadow_active || shadow_active_)
            shadow_ok = shadow_.end() && shadow_ok;
        shadow_active_ = shadow_active;
        uniforms_.screen[3] = shadow_active ? shadow_strength : 0.f;
        shadow_work = shadow_.work;
        if (!shadow_ok)
            return false;
    }
    bool ok = true;
    if (timing_enabled)
        atlas_ns += SDL_GetTicksNS() - atlas_start;
    for (int pass : {0, 2}) {
        if (!ok)
            break;
        // A dry map's water pass writes only zeros: clear its target once and skip the full-size draw.
        if (pass == 2 && dry_) {
            if (!water_cleared_) {
                Uint8 r, g, b, a;
                SDL_GetRenderDrawColor(renderer_, &r, &g, &b, &a);
                ok = SDL_SetRenderTarget(renderer_, targets_[1]) &&
                     SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) && SDL_RenderClear(renderer_) &&
                     SDL_SetRenderDrawColor(renderer_, r, g, b, a) && SDL_SetRenderTarget(renderer_, old);
                water_cleared_ = ok;
            }
            continue;
        }
        GpuPassTimer gpu_time(renderer_, pass == 0 ? "ground-cards" : "water-metadata");
        uniforms_.screen[2] = ips && camera.pixel_scale == 0 ? ips_amount_ : 0.f;
        const auto start = timing_enabled ? SDL_GetTicksNS() : 0;
        uniforms_.control = {float(pass), 0, camera.pixel_scale,
                             camera.pixel_scale > 0 ? (snap ? 1.f : 0.f) : camera.amount};
        uniforms_.rectangle = {0, 0, 224, 144};
        ok = SDL_SetRenderTarget(renderer_, targets_[size_t(pass / 2)]) &&
             SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
             SDL_SetGPURenderState(renderer_, state_) &&
             SDL_RenderTexture(renderer_, world_[0], nullptr, nullptr);
        shaded_pixels[size_t(pass)] += uint64_t(width_) * uint64_t(height_);
        for (const auto &card : world.sprites()) {
            const float x = card.x - world.scroll()[0], y = card.y - world.scroll()[1];
            const auto foot = camera.project(x, y);
            // Whole output pixels, except inside a flat field's fidelity fade: there actors scale with the
            // ground.
            const float pixel_scale = camera.pixel_scale * grid_scale;
            const bool whole = pixel_scale == std::floor(pixel_scale);
            const float grid_x = uniforms_.screen[0] * grid_scale;
            const float grid_y = uniforms_.screen[1] * grid_scale;
            const float actor_scale = whole ? std::max(1.f, std::floor(foot[2] * grid_x + 0.5f)) / grid_scale
                                            : foot[2] * uniforms_.screen[0];
            const float actor_x = whole ? std::floor(foot[0] * grid_x + 0.5f) / grid_x : foot[0];
            const float actor_y = whole ? std::floor(foot[1] * grid_y + 0.5f) / grid_y : foot[1];
            const int extra = card.extra < 0
                                  ? std::max(16, int(std::ceil((foot[1] + 160) / std::max(0.1f, foot[2]))))
                                  : card.extra;
            const int height = card.height + (card.extra < 0 ? extra : 0);
            const int bottom = card.bottom + (card.extra < 0 ? extra : 0);
            const float a = card.landmark ? camera.amount : 1;
            const auto point = [&](float cx, float d) {
                if (!card.landmark)
                    return std::array<float, 3>{actor_x + (cx - x) * actor_scale / uniforms_.screen[0],
                                                actor_y - d * actor_scale / uniforms_.screen[1], foot[2]};
                auto p = camera.project(cx, y - d * (1 - a));
                p[1] -= d * a * p[2];
                return p;
            };
            auto tl = point(x - float(card.width) / 2, float(bottom) - extra + extra * a);
            auto tr = point(x + float(card.width) / 2, float(bottom) - extra + extra * a);
            auto bl = point(x - float(card.width) / 2, float(bottom) - height);
            auto br = point(x + float(card.width) / 2, float(bottom) - height);
            if (tl[2] <= 0 || bl[2] <= 0)
                continue;
            const float minx = std::max(0.f, std::floor(std::min({tl[0], tr[0], bl[0], br[0]}) - 1));
            const float maxx = std::min(224.f, std::ceil(std::max({tl[0], tr[0], bl[0], br[0]}) + 1));
            const float miny = std::max(0.f, std::floor(std::min({tl[1], tr[1], bl[1], br[1]}) - 1));
            const float maxy = std::min(144.f, std::ceil(std::max({tl[1], tr[1], bl[1], br[1]}) + 1));
            if (minx >= maxx || miny >= maxy)
                continue;
            auto found = cards_.find(card.id);
            if (found == cards_.end()) {
                auto *t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                            card.width, card.height);
                if (!t || !SDL_UpdateTexture(t, nullptr, card.pixels->data(), card.width * 4)) {
                    SDL_DestroyTexture(t);
                    ok = false;
                    break;
                }
                SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
                found = cards_.emplace(card.id, t).first;
            }
            // Actors own an output-anchored grid, independently of the moving ground lattice.
            uniforms_.screen[2] = ips ? ips_amount_ : 0.f;
            uniforms_.control[1] = card.landmark ? 2.f : 1.f;
            uniforms_.card = {x, y, float(card.width), float(height)};
            uniforms_.shape = {float(bottom), float(extra), float(card.shaft_tip), card.front ? 1.f : 0.f};
            uniforms_.rectangle = {minx, miny, maxx - minx, maxy - miny};
            SDL_FRect r{minx * width_ / 224, miny * height_ / 144, (maxx - minx) * width_ / 224,
                        (maxy - miny) * height_ / 144};
            // Raster-bound proxy includes fragment invocations later discarded by card coverage.
            shaded_pixels[size_t(pass)] += uint64_t(std::ceil(r.x + r.w - 0.5f) - std::ceil(r.x - 0.5f)) *
                                           uint64_t(std::ceil(r.y + r.h - 0.5f) - std::ceil(r.y - 0.5f));
            ok = ok && SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
                 SDL_RenderTexture(renderer_, found->second, nullptr, &r);
            if (card.veil && pass == 0 && ok) {
                // The half-alpha pixels again, blended over the ground; the ground keeps its ownership alpha.
                const SDL_BlendMode veil = terrain_veil_blend();
                uniforms_.control[1] = 1.25f; // the actor's veil pass (mode7.frag.hlsl)
                ok = SDL_SetTextureBlendMode(found->second, veil) &&
                     SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
                     SDL_RenderTexture(renderer_, found->second, nullptr, &r) &&
                     SDL_SetTextureBlendMode(found->second, SDL_BLENDMODE_NONE);
            }
        }
        if (timing_enabled)
            pass_ns[size_t(pass)] += SDL_GetTicksNS() - start;
    }
    SDL_SetGPURenderState(renderer_, nullptr);
    ok = SDL_SetRenderTarget(renderer_, old) && ok;
    // Palette fades can make many distinct actor cards. Bound obsolete GPU resources.
    if (cards_.size() > 256) {
        std::set<uint64_t> active;
        for (const auto &c : world.sprites())
            active.insert(c.id);
        for (auto i = cards_.begin(); i != cards_.end();) {
            if (!active.count(i->first)) {
                SDL_DestroyTexture(i->second);
                i = cards_.erase(i);
            } else
                ++i;
        }
    }
    last_upload_full = ok && fresh;
    return ok;
}
bool Mode7Renderer::draw_ui_grid(const SDL_FRect &destination) {
    if (ui_regions.empty()) {
        ui_regions.pixels = ui_regions.draws = 0;
        return true;
    }
    if (presentation_diagnostics::fresh()) {
        SDL_DestroyTexture(ui_grid_);
        ui_grid_ = nullptr;
    }
    float grid_w = 0, grid_h = 0;
    if (ui_grid_)
        SDL_GetTextureSize(ui_grid_, &grid_w, &grid_h);
    const int width = int(destination.w), height = int(destination.h);
    if (!ui_grid_ || grid_w != width || grid_h != height) {
        SDL_DestroyTexture(ui_grid_);
        const auto across = ips_mask(width, 224, true), down = ips_mask(height, 144, false);
        // Keep the axes separate: the shader fades each toward white without rebuilding this texture.
        std::vector<uint32_t> pixels(size_t(width * height));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                pixels[size_t(y * width + x)] =
                    (across[size_t(x)] & 0xFFFFFF) | ((down[size_t(y)] & 255) << 24);
        ui_grid_ =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!ui_grid_ || !SDL_UpdateTexture(ui_grid_, nullptr, pixels.data(), width * 4))
            return false;
        SDL_SetTextureScaleMode(ui_grid_, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(ui_grid_, SDL_BLENDMODE_MOD);
    }
    uniforms_.screen[2] = ips_amount_;
    uniforms_.control = {5, 0, 0, 0};
    uniforms_.rectangle = {0, 0, 224, 144};
    const bool ok = SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
                    SDL_SetGPURenderState(renderer_, state_) && ui_regions.draw(renderer_, destination, [&] {
                        return SDL_RenderTexture(renderer_, ui_grid_, nullptr, &destination);
                    });
    SDL_SetGPURenderState(renderer_, nullptr);
    return ok;
}
bool Mode7Renderer::draw_field_grid(const SDL_FRect &destination) {
    if (!ips_ || uniforms_.control[2] <= 0)
        return true;
    // The world grid's lines and strength, on the camera's own pixels. UI stays clear: it is restored on top.
    uniforms_.control[0] = 6;
    uniforms_.control[1] = 0;
    uniforms_.screen[2] = ips_amount_;
    uniforms_.rectangle = {0, 0, 224, 144};
    const bool ok = SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
                    SDL_SetGPURenderState(renderer_, state_) &&
                    SDL_SetTextureBlendMode(targets_[0], SDL_BLENDMODE_MOD) &&
                    SDL_RenderTexture(renderer_, targets_[0], nullptr, &destination);
    SDL_SetTextureBlendMode(targets_[0], SDL_BLENDMODE_NONE);
    uniforms_.screen[2] = 0;
    SDL_SetGPURenderState(renderer_, nullptr);
    return ok;
}
bool Mode7Renderer::draw_minimap(const SDL_FRect &area, int tile, int rows, int columns, int block,
                                 std::array<int, 2> party, float opacity, float brightness, float marker,
                                 float marker_opacity) {
    if (!has_atlas() || opacity <= 0 || tile <= 0)
        return true;
    // Mip level 3: 2x2 averaged colors per tile (texture without noise); the 64x64 world
    // inside its 8-tile border.
    auto *map = mips_[3];
    constexpr float k = 2; // texels per tile
    const SDL_FRect world{float(8 + columns) * k, float(8 + rows) * k, float(64 - 2 * columns) * k,
                          float(64 - 2 * rows) * k};
    SDL_ScaleMode scale_mode;
    SDL_GetTextureScaleMode(map, &scale_mode);
    SDL_SetTextureScaleMode(map, SDL_SCALEMODE_NEAREST);
    const auto control = uniforms_.control;
    uniforms_.control = {9, brightness, 1, std::clamp(opacity, 0.f, 1.f)};
    SDL_SetTextureBlendMode(map, SDL_BLENDMODE_BLEND);
    bool ok = SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
              SDL_SetGPURenderState(renderer_, state_) && SDL_RenderTexture(renderer_, map, &world, &area);
    SDL_SetGPURenderState(renderer_, nullptr);
    SDL_SetTextureBlendMode(map, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(map, scale_mode);
    uniforms_.control = control;
    // The party dot: one game-pixel square centered on the party's tile, no outline.
    SDL_Rect clip{};
    const bool clipped = SDL_RenderClipEnabled(renderer_);
    SDL_GetRenderClipRect(renderer_, &clip);
    const SDL_Rect inside{int(area.x), int(area.y), int(area.w), int(area.h)};
    SDL_SetRenderClipRect(renderer_, &inside);
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(renderer_, &r, &g, &b, &a);
    SDL_BlendMode blend;
    SDL_GetRenderDrawBlendMode(renderer_, &blend);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const float s = float(block);
    const float cx = std::floor(area.x + (float(party[0] - columns) + 0.5f) * float(tile) - s / 2),
                cy = std::floor(area.y + (float(party[1] - rows) + 0.5f) * float(tile) - s / 2);
    const Uint8 alpha = Uint8(std::lround(std::clamp(marker_opacity, 0.f, 1.f) * 255));
    const Uint8 level = Uint8(std::lround(std::clamp(marker, 0.f, 1.f) * 255));
    const SDL_FRect dot{cx, cy, s, s};
    ok = SDL_SetRenderDrawColor(renderer_, level, level, level, alpha) &&
         SDL_RenderFillRect(renderer_, &dot) && ok;
    SDL_SetRenderDrawBlendMode(renderer_, blend);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_SetRenderClipRect(renderer_, clipped ? &clip : nullptr);
    return ok;
}
bool Mode7Renderer::resize(int width, int height) {
    sawgod::startup::Stage timing("Enhanced world scene resize");
    CpuScope cpu_scope("Mode7Renderer::resize");
    if (width == width_ && height == height_)
        return true;
    std::array<SDL_Texture *, 2> next{};
    for (size_t i = 0; i < next.size(); ++i) {
        auto &t = next[i];
        t = SDL_CreateTexture(renderer_, i == 0 ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_XRGB8888,
                              SDL_TEXTUREACCESS_TARGET, width, height);
        if (!t) {
            for (auto *created : next)
                SDL_DestroyTexture(created);
            return false;
        }
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    }
    for (auto *t : targets_)
        SDL_DestroyTexture(t);
    targets_ = next;
    water_cleared_ = false;
    SDL_DestroyTexture(ui_grid_);
    ui_grid_ = nullptr;
    width_ = width;
    height_ = height;
    SDL_DestroyGPURenderState(scanline_state_);
    scanline_state_ = nullptr;
    SDL_DestroyTexture(scanlines_);
    scanlines_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 2, height);
    if (!scanlines_)
        return false;
    SDL_SetTextureBlendMode(scanlines_, SDL_BLENDMODE_NONE);
    // World shadows use bounded quads on a fixed 3x field with prefiltered blob stamps.
    shadow_active_ = false;
    return bind_scene();
}
bool Mode7Renderer::bind_scene() {
    SDL_DestroyGPURenderState(state_);
    state_ = nullptr;
    const auto gpu = [](SDL_Texture *t) {
        return static_cast<SDL_GPUTexture *>(SDL_GetPointerProperty(
            SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    };
    std::array<SDL_GPUTextureSamplerBinding, 12> bindings{};
    for (size_t i = 0; i < 4; ++i)
        bindings[i] = {gpu(world_[i]), sampler_};
    bindings[4] = {gpu(ui_), sampler_};
    for (size_t i = 0; i < 5; ++i)
        bindings[5 + i] = {gpu(mips_[i]), i == 0 ? sampler_ : linear_};
    bindings[10] = {gpu(shadow_.texture(0)), sampler_};
    bindings[11] = {gpu(scanlines_), sampler_};
    SDL_GPURenderStateCreateInfo info{};
    info.fragment_shader = shader_;
    info.num_sampler_bindings = unsigned(bindings.size());
    info.sampler_bindings = bindings.data();
    state_ = SDL_CreateGPURenderState(renderer_, &info);
    // Scanline generation must not bind its own target, even through an unused sampler.
    bindings[11] = {gpu(world_[0]), sampler_};
    SDL_DestroyGPURenderState(scanline_state_);
    scanline_state_ = SDL_CreateGPURenderState(renderer_, &info);
    return state_ && scanline_state_;
}
void Mode7Renderer::shutdown() {
    cached_ui_.clear();
    if (!renderer_)
        return;
    SDL_SetGPURenderState(renderer_, nullptr);
    if (state_)
        SDL_DestroyGPURenderState(state_);
    for (auto *state : mip_states_)
        if (state)
            SDL_DestroyGPURenderState(state);
    auto *device = SDL_GetGPURendererDevice(renderer_);
    if (shader_)
        SDL_ReleaseGPUShader(device, shader_);
    if (sampler_)
        SDL_ReleaseGPUSampler(device, sampler_);
    if (linear_)
        SDL_ReleaseGPUSampler(device, linear_);
    linear_ = nullptr;
    for (auto *t : world_)
        SDL_DestroyTexture(t);
    for (auto *t : mips_)
        SDL_DestroyTexture(t);
    for (auto *t : targets_)
        SDL_DestroyTexture(t);
    for (auto &[id, t] : cards_) {
        (void)id;
        SDL_DestroyTexture(t);
    }
    SDL_DestroyGPURenderState(scanline_state_);
    scanline_state_ = nullptr;
    SDL_DestroyTexture(scanlines_);
    scanlines_ = nullptr;
    shadow_.shutdown();
    shadow_active_ = false;
    SDL_DestroyTexture(ui_);
    SDL_DestroyTexture(ui_grid_);
    world_ = {};
    targets_ = {};
    mips_ = {};
    mip_states_ = {};
    width_ = 224;
    height_ = 144;
    cards_.clear();
    cached_indices_.clear();
    cached_palettes_.clear();
    palette_usage_.clear();
    ui_ = ui_grid_ = nullptr;
    renderer_ = nullptr;
    shader_ = nullptr;
    sampler_ = nullptr;
    state_ = nullptr;
    map_ = -1;
    atlas_valid_ = {};
    mips_valid_ = false;
    revision_ = generation_ = tile_revision_ = palette_revision_ = 0;
}
} // namespace sawgod
