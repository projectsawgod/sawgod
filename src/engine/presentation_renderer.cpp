#include "engine/presentation_renderer.h"
#include "engine/data_dir.h"
#include "engine/gpu_warmup.h"
#ifdef SAWGOD_GPU_TIMESTAMPS
extern "C" void SawGodVulkanPipelineCacheSetPath(SDL_GPUDevice *, const char *);
#endif

#include <cstring>
#include <string>
#include <string_view>

#ifdef SAWGOD_IDENTITY_COMPILED
#include "engine/shaders/identity.generated.h"
#endif

namespace sawgod {
namespace {

SDL_Renderer *legacy_renderer(SDL_Window *window) {
    // Respect an explicit legacy SDL hint, then retain SDL's original preference order.
    // Never reenter the GPU backend after its creation or capability probe failed.
    const char *hint = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    if (hint && *hint && std::string(hint).find("gpu") == std::string::npos)
        if (auto *r = SDL_CreateRenderer(window, hint))
            return r;
    std::string drivers;
    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        const char *name = SDL_GetRenderDriver(i);
        if (std::strcmp(name, SDL_GPU_RENDERER) == 0)
            continue;
        if (!drivers.empty())
            drivers += ',';
        drivers += name;
    }
    return drivers.empty() ? nullptr : SDL_CreateRenderer(window, drivers.c_str());
}

#if defined(__linux__) && !defined(__ANDROID__)
// The player chose a GPU the system's way: DRI_PRIME / MESA_VK_DEVICE_SELECT (Mesa) or prime-run (NVIDIA).
bool prefers_other_gpu() {
    const auto set = [](const char *name) {
        const char *value = SDL_getenv(name);
        return value && *value && std::strcmp(value, "0") != 0;
    };
    return set("DRI_PRIME") || set("MESA_VK_DEVICE_SELECT") || set("__NV_PRIME_RENDER_OFFLOAD");
}
#endif

} // namespace

PresentationRenderer::~PresentationRenderer() { shutdown(); }

bool PresentationRenderer::is_gpu() const {
    return renderer_ && std::strcmp(SDL_GetRendererName(renderer_), SDL_GPU_RENDERER) == 0;
}

bool PresentationRenderer::init(SDL_Window *window, bool legacy, bool identity) {
    shutdown();
    if (!legacy) {
        // Headless runs (tests, screenshots) take the low-power GPU: on a hybrid laptop the NVIDIA driver
        // (615.71) crashes querying swapchain support for an offscreen window.
        const char *video = SDL_GetCurrentVideoDriver();
        if (video && std::string_view(video) == "offscreen")
            SDL_SetHint(SDL_HINT_RENDER_GPU_LOW_POWER, "1");
#if defined(__linux__) && !defined(__ANDROID__)
        // Linux convention: apps run on the system's default GPU (the one driving the display) and the player
        // opts into another one (DRI_PRIME, MESA_VK_DEVICE_SELECT, NVIDIA's prime-run). SDL otherwise picks
        // the discrete GPU itself, and on a hybrid laptop every frame is then copied to the display GPU
        // (PRIME), which stalls presents. Windows keeps SDL's high-performance default, its
        // own convention. Default priority: SDL_RENDER_GPU_LOW_POWER in the environment still wins.
        else if (!prefers_other_gpu())
            SDL_SetHintWithPriority(SDL_HINT_RENDER_GPU_LOW_POWER, "1", SDL_HINT_DEFAULT);
#endif
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
        SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, SDL_GPU_RENDERER);
        SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_DXIL_BOOLEAN, true);
#ifdef SAWGOD_IDENTITY_COMPILED
        SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_MSL_BOOLEAN,
                               sizeof(shaders::identity_msl) > 1);
#endif
        renderer_ = SDL_CreateRendererWithProperties(props);
        SDL_DestroyProperties(props);
#ifdef SAWGOD_GPU_TIMESTAMPS
        if (renderer_)
            SawGodVulkanPipelineCacheSetPath(SDL_GetGPURendererDevice(renderer_), data_dir().c_str());
