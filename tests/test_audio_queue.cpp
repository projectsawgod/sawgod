#include "app/audio_output.h"
#include "app/audio_queue.h"
#include "app/ff_audio.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <limits>
#include <vector>

using sawgod::app::AudioOutput;
using sawgod::app::AudioQueue;
namespace {
constexpr uint64_t kTick = 13'250'000;
std::vector<int16_t> packet(size_t frames = 318) {
    std::vector<int16_t> samples(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        samples[2 * i] = 12000;
        samples[2 * i + 1] = -6000;
    }
    return samples;
}
size_t submit(AudioQueue &q, size_t queued, std::vector<int16_t> samples, uint64_t now) {
    const auto p = q.plan(queued, samples.size() / 2, now);
    return queued + p.silence + q.prepare(std::span(samples).subspan(p.skip * 2), p.shorten);
}
SDL_AudioStream *stream(int rate = 24000) {
    const SDL_AudioSpec input{SDL_AUDIO_S16, 2, 24000}, output{SDL_AUDIO_S16, 2, rate};
    return SDL_CreateAudioStream(&input, &output);
}
} // namespace

TEST_CASE("audio queue: startup establishes the target and balanced production never drifts") {
    AudioQueue q;
    CHECK(q.underrun(0).empty()); // device starts before the first real packet
    CHECK(q.underruns() == 0);
    CHECK(q.plan(0, 0, 0).silence == 0);
    auto first = packet();
    const auto p = q.plan(0, first.size() / 2, 0);
    CHECK(p.silence == AudioQueue::kBase - 318);
    CHECK(q.prepare(first) == 318);
    CHECK(first[0] == 0);
    CHECK(first[1] == 0);
    CHECK(first[2 * AudioQueue::kFade] == 12000);
    size_t queued = p.silence + first.size() / 2;
    for (uint64_t i = 1; i <= 100000; ++i) {
        auto samples = packet();
        const auto original = samples;
        const auto plan = q.plan(queued - 318, 318, i * kTick);
        REQUIRE(plan.silence == 0);
        REQUIRE(plan.skip == 0);
        REQUIRE(plan.shorten == 0);
        queued = queued - 318 + q.prepare(samples);
        REQUIRE(samples == original);
    }
    CHECK(queued == AudioQueue::kBase);
    CHECK(q.underruns() == 0);
}

