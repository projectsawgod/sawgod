#include "engine/focus_pass.h"

#include <algorithm>

#include "engine/cpu_scope.h"
#include "engine/gpu_warmup.h"
#include "engine/shaders/focus.generated.h"

namespace sawgod {
bool FocusPass::init(SDL_Renderer *renderer) {
    shutdown();
    auto *device = SDL_GetGPURendererDevice(renderer);
    if (!device)
        return SDL_SetError("Mode 7 focus requires SDL_GPU");
    renderer_ = renderer;
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1; // SDL supplies the drawn texture at slot 0
    info.num_uniform_buffers = 1;
    if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = shaders::focus_spirv;
        info.code_size = sizeof(shaders::focus_spirv);
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = shaders::focus_dxil;
        info.code_size = sizeof(shaders::focus_dxil);
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = reinterpret_cast<const Uint8 *>(shaders::focus_msl);
        info.code_size = sizeof(shaders::focus_msl) - 1;
    } else {
        shutdown();
        return SDL_SetError("Mode 7 focus shader format unavailable");
    }
    shader_ = SDL_CreateGPUShader(device, &info);
    if (!shader_) {
        shutdown();
        return false;
    }
    SDL_GPURenderStateCreateInfo state{};
    state.fragment_shader = shader_;
    state_ = SDL_CreateGPURenderState(renderer, &state);
    if (!state_) {
        shutdown();
        return false;
    }
    const Uniforms warm{{20.f / 224, 20.f / 144, 0, 0}, {3.f / 144, 224.f / 144, 0, 0}};
    if (!warm_gpu_state(renderer_, state_, &warm, sizeof(warm))) {
        shutdown();
        return false;
    }
    return true;
}

void FocusPass::shutdown() {
    if (state_)
        SDL_DestroyGPURenderState(state_);
    state_ = nullptr;
    if (shader_ && renderer_)
        SDL_ReleaseGPUShader(SDL_GetGPURendererDevice(renderer_), shader_);
    shader_ = nullptr;
}

bool FocusPass::draw(SDL_Texture *picture, const SDL_FRect &dst, float strength, float size) {
    CpuScope cpu_scope("FocusPass::draw");
    if (!state_ || strength <= 0 || dst.h <= 0)
        return SDL_RenderTexture(renderer_, picture, nullptr, &dst);
    // Sharp inside a rectangle concentric with the screen; the blur eases up across a band of one depth along
    // every edge, to full strength at the edge (assets/shaders/focus.frag.hlsl). The depth is the mean of the
    // four edge bands of the former oval around (112,79), so the edges keep about the same softness:
    // 47 native px at the default size 50%, 2 at 100%; scaled to 32/47 (two tiles, 32 px,
    // at 50%; 1.4 px at 100%), and never under half a pixel.
    // Full strength is a 3-native-pixel radius: soft, never smeared (144 native rows per screen height).
    const float band = std::max(0.5f, (92 - 90 * size) * (32.0f / 47.0f));
    uniforms_.band = {band / 224, band / 144, 0, 0};
    uniforms_.control = {strength * 3.0f / 144.0f, dst.w / dst.h, 0, 0};
    SDL_ScaleMode mode = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(picture, &mode);
    SDL_SetTextureScaleMode(picture, SDL_SCALEMODE_LINEAR); // smooth taps for the blur
    const bool ok = SDL_SetGPURenderStateFragmentUniforms(state_, 0, &uniforms_, sizeof(uniforms_)) &&
                    SDL_SetGPURenderState(renderer_, state_) &&
                    SDL_RenderTexture(renderer_, picture, nullptr, &dst);
    SDL_SetGPURenderState(renderer_, nullptr);
    SDL_SetTextureScaleMode(picture, mode);
    return ok;
}
} // namespace sawgod
