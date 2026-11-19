#pragma once

#include <array>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace sawgod::tools {

// Immutable random access: restoring a trial snapshot must also restore its input position without
// mutating a playback cursor. Long unfinished trials can advance far beyond the recording's last input.
class MovieInputs {
  public:
    struct Poke {
        int frame;
        uint16_t addr;
        uint8_t value;
    };
    using Holds = std::vector<std::pair<uint16_t, uint8_t>>;

    MovieInputs(const std::vector<std::pair<int, uint16_t>> &keys, const Holds &holds,
                const std::vector<Poke> &pokes)
        : holds_(holds) {
        for (const auto &[frame, value] : keys)
            keys_[uint64_t(frame)] = value;
        for (const Poke &p : pokes)
            pokes_[uint64_t(p.frame)].emplace_back(p.addr, p.value);
    }

    uint16_t keys_at(uint64_t frame) const {
        const auto next = keys_.upper_bound(frame);
        return next == keys_.begin() ? 0 : std::prev(next)->second;
    }

    void ram_at(std::array<uint8_t, 0x10000> &ram, uint64_t frame) const {
        for (const auto &[addr, value] : holds_)
            ram[addr] = value;
        if (const auto at = pokes_.find(frame); at != pokes_.end())
            for (const auto &[addr, value] : at->second)
                ram[addr] = value;
    }

  private:
    std::map<uint64_t, uint16_t> keys_;
    Holds holds_;
    std::map<uint64_t, Holds> pokes_;
};

} // namespace sawgod::tools
