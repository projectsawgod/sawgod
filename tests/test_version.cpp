#include <cstring>

#include <doctest/doctest.h>

#include "engine/version.h"

TEST_CASE("version string is non-empty") {
    CHECK(sawgod::kVersionString != nullptr);
    CHECK(std::strlen(sawgod::kVersionString) > 0);
}

TEST_CASE("native resolution matches the WonderSwan Color screen") {
    CHECK(sawgod::kNativeWidth == 224);
    CHECK(sawgod::kNativeHeight == 144);
}