#endif
        if (!renderer_ || (identity && !init_identity()) || !probe() || !warm_gpu_defaults(renderer_)) {
            const std::string error = SDL_GetError();
            SDL_Log("GPU presentation failed: %s; using legacy renderer", error.c_str());
            shutdown();
        }
    }
    if (!renderer_)
        renderer_ = legacy_renderer(window);
    if (!renderer_)
        return false;
    const char *driver = is_gpu() ? SDL_GetGPUDeviceDriver(SDL_GetGPURendererDevice(renderer_)) : "legacy";
    const char *device =
        is_gpu() ? SDL_GetStringProperty(SDL_GetGPUDeviceProperties(SDL_GetGPURendererDevice(renderer_)),
                                         SDL_PROP_GPU_DEVICE_NAME_STRING, "")
                 : "";
    SDL_Log("Presentation renderer: %s (%s%s%s), identity shader: %s", SDL_GetRendererName(renderer_), driver,
            *device ? ", " : "", device, state_ ? "on" : "off");
    return true;
}

bool PresentationRenderer::init_identity() {
#ifdef SAWGOD_IDENTITY_COMPILED
    SDL_GPUDevice *device = SDL_GetGPURendererDevice(renderer_);
    const auto formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1; // SDL supplies the source texture at slot 0.
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = shaders::identity_spirv;
        info.code_size = sizeof(shaders::identity_spirv);
    } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = shaders::identity_dxil;
        info.code_size = sizeof(shaders::identity_dxil);
    } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && sizeof(shaders::identity_msl) > 1) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = reinterpret_cast<const Uint8 *>(shaders::identity_msl);
        info.code_size = sizeof(shaders::identity_msl) - 1;
    } else {
        return SDL_SetError("No supported identity shader format");
    }
    shader_ = SDL_CreateGPUShader(device, &info);
    if (!shader_)
        return false;
    SDL_GPURenderStateCreateInfo state_info{};
    state_info.fragment_shader = shader_;
    state_ = SDL_CreateGPURenderState(renderer_, &state_info);
    return state_ && warm_gpu_state(renderer_, state_, nullptr, 0);
#else
    return SDL_SetError("Identity shader outputs unavailable; run tools/compile_shaders.py and rebuild");
#endif
}

bool PresentationRenderer::probe() {
    // Exercise the same formats, targets and blend modes as the app before committing to the backend.
    // This is startup-only; normal presentation never reads the GPU back.
    SDL_Texture *stream =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 1, 1);
    SDL_Texture *target =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 1, 1);
    SDL_Texture *canvas =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 1, 1);
    const Uint32 pixel = 0x123456;
    bool ok =
        stream && target && canvas && SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_BLEND_PREMULTIPLIED) &&
        SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_MOD) &&
        SDL_UpdateTexture(stream, nullptr, &pixel, sizeof(pixel)) && SDL_SetRenderTarget(renderer_, target) &&
        begin_game() && SDL_RenderTexture(renderer_, stream, nullptr, nullptr) && end_game();
    if (ok) {
        SDL_Surface *surface = SDL_RenderReadPixels(renderer_, nullptr);
        SDL_Surface *rgb = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888) : nullptr;
        Uint32 got = 0;
        if (rgb)
            std::memcpy(&got, rgb->pixels, sizeof(got));
        ok = rgb && (got & 0xFFFFFF) == pixel;
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        if (!ok)
            SDL_SetError("GPU XRGB identity readback failed");
    }
    const std::string error = SDL_GetError();
    end_game();
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_DestroyTexture(canvas);
    SDL_DestroyTexture(target);
    SDL_DestroyTexture(stream);
    if (!ok)
        SDL_SetError("%s", error.c_str());
    return ok;
}

bool PresentationRenderer::begin_game() { return !state_ || SDL_SetGPURenderState(renderer_, state_); }

bool PresentationRenderer::end_game() { return !state_ || SDL_SetGPURenderState(renderer_, nullptr); }

void PresentationRenderer::shutdown() {
    if (!renderer_)
        return;
    if (state_) {
        SDL_SetGPURenderState(renderer_, nullptr);
        SDL_DestroyGPURenderState(state_);
    }
    if (shader_)
        SDL_ReleaseGPUShader(SDL_GetGPURendererDevice(renderer_), shader_);
    state_ = nullptr;
    shader_ = nullptr;
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
}

} // namespace sawgod
