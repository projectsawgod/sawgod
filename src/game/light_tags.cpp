// Loads light and water metadata and resolves their visible field effects.
#include "game/light_tags.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace sawgod::game {
namespace {
// Small JSON reader: object/array/string/number/bool/null, bounded nesting and strict trailing checks.
// Material metadata has no dependency on a particular ROM encoding or fixed-size fields.
struct Json {
    enum Kind { Object, Array, String, Number, Boolean, Null } kind = Null;
    std::map<std::string, Json> object;
    std::vector<Json> array;
    std::string string;
    double number = 0;
    const Json &get(const std::string &key) const {
        const auto it = object.find(key);
        if (kind != Object || it == object.end())
            throw std::runtime_error("missing " + key);
        return it->second;
    }
    double numeric() const {
        if (kind != Number || !std::isfinite(number))
            throw std::runtime_error("expected finite number");
        return number;
    }
    std::string text() const {
        if (kind != String)
            throw std::runtime_error("expected string");
        return string;
    }
};
class Reader {
    const std::string &s;
    size_t p = 0;
    void space() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
            ++p;
    }
    bool take(char c) {
        space();
        if (p < s.size() && s[p] == c) {
            ++p;
            return true;
        }
        return false;
    }
    void need(char c) {
        if (!take(c))
            throw std::runtime_error(std::string("expected ") + c);
    }
    std::string quoted() {
        need('"');
        std::string out;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 32)
                throw std::runtime_error("control character in string");
            if (c == '\\') {
                if (p == s.size())
                    break;
                c = s[p++];
                switch (c) {
                case '"':
                case '\\':
                case '/':
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case 'u': { // JSON Unicode escape; keep future metadata Unicode readable.
                    auto hex = [&]() {
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (p == s.size())
                                throw std::runtime_error("short Unicode escape");
                            const char h = s[p++];
                            const int v = h >= '0' && h <= '9'   ? h - '0'
                                          : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                          : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                                                 : -1;
                            if (v < 0)
                                throw std::runtime_error("bad Unicode escape");
                            code = code * 16 + unsigned(v);
                        }
                        return code;
                    };
                    unsigned code = hex();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (p + 2 > s.size() || s[p++] != '\\' || s[p++] != 'u')
                            throw std::runtime_error("missing low surrogate");
                        const unsigned lo = hex();
                        if (lo < 0xDC00 || lo > 0xDFFF)
                            throw std::runtime_error("bad low surrogate");
                        code = 0x10000 + (code - 0xD800) * 1024 + lo - 0xDC00;
                    } else if (code >= 0xDC00 && code <= 0xDFFF)
                        throw std::runtime_error("unpaired surrogate");
                    if (code < 128)
                        out += char(code);
                    else {
                        if (code >= 65536)
                            out += char(0xF0 | (code >> 18));
                        if (code >= 2048)
                            out += char((code >= 65536 ? 0x80 : 0xE0) | ((code >> 12) & 63));
                        out += char((code >= 2048 ? 0x80 : 0xC0) | ((code >> 6) & 63));
                        out += char(0x80 | (code & 63));
                    }
                    continue;
                }
                default:
                    throw std::runtime_error("bad string escape");
                }
            }
            out += c;
        }
        throw std::runtime_error("unterminated string");
    }
    Json value(int depth) {
        if (depth > 32)
            throw std::runtime_error("metadata nesting too deep");
        space();
        Json j;
        if (take('{')) {
            j.kind = Json::Object;
            if (!take('}'))
                do {
                    const auto k = quoted();
                    need(':');
                    if (!j.object.emplace(k, value(depth + 1)).second)
                        throw std::runtime_error("duplicate field " + k);
                    if (take('}'))
                        return j;
                    need(',');
                } while (true);
        } else if (take('[')) {
            j.kind = Json::Array;
            if (!take(']'))
                do {
                    j.array.push_back(value(depth + 1));
                    if (take(']'))
                        return j;
                    need(',');
                } while (true);
        } else if (p < s.size() && s[p] == '"') {
            j.kind = Json::String;
            j.string = quoted();
        } else if (s.compare(p, 4, "true") == 0) {
            p += 4;
            j.kind = Json::Boolean;
            j.number = 1;
        } else if (s.compare(p, 5, "false") == 0) {
            p += 5;
            j.kind = Json::Boolean;
        } else if (s.compare(p, 4, "null") == 0) {
            p += 4;
        } else {
            const size_t start = p;
            if (p < s.size() && s[p] == '-')
                ++p;
            if (p < s.size() && s[p] == '0')
                ++p;
            else {
                const size_t digits = p;
                while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                    ++p;
                if (p == digits)
                    throw std::runtime_error("expected JSON value");
            }
            if (p < s.size() && s[p] == '.') {
                ++p;
                const size_t digits = p;
                while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                    ++p;
                if (p == digits)
                    throw std::runtime_error("bad fraction");
            }
            if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
                ++p;
                if (p < s.size() && (s[p] == '+' || s[p] == '-'))
                    ++p;
                const size_t digits = p;
                while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                    ++p;
                if (p == digits)
                    throw std::runtime_error("bad exponent");
            }
            j.kind = Json::Number;
            j.number = std::strtod(s.substr(start, p - start).c_str(), nullptr);
        }
        return j;
    }

  public:
    explicit Reader(const std::string &text) : s(text) {}
    Json read() {
        auto j = value(0);
        space();
        if (p != s.size())
            throw std::runtime_error("trailing JSON content");
        return j;
    }
};
std::string escape(const std::string &s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 32) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        } else
            out += char(c);
    }
    return out + '"';
}
int integer(const Json &j, int low, int high) {
    const double v = j.numeric();
    if (v < low || v > high || std::floor(v) != v)
        throw std::runtime_error("integer out of range");
    return int(v);
}
float bounded(const Json &j, float low, float high) {
    const double v = j.numeric();
    if (v < low || v > high)
        throw std::runtime_error("number out of range");
    return float(v);
}
uint32_t color(const std::string &s) {
    if (s.size() != 7 || s[0] != '#' || s.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
        throw std::runtime_error("color must be #RRGGBB");
    return uint32_t(std::strtoul(s.c_str() + 1, nullptr, 16));
}
} // namespace

