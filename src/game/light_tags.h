// Editable material metadata extracted from the supplied WSC graphics, never game state.
#pragma once
#include "emu/wsc.h"
#include "engine/point_light.h"
#include <map>
#include <string>

namespace sawgod::game {
class DataTables;
struct LightTag {
    int metatile = 0, mirror = -1; // graphics-set group of four tiles; -1 inherits all mirrors
    std::string type, evidence;
    int anchor = 5, direction = 5; // numpad; exact edges/corners at 0/8/16
    uint32_t color = 0;
    float radius = 24, intensity = 0.18f, confidence = 0.4f;
    std::array<float, 2> offset{8, 8};
    bool enabled = false;
};
struct WaterTag {
    int metatile = 0, mirror = -1;
    std::string kind = "flowing", evidence; // or "still" (single-tile puddle)
    float confidence = 0;
    bool enabled = false;
    // Unmirrored 16x16 coverage, row-major: only the water's own pixels, never its stone/sand/rim.
    std::array<uint16_t, 16> mask{};
};
class LightTags {
  public:
    void seed(const emu::Wsc &w, const DataTables *tables = nullptr);
    std::vector<int> tilesets() const;
    std::string file(int tileset) const;
    std::string json(int tileset) const;
    // Replaces lights and water atomically (version 4 files only); a file that fails keeps the previous tags.
    bool load(int tileset, const std::string &json, std::string *error);
    const LightTag *find(int tileset, int metatile, int mirror) const;
    const std::map<int, std::vector<LightTag>> &sets() const { return sets_; }
    static float animation(int type, uint64_t frame);
    std::vector<PointLight> visible(const emu::FrameLayers &layers, const uint32_t *mask, int tileset,
                                    uint64_t frame) const;

    const WaterTag *water(int tileset, int metatile, int mirror) const;
    // Underlying Screen1 material, including beneath opaque actor pixels.
    bool water_at(const emu::FrameLayers &, int x, int y, int tileset) const;
    // RGB: wrapped map x/y, kind*16 + vertical shore distance (1..12); zero = dry.
    std::vector<uint32_t> water_pixels(const emu::FrameLayers &, const uint32_t *mask, int tileset) const;

  private:
    void seed_water(const emu::Wsc &, const DataTables *);
    std::map<int, std::vector<WaterTag>> water_sets_;
    std::map<int, std::vector<LightTag>> sets_;
    // Tag matching is static until a tileset is edited. Store indices so copies remain valid.
    using TagLookup = std::array<std::array<int, 4>, 128>;
    mutable std::map<int, TagLookup> lookups_;
};
} // namespace sawgod::game
