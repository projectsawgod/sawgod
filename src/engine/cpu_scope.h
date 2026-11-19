// Opt-in CPU attribution; never synchronizes the GPU.
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
namespace sawgod {
class CpuScope {
    const char *name_;
    uint64_t start_, frame_;
    static uint64_t now() {
        return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
    }

  public:
    explicit CpuScope(const char *name, uint64_t frame = 0)
        : name_(name), start_(enabled() ? now() : 0), frame_(frame) {}
    static bool enabled() {
        static const bool on = std::getenv("SAWGOD_GPU_CPU_TIMING") != nullptr;
        return on;
    }
    ~CpuScope() {
        if (start_) {
            const auto end = now();
            if (end - start_ > 500000)
                std::fprintf(stderr, "GPU CPU %s t=%.3f cost=%.3f ms frame=%llu\n", name_, double(end) / 1e6,
                             double(end - start_) / 1e6, (unsigned long long)frame_);
        }
    }
};
} // namespace sawgod
