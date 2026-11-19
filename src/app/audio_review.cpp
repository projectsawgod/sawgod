#include "app/audio_review.h"
#include "app/enhanced_music.h"
#include "app/gb_music.h"
#include "app/host_settings.h"
#include "app/sound_mix.h"
#include "emu/wsc.h"
#include "engine/fidelity.h"
#include "game/bank3d_sound.h"
#include "game/machine.h"
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>

namespace sawgod::app {
namespace {
constexpr uint64_t kFrameNs = 13'250'000; // 159 * 256 cycles at 3.072 MHz
constexpr int kSamplesPerFrame = 636;     // interleaved stereo at 24 kHz
// Bound the whole RIFF file (including its eight-frame lookahead tail) to its 32-bit size field.
constexpr int kMaxFrames =
    int((std::numeric_limits<uint32_t>::max() - 36) / (kSamplesPerFrame * sizeof(int16_t))) -
    EnhancedAudio::kLookahead;
bool integer(std::string_view value, int &out) {
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), out);
    return error == std::errc{} && end == value.data() + value.size();
}
bool wav(const std::string &path, const std::vector<int16_t> &samples) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    auto tag = [&](const char *s) { std::fwrite(s, 1, 4, f); };
    auto le = [&](uint32_t n, int bytes) {
        for (int i = 0; i < bytes; ++i)
            std::fputc(int((n >> (8 * i)) & 255), f);
    };
    tag("RIFF");
    le(36 + uint32_t(samples.size() * 2), 4);
    tag("WAVE");
    tag("fmt ");
    le(16, 4);
    le(1, 2);
    le(2, 2);
    le(24000, 4);
    le(96000, 4);
    le(4, 2);
    le(16, 2);
    tag("data");
    le(uint32_t(samples.size() * 2), 4);
    for (auto s : samples)
        le(uint16_t(s), 2);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}
} // namespace
std::string audio_review_option(AudioReviewOptions &options, std::string_view option,
                                std::string_view value) {
    if (option == "--audio-review-song") {
        if (!integer(value, options.song) || options.song < 0 || options.song > 255)
            return "--audio-review-song requires a WSC request id from 0 to 255";
    } else if (option == "--audio-review-frames") {
        if (!integer(value, options.frames) || options.frames <= 0 || options.frames > kMaxFrames)
            return "--audio-review-frames requires a positive frame count that fits a WAV file";
    } else if (option == "--audio-review-seconds") {
        // (strtod, not std::from_chars: Apple's libc++ has no floating-point from_chars)
        const std::string text(value);
        char *end = nullptr;
        const double seconds = text.empty() ? 0 : std::strtod(text.c_str(), &end);
        const double frames = seconds * 1e9 / double(kFrameNs);
        if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(frames) || frames < 0.5 ||
            frames > kMaxFrames)
            return "--audio-review-seconds requires a positive duration that fits a WAV file";
        options.frames = int(std::llround(frames));
    } else if (option == "--audio-review-start") {
        if (value == "gb")
            options.start = FidelityMode::Gb;
        else if (value == "wsc")
            options.start = FidelityMode::Wsc;
        else if (value == "enhanced")
            options.start = FidelityMode::Enhanced;
        else
            return "--audio-review-start requires gb, wsc or enhanced";
    } else if (option == "--audio-review-step") {
        const auto colon = value.find(':');
        int frame = 0;
        if (colon == std::string_view::npos || !integer(value.substr(0, colon), frame) || frame < 0 ||
            (value.substr(colon + 1) != "+1" && value.substr(colon + 1) != "-1"))
            return "--audio-review-step requires FRAME:+1 or FRAME:-1 (repeat for more steps)";
        options.steps.emplace_back(frame, value.substr(colon + 1) == "+1" ? 1 : -1);
    } else if (option == "--audio-review-output") {
        const std::filesystem::path path(value);
        if (value.empty() || path.has_parent_path() || path.filename() != path ||
            path.extension() != ".wav" || value.find('\\') != std::string_view::npos)
            return "--audio-review-output requires a .wav file name inside the review directory";
        options.output = value;
    } else {
        return "unknown audio review option: " + std::string(option);
    }
    return {};
}
int audio_review(emu::Wsc &w, GbMusic &gb, EnhancedObserver &observer, const std::string &sf,
                 const std::string &dir, const std::optional<AudioReviewOptions> &options) {
    if (options) {
        if (options->output.empty()) {
            std::fprintf(stderr, "sawgod: custom audio review requires --audio-review-output NAME.wav\n");
            return 1;
        }
        for (const auto &[frame, direction] : options->steps) {
            if (frame >= options->frames) {
                std::fprintf(stderr, "sawgod: audio review step %d is outside the capture\n", frame);
                return 1;
            }
        }
    }
    const std::vector<std::string> names =
        options ? std::vector<std::string>{options->output}
                : std::vector<std::string>{"song138-wsc-to-enhanced.wav", "song138-enhanced-to-wsc.wav"};
    for (const auto &name : names) {
        if (std::filesystem::exists(std::filesystem::path(dir) / name)) {
            std::fprintf(stderr, "sawgod: choose an unused audio review directory; keeping old capture\n");
            return 1;
        }
    }
    std::filesystem::create_directories(dir);
    FILE *log = nullptr;
    if (const char *path = std::getenv("SAWGOD_FRAME_LOG"))
        log = std::fopen(path, "w");
    if (log)
        std::fprintf(log, "direction,frame,tick_ms,audio_synth_ms,audio_dropped\n");
    for (size_t run = 0; run < names.size(); ++run) {
        const bool down = run == 1;
        EnhancedAudio worker(sf);
        worker.wait_initialized();
        if (!worker.available()) {
            std::fprintf(stderr, "sawgod: audio review requires the live Enhanced music synthesizer\n");
            if (log)
                std::fclose(log);
            return 1;
        }
        observer.reset();
        gb.reset();
        game::Machine m(w);
        auto saved = w.cpu.s;
        m.ds() = 0;
        m.cs() = 0xD000;
        game::sound_init(m);
        w.cpu.s = saved;
        saved = w.cpu.s;
        m.ax() = uint16_t(options ? options->song : 138);
        m.ds() = 0;
        m.cs() = 0xD000;
        game::sound_request_core(m);
        w.cpu.s = saved;
        w.apu.take();
        Fidelity fidelity(FidelityMode::Wsc, FidelityMode::Swap);
        if (options) {
            // Settle the retained Swap position before capture. All custom levels use the same
            // fixed eight-frame cursor, even a run with no steps, so editor cuts stay sample-aligned.
            for (int level = 2; level > int(options->start); --level)
                fidelity.step(-1, 0);
        } else if (!down)
            fidelity.step(-1, 0);
        std::vector<int16_t> output;
        const int frames =
            options ? options->frames
                    : 1510; // 20 s: fade starts at frame 528 (6.996 s), both arrangements run throughout
        for (int frame = 0; frame < frames; ++frame) {
            const uint64_t now = uint64_t(frame) * kFrameNs + (options ? Fidelity::kFadeNs : 0);
            if (options) {
                for (const auto &[step_frame, direction] : options->steps)
                    if (frame == step_frame)
                        fidelity.step(direction, now);
            } else if (frame == 528)
                fidelity.step(down ? -1 : 1, now);
            const auto weights = fidelity.music_weights(now);
            const float g = worker.available() && observer.playing(w) ? weights[2] : 0;
            const auto mix = music_mix(weights, gb.loaded(), gb.playing(), observer.playing(w));
            const auto cue_mix = music_mix(weights, gb.loaded(), gb.playing(), observer.cue_playing(w));
            if (options) {
                // Same ownership, fallback and GB gains as the live tick; no ordinary SFX in a review.
                const uint8_t cue_slots = observer.wsc_music_slots(w);
                apply_channel_gains(w, mix.wsc, 0, gb.wsc_music_slots(w) | cue_slots, gb.mapped_wsc_slots(w),
                                    mix.gb);
                apply_cue_gain(w, cue_slots, cue_mix.wsc);
            } else
                apply_channel_gains(w, 1 - g, 0);
            const auto start = std::chrono::steady_clock::now();
            paused_tick(w);
            auto samples = w.apu.take();
            gb.frame(w, samples, options ? mix.gb : 0, 0);
            auto packet = observer.frame(w);
            packet.samples = std::move(samples);
            packet.gain = options ? (enhanced_song(packet.song) ? weights[2] : mix.enhanced) : g;
            if (options)
                packet.cue_gain = cue_mix.enhanced;
            // Explicit offline backpressure: gameplay uses the nonblocking submit directly.
            while (!worker.submit(packet)) {
                auto ready = worker.take();
                output.insert(output.end(), ready.begin(), ready.end());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto ready = worker.take();
            output.insert(output.end(), ready.begin(), ready.end());
            if (log) {
                const double cost =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
                std::fprintf(log, "%s,%d,%.4f,%.4f,%llu\n", down ? "down" : "up", frame, cost,
                             worker.cost_ms(), (unsigned long long)worker.dropped());
            }
        }
        // Keep draining the bounded output queue while the worker completes, then flush its lookahead.
        std::atomic<bool> complete{false};
        std::thread finish([&] {
            worker.finish();
            complete.store(true);
        });
        while (!complete.load()) {
            auto ready = worker.take();
            output.insert(output.end(), ready.begin(), ready.end());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        finish.join();
        auto ready = worker.take();
        output.insert(output.end(), ready.begin(), ready.end());
        const auto path = (std::filesystem::path(dir) / names[run]).string();
        if (output.size() != size_t(frames + EnhancedAudio::kLookahead) * kSamplesPerFrame) {
            std::fprintf(stderr, "sawgod: audio review incomplete (%zu samples)\n", output.size());
            if (log)
                std::fclose(log);
            return 1;
        }
        // The worker flushed every captured frame. Remove only its common silent startup delay
        // in custom runs, so FRAME steps and requested duration refer directly to the WAV timeline.
        if (options)
            output.erase(output.begin(), output.begin() + EnhancedAudio::kLookahead * kSamplesPerFrame);
        if (!wav(path, output)) {
            std::fprintf(stderr, "sawgod: cannot write audio review %s\n", path.c_str());
            if (log)
                std::fclose(log);
            return 1;
        }
        const auto cost = worker.cost();
        std::printf("synth: %llu frames, mean %.4f ms, max %.4f ms\n", (unsigned long long)cost.frames,
                    cost.total_ns / (1e6 * cost.frames), cost.max_ns / 1e6);
        std::printf("%s: %d frames, %zu samples, %llu backpressure retries\n", path.c_str(), frames,
                    output.size(), (unsigned long long)worker.dropped());
    }
    if (log)
        std::fclose(log);
    return 0;
}
} // namespace sawgod::app
