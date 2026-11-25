#include "engine/startup_timing.h"
#include "engine/debug_log.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <fstream>
#include <sstream>
#include <time.h>
#include <unistd.h>
#endif
namespace sawgod::startup {
namespace {
using Clock = std::chrono::steady_clock;
const auto native_start = Clock::now();
double origin = std::chrono::duration<double, std::milli>(native_start.time_since_epoch()).count();
struct Entry {
    std::string name;
    double end, duration;
};
std::vector<Entry> entries;
size_t written = 0;
bool finished = false;
std::recursive_mutex mutex;
double now() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }
void add(const char *name, double end, double duration) {
    std::lock_guard lock(mutex);
    if (finished && std::string_view(name).find("background") == std::string_view::npos)
        return;
    entries.push_back({name, end - origin, duration});
    char line[256];
    std::snprintf(line, sizeof line, "startup: %.3f ms: %s (%.3f ms)", end - origin, name, duration);
    std::fprintf(stderr, "%s\n", line);
    flush();
}
} // namespace
void flush() {
    std::lock_guard lock(mutex);
    if (!debug_log::active())
        return;
    while (written < entries.size()) {
        const auto &e = entries[written++];
        char line[256];
        std::snprintf(line, sizeof line, "startup: %.3f ms: %s (%.3f ms)", e.end, e.name.c_str(), e.duration);
        debug_log::info(line);
    }
}
void platform_origin(double ms) { origin = ms; }
void platform_stage(const char *name, double end, double duration) { add(name, end, duration); }
void begin() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user, current;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        GetSystemTimeAsFileTime(&current);
        const auto ticks = [](FILETIME value) {
            return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
        };
        origin = now() - double(ticks(current) - ticks(created)) / 10000.0;
    }
#elif defined(__linux__) && !defined(__ANDROID__)
    // /proc records process creation in boot-clock ticks, before dynamic libraries load.
    std::ifstream stat("/proc/self/stat");
    std::string line;
    if (std::getline(stat, line) && line.rfind(')') != std::string::npos) {
        std::istringstream fields(line.substr(line.rfind(')') + 2));
        std::string field;
        for (int index = 3; index <= 22 && fields >> field; ++index)
            if (index == 22) {
                timespec boot{};
                if (clock_gettime(CLOCK_BOOTTIME, &boot) == 0)
                    origin = now() - (double(boot.tv_sec) * 1000 + double(boot.tv_nsec) / 1e6 -
                                      std::stod(field) * 1000 / sysconf(_SC_CLK_TCK));
            }
    }
#endif
#ifndef __ANDROID__
    add("process start", origin, 0);
#endif
    mark("native main");
}
void mark(const char *name) { add(name, now(), 0); }
Stage::Stage(const char *name) : name_(name), start_(now()) {}
Stage::~Stage() { add(name_, now(), now() - start_); }
void finish() {
    std::lock_guard lock(mutex);
    if (finished)
        return;
    mark("first frame presented");
    auto sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto &a, const auto &b) { return a.duration > b.duration; });
    char line[1024];
    int n = std::snprintf(line, sizeof line, "startup: total %.3f ms; top stages", now() - origin);
    for (size_t i = 0; i < std::min<size_t>(5, sorted.size()); ++i)
        n += std::snprintf(line + n, sizeof line - size_t(n), " %s=%.3f ms;", sorted[i].name.c_str(),
                           sorted[i].duration);
    std::fprintf(stderr, "%s\n", line);
    debug_log::info(line);
    finished = true;
}
} // namespace sawgod::startup
