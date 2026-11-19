// Read-only diagnostics for accelerated real-app mode switching replays.
#pragma once
#include "emu/wsc.h"
#include "engine/assets.h"
#include <cstdio>
#include <string>
#include <vector>

namespace sawgod::app {
class ShotSoak {
  public:
    ~ShotSoak() {
        if (file_)
            std::fclose(file_);
    }
    bool open(const std::string &path) {
        file_ = std::fopen(path.c_str(), "w");
        if (file_)
            std::fputs(
                "view,movie_frame,game_frame,graphics,settled,options,scene,field_picture,battle,"
                "full_ui,native,opening,map,world,mode7,mode7_drawn,pixel_scale,held,picture,ram,sram,iram\n",
                file_);
        return file_;
    }
    // Same VBlank work-RAM sample and six interrupt-stack-byte normalization as runtime goldens.
    void irq(const emu::Wsc &w, uint64_t movie_frame) {
        if (w.scanline != 144 || (movie_frame + 1) % 300)
            return;
        std::vector<uint8_t> ram(w.ram.begin(), w.ram.end());
        for (int i = 0; i < 6; ++i)
            ram[uint16_t(w.cpu.s.r[emu::SP] + i)] = 0;
        iram_ = hash(ram.data(), ram.size());
    }
    void frame(size_t view, uint64_t movie_frame, const emu::Wsc &w, int graphics, bool settled, bool options,
               int scene, bool field, bool battle, bool full_ui, bool native, bool opening, int map,
               bool world, float mode7, bool drawn, float scale, bool held,
               const std::vector<uint32_t> &pixels, bool last) {
        const bool checkpoint = movie_frame % 300 == 0 || last;
        const auto ram = checkpoint ? hash(w.ram.data(), w.ram.size()) : "-";
        const auto sram = checkpoint ? hash(w.sram.data(), w.sram.size()) : "-";
        // Hash explicit RGB bytes, independent of pixel padding, pitch and host endianness.
        std::vector<uint8_t> rgb;
        rgb.reserve(pixels.size() * 3);
        for (auto p : pixels)
            for (int shift : {16, 8, 0})
                rgb.push_back(uint8_t(p >> shift));
        const auto picture = hash(rgb.data(), rgb.size());
        std::fprintf(file_, "%zu,%llu,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%d,%.6f,%d,%s,%s,%s,%s\n",
                     view, (unsigned long long)movie_frame, (unsigned long long)w.frame_count, graphics,
                     int(settled), int(options), scene, int(field), int(battle), int(full_ui), int(native),
                     int(opening), map, int(world), double(mode7), int(drawn), double(scale), int(held),
                     picture.c_str(), ram.c_str(), sram.c_str(),
                     checkpoint && !iram_.empty() ? iram_.c_str() : "-");
        if (checkpoint)
            std::fflush(file_);
    }

  private:
    static std::string hash(const uint8_t *data, size_t size) { return to_hex(sha1(data, size)); }
    FILE *file_ = nullptr;
    std::string iram_;
};
} // namespace sawgod::app
