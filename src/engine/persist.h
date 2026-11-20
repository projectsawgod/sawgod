// persist.h - crash-safe file writes for saves and settings.
//
// WriteFileAtomic writes the data to a temporary file in the destination's directory, flushes it to disk and
// then replaces the destination in one step (rename on POSIX, MoveFileEx on Windows). If anything fails the
// previous file is left exactly as it was and the error says what failed; the temporary file is removed. A
// crash or power loss mid-write leaves either the old file or the new one, never a truncated mix.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sawgod {

bool WriteFileAtomic(const std::string &path, const void *data, size_t size, std::string *error = nullptr);

inline bool WriteFileAtomic(const std::string &path, const std::vector<uint8_t> &data,
                            std::string *error = nullptr) {
    return WriteFileAtomic(path, data.data(), data.size(), error);
}

// The temporary file WriteFileAtomic writes first (exposed for tests).
std::string AtomicTempPath(const std::string &path);

// Persists cartridge SRAM during play and on exit. Watch a selected range, debounce changes until
// stable for `settle` frames, then atomically write the whole raw image. The port watches its save area;
// the original-cartridge front end watches all SRAM with a maximum dirty interval, since work memory
// can keep changing every frame. Filesystem work stays at host frame boundaries.
class SaveFlusher {
  public:
    using Writer = bool (*)(const std::string &path, const std::vector<uint8_t> &data, std::string *error);

    SaveFlusher(std::string path, size_t area_begin, size_t area_end, int settle = 30,
                Writer writer = &WriteFileAtomic, int max_wait = 0);

    // max_wait bounds the dirty interval even while writes continue (zero: no bound).
    // Useful when watching all cartridge SRAM, including continuously changing work memory.

    // The SRAM as loaded (or as last written): the baseline that later frames are compared against.
    void reset(const std::vector<uint8_t> &sram);
    // Call once per game frame. Returns false only when a write was due and failed (`error` says why); the
    // write is retried on the next change.
    bool frame(const std::vector<uint8_t> &sram, std::string *error = nullptr);
    // Unconditional write of the whole image (app exit).
    bool flush(const std::vector<uint8_t> &sram, std::string *error = nullptr);

    int writes() const { return writes_; }

  private:
    bool area_equal(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) const;

    std::string path_;
    size_t begin_, end_;
    int settle_, max_wait_, dirty_age_ = 0;
    Writer writer_;
    std::vector<uint8_t> written_; // the save area as last written (or loaded)
    std::vector<uint8_t> last_;    // the save area at the previous frame
    int stable_ = 0;
    int writes_ = 0;
};

} // namespace sawgod
