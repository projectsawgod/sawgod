// CPU-side submitted upload work; independent of the GPU's execution time.
#pragma once
#include <SDL3/SDL.h>
#ifdef SAWGOD_GPU_UPLOADS
extern "C" void SawGodRendererUploadWork(SDL_Renderer *, Uint64 *, Uint64 *, Uint64 *);
#endif
namespace sawgod {
struct GpuUploadWork {
    // allocations counts staging objects, not additional backing memory allocated by GPU cycling.
    Uint64 bytes = 0, calls = 0, allocations = 0;
    static GpuUploadWork read(SDL_Renderer *renderer) {
        GpuUploadWork work;
#ifdef SAWGOD_GPU_UPLOADS
        SawGodRendererUploadWork(renderer, &work.bytes, &work.calls, &work.allocations);
#else
        (void)renderer;
#endif
        return work;
    }
    GpuUploadWork operator-(const GpuUploadWork &before) const {
        return {bytes - before.bytes, calls - before.calls, allocations - before.allocations};
    }
};
} // namespace sawgod
