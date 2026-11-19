#include "engine/config.h"

#include "engine/persist.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sawgod {

namespace {

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Strips a trailing `# comment` that isn't inside a "string".
std::string strip_comment(const std::string &s) {
    bool in_str = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (in_str && s[i] == '\\') {
            ++i;
        } else if (s[i] == '"') {
            in_str = !in_str;
        } else if (!in_str && s[i] == '#') {
            return s.substr(0, i);
        }
    }
    return s;
}

std::string quote(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + '"';
}

// A TOML basic string, or nullopt if `v` isn't one.
std::optional<std::string> unquote(const std::string &v) {
    if (v.size() < 2 || v.front() != '"' || v.back() != '"')
        return std::nullopt;
    std::string out;
    for (size_t i = 1; i + 1 < v.size(); ++i) {
        if (v[i] == '\\' && i + 2 < v.size())
            ++i;
        out += v[i];
    }
    return out;
}

std::optional<long> parse_int(const std::string &v) {
    if (v.empty())
        return std::nullopt;
    std::string digits;
    for (char c : v)
        if (c != '_')
            digits += c;
    char *end = nullptr;
    long n = std::strtol(digits.c_str(), &end, 10);
    if (end && *end == '.') { // a float: round it
        double d = std::strtod(digits.c_str(), &end);
        n = long(d < 0 ? d - 0.5 : d + 0.5);
    }
    if (!end || *end != 0)
        return std::nullopt;
    return n;
}

std::pair<std::string, std::string> split_name(const std::string &name) {
    size_t dot = name.find('.');
    if (dot == std::string::npos)
        return {"", name};
    return {name.substr(0, dot), name.substr(dot + 1)};
}

} // namespace

int CVar::normalize(long n) const {
    if (nonzero_value)
        return n ? nonzero_value : 0;
    n = std::clamp<long>(n, min, max);
    if (!values.empty())
        return *std::min_element(values.begin(), values.end(),
                                 [n](int a, int b) { return std::abs(n - a) < std::abs(n - b); });
    return int(n);
}

std::string CVar::display() const {
    if (!values.empty())
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] == get() && i < choice_labels.size())
                return choice_labels[i];
    switch (type) {
    case Type::Bool:
        return on() ? "ON" : "OFF";
    case Type::Int: {
        if (decimals <= 0)
            return std::to_string(get()) + suffix;
        int div = 1;
        for (int i = 0; i < decimals; ++i)
            div *= 10;
        std::string frac = std::to_string(std::abs(get()) % div);
        frac.insert(0, size_t(decimals) - frac.size(), '0');
        return (get() < 0 ? "-" : "") + std::to_string(std::abs(get()) / div) + "." + frac + suffix;
    }
    case Type::Enum: {
        int i = get();
        if (i >= 0 && size_t(i) < choice_labels.size())
            return choice_labels[i];
        return i >= 0 && size_t(i) < choices.size() ? choices[i] : "?";
    }
    }
    return {};
}

CVar &Config::add(CVar v) {
    v.value = v.def;
    vars_.push_back(std::move(v));
    return vars_.back();
}

CVar &Config::add_bool(const std::string &name, const std::string &category, const std::string &label,
                       const std::string &help, bool def) {
    CVar v;
    v.name = name;
    v.category = category;
    v.label = label;
    v.help = help;
    v.type = CVar::Type::Bool;
    v.def = def ? 1 : 0;
    v.min = 0;
    v.max = 1;
    return add(std::move(v));
}

CVar &Config::add_int(const std::string &name, const std::string &category, const std::string &label,
                      const std::string &help, int def, int min, int max, int step,
                      const std::string &suffix) {
    CVar v;
    v.name = name;
    v.category = category;
    v.label = label;
    v.help = help;
    v.type = CVar::Type::Int;
    v.def = def;
    v.min = min;
    v.max = max;
    v.step = step;
    v.suffix = suffix;
    return add(std::move(v));
}

CVar &Config::add_enum(const std::string &name, const std::string &category, const std::string &label,
                       const std::string &help, std::vector<std::string> choices,
                       std::vector<std::string> labels, int def) {
    CVar v;
    v.name = name;
    v.category = category;
    v.label = label;
    v.help = help;
    v.type = CVar::Type::Enum;
    v.def = def;
    v.min = 0;
    v.max = int(choices.size()) - 1;
    v.choices = std::move(choices);
    v.choice_labels = std::move(labels);
    return add(std::move(v));
}

CVar *Config::find(const std::string &name) {
    for (CVar &v : vars_)
        if (v.name == name)
            return &v;
    return nullptr;
}

const CVar *Config::find(const std::string &name) const {
    for (const CVar &v : vars_)
        if (v.name == name)
            return &v;
    return nullptr;
}

std::vector<std::string> Config::categories() const {
    std::vector<std::string> out;
    for (const CVar &v : vars_)
        if (std::find(out.begin(), out.end(), v.category) == out.end())
            out.push_back(v.category);
    return out;
}

void Config::set(CVar &v, int value) {
    value = v.normalize(value);
    if (v.value != value || v.session)
        dirty_ = true;
    v.value = value;
    v.session.reset();
}

void Config::override_session(CVar &v, int value) { v.session = v.normalize(value); }

void Config::reset_to_defaults() {
    // Only what the menu shows: hidden start-up settings such as the renderer and
    // the last window size keep their values.
    for (CVar &v : vars_) {
        if (v.hidden)
            continue;
        v.value = v.def;
        v.session.reset();
    }
    dirty_ = true;
}

