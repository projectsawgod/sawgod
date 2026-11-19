#pragma once

#include "chrome_fixture.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include <algorithm>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>
using namespace sawgod::game;
using sawgod::emu::Wsc;
namespace fx = chrome_fixture;

namespace {
inline uint32_t at(const std::vector<uint32_t> &px, int x, int y) { return px[size_t(y * Wsc::kWidth + x)]; }
} // namespace

#include "game/machine.h"

// The GB picture's item symbols come from the Game Boy game itself: its item-name table spells the Heart, the
// "resists" abilities and the "weak to" abilities with a symbol character first, and nothing else has one.

#include "ui/render_audit.h"
