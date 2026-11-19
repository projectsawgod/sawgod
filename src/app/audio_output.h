// SDL adapter. The stream's recursive mutex serializes the producer and the device get-callback.
#pragma once
#include "app/audio_queue.h"
#include <cstdio>

struct SDL_AudioStream;
namespace sawgod::app {
class AudioOutput {
  public:
    // A supplied, unbound 24 kHz S16 stereo stream is owned. Useful for device-free checks.
    explicit AudioOutput(SDL_AudioStream *stream = nullptr);
    ~AudioOutput();
    AudioOutput(const AudioOutput &) = delete;
    AudioOutput &operator=(const AudioOutput &) = delete;
    bool open();
    void close();
    void clear();
    void submit(std::span<int16_t> samples, uint64_t now);
    struct Stats {
        size_t queued = 0, target = AudioQueue::kBase;
        uint64_t underruns = 0, trimmed = 0;
    };
    Stats stats() const;

  private:
    void initialize();
    static void get(void *userdata, SDL_AudioStream *stream, int additional, int total);
    SDL_AudioStream *stream_ = nullptr;
    AudioQueue queue_;
    // Opt-in diagnostic: unmodified producer PCM, for comparison with SDL's disk-device output.
    FILE *source_log_ = nullptr;
    SDL_AudioStream *source_stream_ = nullptr;
    bool source_started_ = false;
    void record_source(std::span<const int16_t> samples, size_t silence);
    void drain_source();
    // Used only by the producer; the callback's tail lives in AudioQueue's fixed array.
    std::array<int16_t, AudioQueue::kHighWater * 2> silence_{};
};
} // namespace sawgod::app
