#include "app/audio_review.h"
#include <doctest/doctest.h>

using namespace sawgod;

TEST_CASE("audio review: seconds use the native frame clock and steps retain command order") {
    app::AudioReviewOptions options;
    REQUIRE(app::audio_review_option(options, "--audio-review-song", "151").empty());
    REQUIRE(app::audio_review_option(options, "--audio-review-seconds", "70").empty());
    REQUIRE(app::audio_review_option(options, "--audio-review-start", "gb").empty());
    REQUIRE(app::audio_review_option(options, "--audio-review-step", "302:+1").empty());
    REQUIRE(app::audio_review_option(options, "--audio-review-step", "604:-1").empty());
    REQUIRE(app::audio_review_option(options, "--audio-review-output", "151_steps_test.wav").empty());
    CHECK(options.song == 151);
    CHECK(options.frames == 5283);
    CHECK(options.start == FidelityMode::Gb);
    CHECK(options.steps == std::vector<std::pair<int, int>>{{302, 1}, {604, -1}});
    CHECK(options.output == "151_steps_test.wav");
    REQUIRE(app::audio_review_option(options, "--audio-review-seconds", "90").empty());
    CHECK(options.frames == 6792);
    REQUIRE(app::audio_review_option(options, "--audio-review-frames", "1510").empty());
    CHECK(options.frames == 1510);
}

TEST_CASE("audio review: malformed options fail instead of silently changing a capture") {
    app::AudioReviewOptions options;
    for (const char *value : {"-1", "256", "151junk", "999999999999999999999"})
        CHECK_FALSE(app::audio_review_option(options, "--audio-review-song", value).empty());
    for (const char *value : {"0", "-1", "1.5", "4000000"})
        CHECK_FALSE(app::audio_review_option(options, "--audio-review-frames", value).empty());
    for (const char *value : {"0", "-1", "nan", "inf", "1e100", "70seconds"})
        CHECK_FALSE(app::audio_review_option(options, "--audio-review-seconds", value).empty());
    for (const char *value : {"-1:+1", "302:1", "302:+2", "302:-1junk", "302"})
        CHECK_FALSE(app::audio_review_option(options, "--audio-review-step", value).empty());
    for (const char *value :
         {"", "../capture.wav", "/capture.wav", "dir/capture.wav", "dir\\capture.wav", "capture"})
        CHECK_FALSE(app::audio_review_option(options, "--audio-review-output", value).empty());
    CHECK_FALSE(app::audio_review_option(options, "--audio-review-start", "swap").empty());
    CHECK_FALSE(app::audio_review_option(options, "--audio-review-typo", "gb").empty());
}
