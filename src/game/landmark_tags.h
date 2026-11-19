// Host-only, editable presentation footprints. Coordinates are playable map cells, not ROM offsets.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sawgod::game {
struct LandmarkCell {
    int x, y, metatile, mirror;
};
struct Landmark {
    std::string id, kind;
    // extra_shaft_rows: -1 stretches the column above the screen, hiding the painted tip.
    int map, tileset, base_row;
    float extra_shaft_rows; // fractional rows allow an exact x1.5 painted height
    int shaft_row;
    bool enabled;
    std::vector<LandmarkCell> cells;
    std::vector<int> cutout_ground_metatiles{}; // extra existing ground colors removed from card edges
};
struct LandmarkGround {
    int map, tileset, metatile;
};
class LandmarkTags {
  public:
    LandmarkTags();
    const std::vector<Landmark> &entries() const { return entries_; }
    bool plain(int map, int set, int metatile) const;
    std::string csv() const;
    // Refresh only intact generated caches; preserve edited and custom files.
    std::string refresh(const std::string &text, bool *edited) const;
    uint64_t revision() const { return revision_; }
    // Complete atomic replacement; invalid files preserve the preceding table.
    bool load(const std::string &csv, std::string *error);
    static constexpr const char *file = "materials/landmarks.csv";

  private:
    uint64_t revision_ = 1;
    std::vector<Landmark> entries_;
    std::vector<LandmarkGround> grounds_;
};
} // namespace sawgod::game
