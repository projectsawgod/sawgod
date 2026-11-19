// Opt-in measurements of the final composite, independent of game/interpolation coordinates.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <vector>
namespace sawgod::app {
struct PixelPatch {
    int x = 0, y = 0, width = 0, height = 0;
    std::vector<uint32_t> pixels;
    std::vector<uint8_t> mask;
};
struct PixelMatch {
    float x = 0, y = 0, error = 255;
    bool valid = false;
};
inline PixelPatch pixel_patch(std::span<const uint32_t> image, int width, int height, int x, int y, int w,
                              int h) {
    PixelPatch p{x, y, w, h, {}, {}};
    if (x < 0 || y < 0 || x + w > width || y + h > height || image.size() != size_t(width * height))
        return {};
    p.pixels.reserve(size_t(w * h));
    for (int row = 0; row < h; ++row)
        p.pixels.insert(p.pixels.end(), image.begin() + (y + row) * width + x,
                        image.begin() + (y + row) * width + x + w);
    return p;
}
// Search the actual RGB pixels. Coordinates are displacement of the reference picture, not of its
// simulation state. Low-texture/occluded/faded patches explicitly report invalid instead of guessing.
inline PixelMatch match_pixels(const PixelPatch &p, std::span<const uint32_t> image, int width, int height,
                               int expected_x = 0, int expected_y = 0, int radius = 8) {
    if (p.pixels.empty() || image.size() != size_t(width * height))
        return {};
    const auto score = [&](int dx, int dy) {
        if (p.x + dx < 0 || p.y + dy < 0 || p.x + dx + p.width > width || p.y + dy + p.height > height)
            return 255.f;
        uint64_t sum = 0;
        unsigned count = 0;
        for (int y = 0; y < p.height; y += 2)
            for (int x = 0; x < p.width; x += 2) {
                const size_t i = size_t(y * p.width + x);
                if (!p.mask.empty() && !p.mask[i])
                    continue;
                const auto a = p.pixels[i], b = image[size_t((p.y + y + dy) * width + p.x + x + dx)];
                for (unsigned shift : {0u, 8u, 16u})
                    sum += unsigned(std::abs(int(a >> shift & 255) - int(b >> shift & 255)));
                ++count;
            }
        return count ? float(sum) / float(count * 3) : 255.f;
    };
    PixelMatch best;
    for (int dy = expected_y - radius; dy <= expected_y + radius; ++dy)
        for (int dx = expected_x - radius; dx <= expected_x + radius; ++dx) {
            const float error = score(dx, dy);
            if (error < best.error ||
                (error == best.error && dx * dx + dy * dy < best.x * best.x + best.y * best.y))
                best = {float(dx), float(dy), error, false};
        }
    float second = 255;
    for (int dy = expected_y - radius; dy <= expected_y + radius; ++dy)
        for (int dx = expected_x - radius; dx <= expected_x + radius; ++dx)
            if (std::abs(dx - best.x) > 1 || std::abs(dy - best.y) > 1)
                second = std::min(second, score(dx, dy));
    // Reject ambiguous periodic patterns as well as flat/occluded patches and search boundaries.

    unsigned range = 0;
    for (unsigned shift : {0u, 8u, 16u}) {
        unsigned lo = 255, hi = 0;
        for (size_t i = 0; i < p.pixels.size(); ++i)
            if (p.mask.empty() || p.mask[i]) {
                const unsigned c = p.pixels[i] >> shift & 255;
                lo = std::min(lo, c);
                hi = std::max(hi, c);
            }
        range = std::max(range, hi - lo);
    }
    best.valid = range > 24 && best.error < 12 && second > best.error + 0.1f &&
                 std::abs(best.x - expected_x) < radius && std::abs(best.y - expected_y) < radius;
    return best;
}
struct PictureMotionSample {
    // Ground x/y is the cumulative measured translation at a fixed, labeled screen patch.
    // Party x/y is the measured foot anchor, registered separately for each current art key.
    std::array<float, 2> ground{}, party{};
    PixelMatch ground_match{}, party_match{};
    int patch_x = 0, patch_y = 0;
    uint64_t ground_segment = 0;
};
class PictureMotion {
  public:
    void reset() {
        previous_ = {};
        templates_.clear();
        position_ = {};
        segment_ = 0;
    }
    PictureMotionSample measure(std::span<const uint32_t> image, int width, int height, int x, int y,
                                std::array<float, 2> expected_ground_delta, uint64_t art,
                                std::array<float, 2> foot, int scale, std::span<const uint32_t> actor) {
        PictureMotionSample out;
        // 48 output pixels; a fixed patch near the party's ground height, away from its art.
        const int px = std::clamp(x - 24, 16, std::max(16, width - 64));
        const int py = std::clamp(y - 24, 16, std::max(16, height - 64));
        out.patch_x = px;
        out.patch_y = py;
        if (previous_.pixels.empty())
            position_ = {float(x), float(y)};
        if (previous_.x == px && previous_.y == py) {
            out.ground_match =
                match_pixels(previous_, image, width, height, int(std::round(expected_ground_delta[0])),
                             int(std::round(expected_ground_delta[1])));
            if (out.ground_match.valid) {
                position_[0] += out.ground_match.x;
                position_[1] += out.ground_match.y;
            } else {
                position_ = {float(x), float(y)};
                ++segment_;
            }
        }
        out.ground = position_;
        out.ground_segment = segment_;
        previous_ = pixel_patch(image, width, height, px, py, 48, 48);
        if (scale > 0 && actor.size() == 256) {
            auto found = templates_.find(art);
            if (found == templates_.end()) {
                // Register a template with the raster foot used by the renderer. The template retains
                // final RGB (including effects/grid), and only its actor-owned samples participate.
                const int fx = int(std::round(foot[0])), fy = int(std::round(foot[1]));
                auto p = pixel_patch(image, width, height, fx - 8 * scale, fy - 15 * scale, 16 * scale,
                                     16 * scale);
                if (!p.pixels.empty()) {
                    p.mask.resize(p.pixels.size());
                    for (int yy = 0; yy < p.height; ++yy)
                        for (int xx = 0; xx < p.width; ++xx)
                            // The upper half avoids changing terrain occlusion and contact effects at the
                            // feet.
                            p.mask[size_t(yy * p.width + xx)] =
                                yy < 8 * scale && (actor[size_t((yy / scale) * 16 + xx / scale)] >> 24) != 0;
                    found = templates_.emplace(art, Template{std::move(p), {float(fx), float(fy)}}).first;
                }
            } else {
                const auto &t = found->second;
                out.party_match =
                    match_pixels(t.patch, image, width, height, int(std::round(foot[0] - t.foot[0])),
                                 int(std::round(foot[1] - t.foot[1])));
                if (out.party_match.valid)
                    out.party = {t.foot[0] + out.party_match.x, t.foot[1] + out.party_match.y};
            }
            if (templates_.size() > 64)
                templates_.clear();
        }
        return out;
    }

  private:
    struct Template {
        PixelPatch patch;
        std::array<float, 2> foot;
    };
    PixelPatch previous_;
    std::map<uint64_t, Template> templates_;
    std::array<float, 2> position_{};
    uint64_t segment_ = 0;
};
} // namespace sawgod::app
