// Loads and resolves landmark metadata for enhanced field presentation.
#include "game/landmark_tags.h"
#include "game/game_config.h"
#include "game/text.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace sawgod::game {
namespace {
constexpr const char *kStamp = "# sawgod-landmarks-defaults=";
std::string fingerprint(const std::string &text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text)
        hash = (hash ^ c) * 1099511628211ull;
    return std::to_string(hash);
}
constexpr const char *kHeader =
    "record,id,kind,map,tileset,base_row,extra_shaft_rows,shaft_row,enabled,x,y,metatile,mirror\n";
int number(const std::string &s, int lo, int hi) {
    int v = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size() || v < lo || v > hi)
        throw std::runtime_error("invalid landmark number: " + s);
    return v;
}
float shaft_rows(const std::string &s) {
    // Locale-independent, the whole string. (Not std::from_chars: Apple's library has no floating-point
    // version before macOS 13.3, and the macOS build supports 11.)
    float value = 0;
    std::istringstream in(s);
    in.imbue(std::locale::classic());
    in >> std::noskipws >> value;
    if (s.empty() || in.fail() || in.peek() != std::char_traits<char>::eof() || !std::isfinite(value) ||
        (value != -1 && (value < 0 || value > 64)))
        throw std::runtime_error("invalid extra shaft rows: " + s);
    return value;
}
bool kind(const std::string &s) {
    return s == "town" || s == "castle" || s == "tower" || s == "tokyo_tower" || s == "other";
}
} // namespace
LandmarkTags::LandmarkTags() {
#include "game/landmark_defaults.generated.inc"
}
bool LandmarkTags::plain(int map, int set, int tile) const {
    return std::any_of(grounds_.begin(), grounds_.end(), [&](const LandmarkGround &g) {
        return g.map == map && g.tileset == set && g.metatile == tile;
    });
}
std::string LandmarkTags::csv() const {
    std::string out = kHeader;
    for (const auto &g : grounds_)
        out += "ground,,," + std::to_string(g.map) + "," + std::to_string(g.tileset) + ",,,,,,," +
               std::to_string(g.metatile) + ",\n";
    for (const auto &r : entries_) {
        for (int tile : r.cutout_ground_metatiles)
            out += "cutout," + r.id + "," + r.kind + "," + std::to_string(r.map) + "," +
                   std::to_string(r.tileset) + "," + std::to_string(r.base_row) + "," +
                   std::to_string(r.extra_shaft_rows) + "," + std::to_string(r.shaft_row) + "," +
                   std::to_string(int(r.enabled)) + ",,," + std::to_string(tile) + ",\n";
        for (const auto &c : r.cells)
            out += "cell," + r.id + "," + r.kind + "," + std::to_string(r.map) + "," +
                   std::to_string(r.tileset) + "," + std::to_string(r.base_row) + "," +
                   std::to_string(r.extra_shaft_rows) + "," + std::to_string(r.shaft_row) + "," +
                   std::to_string(int(r.enabled)) + "," + std::to_string(c.x) + "," + std::to_string(c.y) +
                   "," + std::to_string(c.metatile) + "," + std::to_string(c.mirror) + "\n";
    }
    return std::string(kStamp) + fingerprint(out) + "\n" + out;
}
std::string LandmarkTags::refresh(const std::string &text, bool *edited) const {
    const auto current = csv();
    const auto newline = text.find('\n');
    const auto body = newline == std::string::npos ? std::string() : text.substr(newline + 1);
    const bool stamped = text.starts_with(kStamp);
    const bool intact = stamped && text.substr(0, newline) == std::string(kStamp) + fingerprint(body);
    *edited = !intact;
    return *edited ? text : current;
}
bool LandmarkTags::load(const std::string &text, std::string *error) {
    try {
        const auto rows = parse_csv(text.starts_with(kStamp) ? text.substr(text.find('\n') + 1) : text);
        const auto header = parse_csv(kHeader);
        if (rows.empty() || rows.front() != header.front() || rows.size() > 16385)
            throw std::runtime_error("invalid landmark header/size");
        std::vector<Landmark> entries;
        std::vector<LandmarkGround> grounds;
        std::map<std::string, size_t> ids;
        std::set<std::tuple<int, int, int>> occupied, plain;
        for (size_t i = 1; i < rows.size(); ++i) {
            const auto &r = rows[i];
            if (r.size() != 13)
                throw std::runtime_error("landmark row needs 13 columns");
            const int map = number(r[3], 0, 210), set = number(r[4], 0, 63), tile = number(r[11], 0, 127);
            if (!GameConfig::mode7_map(map))
                throw std::runtime_error("landmarks require a world map");
            if (r[0] == "ground") {
                if (!plain.emplace(map, set, tile).second)
                    throw std::runtime_error("duplicate plain ground");
                grounds.push_back({map, set, tile});
                continue;
            }
            if ((r[0] != "cell" && r[0] != "cutout") || r[1].empty() ||
                r[1].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                    std::string::npos ||
                !kind(r[2]))
                throw std::runtime_error("invalid landmark record/id/kind");
            Landmark entry{r[1],
                           r[2],
                           map,
                           set,
                           number(r[5], 1, 64),
                           shaft_rows(r[6]),
                           number(r[7], 0, 63),
                           bool(number(r[8], 0, 1)),
                           {}};
            const auto [it, added] = ids.emplace(entry.id, entries.size());
            if (added)
                entries.push_back(entry);
            auto &e = entries[it->second];
            if (std::tie(e.kind, e.map, e.tileset, e.base_row, e.extra_shaft_rows, e.shaft_row, e.enabled) !=
                std::tie(entry.kind, entry.map, entry.tileset, entry.base_row, entry.extra_shaft_rows,
                         entry.shaft_row, entry.enabled))
                throw std::runtime_error("inconsistent footprint metadata");
            if (r[0] == "cutout") {
                if (std::find(e.cutout_ground_metatiles.begin(), e.cutout_ground_metatiles.end(), tile) !=
                    e.cutout_ground_metatiles.end())
                    throw std::runtime_error("duplicate cutout ground");
                e.cutout_ground_metatiles.push_back(tile);
            } else {
                const LandmarkCell cell{number(r[9], 0, 63), number(r[10], 0, 63), tile, number(r[12], 0, 3)};
                if (!occupied.emplace(map, cell.x, cell.y).second)
                    throw std::runtime_error("overlapping landmark cells");
                e.cells.push_back(cell);
            }
        }
        for (const auto &e : entries) {
            int top = 64, bottom = -1;
            for (const auto &c : e.cells) {
                top = std::min(top, c.y);
                bottom = std::max(bottom, c.y);
            }
            if (e.cells.empty() || e.base_row != bottom + 1 || e.shaft_row >= e.base_row - top ||
                (e.extra_shaft_rows && e.kind != "tower"))
                throw std::runtime_error("invalid base/shaft row");
        }
        ++revision_;
        entries_ = std::move(entries);
        grounds_ = std::move(grounds);
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = e.what();
        return false;
    }
}
} // namespace sawgod::game
