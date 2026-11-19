#include "app/input_movie.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "emu/wsc.h"

namespace sawgod::app {

std::string button_names(uint16_t keys) {
    using namespace sawgod::emu;
    static const std::pair<uint16_t, const char *> kNames[] = {
        {KEY_X1, "up"},       {KEY_X2, "right"},  {KEY_X3, "down"},  {KEY_X4, "left"},
        {KEY_Y1, "up2"},      {KEY_Y2, "right2"}, {KEY_Y3, "down2"}, {KEY_Y4, "left2"},
        {KEY_START, "start"}, {KEY_A, "a"},       {KEY_B, "b"},
    };
    std::string s;
    for (const auto &[bit, name] : kNames)
        if (keys & bit) {
            if (!s.empty())
                s += ',';
            s += name;
        }
    return s;
}

std::vector<std::pair<uint64_t, uint16_t>> read_input_movie(const std::string &path) {
    std::vector<std::pair<uint64_t, uint16_t>> movie;
    static const std::pair<const char *, uint16_t> kNames[] = {
        {"up", sawgod::emu::KEY_X1},    {"right", sawgod::emu::KEY_X2}, {"down", sawgod::emu::KEY_X3},
        {"left", sawgod::emu::KEY_X4},  {"up2", sawgod::emu::KEY_Y1},   {"right2", sawgod::emu::KEY_Y2},
        {"down2", sawgod::emu::KEY_Y3}, {"left2", sawgod::emu::KEY_Y4}, {"start", sawgod::emu::KEY_START},
        {"a", sawgod::emu::KEY_A},      {"b", sawgod::emu::KEY_B}};
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        std::fprintf(stderr, "sawgod: cannot read %s\n", path.c_str());
    char line[256];
    while (f && std::fgets(line, sizeof line, f)) {
        char *end;
        const long fr = std::strtol(line, &end, 10);
        if (end == line)
            continue;
        uint16_t keys = 0;
        std::string rest = end;
        for (size_t i = 0; i < rest.size();) {
            size_t j = rest.find_first_of(", \r\n", i);
            if (j == std::string::npos)
                j = rest.size();
            for (const auto &[name, bit] : kNames)
                if (rest.compare(i, j - i, name) == 0 && std::strlen(name) == j - i)
                    keys |= bit;
            i = j + 1;
        }
        movie.emplace_back(uint64_t(fr), keys);
    }
    if (f)
        std::fclose(f);
    std::stable_sort(movie.begin(), movie.end(), [](auto &x, auto &y) { return x.first < y.first; });
    return movie;
}

InputMovie::~InputMovie() {
    if (record_file_)
        std::fclose(record_file_);
}

void InputMovie::open(const LaunchArgs &args, bool have_game) {
    if (have_game && !args.record.empty()) {
        record_path_ = args.record;
        record_file_ = std::fopen(args.record.c_str(), "w");
        if (!record_file_)
            std::fprintf(stderr, "sawgod: cannot open %s for --record\n", args.record.c_str());
    }
    if (!args.inputs.empty())
        changes_ = read_input_movie(args.inputs);
}

uint16_t InputMovie::held(uint64_t frame, uint16_t raw) {
    if (changes_.empty())
        return raw;
    while (next_change_ < changes_.size() && changes_[next_change_].first <= frame)
        held_ = changes_[next_change_++].second;
    return held_; // as sawgod-headless --inputs
}

void InputMovie::record(uint64_t frame, uint16_t keys) {
    if (!record_file_ || keys == last_recorded_keys_)
        return;
    std::fprintf(record_file_, "%llu %s\n", (unsigned long long)frame, button_names(keys).c_str());
    std::fflush(record_file_);
    last_recorded_keys_ = keys;
}

void InputMovie::finish() {
    if (!record_file_)
        return;
    std::fclose(record_file_);
    record_file_ = nullptr;
    std::fprintf(stderr, "sawgod: recorded movie saved to %s\n", record_path_.c_str());
}

} // namespace sawgod::app