TEST_CASE("audio queue: one stall appends one stereo tail and primes before a split fade-in") {
    AudioQueue q;
    submit(q, 0, packet(), 0);
    const auto tail = q.underrun(1'000'000'000);
    REQUIRE(tail.size() == AudioQueue::kFade * 2);
    CHECK(tail[0] == 11900);
    CHECK(tail[1] == -5950);
    CHECK(tail[tail.size() - 2] == 0);
    CHECK(tail.back() == 0);
    for (size_t i = 2; i < tail.size(); i += 2) {
        CHECK(tail[i] <= tail[i - 2]);
        CHECK(tail[i + 1] >= tail[i - 1]);
    }
    for (int i = 0; i < 100; ++i)
        CHECK(q.underrun(1'000'000'000 + uint64_t(i)).empty());
    CHECK(q.underruns() == 1);
    constexpr size_t raised = AudioQueue::kBase + AudioQueue::kRate * 20 / 1000; // one dry-out: +20 ms
    CHECK(q.target() == raised);
    const auto p = q.plan(20, 30, 2'000'000'000); // some tail is still queued
    CHECK(p.silence == raised - 20 - 30);
    auto a = packet(30), b = packet(100);
    q.prepare(a);
    q.prepare(b);
    CHECK(a[0] == 0);
    CHECK(a[58] == 2900);
    CHECK(b[0] == 3000);
    CHECK(b[2 * 90] == 12000);
    CHECK(q.plan(2000, 30, 2'010'000'000).silence == 0);
}

TEST_CASE("audio queue: repeated stalls grow to the cap and ten quiet seconds start gentle recovery") {
    AudioQueue q;
    uint64_t now = 0;
    for (int i = 0; i < 12; ++i) {
        submit(q, 0, packet(), now);
        now += 100'000'000;
        REQUIRE_FALSE(q.underrun(now).empty());
        CHECK(q.target() == std::min(AudioQueue::kBase + size_t(i + 1) * 480, AudioQueue::kMaximum));
    }
    CHECK(q.underruns() == 12);
    size_t queued = submit(q, 0, packet(), now);
    CHECK(q.plan(queued - 318, 318, now + 9'999'999'999).shorten == 0);
    CHECK(q.target() == AudioQueue::kMaximum);
    const auto first = q.plan(queued - 318, 318, now + 10'000'000'000);
    CHECK(q.target() == AudioQueue::kMaximum - 48);
    CHECK(first.shorten == 1);
    auto samples = packet();
    queued = queued - 318 + q.prepare(samples, first.shorten);
    size_t shortened = first.shorten;
    for (uint64_t t = now + 10'000'000'000 + kTick; t < now + 45'000'000'000; t += kTick) {
        const auto p = q.plan(queued - 318, 318, t);
        REQUIRE(p.skip == 0);
        REQUIRE(p.silence == 0);
        REQUIRE(p.shorten <= 1);
        shortened += p.shorten;
        samples = packet();
        queued = queued - 318 + q.prepare(samples, p.shorten);
    }
    CHECK(q.target() == AudioQueue::kBase);
    CHECK(queued == AudioQueue::kBase);
    CHECK(shortened == AudioQueue::kMaximum - AudioQueue::kBase);
}

TEST_CASE("audio queue: latency recovery preserves endpoints and interpolates one frame smoothly") {
    AudioQueue q;
    auto warmup = packet();
    q.prepare(warmup); // complete the startup fade first
    std::vector<int16_t> samples(640);
    for (size_t i = 0; i < 320; ++i) {
        samples[2 * i] = int16_t(i * 50);
        samples[2 * i + 1] = -samples[2 * i];
    }
    CHECK(q.prepare(samples, 1) == 319);
    CHECK(samples[0] == 0);
    CHECK(samples[636] == 15950);
    for (size_t i = 1; i < 319; ++i) {
        CHECK(samples[2 * i] - samples[2 * i - 2] >= 50);
        CHECK(samples[2 * i] - samples[2 * i - 2] <= 51);
        CHECK(samples[2 * i + 1] == -samples[2 * i]);
    }
}

TEST_CASE("audio queue: backlog trim drains to target and handles an oversized incoming batch") {
    AudioQueue q;
    size_t queued = submit(q, 0, packet(), 0);
    CHECK(queued == AudioQueue::kBase);
    auto p = q.plan(AudioQueue::kHighWater + 1000, 318, kTick);
    CHECK(p.skip == 318);
    // Hysteresis continues below high water, down to the target including the new packet.
    p = q.plan(AudioQueue::kBase + 50, 318, 2 * kTick);
    CHECK(p.skip == 318);
    p = q.plan(AudioQueue::kBase - 100, 318, 3 * kTick);
    CHECK(p.skip == 218);
    p = q.plan(AudioQueue::kBase - 318, 318, 4 * kTick);
    CHECK(p.skip == 0);
    CHECK(p.silence == 0);
    p = q.plan(0, AudioQueue::kHighWater * 2, 5 * kTick);
    CHECK(p.skip == AudioQueue::kHighWater * 2 - AudioQueue::kBase);
    q.reset();
    CHECK(q.target() == AudioQueue::kBase);
    CHECK(q.underruns() == 0);
    CHECK(q.underrun(0).empty());
    CHECK(submit(q, 0, packet(), 0) == AudioQueue::kBase);
}

TEST_CASE("audio queue: fast-forward grains and speed changes retain PCM and stay buffered") {
    sawgod::app::FastForwardAudio ff;
    AudioQueue q;
    size_t queued = submit(q, 0, packet(), 0);
    uint64_t tick = 0;
    for (int speed : {1, 2, 4, 8, 4, 1})
        for (int i = 0; i < 2000; ++i) {
            REQUIRE(queued >= 318);
            queued -= 318;
            auto samples = ff.process(packet(318 * size_t(speed)), speed);
            const auto original = samples;
            const auto p = q.plan(queued, samples.size() / 2, ++tick * kTick);
            REQUIRE(p.silence == 0);
            const auto accepted = std::span(samples).subspan(p.skip * 2);
            queued += q.prepare(accepted, p.shorten);
            REQUIRE(samples == original);
            REQUIRE(queued <= AudioQueue::kHighWater);
        }
    CHECK(q.underruns() == 0);
}

TEST_CASE("audio queue: adaptive recovery drains Fast-Forward latency across grain gaps") {
    for (int speed : {2, 4, 8}) {
        CAPTURE(speed);
        AudioQueue q;
        auto warmup = packet();
        for (int i = 0; i < 3; ++i) {
            q.prepare(warmup);
            q.underrun(0);
        }
        REQUIRE(q.target() == AudioQueue::kMaximum);
        sawgod::app::FastForwardAudio ff;
        size_t queued = 0, peak = 0;
        for (uint64_t tick = 0; tick < 4000; ++tick) {
            if (tick) {
                REQUIRE(queued >= 318);
                queued -= 318;
            }
            auto samples = ff.process(packet(318 * size_t(speed)), speed);
            queued = submit(q, queued, samples, tick * kTick);
            if (tick > 3500)
                peak = std::max(peak, queued);
        }
        CHECK(q.target() == AudioQueue::kBase);
        // Back at the base, give or take where the last priming fell in a 60 ms grain's arrival.
        CHECK(peak <= AudioQueue::kBase + sawgod::app::FastForwardAudio::kGrain / 3);
        CHECK(q.trimmed() == 0); // recovered by interpolation, never by discarding sound
        CHECK(q.underruns() == 3);
    }
}

TEST_CASE("audio queue SDL: pulls append a complete tail, resume with silence, and clear safely") {
    SDL_AudioStream *s = stream();
    REQUIRE(s != nullptr);
    AudioOutput output(s);
    auto samples = packet();
    output.submit(samples, SDL_GetTicksNS());
    CHECK(output.stats().queued == AudioQueue::kBase);
    std::vector<int16_t> rendered(AudioQueue::kBase * 2);
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), int(rendered.size() * 2)) == int(rendered.size() * 2));
    CHECK(rendered.back() == -6000);
    // SDL's stream work buffer and queue pools are now warm. The callback reuses them.
    const int allocations = SDL_GetNumAllocations();
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), int(rendered.size() * 2)) ==
            int(AudioQueue::kFade * 4));
    CHECK(SDL_GetNumAllocations() == allocations);
    CHECK(rendered[0] == 11900);
    CHECK(rendered[1] == -5950);
    CHECK(rendered[AudioQueue::kFade * 2 - 1] == 0);
    CHECK(output.stats().underruns == 1);
    CHECK(SDL_GetAudioStreamData(s, rendered.data(), int(rendered.size() * 2)) == 0);
    CHECK(output.stats().underruns == 1);
    samples = packet();
    output.submit(samples, SDL_GetTicksNS());
    constexpr size_t raised = AudioQueue::kBase + AudioQueue::kRate * 20 / 1000; // one dry-out: +20 ms
    CHECK(output.stats().queued == raised);
    rendered.resize(raised * 2);
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), int(rendered.size() * 2)) == int(rendered.size() * 2));
    const size_t silence = raised - 318;
    CHECK(std::all_of(rendered.begin(), rendered.begin() + ptrdiff_t(silence * 2),
                      [](int16_t v) { return v == 0; }));
    CHECK(rendered[silence * 2] == 0);
    CHECK(rendered[(silence + AudioQueue::kFade) * 2] == 12000);
    // Reset with accepted PCM still pending: retain it, then a tail, rather than cutting mid-wave.
    samples = packet();
    output.submit(samples, SDL_GetTicksNS());
    const auto before_clear = output.stats().queued;
    output.clear();
    CHECK(output.stats().queued == before_clear + AudioQueue::kFade);
    rendered.resize((before_clear + AudioQueue::kFade) * 2);
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), int(rendered.size() * 2)) == int(rendered.size() * 2));
    CHECK(rendered[before_clear * 2 - 2] == 12000);
    CHECK(rendered[before_clear * 2] == 11900);
    CHECK(rendered.back() == 0);
    CHECK(output.stats().target == AudioQueue::kBase);
    CHECK(output.stats().underruns == 0);
    CHECK(SDL_GetAudioStreamData(s, rendered.data(), int(rendered.size() * 2)) == 0);
    CHECK(output.stats().underruns == 0);
}

