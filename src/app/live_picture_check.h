// Opt-in final RGB proof and snapshot timing for the ordinary main loop.
#pragma once
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

namespace sawgod::app {
class LivePictureCheck {
    std::unique_ptr<std::FILE, int (*)(std::FILE *)> file_{nullptr, std::fclose};

  public:
    explicit LivePictureCheck(const std::string &path) {
        if (path.empty())
            return;
        file_.reset(std::fopen(path.c_str(), "w"));
        if (!file_) {
            std::perror(path.c_str());
            std::exit(2);
        }
        std::fputs("view,frame,map,scene,wipe,retained,retain_ns,draw_ns,work_ns,retain_cpu_ns,work_cpu_ns,"
                   "picture\n",
                   file_.get());
    }
    static uint64_t cpu_time() {
#if defined(__linux__)
        timespec time{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
        return uint64_t(time.tv_sec) * 1'000'000'000 + uint64_t(time.tv_nsec);
#else
        // The portable fallback includes worker CPU; Linux proofs use the presenting thread only.
        return uint64_t(double(std::clock()) * 1e9 / CLOCKS_PER_SEC);
#endif
    }
    void record(SDL_Renderer *renderer, int view, uint64_t frame, int map, int scene, bool wipe,
                bool retained, uint64_t retain_ns, uint64_t draw_ns, uint64_t work_ns, uint64_t retain_cpu_ns,
                uint64_t work_cpu_ns) {
        if (!file_)
            return;
        auto *surface = SDL_RenderReadPixels(renderer, nullptr);
        auto *rgb = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24) : nullptr;
        if (!rgb) {
            std::fprintf(stderr, "live picture readback failed: %s\n", SDL_GetError());
            std::exit(2);
        }
        uint64_t hash = 14695981039346656037ull;
        for (int y = 0; y < rgb->h; ++y) {
            const auto *row = static_cast<const uint8_t *>(rgb->pixels) + y * rgb->pitch;
            for (int x = 0; x < rgb->w * 3; ++x)
                hash = (hash ^ row[x]) * 1099511628211ull;
        }
        std::fprintf(file_.get(), "%d,%llu,%d,%d,%d,%d,%llu,%llu,%llu,%llu,%llu,%016llx\n", view,
                     (unsigned long long)frame, map, scene, int(wipe), int(retained),
                     (unsigned long long)retain_ns, (unsigned long long)draw_ns, (unsigned long long)work_ns,
                     (unsigned long long)retain_cpu_ns, (unsigned long long)work_cpu_ns,
                     (unsigned long long)hash);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
    }
};
} // namespace sawgod::app