void LightTags::seed(const emu::Wsc &w, const DataTables *tables) {
    sets_.clear();
    water_sets_.clear();
    if (w.rom.size() != 0x400000)
        return;
    struct Decision {
        int set, tile;
        const char *type;
        int anchor, direction;
        uint32_t color; // fixed color (beacon); 0 = sampled from the live picture
    };
    static constexpr Decision decisions[] = {
#include "game/light_defaults.generated.inc"
    };
    // Include empty sets so an old extracted list cannot resurrect rejected candidates.
    for (int set = 0; set < 64; ++set)
        sets_[set] = {};
    for (const auto &d : decisions) {
        LightTag t;
        t.metatile = d.tile;
        t.type = d.type;
        t.anchor = d.anchor;
        t.direction = d.direction;
        t.offset = {float((d.anchor - 1) % 3) * 8, float(2 - (d.anchor - 1) / 3) * 8};
        t.color = d.color ? d.color : 0xFFFFFF; // Fallback metadata; visible sources sample the live raster.
        t.radius = t.type == "beacon" ? 20 : 32;
        t.intensity = t.type == "beacon"      ? 1.0f
                      : t.type == "pulse"     ? 0.14f
                      : t.type == "soft_glow" ? 0.15f
                                              : 0.18f;
        t.enabled = true;
        t.confidence = 1;
        t.evidence = "Reviewed light";
        sets_[d.set].push_back(t);
    }
    seed_water(w, tables);
}
std::vector<int> LightTags::tilesets() const {
    std::vector<int> result;
    for (const auto &[s, t] : sets_) {
        (void)t;
        result.push_back(s);
    }
    return result;
}
std::string LightTags::file(int set) const {
    char name[80];
    std::snprintf(name, sizeof name, "materials/tileset-%02d.json", set);
    return name;
}
std::string LightTags::json(int set) const {
    std::ostringstream o;
    o << "{\n  \"version\": 4,\n  \"tileset\": " << set << ",\n  \"metatiles\": [";
    std::map<std::pair<int, int>, std::pair<const LightTag *, const WaterTag *>> records;
    if (sets_.count(set))
        for (const auto &t : sets_.at(set))
            records[{t.metatile, t.mirror}].first = &t;
    if (water_sets_.count(set))
        for (const auto &t : water_sets_.at(set))
            records[{t.metatile, t.mirror}].second = &t;
    bool first = true;
    for (const auto &[key, tags] : records) {
        o << (first ? "\n" : ",\n") << "    {\"metatile\": " << key.first << ", \"mirror\": " << key.second;
        if (tags.first) {
            const auto &t = *tags.first;
            char c[16];
            std::snprintf(c, sizeof c, "#%06X", t.color);
            o << ", \"light\": {\"enabled\": " << (t.enabled ? "true" : "false")
              << ", \"type\": " << escape(t.type) << ", \"color\": " << escape(c)
              << ", \"anchor\": " << t.anchor << ", \"direction\": " << t.direction
              << ", \"radius\": " << t.radius << ", \"intensity\": " << t.intensity << ", \"offset\": ["
              << t.offset[0] << ", " << t.offset[1] << "]}, \"confidence\": " << t.confidence
              << ", \"evidence\": " << escape(t.evidence);
        }
        if (tags.second) {
            const auto &t = *tags.second;
            o << ", \"water\": {\"enabled\": " << (t.enabled ? "true" : "false")
              << ", \"kind\": " << escape(t.kind) << ", \"mask\": [";
            for (int y = 0; y < 16; ++y) {
                std::string row;
                for (int x = 0; x < 16; ++x)
                    row += t.mask[size_t(y)] & (1u << x) ? '1' : '0';
                o << (y ? ", " : "") << escape(row);
            }
            o << "], \"confidence\": " << t.confidence << ", \"evidence\": " << escape(t.evidence) << "}";
        }
        o << "}";
        first = false;
    }
    o << "\n  ]\n}\n";
    return o.str();
}
bool LightTags::load(int set, const std::string &text, std::string *error) {
    try {
        const auto root = Reader(text).read();
        integer(root.get("version"), 4, 4);
        if (integer(root.get("tileset"), 0, 63) != set)
            throw std::runtime_error("wrong tileset/version");
        const auto &items = root.get("metatiles");
        if (items.kind != Json::Array)
            throw std::runtime_error("metatiles must be an array");
        std::vector<LightTag> tags;
        std::vector<WaterTag> waters;
        std::set<std::pair<int, int>> water_keys;
        std::set<std::pair<int, int>> keys;
        for (const auto &item : items.array) {
            if (item.object.count("water") && item.get("water").kind != Json::Null) {
                WaterTag t;
                t.metatile = integer(item.get("metatile"), 0, 127);
                t.mirror = integer(item.get("mirror"), -1, 3);
                if (!water_keys.emplace(t.metatile, t.mirror).second)
                    throw std::runtime_error("duplicate water metatile/mirror");
                const auto &water = item.get("water");
                const auto &enabled = water.get("enabled");
                if (enabled.kind != Json::Boolean)
                    throw std::runtime_error("water enabled must be boolean");
                t.enabled = enabled.number != 0;
                t.kind = water.get("kind").text();
                if (t.kind != "flowing" && t.kind != "still")
                    throw std::runtime_error("unknown water kind");
                const auto &rows = water.get("mask");
                if (rows.kind != Json::Array || rows.array.size() != 16)
                    throw std::runtime_error("water mask requires 16 rows");
                for (size_t y = 0; y < 16; ++y) {
                    const auto row = rows.array[y].text();
                    if (row.size() != 16 || row.find_first_not_of("01") != std::string::npos)
                        throw std::runtime_error("water mask row requires sixteen 0/1 pixels");
                    for (int x = 0; x < 16; ++x)
                        if (row[size_t(x)] == '1')
                            t.mask[y] |= uint16_t(1u << x);
                }
                t.confidence = bounded(water.get("confidence"), 0, 1);
                t.evidence = water.get("evidence").text();
                waters.push_back(t);
            }
            // height-only records are valid material records, without point emission.
            if (!item.object.count("light") || item.get("light").kind == Json::Null)
                continue;
            LightTag t;
            t.metatile = integer(item.get("metatile"), 0, 127);
            t.mirror = integer(item.get("mirror"), -1, 3);
            if (!keys.emplace(t.metatile, t.mirror).second)
                throw std::runtime_error("duplicate metatile/mirror");
            const auto &l = item.get("light");
            const auto &enabled = l.get("enabled");
            if (enabled.kind != Json::Boolean)
                throw std::runtime_error("enabled must be boolean");
            t.enabled = enabled.number != 0;
            t.type = l.get("type").text();
            if (t.type != "flame" && t.type != "soft_glow" && t.type != "harsh_electric" &&
                t.type != "pulse" && t.type != "beam" && t.type != "beacon")
                throw std::runtime_error("unknown light type");
            if (l.object.count("anchor"))
                t.anchor = integer(l.get("anchor"), 1, 9);
            if (l.object.count("direction"))
                t.direction = integer(l.get("direction"), 1, 9);
            if (t.type == "beam" && t.direction == 5)
                throw std::runtime_error("beam requires a direction");
            t.color = color(l.get("color").text());
            t.radius = bounded(l.get("radius"), 1, 96);
            t.intensity = bounded(l.get("intensity"), 0, 1);
            t.offset = {float((t.anchor - 1) % 3) * 8, float(2 - (t.anchor - 1) / 3) * 8};
            if (l.object.count("offset")) {
                const auto &offset = l.get("offset");
                if (offset.kind != Json::Array || offset.array.size() != 2)
                    throw std::runtime_error("offset must have two coordinates");
                for (size_t i = 0; i < 2; ++i)
                    t.offset[i] = bounded(offset.array[i], 0, 16);
            }
            t.confidence = bounded(item.get("confidence"), 0, 1);
            t.evidence = item.get("evidence").text();
            tags.push_back(t);
        }
        sets_[set] = std::move(tags);
        water_sets_[set] = std::move(waters);
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = e.what();
        return false;
    }
}
const LightTag *LightTags::find(int set, int base, int mirror) const {
    const auto it = sets_.find(set);
    if (it == sets_.end())
        return nullptr;
    const LightTag *fallback = nullptr;
    for (const auto &t : it->second)
        if (t.metatile == base) {
            if (t.mirror == mirror)
                return &t;
            if (t.mirror == -1)
                fallback = &t;
        }
    return fallback;
}
float LightTags::animation(int type, uint64_t frame) {
    if (type == 5) {
        const float t = float(frame % 90);
        return (1 - std::clamp((t - 30) / 6, 0.0f, 1.0f)) * std::clamp((t + 1) / 6, 0.0f, 1.0f);
    }
    if (type == 0) {
        const auto knot = [](uint64_t f) {
            uint32_t h = uint32_t(f);
            h ^= h >> 16;
            h *= 0x7FEB352Du;
            h ^= h >> 15;
            return float(h & 65535) / 65535;
        };
        const float a = float(frame % 8) / 8;
        return 0.94f + 0.12f * (knot(frame / 8) * (1 - a) + knot(frame / 8 + 1) * a);
    }
    if (type == 3)
        return 0.875f + 0.125f * std::cos(float(frame % 180) / 180 * 6.2831853f);
    return 1;
}
std::vector<PointLight> LightTags::visible(const emu::FrameLayers &layers, const uint32_t *mask, int set,
                                           uint64_t frame) const {
    std::array<std::array<const LightTag *, 4>, 128> lookup{};
    if (const auto it = sets_.find(set); it != sets_.end()) {
        // find()'s precedence (the first exact mirror, else the last -1 fallback) without searching
        // the entire tag list for each of the 512 lookup entries per chunk.
        const auto &tags = it->second;
        for (const auto &tag : tags)
            if (tag.mirror == -1 && tag.metatile >= 0 && tag.metatile < 128)
                lookup[size_t(tag.metatile)].fill(&tag);
        for (auto tag = tags.rbegin(); tag != tags.rend(); ++tag)
            if (tag->mirror >= 0 && tag->mirror < 4 && tag->metatile >= 0 && tag->metatile < 128)
                lookup[size_t(tag->metatile)][size_t(tag->mirror)] = &*tag;
    }
    struct Sample {
        float score;
        uint16_t color;
    };
    struct Source {
        PointLight light;
        const LightTag *tag = nullptr;
        std::vector<Sample> samples;
        int x = 0, y = 0;
    };
    std::map<std::tuple<int, int, int, int>, Source> sources;
    // Consecutive pixels of a row nearly always belong to the same 16x16 source: look it up once per run.
    std::tuple<int, int, int, int> last_key{-1, -1, 0, 0};
    Source *last = nullptr;
    for (int y = 0; y < 144; ++y) {
        if (!layers.valid[size_t(y)])
            continue;
        const auto &regs = layers.registers[size_t(y)];
        for (int x = 0; x < 224; ++x) {
            const size_t at = size_t(y * 224 + x);
            if (mask[at] & 0xFFFF00)
                continue; // Never sample UI or foreground actor pixels.
            const auto &p = layers.planes[emu::FrameLayers::Screen1][at];
            if (!(p.flags & emu::RasterPixel::Present) || p.tile < 0x200 || p.tile >= 0x400 || p.palette > 3)
                continue;
            const int b = (p.tile - 0x200) / 4;
            const int mirror =
                (p.flags & emu::RasterPixel::HFlip ? 1 : 0) | (p.flags & emu::RasterPixel::VFlip ? 2 : 0);
            const auto *tag = lookup[size_t(b)][size_t(mirror)];
            if (!tag || !tag->enabled)
                continue;
            const int q = (p.tile & 3) ^ mirror;
            const int ox = x - ((x + regs[0x10]) & 7) - (q % 2) * 8;
            const int oy = y - ((y + regs[0x11]) & 7) - (q / 2) * 8;
            const std::tuple key{b, mirror, ox, oy};
            if (!last || key != last_key) {
                last = &sources[key];
                last_key = key;
            }
            auto &source = *last;
            auto &light = source.light;
            if (!source.tag) { // the same for every pixel of this source: set it up once
                source.tag = tag;
                source.x = ox;
                source.y = oy;
                light.type = tag->type == "flame"            ? 0
                             : tag->type == "soft_glow"      ? 1
                             : tag->type == "harsh_electric" ? 2
                             : tag->type == "pulse"          ? 3
                             : tag->type == "beam"           ? 4
                                                             : 5; // beacon
                light.position = {float(ox) + (mirror & 1 ? 16 - tag->offset[0] : tag->offset[0]),
                                  float(oy) + (mirror & 2 ? 16 - tag->offset[1] : tag->offset[1]),
                                  std::min(tag->radius, 32.0f), light.type == 2 ? 1.0f : 0.0f};
                float dx = float((tag->direction - 1) % 3 - 1), dy = float(1 - (tag->direction - 1) / 3);
                if (mirror & 1)
                    dx = -dx;
                if (mirror & 2)
                    dy = -dy;
                const float length = std::sqrt(dx * dx + dy * dy);
                light.beam = {length ? dx / length : 0, length ? dy / length : 0,
                              light.type == 4 ? 1.0f : 0.0f, 1};
            }
            // Pulse samples only the central gem, never the frame. Saturated
            // pixels outrank neutral highlights; gray-only mod art has a center fallback.
            const uint16_t c = layers.color(at);
            const float r = float(c >> 8 & 15), g = float(c >> 4 & 15), blue = float(c & 15);
            const float luminance = r * 3 + g * 6 + blue;
            float score = luminance;
            if (light.type == 3) {
                const float cx = float(x - ox) + 0.5f - 8, cy = float(y - oy) + 0.5f - 8;
                if (std::abs(cx) > 4 || std::abs(cy) > 4)
                    continue;
                const float peak = std::max({r, g, blue}), low = std::min({r, g, blue});
                score = peak >= 3 && peak - low >= peak * 0.25f ? 1000 + peak * 10 : luminance * 0.01f;
            } else if (light.type != 1) {
                const float ax = float(x) + 0.5f - light.position[0],
                            ay = float(y) + 0.5f - light.position[1];
                score /= 1 + (ax * ax + ay * ay) / 64;
            }
            source.samples.push_back({score, c});
        }
    }
    // Touching sign/run tiles share their brightest emitting pixels. Do this
    // before selecting highlights, so dark lettering cannot dim a segment.
    std::vector<Source *> ordered;
    for (auto &[key, source] : sources) {
        (void)key;
        ordered.push_back(&source);
    }
    std::vector<size_t> color_parent(ordered.size());
    for (size_t i = 0; i < ordered.size(); ++i)
        color_parent[i] = i;
    const auto color_root = [&](size_t i) {
        while (color_parent[i] != i)
            i = color_parent[i];
        return i;
    };
    for (size_t i = 0; i < ordered.size(); ++i)
        for (size_t j = 0; j < i; ++j) {
            const auto &a = *ordered[i], &b = *ordered[j];
            const bool touching =
                (std::abs(a.x - b.x) == 16 && a.y == b.y) || (std::abs(a.y - b.y) == 16 && a.x == b.x);
            if (touching && a.light.type == b.light.type && a.light.type != 3)
                color_parent[color_root(i)] = color_root(j);
        }
    // Visit each group's samples in exactly the original source/pixel order, without first
    // concatenating and copying every sample into a second set of vectors.
    struct GroupColor {
        std::array<float, 3> emission{};
        float peak = 0, best = 0, weight = 0, intensity = 0;
    };
    std::map<size_t, GroupColor> group_colors;
    for (size_t i = 0; i < ordered.size(); ++i) {
        auto &g = group_colors[color_root(i)];
        g.intensity = std::max(g.intensity, ordered[i]->tag->intensity);
        for (const auto &s : ordered[i]->samples)
            g.best = std::max(g.best, s.score);
    }
    for (size_t i = 0; i < ordered.size(); ++i) {
        auto &g = group_colors[color_root(i)];
        for (const auto &s : ordered[i]->samples)
            if (s.score >= g.best * 0.85f) {
                const float w = s.score;
                g.weight += w;
                for (int c = 0; c < 3; ++c) {
                    const float value = float(s.color >> (8 - c * 4) & 15) / 15;
                    g.emission[size_t(c)] += value * w;
                    g.peak = std::max(g.peak, value);
                }
            }
    }
    for (auto &[id, g] : group_colors) {
        (void)id;
        if (g.weight > 0)
            for (int c = 0; c < 3; ++c)
                g.emission[size_t(c)] /= g.weight;
        const float maximum = std::max({g.emission[0], g.emission[1], g.emission[2]});
        if (maximum > 0)
            for (int c = 0; c < 3; ++c)
                g.emission[size_t(c)] /= maximum;
    }
    std::vector<PointLight> lights;
    for (size_t i = 0; i < ordered.size(); ++i) {
        auto &source = *ordered[i];
        auto &l = source.light;
        const auto &group = group_colors[color_root(i)];
        for (int c = 0; c < 3; ++c)
            l.emission[size_t(c)] = group.emission[size_t(c)];
        const float peak = group.peak;
        if (l.type == 0) {
            l.emission[0] = 0.8f + 0.2f * l.emission[0];
            l.emission[1] = 0.6f + 0.12f * l.emission[1];
            l.emission[2] *= 0.25f;
        } else if (l.type == 2) {
            l.emission[0] = 0.75f + 0.2f * l.emission[0];
            l.emission[1] = 0.82f + 0.15f * l.emission[1];
            l.emission[2] = 0.9f + 0.1f * l.emission[2];
        }
        l.emission[3] = peak * group.intensity;
        if (l.type == 5) {
            // Beacon: the decision's fixed color, never the tile's (Tokyo Tower's aviation
            // light). On for 0.6 s of every 1.5 s with soft edges, in step across the screen.
            for (int c = 0; c < 3; ++c)
                l.emission[size_t(c)] = float(source.tag->color >> (16 - c * 8) & 255) / 255;
            l.emission[3] = source.tag->intensity;
        }
        l.emission[3] *= animation(l.type, frame);
        lights.push_back(l);
    }
    // A straight sign can contain shared-corner anchors as well as ordinary
    // centers. Its emitting span follows the whole strip, rather than shrinking
    // toward those anchors. A compact fixture with one shared anchor stays a point.
    std::map<size_t, std::vector<size_t>> strips;
    for (size_t i = 0; i < ordered.size(); ++i)
        strips[color_root(i)].push_back(i);
    std::vector<bool> in_strip(lights.size());
    std::vector<PointLight> strip_lights;
    for (const auto &[id, members] : strips) {
        (void)id;
        if (members.size() < 2)
            continue;
        const auto &first = *ordered[members.front()];
        bool horizontal = true, vertical = true;
        int lo_x = first.x, hi_x = first.x, lo_y = first.y, hi_y = first.y;
        float lo_ax = first.light.position[0], hi_ax = lo_ax;
        float lo_ay = first.light.position[1], hi_ay = lo_ay;
        for (size_t i : members) {
            const auto &s = *ordered[i];
            horizontal &= s.y == first.y;
            vertical &= s.x == first.x;
            lo_x = std::min(lo_x, s.x);
            hi_x = std::max(hi_x, s.x);
            lo_y = std::min(lo_y, s.y);
            hi_y = std::max(hi_y, s.y);
            lo_ax = std::min(lo_ax, s.light.position[0]);
            hi_ax = std::max(hi_ax, s.light.position[0]);
            lo_ay = std::min(lo_ay, s.light.position[1]);
            hi_ay = std::max(hi_ay, s.light.position[1]);
        }
        if ((!horizontal || hi_ax - lo_ax <= 6) && (!vertical || hi_ay - lo_ay <= 6))
            continue;
        auto strip = lights[members.front()];
        if (strip.type == 3 || strip.type == 4)
            continue;
        const int axis = horizontal ? 0 : 1;
        strip.position[size_t(axis)] = float(axis == 0 ? lo_x + hi_x + 16 : lo_y + hi_y + 16) / 2;
        strip.extent[size_t(axis)] = float(axis == 0 ? hi_x - lo_x : hi_y - lo_y) / 2;
        strip.beam[3] = float(members.size());
        for (size_t i : members)
            in_strip[i] = true;
        strip_lights.push_back(strip);
    }
    for (size_t i = 0; i < lights.size(); ++i)
        if (!in_strip[i])
            strip_lights.push_back(lights[i]);
    lights = std::move(strip_lights);
    // Connected components make merging independent of scan order, including four shared-corner tiles.
    std::vector<size_t> parent(lights.size());
    for (size_t i = 0; i < parent.size(); ++i)
        parent[i] = i;
    const auto root = [&](size_t i) {
        while (parent[i] != i)
            i = parent[i];
        return i;
    };
    for (size_t i = 0; i < lights.size(); ++i)
        for (size_t j = 0; j < i; ++j) {
            const auto &a = lights[i], &b = lights[j];
            const float dx = a.position[0] - b.position[0], dy = a.position[1] - b.position[1];
            if (a.type == b.type && dx * dx + dy * dy <= 36)
                parent[root(i)] = root(j);
        }
    std::map<size_t, PointLight> groups;
    for (size_t i = 0; i < lights.size(); ++i) {
        const auto &l = lights[i];
        auto &g = groups[root(i)];
        g.type = l.type;
        g.position[0] += l.position[0] * l.beam[3];
        g.position[1] += l.position[1] * l.beam[3];
        g.position[2] = std::max(g.position[2], l.position[2]);
        g.position[3] = l.position[3];
        g.extent[0] = std::max(g.extent[0], l.extent[0]);
        g.extent[1] = std::max(g.extent[1], l.extent[1]);
        // Exposure-weighted mean hue and max intensity: four fixture tiles never quadruple brightness.
        for (int c = 0; c < 3; ++c)
            g.emission[size_t(c)] += l.emission[size_t(c)] * l.emission[3];
        // Preserve the dominant tile's reviewed direction if a beam fixture combines directions.
        // Equal-exposure ties use the first tile in deterministic source order.
        if (g.beam[3] == 0 || l.emission[3] > g.emission[3]) {
            g.beam[0] = l.beam[0];
            g.beam[1] = l.beam[1];
        }
        g.emission[3] = std::max(g.emission[3], l.emission[3]);
        g.beam[2] += l.emission[3];
        g.beam[3] += l.beam[3];
    }
    std::vector<PointLight> out;
    for (auto &[id, g] : groups) {
        (void)id;
        g.position[0] /= g.beam[3];
        g.position[1] /= g.beam[3];
        if (g.beam[2] > 0)
            for (int c = 0; c < 3; ++c)
                g.emission[size_t(c)] /= g.beam[2];
        g.beam[2] = g.type == 4 ? 1 : 0;
        out.push_back(g);
    }
    // Represent rectangular runs with an emitting footprint rather than a giant
    // center light. No radius grows with fixture size. Rows merge first, then
    // matching rows vertically; irregular runs keep separate bounded rectangles.
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < out.size() && !changed; ++i)
            for (size_t j = i + 1; j < out.size(); ++j) {
                auto &a = out[i];
                const auto &b = out[j];
                bool same_emission = true;
                for (size_t c = 0; c < 4; ++c)
                    same_emission &= std::abs(a.emission[c] - b.emission[c]) < 0.00001f;
                if (a.type == 3 || a.type == 4 || a.type != b.type || !same_emission)
                    continue;
                const float dx = std::abs(a.position[0] - b.position[0]),
                            dy = std::abs(a.position[1] - b.position[1]);
                const bool horizontal = dy < 0.01f && a.extent[1] == b.extent[1] &&
                                        std::abs(dx - a.extent[0] - b.extent[0] - 16) < 0.01f;
                const bool vertical = dx < 0.01f && a.extent[0] == b.extent[0] &&
                                      std::abs(dy - a.extent[1] - b.extent[1] - 16) < 0.01f;
                if (!horizontal && !vertical)
                    continue;
                const int axis = horizontal ? 0 : 1;
                const float lo =
                    std::min(a.position[axis] - a.extent[axis], b.position[axis] - b.extent[axis]);
                const float hi =
                    std::max(a.position[axis] + a.extent[axis], b.position[axis] + b.extent[axis]);
                a.position[axis] = (lo + hi) / 2;
                a.extent[axis] = (hi - lo) / 2;
                a.beam[3] += b.beam[3];
                out.erase(out.begin() + std::ptrdiff_t(j));
                changed = true;
                break;
            }
    }
    // Extend runs along their length to cover the end tiles too. A shared-corner
    // wall fixture remains a point; an emitting field is flat throughout its footprint.
    for (auto &l : out)
        for (auto &extent : l.extent)
            if (extent > 0)
                extent += 8;
    return out;
}
} // namespace sawgod::game
