// Opt-in diagnostic only: isolate a pass with a Vulkan timestamp and a 1-pixel fence.
// Normal play keeps the nonblocking whole-frame query; this mode intentionally serializes passes.
#pragma once
#include <SDL3/SDL.h>
#ifdef SAWGOD_GPU_TIMESTAMPS
extern "C" bool SawGodRendererTimingStart(SDL_Renderer *, Uint64);
extern "C" bool SawGodRendererTimingPoll(SDL_Renderer *, Uint64 *, double *);
#endif
namespace sawgod {
class GpuPassTimer {
  public:
    GpuPassTimer(SDL_Renderer *renderer, const char *name) : renderer_(renderer), name_(name) {
#ifdef SAWGOD_GPU_TIMESTAMPS
        static const bool enabled = SDL_getenv("SAWGOD_GPU_PASS_TIMING") != nullptr;
        static Uint64 next = 0;
        if (enabled) {
            id_ = ++next;
            active_ = SawGodRendererTimingStart(renderer_, id_);
        }
#endif
    }
    ~GpuPassTimer() {
#ifdef SAWGOD_GPU_TIMESTAMPS
        if (!active_)
            return;
        const SDL_Rect pixel{0, 0, 1, 1};
        SDL_DestroySurface(SDL_RenderReadPixels(renderer_, &pixel));
        Uint64 frame = 0;
        double ms = 0;
        if (SawGodRendererTimingPoll(renderer_, &frame, &ms) && frame == id_)
            SDL_Log("GPU pass: %s %.6f ms", name_, ms);
#endif
    }

  private:
    [[maybe_unused]] SDL_Renderer *renderer_;
    [[maybe_unused]] const char *name_;
    [[maybe_unused]] Uint64 id_ = 0;
    [[maybe_unused]] bool active_ = false;
};
} // namespace sawgod
