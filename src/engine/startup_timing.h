// Monotonic startup milestones; buffered until the debug log opens.
#pragma once
#include <string>
namespace sawgod::startup {
void platform_origin(double ms);
void platform_stage(const char *name, double end, double duration);
void begin();
void mark(const char *name);
void flush();
void finish();
class Stage {
    const char *name_;
    double start_;

  public:
    explicit Stage(const char *name);
    ~Stage();
};
} // namespace sawgod::startup
