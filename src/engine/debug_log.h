// The debug log: what went wrong during play, and how badly (docs/DEBUG_LOG.md). One small text file per
// session in <data>/logs/, problems only: a repeated message is written once and counted, a file stops at a
// size limit, and only the newest few files are kept, so it stays a few megabytes at most. Never touches the
// game.
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace sawgod::debug_log {

// How badly it needs fixing, as the file's header says.
enum class Severity {
    Warning, // cosmetic, or a fallback took over: fix if cheap
    Error,   // a feature failed or the game looks wrong: fix before release
    Critical // a crash or lost progress: fix first
};

struct Limits {
    size_t file_bytes = 256 * 1024;
    int files_kept = 10; // 0: every file is kept
};

// Opens this session's file, deletes the oldest beyond the limit, reports a previous session that never
// ended, and catches crashes. False (and no log) if the folder can't be written: the game plays on.
bool start(const std::filesystem::path &folder, std::string_view version, const Limits &limits = {});
// The session's closing line: what each repeated message's count came to and the totals.
void stop();
bool active();
std::filesystem::path current_file();

// Also prints "sawgod: <message>" to stderr, with or without a log, so a call site needs nothing else.
void report(Severity severity, std::string_view area, std::string_view message);
inline void warning(std::string_view area, std::string_view message) {
    report(Severity::Warning, area, message);
}
inline void error(std::string_view area, std::string_view message) { report(Severity::Error, area, message); }
inline void critical(std::string_view area, std::string_view message) {
    report(Severity::Critical, area, message);
}

// The same with printf formatting, for the app's many call sites.
#if defined(__GNUC__) || defined(__clang__)
#define SAWGOD_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define SAWGOD_PRINTF(f, a)
#endif
void warnf(const char *area, const char *format, ...) SAWGOD_PRINTF(2, 3);
void errorf(const char *area, const char *format, ...) SAWGOD_PRINTF(2, 3);
void criticalf(const char *area, const char *format, ...) SAWGOD_PRINTF(2, 3);

// Where the game is ("map 1, GB, shop"): follows every line, and is what a crash report has to go on.
void set_context(std::string_view context);

// For tests: the crash report a signal would write, without crashing.
void write_crash_for_test(const char *what);

} // namespace sawgod::debug_log
