#include "app/audio_output.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace sawgod::app {
AudioOutput::AudioOutput(SDL_AudioStream *stream) : stream_(stream) {
    if (stream_)
        initialize();
}

AudioOutput::~AudioOutput() { close(); }

bool AudioOutput::open() {
    close();
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(AudioQueue::kRate)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_)
        return false;
    if (const char *path = SDL_getenv("SAWGOD_AUDIO_SOURCE_LOG")) {
        SDL_AudioSpec device{};
        if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &device, nullptr)) {
            source_log_ = std::fopen(path, "wb");
            if (source_log_)
                source_stream_ = SDL_CreateAudioStream(&spec, &device);
        }
    }
    initialize();
    return SDL_ResumeAudioStreamDevice(stream_);
}

void AudioOutput::initialize() {
    // Populate SDL's reusable queue blocks on the producer thread before installing the callback.
    // A dry-out needs only a 480-byte tail; it reuses the storage freed by playback.
    SDL_PutAudioStreamData(stream_, silence_.data(), int(sizeof(silence_)));
    SDL_ClearAudioStream(stream_);
    queue_.reset();
    SDL_SetAudioStreamGetCallback(stream_, get, this);
}

void AudioOutput::close() {
    if (stream_)
        SDL_DestroyAudioStream(stream_); // waits for any device callback before freeing its userdata
    stream_ = nullptr;
    if (source_stream_) {
        SDL_FlushAudioStream(source_stream_);
        drain_source();
        SDL_DestroyAudioStream(source_stream_);
    }
    source_stream_ = nullptr;
    source_started_ = false;
    if (source_log_)
        std::fclose(source_log_);
    source_log_ = nullptr;
}

void AudioOutput::clear() {
    if (!stream_)
        return;
    SDL_LockAudioStream(stream_);
    // A soft reset keeps the already accepted audio and ends it at zero. Clearing SDL's queue
    // immediately would cut whatever sample the device was playing, before a fade could reach it.
    const auto tail = queue_.underrun(SDL_GetTicksNS());
    if (!tail.empty()) {
        SDL_PutAudioStreamData(stream_, tail.data(), int(tail.size_bytes()));
        SDL_FlushAudioStream(stream_);
    }
    queue_.reset();
    SDL_UnlockAudioStream(stream_);
}

void AudioOutput::submit(std::span<int16_t> samples, uint64_t now) {
    if (!stream_ || samples.empty())
        return;
    SDL_LockAudioStream(stream_);
    const size_t queued = size_t(std::max(SDL_GetAudioStreamQueued(stream_), 0)) / 4;
    const auto p = queue_.plan(queued, samples.size() / 2, now);
    record_source(samples, p.silence);
    if (p.silence)
        SDL_PutAudioStreamData(stream_, silence_.data(), int(p.silence * 4));
    samples = samples.subspan(p.skip * 2);
    const size_t frames = queue_.prepare(samples, p.shorten);
    if (frames)
        SDL_PutAudioStreamData(stream_, samples.data(), int(frames * 4));
    SDL_UnlockAudioStream(stream_);
}

void AudioOutput::record_source(std::span<const int16_t> samples, size_t silence) {
    if (!source_stream_)
        return;
    // Independent resampling of every original producer sample: no skips, recovery silence, or envelopes.
    // Retain the initial priming silence so the fractional resampler phase matches the played stream.
    if (!source_started_ && silence)
        SDL_PutAudioStreamData(source_stream_, silence_.data(), int(silence * 4));
    source_started_ = true;
    SDL_PutAudioStreamData(source_stream_, samples.data(), int(samples.size_bytes()));
    drain_source();
}

void AudioOutput::drain_source() {
    std::array<uint8_t, 16384> buffer{};
    int bytes = 0;
    while ((bytes = SDL_GetAudioStreamData(source_stream_, buffer.data(), int(buffer.size()))) > 0)
        std::fwrite(buffer.data(), 1, size_t(bytes), source_log_);
}

void AudioOutput::get(void *userdata, SDL_AudioStream *stream, int additional, int) {
    if (additional <= 0)
        return;
    auto &self = *static_cast<AudioOutput *>(userdata);
    // SDL already holds the stream lock here. No synthesis, allocation, or waiting on the producer.
    const auto tail = self.queue_.underrun(SDL_GetTicksNS());
    if (!tail.empty()) {
        SDL_PutAudioStreamData(stream, tail.data(), int(tail.size_bytes()));
        SDL_FlushAudioStream(stream); // play the complete tail, including resampler padding
    }
}

AudioOutput::Stats AudioOutput::stats() const {
    if (!stream_)
        return {};
    SDL_LockAudioStream(stream_);
    const Stats result{size_t(std::max(SDL_GetAudioStreamQueued(stream_), 0)) / 4, queue_.target(),
                       queue_.underruns(), queue_.trimmed()};
    SDL_UnlockAudioStream(stream_);
    return result;
}
} // namespace sawgod::app