TEST_CASE("audio queue SDL: thirty accelerated minutes at 48 kHz have no underruns or queue drift") {
    SDL_AudioStream *s = stream(48000);
    REQUIRE(s != nullptr);
    AudioOutput output(s);
    constexpr uint64_t duration = 1800'000'000'000;
    constexpr uint64_t device_frames = 512;
    uint64_t tick = 0, pull = 1;
    std::array<int16_t, device_frames * 2> rendered{};
    auto source = packet();
    size_t minimum = std::numeric_limits<size_t>::max(), maximum = 0;
    double first_sum = 0, last_sum = 0;
    size_t first_count = 0, last_count = 0;
    while (tick * kTick < duration) {
        const uint64_t tick_time = tick * kTick, pull_time = pull * device_frames * 1'000'000'000 / 48000;
        if (pull_time <= tick_time) {
            const int got = SDL_GetAudioStreamData(s, rendered.data(), int(sizeof(rendered)));
            REQUIRE(got == int(sizeof(rendered)));
            ++pull;
        } else {
            auto samples = source;
            output.submit(samples, tick_time);
            const auto state = output.stats();
            REQUIRE(state.underruns == 0);
            REQUIRE(state.target == AudioQueue::kBase);
            minimum = std::min(minimum, state.queued);
            maximum = std::max(maximum, state.queued);
            if (tick_time < 60'000'000'000) {
                first_sum += double(state.queued);
                ++first_count;
            }
            if (tick_time >= duration - 60'000'000'000) {
                last_sum += double(state.queued);
                ++last_count;
            }
            ++tick;
        }
    }
    const double first_ms = first_sum / double(first_count) / 24;
    const double last_ms = last_sum / double(last_count) / 24;
    MESSAGE("30-minute SDL stream: post-submit min/max ms = ", double(minimum) / 24, "/",
            double(maximum) / 24, ", first/last minute mean ms = ", first_ms, "/", last_ms,
            ", drift ms = ", last_ms - first_ms);
    CHECK(minimum >= AudioQueue::kBase - 16); // resampler's retained input history
    CHECK(maximum <= AudioQueue::kBase + device_frames / 2 + 16);
    CHECK(std::abs(last_ms - first_ms) < 0.1);
}

