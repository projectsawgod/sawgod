// Included in the GPU renderer translation unit, keeping its private command buffer private.
bool SawGodVulkanTimingStart(SDL_GPUDevice *, SDL_GPUCommandBuffer *, Uint64);
bool SawGodVulkanTimingPoll(SDL_GPUDevice *, Uint64 *, double *);
bool SawGodRendererTimingStart(SDL_Renderer *renderer, Uint64 frame) {
    if (SDL_strcmp(SDL_GetRendererName(renderer), "gpu") != 0) return false;
    GPU_RenderData *data = (GPU_RenderData *)renderer->internal;
    if (!SDL_FlushRenderer(renderer)) return false;
    if (data->state.render_pass) { SDL_EndGPURenderPass(data->state.render_pass); data->state.render_pass = NULL; }
    return SawGodVulkanTimingStart(data->device, data->state.command_buffer, frame);
}
bool SawGodRendererTimingPoll(SDL_Renderer *renderer, Uint64 *frame, double *ms) {
    if (SDL_strcmp(SDL_GetRendererName(renderer), "gpu") != 0) return false;
    GPU_RenderData *data = (GPU_RenderData *)renderer->internal;
    return SawGodVulkanTimingPoll(data->device, frame, ms);
}
