// Opt-in presentation diagnostics. The disabled path is one cached flag check; no hashing or I/O.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <type_traits>

namespace sawgod::presentation_diagnostics {
inline bool fresh() {
    static const bool enabled = [] {
        const char *v = std::getenv("SAWGOD_PRESENTATION_FRESH");
        return v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}
// Trace-only runs collect CPU contracts without submitting a picture. Captures leave this off.
inline bool cpu_only() {
    static const bool enabled = [] {
        const char *v = std::getenv("SAWGOD_PRESENTATION_CPU_ONLY");
        const char *trace = std::getenv("SAWGOD_PRESENTATION_TRACE");
        return trace && *trace && v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}
class Hash {
  public:
    uint64_t value = 14695981039346656037ull;
    size_t bytes = 0;
    void data(const void *p, size_t n) {
        const auto *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; ++i)
            value = (value ^ b[i]) * 1099511628211ull;
        bytes += n;
    }
    template <class T> void add(const T &v) {
        static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
        data(&v, sizeof(v));
    }
    template <class T, size_t N> void add(const std::array<T, N> &v) {
        for (const auto &x : v)
            add(x);
    }
    template <class T> void range(const T &v) {
        add(v.size());
        for (const auto &x : v)
            add(x);
    }
};
class Trace {
  public:
    Trace() {
        if (const char *p = std::getenv("SAWGOD_PRESENTATION_TRACE"); p && *p) {
            file_ = std::fopen(p, "w");
            if (!file_) {
                std::perror(p);
                std::exit(2);
            }
            std::fprintf(file_, "view,game_frame,component,bytes,hash\n");
        }
    }
    ~Trace() {
        if (file_) {
            flush();
            std::fclose(file_);
        }
    }
    bool enabled() const { return file_ && started_; }
    void begin(uint64_t game_frame) {
        if (file_) {
            flush();
            ++view_;
            frame_ = game_frame;
            started_ = true;
        }
    }
    void influence_begin(const void *owner, int plane) {
        if (!enabled())
            return;
        auto [i, inserted] = influence_ids_.try_emplace(owner, influence_ids_.size());
        (void)inserted;
        influence_planes_[owner] = "Influence." + std::to_string(i->second) + "." + std::to_string(plane);
    }
    void influence_end(const void *owner, const Hash &h) {
        if (enabled())
            sticky_[influence_planes_.at(owner)] = h;
    }
    void write(const char *component, const Hash &h) {
        if (enabled())
            std::fprintf(file_, "%llu,%llu,%s,%zu,%016llx\n", static_cast<unsigned long long>(view_ - 1),
                         static_cast<unsigned long long>(frame_), component, h.bytes,
                         static_cast<unsigned long long>(h.value));
    }
    template <class T> void texture(const void *texture, const T &pixels) {
        if (enabled()) {
            Hash h;
            h.range(pixels);
            textures_[texture] = h.value;
        }
    }
    uint64_t texture(const void *texture) const {
        const auto found = textures_.find(texture);
        return found == textures_.end() ? 0 : found->second;
    }
    template <class T> void range(const char *component, const T &v) {
        if (enabled()) {
            Hash h;
            h.range(v);
            write(component, h);
        }
    }

  private:
    void flush() {
        if (enabled())
            for (const auto &[name, h] : sticky_)
                write(name.c_str(), h);
    }
    std::map<const void *, size_t> influence_ids_;
    std::map<const void *, std::string> influence_planes_;
    std::map<std::string, Hash> sticky_;
    std::map<const void *, uint64_t> textures_;
    FILE *file_ = nullptr;
    uint64_t view_ = 0, frame_ = 0;
    bool started_ = false;
};
inline Trace &trace() {
    static Trace result;
    return result;
}
} // namespace sawgod::presentation_diagnostics
