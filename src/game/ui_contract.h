// Describes the actions and navigation obligations of host UI screens.
#pragma once
#include <string_view>
namespace sawgod::game {
// Screens declare only meaningful obligations. Noninteractive elements use render/reset;
// wrap belongs to the cursor, rather than pretending an individual row can wrap.
struct UiElement {
    std::string_view id;
    std::string_view obligations; // space-separated, consumed by the pilot coverage gate
};
} // namespace sawgod::game