std::optional<std::string> Config::raw(const std::string &name) const {
    auto it = unknown_.find(name);
    if (it == unknown_.end())
        return std::nullopt;
    return it->second;
}

void Config::set_raw(const std::string &name, const std::string &value) {
    auto it = unknown_.find(name);
    if (it != unknown_.end() && it->second == value)
        return;
    unknown_[name] = value;
    dirty_ = true;
}

std::string Config::string_value(const std::string &name, const std::string &fallback) const {
    const auto value = raw(name);
    return value ? unquote(*value).value_or(fallback) : fallback;
}

void Config::set_string(const std::string &name, const std::string &value) { set_raw(name, quote(value)); }

void Config::erase_raw(const std::string &name) {
    if (unknown_.erase(name))
        dirty_ = true;
}

namespace {
// Characters a help line takes on screen: a {Action} tag draws as one controller glyph.
size_t help_width(const std::string &help) {
    size_t width = 0;
    for (size_t i = 0; i < help.size(); ++i, ++width)
        if (help[i] == '{')
            if (const size_t end = help.find('}', i); end != std::string::npos)
                i = end;
    return width;
}
} // namespace

std::vector<std::string> Config::overlong_help() const {
    std::vector<std::string> out;
    for (const CVar &v : vars_) {
        if (help_width(v.help) > kHelpWidth)
            out.push_back(v.name + ": " + v.help);
        for (const std::string &h : v.choice_help)
            if (help_width(h) > kHelpWidth)
                out.push_back(v.name + ": " + h);
    }
    return out;
}

std::string Config::to_toml() const {
    std::string out =
        "# SawGod settings (docs/CONFIG.md), written by the options menu. Values edited here are kept\n"
        "# (comments are rewritten). A missing key means its registered default.\n";
    std::vector<std::string> sections;
    for (const CVar &v : vars_) {
        std::string s = split_name(v.name).first;
        if (std::find(sections.begin(), sections.end(), s) == sections.end())
            sections.push_back(s);
    }
    for (const auto &[name, raw] : unknown_) {
        std::string s = split_name(name).first;
        if (std::find(sections.begin(), sections.end(), s) == sections.end())
            sections.push_back(s);
    }
    for (const std::string &section : sections) {
        out += "\n";
        if (!section.empty())
            out += "[" + section + "]\n";
        for (const CVar &v : vars_) {
            auto [s, key] = split_name(v.name);
            if (s != section)
                continue;
            // Hidden settings have no label, and enums whose choices all have help no general help.
            out += "#";
            if (!v.label.empty())
                out += " " + v.label + (v.help.empty() ? "" : ":");
            if (!v.help.empty())
                out += " " + v.help;
            std::string value;
            switch (v.type) {
            case CVar::Type::Bool:
                out += " (default " + std::string(v.def ? "true" : "false") + ")\n";
                value = v.value ? "true" : "false";
                break;
            case CVar::Type::Int:
                out += " (" + std::to_string(v.min) + "-" + std::to_string(v.max) + ", default " +
                       std::to_string(v.def) + ")\n";
                value = std::to_string(v.value);
                break;
            case CVar::Type::Enum: {
                out += " (";
                for (size_t i = 0; i < v.choices.size(); ++i)
                    out += (i ? " | " : "") + quote(v.choices[i]);
                out += "; default " + quote(v.choices[v.def]) + ")\n";
                value = quote(v.choices[v.value]);
                break;
            }
            }
            out += key + " = " + value + "\n";
        }
        for (const auto &[name, raw] : unknown_) {
            auto [s, key] = split_name(name);
            if (s == section)
                out += key + " = " + raw + "\n";
        }
    }
    return out;
}

void Config::from_toml(const std::string &text, std::vector<std::string> *warnings) {
    auto warn = [&](int line, const std::string &msg) {
        if (warnings)
            warnings->push_back("line " + std::to_string(line) + ": " + msg);
    };
    std::string section;
    int line_no = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        line = trim(strip_comment(line));
        if (line.empty())
            continue;
        if (line.front() == '[') {
            if (line.back() != ']' || line.size() < 3) {
                warn(line_no, "bad section header");
                continue;
            }
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn(line_no, "expected key = value");
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string raw = trim(line.substr(eq + 1));
        if (auto unq = unquote(key))
            key = *unq;
        std::string name = section.empty() ? key : section + "." + key;
        loaded_.insert(name);
        CVar *v = find(name);
        if (!v) {
            unknown_[name] = raw;
            continue;
        }
        switch (v->type) {
        case CVar::Type::Bool:
            if (raw == "true" || raw == "false")
                v->value = raw == "true";
            else
                warn(line_no, name + ": expected true or false");
            break;
        case CVar::Type::Int:
            if (auto n = parse_int(raw))
                v->value = v->normalize(*n);
            else
                warn(line_no, name + ": expected a number");
            break;
        case CVar::Type::Enum: {
            auto s = unquote(raw);
            auto it = s ? std::find(v->choices.begin(), v->choices.end(), *s) : v->choices.end();
            if (it != v->choices.end())
                v->value = int(it - v->choices.begin());
            else
                warn(line_no, name + ": unknown choice " + raw);
            break;
        }
        }
    }
}

bool Config::load(const std::string &path, std::vector<std::string> *warnings) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    std::fclose(f);
    from_toml(text, warnings);
    return true;
}

bool Config::save(const std::string &path, std::string *error) {
    const std::string text = to_toml();
    if (!WriteFileAtomic(path, text.data(), text.size(), error))
        return false;
    dirty_ = false;
    return true;
}

} // namespace sawgod
