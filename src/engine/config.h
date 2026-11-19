// Named settings ("CVars"): typed values with defaults, a menu label, a one-line description and a
// category, persisted as a small TOML file (docs/CONFIG.md). Pure C++, no SDL: the front end registers its
// settings (engine/settings.h), loads the file at start-up and saves it when something changed.
//
// Defaults come from the registry: game mechanics retain the WSC behavior while host presentation can
// use its own defaults. Values are ints underneath: a bool is 0/1, an enum the index of one of its choices
// (stored in the file by name, so reordering choices keeps a saved file's picks).
#pragma once

#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sawgod {

// The options overlay's help line holds exactly one line of the game's 8x8 font: 46 characters (the 384-pixel
// overlay canvas is 48 characters wide, less the window frame). Every description must fit;
// Config::overlong_help() lists the ones that don't (the unit tests fail on any).
inline constexpr size_t kHelpWidth = 46;

struct CVar {
    enum class Type { Bool, Int, Enum };

    std::string name;     // "video.scale": section "video", key "scale" in the TOML file
    std::string category; // menu page, e.g. "Video"
    // settings.cpp holds only text the game shows: hidden settings have no label or help, and an enum
    // whose choices all have choice_help has no general help.
    std::string label; // menu row label
    std::string help;  // one-line description (the overlay's help line, the TOML comment)
    Type type = Type::Bool;
    int def = 0;
    int min = 0, max = 1;
    int step = 1;                           // Int: menu increment
    std::string suffix;                     // Int: shown after the number in the menu ("x", "%")
    bool bar_reverse = false;               // Int bar: right decreases the stored value (e.g. text delay)
    bool bar = false;                       // Int: a bar in the menu even with few steps (like the volumes)
    int decimals = 0;                       // Int: shown as value / 10^decimals (125 -> "1.25")
    std::string bar_low, bar_high;          // Int bar: words at its ends ("Fast", "Slow"); then it has a cell
                                            // per value, the first lit at the minimum
    std::vector<std::string> choices;       // Enum: names as stored in the file
    std::vector<std::string> choice_labels; // Enum: names as shown in the menu
    std::vector<std::string> choice_help;   // Enum: optional per-choice description (may be empty)
    // Sparse integer choices in display order; labels reuse choice_labels. Ties keep the first value.
    std::vector<int> values;
    std::vector<int> choice_order; // Enum: display order only; stored IDs and indices stay unchanged
    int nonzero_value = 0;         // Int toggle: any nonzero input maps to this fixed On strength
    int normalize(long value) const;
    bool hidden = false; // kept in the file but not listed in the menu (e.g. window size)

    int get() const { return session ? *session : value; }
    bool on() const { return get() != 0; }
    // Text for the current value as the menu shows it.
    std::string display() const;

  private:
    friend class Config;
    int value = 0;
    std::optional<int> session; // command-line override for this run only: shown and used, never saved
};

class Config {
  public:
    CVar &add_bool(const std::string &name, const std::string &category, const std::string &label,
                   const std::string &help, bool def);
    CVar &add_int(const std::string &name, const std::string &category, const std::string &label,
                  const std::string &help, int def, int min, int max, int step = 1,
                  const std::string &suffix = "");
    CVar &add_enum(const std::string &name, const std::string &category, const std::string &label,
                   const std::string &help, std::vector<std::string> choices, std::vector<std::string> labels,
                   int def);

    CVar *find(const std::string &name);
    const CVar *find(const std::string &name) const;
    std::deque<CVar> &vars() { return vars_; }
    const std::deque<CVar> &vars() const { return vars_; }
    // Categories in registration order.
    std::vector<std::string> categories() const;

    // A user change (menu): clamps, drops any command-line override for it, marks the config dirty.
    void set(CVar &v, int value);
    // A command-line override: used for this run, not written back (the file keeps its own value).
    void override_session(CVar &v, int value);
    void reset_to_defaults(); // every setting the menu shows back to its default (overrides dropped), dirty
    // "name: text" for every description (help, per-choice help) longer than kHelpWidth.
    std::vector<std::string> overlong_help() const;
    // Keys that are not settings, as raw TOML value text: kept from the file and written back. Front-end
    // tables that are not CVars (the [keyboard] / [gamepad] bindings, engine/input.h) live here too.
    std::optional<std::string> raw(const std::string &name) const;
    void set_raw(const std::string &name, const std::string &value); // marks dirty if it changed
    // File-only string settings (paths), using the same quoting as enum values.
    std::string string_value(const std::string &name, const std::string &fallback) const;
    void set_string(const std::string &name, const std::string &value);
    void erase_raw(const std::string &name); // marks dirty if it was there
    bool loaded(const std::string &name) const { return loaded_.count(name) != 0; }
    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // TOML text <-> values. Unknown keys are kept and written back unchanged (a newer build's settings
    // survive an older build saving). Bad values are reported in *warnings and leave the default.
    std::string to_toml() const;
    void from_toml(const std::string &text, std::vector<std::string> *warnings = nullptr);

    // File I/O; save writes a temporary file and renames it over the old one.
    bool load(const std::string &path, std::vector<std::string> *warnings = nullptr);
    bool save(const std::string &path, std::string *error = nullptr);

  private:
    CVar &add(CVar v);
    std::deque<CVar> vars_;                      // deque: references stay valid as settings are added
    std::map<std::string, std::string> unknown_; // "section.key" -> raw TOML value text
    std::set<std::string> loaded_;
    bool dirty_ = false;
};

} // namespace sawgod
