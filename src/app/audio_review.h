#pragma once
#include "engine/settings.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace sawgod::emu {
class Wsc;
}
namespace sawgod::app {
class GbMusic;
class EnhancedObserver;
struct AudioReviewOptions {
    int song = 138;
    int frames = 1510;
    FidelityMode start = FidelityMode::Wsc;
    std::vector<std::pair<int, int>> steps; // game frame, direction; applied in command-line order per frame
    std::string output;                     // one file name inside the review directory
};
// --audio-review-{song,frames,seconds,start,step,output}; returns an error or an empty string.
std::string audio_review_option(AudioReviewOptions &, std::string_view option, std::string_view value);
// Accelerated, sound-driver-only capture from the app's live observer, worker and fidelity mixer.
// No options preserves the two legacy song138 captures, including their initial fade and PCM bytes.
int audio_review(emu::Wsc &, GbMusic &, EnhancedObserver &, const std::string &soundfont,
                 const std::string &dir, const std::optional<AudioReviewOptions> & = std::nullopt);
} // namespace sawgod::app
