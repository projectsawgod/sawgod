#include "engine/button_icons.h"

#include "engine/indexed_png.h"

#include <cctype>
#include <cstdio>
#include <map>

namespace sawgod {

namespace {

const unsigned char kAtlasPng[] = {
#include "button_icons_png.inc"
};
constexpr const char *kIndexCsv =
#include "button_icons_csv.inc"
    ;

// One CSV field: plain or "quoted" ("" inside quotes is a quote).
std::vector<std::string> csv_fields(std::string_view line) {
    std::vector<std::string> out(1);
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted && c == '"' && i + 1 < line.size() && line[i + 1] == '"')
            out.back() += '"', ++i;
        else if (c == '"')
            quoted = !quoted;
        else if (c == ',' && !quoted)
            out.emplace_back();
        else
            out.back() += c;
    }
    return out;
}

// Clear pixels the ink encloses (not reachable from the picture's edge) become Symbol.
void mark_symbol(ButtonIcon &icon) {
    std::vector<uint8_t> outside(icon.px.size(), 0);
    std::vector<std::pair<int, int>> todo;
    for (int x = 0; x < icon.w; ++x)
        todo.insert(todo.end(), {{x, 0}, {x, icon.h - 1}});
    for (int y = 0; y < icon.h; ++y)
        todo.insert(todo.end(), {{0, y}, {icon.w - 1, y}});
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        if (x < 0 || y < 0 || x >= icon.w || y >= icon.h)
            continue;
        const size_t i = size_t(y) * size_t(icon.w) + size_t(x);
        if (outside[i] || icon.px[i] != ButtonIcon::Clear)
            continue;
        outside[i] = 1;
        todo.insert(todo.end(), {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}});
    }
    for (size_t i = 0; i < icon.px.size(); ++i)
        if (icon.px[i] == ButtonIcon::Clear && !outside[i])
            icon.px[i] = ButtonIcon::Symbol;
}

// The Icon Types' buttons: Xbox near black with the face letters in Xbox's colors,
// Switch dark gray with light letters (a shade lighter than Xbox, so switching between them shows), PS light
// with the face shapes in PlayStation's colors. Under a dark button the shadow is a gray lighter than the
// button. (Neither Microsoft nor Sony publishes RGB values: close approximations.)
void style_pad_icon(ButtonIcon &icon, char family, std::string_view name) {
    static constexpr std::pair<const char *, uint32_t> kFace[] = {
        {"pad A", 0x5DC21E},    {"pad B", 0xE84040},     {"pad X", 0x2A7BDE},     {"pad Y", 0xF5C518},
        {"PS cross", 0x7B9FE0}, {"PS circle", 0xF25C54}, {"PS square", 0xE58FCF}, {"PS triangle", 0x3DB39E}};
    constexpr uint32_t kLightSymbol = 0xDDDDDD;
    std::optional<uint32_t> face;
    for (const auto &[n, rgb] : kFace)
        if (name == n)
            face = rgb;
    icon.motion = 0xF0901E; // a stick's direction arc in orange, in every type
    if (family == 'p') {
        icon.symbol = face;
        return;
    }
    icon.body = family == 'x' ? 0x141418 : 0x2C2C34;
    icon.shadow = family == 'x' ? 0x3C3C46 : 0x54545F;
    icon.symbol = family == 'x' && face ? *face : kLightSymbol;
}

const std::map<std::string, ButtonIcon, std::less<>> &icons() {
    static const std::map<std::string, ButtonIcon, std::less<>> set = [] {
        std::map<std::string, ButtonIcon, std::less<>> out;
        IndexedImage atlas;
        std::string error;
        // white or transparent: clear; black: ink; grey: ink without a shadow
        if (!decode_indexed_png(std::vector<uint8_t>(std::begin(kAtlasPng), std::end(kAtlasPng)),
                                {0xFFFFFF, 0x000000, 0x808080}, &atlas, &error, ButtonIcon::Clear)) {
            std::fprintf(stderr, "button icons: %s\n", error.c_str());
            return out;
        }
        std::string_view csv = kIndexCsv;
        while (!csv.empty()) {
            const size_t end = csv.find('\n');
            std::string_view line = csv.substr(0, end);
            csv = end == std::string_view::npos ? std::string_view{} : csv.substr(end + 1);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (line.empty() || line[0] == '#' || line.rfind("name,", 0) == 0)
                continue;
            const std::vector<std::string> f = csv_fields(line);
            if (f.size() != 7)
                continue;
            ButtonIcon icon;
            const int x = std::atoi(f[1].c_str()), y = std::atoi(f[2].c_str());
            icon.w = std::atoi(f[3].c_str()), icon.h = std::atoi(f[4].c_str());
            icon.dx = std::atoi(f[5].c_str()), icon.dy = std::atoi(f[6].c_str());
            if (icon.w <= 0 || icon.h <= 0 || x < 0 || y < 0 || x + icon.w > atlas.width ||
                y + icon.h > atlas.height)
                continue;
            for (int r = 0; r < icon.h; ++r)
                for (int c = 0; c < icon.w; ++c)
                    icon.px.push_back(atlas.pixels[size_t(y + r) * size_t(atlas.width) + size_t(x + c)]);
            if (f[0].rfind("char ", 0) != 0) // (the lettering stays plain: keycaps cut it out of the key)
                mark_symbol(icon);
            out[f[0]] = std::move(icon);
        }
        return out;
    }();
    return set;
}

