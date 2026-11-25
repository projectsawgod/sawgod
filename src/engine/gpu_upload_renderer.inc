// Included in SDL's private GPU renderer. Keep upload staging with its texture instead of allocating
// and releasing driver memory for every update. Cycling preserves earlier copies still in flight,
// including multiple rectangles of the same texture in one command buffer.
static SDL_GPUTransferBuffer *SawGodUploadBuffer(GPU_RenderData *renderer, GPU_TextureData *texture,
                                                 Uint32 size) {
    if (texture->sawgod_upload_size < size) {
        SDL_GPUTransferBufferCreateInfo info;
        SDL_zero(info);
        info.size = size;
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        SDL_GPUTransferBuffer *buffer = SDL_CreateGPUTransferBuffer(renderer->device, &info);
        if (!buffer) {
            return NULL;
        }
        if (texture->sawgod_upload) {
            SDL_ReleaseGPUTransferBuffer(renderer->device, texture->sawgod_upload);
        }
        ++renderer->sawgod_upload_allocations;
        texture->sawgod_upload = buffer;
        texture->sawgod_upload_size = size;
        if (SDL_getenv("SAWGOD_GPU_CPU_TIMING")) {
            SDL_Log("GPU upload staging texture=%p bytes=%u", (void *)texture->texture, size);
        }
    }
    return texture->sawgod_upload;
}

SDL_DECLSPEC void SawGodRendererUploadWork(SDL_Renderer *renderer, Uint64 *bytes, Uint64 *calls, Uint64 *allocations) {
    *bytes = *calls = *allocations = 0;
    if (SDL_strcmp(SDL_GetRendererName(renderer), "gpu") != 0)
        return;
    GPU_RenderData *data = (GPU_RenderData *)renderer->internal;
    *bytes = data->sawgod_upload_bytes;
    *calls = data->sawgod_upload_calls;
    *allocations = data->sawgod_upload_allocations;
}
