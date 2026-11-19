#pragma once
// First complete occurrence of each entry/palette/action, in real replay context.
// PNGs and timing records are private/review evidence, never runtime resources.
#include "emu/png.h"
#include "game/hooks.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace sawgod::tools {
class GbEffectsCapture {
  public:
    explicit GbEffectsCapture(std::string path) : root_(std::move(path)) {}
    void begin(uint64_t frame, const game::hooks::Args &a) {
        finish(frame);
        char name[40];
        std::snprintf(name, sizeof name, "%06X-%02X-%d", a.a, a.c, a.b);
        key_ = name;
        if (seen_.contains(key_)) {
            key_.clear();
            return;
        }
        std::filesystem::create_directories(root_ / key_);
        stream_.open(root_ / key_ / "frames.tsv");
        stream_ << "frame\tduration\timage\n";
        start_ = frame;
        last_ = 0;
        number_ = 0;
        previous_gb_.clear();
    }
    void frame(uint64_t frame, const std::vector<uint32_t> &gb) {
        if (key_.empty())
            return;
        if (gb == previous_gb_)
            return;
        if (last_)
            stream_ << last_ << '\t' << frame - last_ << '\t' << number_ - 1 << '\n';
        last_ = frame;
        const auto stem = (root_ / key_ / std::to_string(number_++)).string();
        emu::write_file(stem + "-gb.png", emu::encode_png(gb.data(), 224, 144));
        previous_gb_ = gb;
    }
    void finish(uint64_t frame) {
        if (key_.empty())
            return;
        if (last_)
            stream_ << last_ << '\t' << std::max(uint64_t(1), frame + 1 - last_) << '\t' << number_ - 1
                    << '\n';
        stream_.close();
        std::ofstream info(root_ / key_ / "native.tsv");
        info << "start\tend\n" << start_ << '\t' << frame << '\n';
        seen_.insert(key_);
        key_.clear();
    }

  private:
    std::filesystem::path root_;
    std::string key_;
    std::set<std::string> seen_;
    std::ofstream stream_;
    uint64_t start_ = 0, last_ = 0;
    unsigned number_ = 0;
    std::vector<uint32_t> previous_gb_;
};
} // namespace sawgod::tools
