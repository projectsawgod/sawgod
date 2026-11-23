#include "vanilla/input.h"
#include <doctest/doctest.h>
#include <fstream>

namespace {
struct MappingFile {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("sawgod-vanilla-input-" + std::to_string(SDL_GetPerformanceCounter()) + ".txt");
    ~MappingFile() { std::filesystem::remove(path); }
    void write(const std::string &text) const {
        std::ofstream file(path);
        file << text;
        REQUIRE(file.good());
    }
};
} // namespace

TEST_CASE("vanilla input template preserves and reproduces all built-in WSC bindings") {
    using namespace sawgod;
    const auto defaults = ui::InputMap::defaults();
    vanilla::Input input;
    REQUIRE_NOTHROW(input.load(SAWGOD_VANILLA_INPUT_TEMPLATE));
    CHECK(input.mapping() == defaults);

    MappingFile file;
    file.write("key a Space\nbutton x1 west\n");
    input.load(file.path);
    const auto custom = input.mapping();
    input.load(SAWGOD_VANILLA_INPUT_TEMPLATE);
    CHECK(input.mapping() == custom);

    // Clear every WSC binding first so missing template entries cannot inherit a passing default.
    std::string cleared;
    for (int i = 0; i <= int(ui::WscButton::Start); ++i) {
        const auto button = std::string(ui::wsc_button_id(ui::WscButton(i)));
        cleared += "key " + button + " none\nbutton " + button + " none\n";
    }
    file.write(cleared);
    input.load(file.path);
    std::ifstream source(SAWGOD_VANILLA_INPUT_TEMPLATE);
    REQUIRE(source.good());
    std::string uncommented, line;
    int entries = 0;
    while (std::getline(source, line)) {
        if (line.starts_with("# key ") || line.starts_with("# button ") || line.starts_with("# axis ")) {
            uncommented += line.substr(2) + "\n";
            ++entries;
        }
    }
    CHECK(entries == 2 * (int(ui::WscButton::Start) + 1));
    file.write(uncommented);
    REQUIRE_NOTHROW(input.load(file.path));
    CHECK(input.mapping() == defaults);
}

TEST_CASE("vanilla input accepts replacements and rejects malformed second bindings") {
    using namespace sawgod;
    vanilla::Input input;
    MappingFile file;
    file.write("button x1 dpup axis lefty- # two gamepad bindings\nkey a Space\nbutton start none\n");
    REQUIRE_NOTHROW(input.load(file.path));
    CHECK(input.mapping().pad[0] == ui::InputMap::defaults().pad[0]);
    CHECK(input.mapping().keys[int(ui::WscButton::A)][0] ==
          ui::Binding{ui::Binding::Kind::Key, SDL_SCANCODE_SPACE});
    CHECK(input.mapping().keys[int(ui::WscButton::A)][1].empty());
    CHECK(input.mapping().pad[int(ui::WscButton::Start)] == ui::BindSlots{});
    file.write("key a X key Space\n");
    REQUIRE_NOTHROW(input.load(file.path));
    CHECK(input.mapping().keys[int(ui::WscButton::A)][1] ==
          ui::Binding{ui::Binding::Kind::Key, SDL_SCANCODE_SPACE});
    for (const auto *bad : {"button x1 dpup axis", "button x1 dpup key W", "key a X axis lefty-",
                            "button x1 dpup axis bogus-", "key a X key Space extra", "key a Escape",
                            "key a F11", "key a none key X", "button x1 dpup axis none"}) {
        CAPTURE(bad);
        file.write(std::string("# comment\n") + bad + "\n");
        CHECK_THROWS_WITH(input.load(file.path), "Invalid input mapping at line 2");
    }
}