TEST_CASE("audio queue SDL: a partial shortage fades after queued PCM and small pulls finish the tail") {
    SDL_AudioStream *s = stream();
    REQUIRE(s != nullptr);
    AudioOutput output(s);
    auto samples = packet();
    output.submit(samples, SDL_GetTicksNS());
    std::vector<int16_t> rendered((AudioQueue::kBase - 20) * 2);
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), int(rendered.size() * 2)) == int(rendered.size() * 2));
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), 60 * 4) == 60 * 4);
    CHECK(rendered[38] == 12000);
    CHECK(rendered[39] == -6000);
    CHECK(rendered[40] == 11900);
    CHECK(rendered[41] == -5950);
    CHECK(output.stats().underruns == 1);
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), 40 * 4) == 40 * 4);
    CHECK(rendered[0] == 7900);
    REQUIRE(SDL_GetAudioStreamData(s, rendered.data(), 40 * 4) == 40 * 4);
    CHECK(rendered[78] == 0);
    CHECK(rendered[79] == 0);
    CHECK(SDL_GetAudioStreamData(s, rendered.data(), 40 * 4) == 0);
    CHECK(output.stats().underruns == 1);
}

TEST_CASE("audio queue: the first sound fades in even after silent startup packets") {
    AudioQueue q;
    std::vector<int16_t> silence(636);
    for (int i = 0; i < 20; ++i)
        q.prepare(silence);
    auto sound = packet();
    q.prepare(sound);
    CHECK(sound[0] == 0);
    CHECK(sound[1] == 0);
    CHECK(sound[2] == 100);
    CHECK(sound[2 * AudioQueue::kFade] == 12000);
}

TEST_CASE("audio queue: whole discarded packets preserve the endpoint for a stereo trim crossfade") {
    AudioQueue q;
    submit(q, 0, packet(), 0);
    auto p = q.plan(AudioQueue::kHighWater + 1000, 318, kTick);
    REQUIRE(p.skip == 318);
    auto discarded = packet();
    CHECK(q.prepare(std::span(discarded).subspan(p.skip * 2)) == 0);
    auto next = packet();
    for (size_t i = 0; i < next.size(); i += 2) {
        next[i] = -12000;
        next[i + 1] = 6000;
    }
    p = q.plan(AudioQueue::kBase - 300, 318, 2 * kTick);
    REQUIRE(p.skip == 18);
    auto accepted = std::span(next).subspan(p.skip * 2);
    CHECK(q.prepare(accepted) == 300);
    CHECK(accepted[0] == 12000);
    CHECK(accepted[1] == -6000);
    CHECK(accepted[2 * AudioQueue::kFade] == -12000);
    CHECK(accepted[2 * AudioQueue::kFade + 1] == 6000);
    for (size_t i = 1; i < 300; ++i) {
        CHECK(std::abs(int(accepted[2 * i]) - accepted[2 * i - 2]) <= 200);
        CHECK(std::abs(int(accepted[2 * i + 1]) - accepted[2 * i - 1]) <= 100);
    }
    CHECK(q.trimmed() == 336);
}
