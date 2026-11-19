#include "engine/indexed_png.h"

#include <miniz.h>

#include <cstdlib>
#include <cstring>

namespace sawgod {

namespace {

void put32(std::vector<uint8_t> &out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
        out.push_back(uint8_t(v >> s));
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    put32(out, uint32_t(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, uint32_t(mz_crc32(MZ_CRC32_INIT, out.data() + start, out.size() - start)));
}

uint32_t get32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint8_t nearest(const std::vector<uint32_t> &palette, int r, int g, int b) {
    int best = 0, dist = 1 << 30;
    for (size_t i = 0; i < palette.size(); ++i) {
        const int dr = int(palette[i] >> 16 & 0xFF) - r, dg = int(palette[i] >> 8 & 0xFF) - g,
                  db = int(palette[i] & 0xFF) - b, d = dr * dr + dg * dg + db * db;
        if (d < dist)
            dist = d, best = int(i);
    }
    return uint8_t(best);
}

} // namespace

std::vector<uint8_t> encode_indexed_png(const IndexedImage &image, const std::vector<uint32_t> &palette) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(image.width));
    put32(ihdr, uint32_t(image.height));
    ihdr.insert(ihdr.end(), {8, 3, 0, 0, 0}); // 8-bit, palette, deflate, no filter method, not interlaced
    chunk(out, "IHDR", ihdr);
    std::vector<uint8_t> plte;
    for (uint32_t c : palette)
        plte.insert(plte.end(), {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)});
    chunk(out, "PLTE", plte);
    std::vector<uint8_t> raw;
    raw.reserve(size_t(image.height) * (size_t(image.width) + 1));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), image.pixels.begin() + ptrdiff_t(y) * image.width,
                   image.pixels.begin() + ptrdiff_t(y + 1) * image.width);
    }
    mz_ulong size = mz_compressBound(mz_ulong(raw.size()));
    std::vector<uint8_t> idat(size);
    mz_compress2(idat.data(), &size, raw.data(), mz_ulong(raw.size()), 9);
    idat.resize(size);
    chunk(out, "IDAT", idat);
    chunk(out, "IEND", {});
    return out;
}

static bool decode_png(const std::vector<uint8_t> &png, const std::vector<uint32_t> &palette,
                       IndexedImage *image, RgbaImage *rgba, std::string *error, int transparent) {
    auto fail = [&](const char *why) {
        if (error)
            *error = why;
        return false;
    };
    static const uint8_t kSig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (png.size() < 8 || std::memcmp(png.data(), kSig, 8) != 0)
        return fail("not a PNG");
    int width = 0, height = 0, depth = 0, type = -1, interlace = 0;
    std::vector<uint8_t> idat, plte;
    for (size_t p = 8; p + 12 <= png.size();) {
        const uint32_t len = get32(&png[p]);
        if (p + 12 + len > png.size())
            return fail("truncated PNG");
        const char *t = reinterpret_cast<const char *>(&png[p + 4]);
        const uint8_t *d = &png[p + 8];
        if (std::memcmp(t, "IHDR", 4) == 0 && len >= 13) {
            width = int(get32(d)), height = int(get32(d + 4)), depth = d[8], type = d[9], interlace = d[12];
        } else if (std::memcmp(t, "PLTE", 4) == 0) {
            plte.assign(d, d + len);
        } else if (std::memcmp(t, "IDAT", 4) == 0) {
            idat.insert(idat.end(), d, d + len);
        } else if (std::memcmp(t, "IEND", 4) == 0) {
            break;
        }
        p += 12 + len;
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return fail("bad PNG size");
    if (interlace)
        return fail("interlaced PNG (save it without interlacing)");
    const int channels = type == 0 || type == 3 ? 1 : type == 4 ? 2 : type == 2 ? 3 : type == 6 ? 4 : 0;
    if (!channels || (type != 0 && type != 3 && depth != 8) ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8))
        return fail("unsupported PNG color type or bit depth");
    if (rgba && (type != 6 || depth != 8))
        return fail("overlay must be RGBA8");
    const size_t bpp = size_t((channels * depth + 7) / 8),
                 stride = size_t((width * channels * depth + 7) / 8);
    std::vector<uint8_t> raw(size_t(height) * (stride + 1));
    mz_ulong size = mz_ulong(raw.size());
    if (mz_uncompress(raw.data(), &size, idat.data(), mz_ulong(idat.size())) != MZ_OK || size != raw.size())
        return fail("bad PNG data");
    std::vector<uint8_t> prev(stride, 0), cur(stride);
    if (rgba) {
        rgba->width = width, rgba->height = height;
        rgba->pixels.assign(size_t(width) * size_t(height), 0);
    } else {
        image->width = width, image->height = height;
        image->pixels.assign(size_t(width) * size_t(height), 0);
    }
    for (int y = 0; y < height; ++y) {
        const uint8_t filter = raw[size_t(y) * (stride + 1)];
        const uint8_t *src = &raw[size_t(y) * (stride + 1) + 1];
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            const int pred = filter == 1   ? a
                             : filter == 2 ? b
                             : filter == 3 ? (a + b) / 2
                             : filter == 4 ? paeth(a, b, c)
                                           : 0;
            if (filter > 4)
                return fail("bad PNG filter");
            cur[i] = uint8_t(src[i] + pred);
        }
        for (int x = 0; x < width; ++x) {
            if (rgba) {
                const auto *p = &cur[size_t(x) * 4];
                rgba->pixels[size_t(y) * size_t(width) + size_t(x)] =
                    uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
                continue;
            }
            uint8_t v;
            if (depth < 8) {
                const int per = 8 / depth, bit = (per - 1 - x % per) * depth;
                v = uint8_t(cur[size_t(x / per)] >> bit & ((1 << depth) - 1));
                if (type == 0) // grayscale below 8 bits: scale to 0-255
                    v = uint8_t(v * 255 / ((1 << depth) - 1));
            } else {
                v = cur[size_t(x) * size_t(channels)];
            }
            uint8_t index;
            if (type == 3) {
                index = v;
            } else if (transparent >= 0 && (type == 4 || type == 6) &&
                       cur[size_t(x) * size_t(channels) + size_t(channels - 1)] < 128) {
                index = uint8_t(transparent);
            } else if (type == 0 || type == 4) {
                index = nearest(palette, v, v, v);
            } else {
                const uint8_t *px = &cur[size_t(x) * size_t(channels)];
                index = nearest(palette, px[0], px[1], px[2]);
            }
            image->pixels[size_t(y) * size_t(width) + size_t(x)] = index;
        }
        std::swap(prev, cur);
    }
    return true;
}

bool decode_indexed_png(const std::vector<uint8_t> &png, const std::vector<uint32_t> &palette,
                        IndexedImage *image, std::string *error, int transparent) {
    return decode_png(png, palette, image, nullptr, error, transparent);
}
bool decode_rgba_png(const std::vector<uint8_t> &png, RgbaImage *image, std::string *error) {
    return decode_png(png, {}, nullptr, image, error, -1);
}

} // namespace sawgod
