#include "engine/debug_log.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace sawgod::debug_log {
namespace {
constexpr const char *kEnd = "session end:";
constexpr size_t kCriticalRoom = 4096; // always kept free: a crash's report still fits a full file

struct Log {
    std::mutex mutex;
    FILE *file = nullptr;
    fs::path path;
    Limits limits;
    size_t written = 0;
    bool full = false;
    std::chrono::steady_clock::time_point begin;
    std::string home, context;
    std::map<std::string, int> seen; // severity + area + message: times reported
    int counts[3] = {};
} g;

// Read by the crash handler, which can't take locks or allocate.
char crash_context[160] = "";
int crash_fd = -1;

const char *name(Severity s) {
    return s == Severity::Critical ? "CRITICAL" : s == Severity::Error ? "ERROR" : "WARNING";
}

void put(int fd, const char *text) {
#ifdef _WIN32
    (void)!_write(fd, text, unsigned(std::strlen(text)));
#else
    (void)!::write(fd, text, std::strlen(text));
#endif
}

void crash_report(const char *what) {
    if (crash_fd < 0)
        return;
    put(crash_fd, "CRITICAL crash: ");
    put(crash_fd, what);
    if (crash_context[0]) {
        put(crash_fd, " [");
        put(crash_fd, crash_context);
        put(crash_fd, "]");
    }
    put(crash_fd, "\nsession end: crashed\n");
    crash_fd = -1; // once: a crash inside this one still ends the process
}

void on_signal(int number) {
    crash_report(number == SIGSEGV   ? "invalid memory access (SIGSEGV)"
                 : number == SIGABRT ? "aborted (SIGABRT)"
                 : number == SIGFPE  ? "arithmetic fault (SIGFPE)"
                 : number == SIGILL  ? "illegal instruction (SIGILL)"
                                     : "fatal signal");
    std::signal(number, SIG_DFL);
    std::raise(number);
}

void on_terminate() {
    const char *what = "uncaught exception";
    try {
        if (const auto current = std::current_exception())
            std::rethrow_exception(current);
    } catch (const std::exception &e) {
        what = e.what();
    } catch (...) {
    }
    crash_report(what);
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

#ifdef _WIN32
LONG WINAPI on_exception(EXCEPTION_POINTERS *e) {
    crash_report(e->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW ? "stack overflow"
                                                                               : "unhandled exception");
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void catch_crashes() {
    std::set_terminate(on_terminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_exception);
    for (int number : {SIGABRT, SIGFPE, SIGILL})
        std::signal(number, on_signal);
#else
    // Its own stack, so running out of stack (a fiber's, say) is reported too.
    static char stack[64 * 1024];
    stack_t alternate{};
    alternate.ss_sp = stack;
    alternate.ss_size = sizeof stack;
    sigaltstack(&alternate, nullptr);
    struct sigaction action {};
    action.sa_handler = on_signal;
    action.sa_flags = SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int number : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL})
        sigaction(number, &action, nullptr);
#endif
}

std::vector<fs::path> session_files(const fs::path &folder) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(folder, ec)) {
        const std::string file = entry.path().filename().string();
        if (file.rfind("sawgod-", 0) == 0 && entry.path().extension() == ".log")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end()); // their names sort by time
    return files;
}

// A session's file with no closing line: killed, hung, or the power went.
bool ended(const fs::path &path) {
    std::ifstream in(path);
    std::string line;
    bool end = false;
    while (std::getline(in, line))
        end = end || line.rfind(kEnd, 0) == 0;
    return end;
}

std::string clock_text(std::chrono::steady_clock::duration since) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since).count();
    char text[32];
    std::snprintf(text, sizeof text, "%02d:%02d:%02d", int(seconds / 3600), int(seconds / 60 % 60),
                  int(seconds % 60));
    return text;
}

// Paths in messages lose the home folder: the file may get passed along.
std::string without_home(std::string text) {
    if (g.home.empty())
        return text;
    for (size_t at = 0; (at = text.find(g.home, at)) != std::string::npos;)
        text.replace(at, g.home.size(), "~");
    return text;
}

void write_line(const std::string &line) {
    std::fputs(line.c_str(), g.file);
    std::fflush(g.file);
    g.written += line.size();
}
} // namespace