std::function<ButtonIcon(int)> &token_source() {
    static std::function<ButtonIcon(int)> source;
    return source;
}

} // namespace

const ButtonIcon *button_icon(std::string_view name) {
    const auto &set = icons();
    const auto it = set.find(name);
    return it == set.end() ? nullptr : &it->second;
}

ButtonIcon keycap_icon(std::string_view label) {
    // as the set's own keys: 9 rows, 1-pixel corners, the label at rows 2-6 with two columns of key each side
    std::vector<const ButtonIcon *> glyphs;
    for (char c : label) {
        if (c == ' ')
            continue;
        const ButtonIcon *g =
            button_icon(std::string("char ") + char(std::toupper(static_cast<unsigned char>(c))));
        if (!g)
            return {};
        glyphs.push_back(g);
    }
    if (glyphs.empty())
        return {};
    ButtonIcon key;
    key.w = 4 * int(glyphs.size()) - 1 + 4;
    key.h = kButtonIconHeight;
    key.px.assign(size_t(key.w * key.h), ButtonIcon::Ink);
    for (int x : {0, key.w - 1})
        for (int y : {0, key.h - 1})
            key.px[size_t(y * key.w + x)] = ButtonIcon::Clear;
    int pen = 2;
    for (const ButtonIcon *g : glyphs) {
        for (int r = 0; r < g->h; ++r)
            for (int c = 0; c < g->w; ++c)
                if (g->at(c, r) != ButtonIcon::Clear)
                    key.px[size_t((2 + r) * key.w + pen + c)] = ButtonIcon::Clear;
        pen += 4;
    }
    return key;
}

const char *button_token_name(int i) {
    static constexpr const char *kNames[kButtonTokenY + 1] = {"X1", "X2", "X3", "X4",    "Y1", "Y2", "Y3",
                                                              "Y4", "A",  "B",  "Start", "X",  "Y"};
    return i >= 0 && i <= kButtonTokenY ? kNames[i] : "";
}

ButtonIcon named_icon(std::string_view name) {
    if (name.rfind("key:", 0) == 0)
        return keycap_icon(name.substr(4));
    char family = 0;
    if (name.size() > 2 && name[1] == ':' && (name[0] == 'n' || name[0] == 'x' || name[0] == 'p')) {
        family = name[0];
        name.remove_prefix(2);
    }
    const ButtonIcon *icon = button_icon(name);
    if (!icon)
        return {};
    ButtonIcon out = *icon;
    if (family)
        style_pad_icon(out, family, name);
    return out;
}

ButtonIcon text_token_icon(const TextToken &token) {
    return token.button >= 0 ? button_token_icon(token.button) : named_icon(token.icon);
}

std::string text_token_text(const TextToken &token) {
    if (token.button >= 0)
        return button_token_name(token.button);
    if (token.icon.rfind("key:", 0) == 0)
        return token.icon.substr(4);
    return token.icon.size() > 2 && token.icon[1] == ':' ? token.icon.substr(2) : token.icon; // "x:pad A"
}

void set_button_token_icons(std::function<ButtonIcon(int token)> source) {
    token_source() = std::move(source);
}

ButtonIcon button_token_icon(int token) {
    return token_source() && token >= 0 && token <= kButtonTokenY ? token_source()(token) : ButtonIcon{};
}

ButtonIcon pad_group_icon(bool y, bool keyboard) {
    if (y)
        return keyboard ? keycap_icon("WASD") : named_icon("R stick");
    ButtonIcon cross; // a d-pad: arms three pixels wide, its hub clear
    cross.w = cross.h = kButtonIconHeight;
    cross.px.assign(size_t(cross.w * cross.h), ButtonIcon::Clear);
    for (int r = 0; r < cross.h; ++r)
        for (int c = 0; c < cross.w; ++c)
            if ((c >= 3 && c <= 5) || (r >= 3 && r <= 5))
                cross.px[size_t(r * cross.w + c)] = r == 4 && c == 4 ? ButtonIcon::Symbol : ButtonIcon::Ink;
    return cross;
}

} // namespace sawgod
