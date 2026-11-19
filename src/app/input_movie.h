// Input movies of the app (app/main.cpp): --inputs FILE replays one, --record FILE writes one. The format is
// sawgod-headless's --inputs: "FRAME buttons" lines (tests/movies/*.txt without the '*' tap or '+N' shift
// shorthands).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/launch_args.h"

namespace sawgod::app {

// Inverse of sawgod-headless's parse_buttons: the names it accepts, in a fixed order, for --record.
std::string button_names(uint16_t keys);

// The file's held-button changes, (frame, keys) in frame order (a message and what could be read if not all).
std::vector<std::pair<uint64_t, uint16_t>> read_input_movie(const std::string &path);

// The launch's input movie, either way.
//
// --record: one line per held-button change, "FRAME buttons". FRAME is the count of run_frame() calls
// completed so far, matching how sawgod-headless's --inputs takes effect (a change recorded at frame F is
// first held on the (F+1)-th run_frame() call).
// --inputs FILE: "FRAME buttons" lines (sawgod-headless --inputs format, e.g. tmp/difftest/*.inputs).
class InputMovie {
  public:
    InputMovie() = default;
    InputMovie(const InputMovie &) = delete;
    InputMovie &operator=(const InputMovie &) = delete;
    ~InputMovie();

    // have_game: a ROM is loaded (nothing is recorded without one).
    void open(const LaunchArgs &args, bool have_game);
    // --inputs is being replayed.
    bool replaying() const { return !changes_.empty(); }
    // The buttons held on game frame `frame`: the replayed movie's, or `raw` without one.
    uint16_t held(uint64_t frame, uint16_t raw);
    // Writes a line when `keys` differ from the last recorded ones.
    void record(uint64_t frame, uint16_t keys);
    // Closes the recording and says where it is.
    void finish();

  private:
    std::string record_path_;
    FILE *record_file_ = nullptr;
    uint16_t last_recorded_keys_ = 0;
    std::vector<std::pair<uint64_t, uint16_t>> changes_;
    size_t next_change_ = 0;
    uint16_t held_ = 0;
};

} // namespace sawgod::app