bool start(const fs::path &folder, std::string_view version, const Limits &limits) {
    stop();
    std::error_code ec;
    fs::create_directories(folder, ec);
    const auto previous = session_files(folder);
    const auto clock = std::chrono::system_clock::now();
    const std::time_t now = std::chrono::system_clock::to_time_t(clock);
    const int millis =
        int(std::chrono::duration_cast<std::chrono::milliseconds>(clock.time_since_epoch()).count() % 1000);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[40], when[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
    std::snprintf(stamp + std::strlen(stamp), sizeof stamp - std::strlen(stamp), "-%03d", millis);
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
    // Names sort by time, to the millisecond; one that wouldn't sort last (the same millisecond, or the clock
    // went back) follows the newest instead.
    std::string name = "sawgod-" + std::string(stamp);
    if (!previous.empty() && folder / (name + ".log") <= previous.back())
        name = previous.back().stem().string() + "a";
    const fs::path path = folder / (name + ".log");
    FILE *file = std::fopen(path.string().c_str(), "wb");
    if (!file)
        return false;
    {
        std::lock_guard lock(g.mutex);
        g.file = file;
        g.path = path;
        g.limits = limits;
        g.written = 0;
        g.full = false;
        g.begin = std::chrono::steady_clock::now();
        g.seen.clear();
        std::fill(std::begin(g.counts), std::end(g.counts), 0);
        const char *home = std::getenv("HOME");
#ifdef _WIN32
        if (!home)
            home = std::getenv("USERPROFILE");
#endif
        g.home = home ? home : "";
        write_line(std::string(version) + ", " + when + "\n");
        write_line(
            "# WARNING: cosmetic, or a fallback took over; fix if cheap. ERROR: a feature failed or the "
            "game looks wrong; fix before release. CRITICAL: a crash or lost progress; fix first.\n");
#ifdef _WIN32
        crash_fd = _fileno(file);
#else
        crash_fd = fileno(file);
#endif
    }
    catch_crashes();
    if (!previous.empty() && !ended(previous.back()))
        error("session", "the previous session never ended (killed, hung, or lost power): " +
                             previous.back().filename().string());
    // This session's file counts toward the limit.
    if (limits.files_kept > 0)
        for (size_t i = 0; i + size_t(limits.files_kept - 1) < previous.size(); ++i)
            fs::remove(previous[i], ec);
    return true;
}

void info(std::string_view message) {
    std::lock_guard lock(g.mutex);
    if (g.file) write_line(std::string(message) + "\n");
}

void stop() {
    std::lock_guard lock(g.mutex);
    if (!g.file)
        return;
    g.full = false;
    for (const auto &[key, count] : g.seen)
        if (count > 1)
            write_line("repeated " + std::to_string(count) + " times: " + key + "\n");
    write_line(std::string(kEnd) + " " + std::to_string(g.counts[size_t(Severity::Critical)]) +
               " critical, " + std::to_string(g.counts[size_t(Severity::Error)]) + " errors, " +
               std::to_string(g.counts[size_t(Severity::Warning)]) + " warnings\n");
    crash_fd = -1;
    std::fclose(g.file);
    g.file = nullptr;
}

bool active() {
    std::lock_guard lock(g.mutex);
    return g.file != nullptr;
}

fs::path current_file() {
    std::lock_guard lock(g.mutex);
    return g.path;
}

void report(Severity severity, std::string_view area, std::string_view message) {
    std::fprintf(stderr, "sawgod: %.*s\n", int(message.size()), message.data());
    std::lock_guard lock(g.mutex);
    if (!g.file)
        return;
    const std::string text = without_home(std::string(message));
    ++g.counts[size_t(severity)];
    if (g.full && severity != Severity::Critical)
        return; // not even remembered: what's kept stays as small as the file
    const std::string key = std::string(name(severity)) + " " + std::string(area) + ": " + text;
    if (++g.seen[key] > 1)
        return;
    std::string line = clock_text(std::chrono::steady_clock::now() - g.begin) + " " + key;
    if (!g.context.empty())
        line += " [" + g.context + "]";
    line += "\n";
    const size_t room = severity == Severity::Critical
                            ? g.limits.file_bytes
                            : g.limits.file_bytes - std::min(g.limits.file_bytes, kCriticalRoom);
    if (g.written + line.size() > room) {
        if (!g.full)
            write_line("log full: further messages are only counted\n");
        g.full = true;
        return;
    }
    write_line(line);
}

namespace {
void reportv(Severity severity, const char *area, const char *format, va_list args) {
    char text[1024];
    std::vsnprintf(text, sizeof text, format, args);
    report(severity, area, text);
}
} // namespace

void warnf(const char *area, const char *format, ...) {
    va_list args;
    va_start(args, format);
    reportv(Severity::Warning, area, format, args);
    va_end(args);
}
void errorf(const char *area, const char *format, ...) {
    va_list args;
    va_start(args, format);
    reportv(Severity::Error, area, format, args);
    va_end(args);
}
void criticalf(const char *area, const char *format, ...) {
    va_list args;
    va_start(args, format);
    reportv(Severity::Critical, area, format, args);
    va_end(args);
}

void set_context(std::string_view context) {
    std::lock_guard lock(g.mutex);
    g.context = context;
    const size_t n = std::min(context.size(), sizeof crash_context - 1);
    std::memcpy(crash_context, context.data(), n);
    crash_context[n] = 0;
}

void write_crash_for_test(const char *what) {
    std::lock_guard lock(g.mutex);
    std::fflush(g.file);
    crash_report(what);
}

} // namespace sawgod::debug_log
