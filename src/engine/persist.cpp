#include "engine/persist.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sawgod {

// Per process: two running copies writing the same file must not truncate or rename each other's temporary.
std::string AtomicTempPath(const std::string &path) {
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = int(getpid());
#endif
    return path + "." + std::to_string(pid) + ".tmp";
}

namespace {

#ifdef _WIN32
std::wstring Wide(const std::string &utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? size_t(n) : 0, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
    if (!w.empty())
        w.pop_back(); // the terminator MultiByteToWideChar counted
    return w;
}

FILE *OpenWrite(const std::string &path) { return _wfopen(Wide(path).c_str(), L"wb"); }

bool SyncFile(FILE *f) { return _commit(_fileno(f)) == 0; }

bool Replace(const std::string &from, const std::string &to) {
    return MoveFileExW(Wide(from).c_str(), Wide(to).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void RemoveFile(const std::string &path) { _wremove(Wide(path).c_str()); }

void SyncDir(const std::string &) {}
#else
FILE *OpenWrite(const std::string &path) { return std::fopen(path.c_str(), "wb"); }

bool SyncFile(FILE *f) { return fsync(fileno(f)) == 0; }

bool Replace(const std::string &from, const std::string &to) {
    return std::rename(from.c_str(), to.c_str()) == 0;
}

void RemoveFile(const std::string &path) { std::remove(path.c_str()); }

// Makes the rename itself durable; best effort (a failure here doesn't undo a completed replace).
void SyncDir(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
    const int fd = open(dir.c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}
#endif

bool Fail(std::string *error, const std::string &what, const std::string &path, int err) {
    if (error)
        *error = what + " " + path + (err ? std::string(": ") + std::strerror(err) : std::string());
    return false;
}

} // namespace

bool WriteFileAtomic(const std::string &path, const void *data, size_t size, std::string *error) {
    const std::string tmp = AtomicTempPath(path);
    errno = 0;
    FILE *f = OpenWrite(tmp);
    if (!f)
        return Fail(error, "cannot create", tmp, errno);
    errno = 0;
    bool ok = std::fwrite(data, 1, size, f) == size;
    int err = errno;
    if (ok && std::fflush(f) != 0)
        ok = false, err = errno;
    if (ok && !SyncFile(f))
        ok = false, err = errno;
    if (std::fclose(f) != 0 && ok)
        ok = false, err = errno;
    if (!ok) {
        RemoveFile(tmp);
        return Fail(error, "cannot write", tmp, err);
    }
    errno = 0;
    if (!Replace(tmp, path)) {
        err = errno;
        RemoveFile(tmp);
        return Fail(error, "cannot replace", path, err);
    }
    SyncDir(path);
    return true;
}

SaveFlusher::SaveFlusher(std::string path, size_t area_begin, size_t area_end, int settle, Writer writer,
                         int max_wait)
    : path_(std::move(path)), begin_(area_begin), end_(area_end), settle_(settle),
      max_wait_(std::max(max_wait, 0)), writer_(writer) {}

bool SaveFlusher::area_equal(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) const {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), b.size()) == 0);
}

void SaveFlusher::reset(const std::vector<uint8_t> &sram) {
    const size_t end = std::min(end_, sram.size()), begin = std::min(begin_, end);
    written_.assign(sram.begin() + long(begin), sram.begin() + long(end));
    last_ = written_;
    stable_ = dirty_age_ = 0;
}

bool SaveFlusher::frame(const std::vector<uint8_t> &sram, std::string *error) {
    const size_t end = std::min(end_, sram.size()), begin = std::min(begin_, end);
    const std::vector<uint8_t> area(sram.begin() + long(begin), sram.begin() + long(end));
    if (area_equal(area, written_)) {
        last_ = area;
        stable_ = dirty_age_ = 0;
        return true;
    }
    ++dirty_age_;
    const bool changed = area.size() != last_.size() || !area_equal(area, last_);
    last_ = area;
    stable_ = changed ? 0 : stable_ + 1;
    if (stable_ < settle_ && (!max_wait_ || dirty_age_ < max_wait_))
        return true;
    dirty_age_ = 0;
    if (!writer_(path_, sram, error)) {
        written_ = area; // don't retry every frame: the next change in the save area tries again
        return false;
    }
    written_ = area;
    ++writes_;
    return true;
}

bool SaveFlusher::flush(const std::vector<uint8_t> &sram, std::string *error) {
    if (!writer_(path_, sram, error))
        return false;
    reset(sram);
    ++writes_;
    return true;
}

} // namespace sawgod
