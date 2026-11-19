#include "emu/png.h"

#include <algorithm>
#include <cstdio>

namespace sawgod::emu {

namespace {

uint32_t crc32(const uint8_t *p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void be32(std::vector<uint8_t> &v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8)
        v.push_back(uint8_t(x >> s));
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    be32(out, uint32_t(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(&out[start], out.size() - start));
}

} // namespace

std::vector<uint8_t> encode_png(const uint32_t *pixels, int width, int height) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter: none
        for (int x = 0; x < width; ++x) {
            uint32_t c = pixels[y * width + x];
            raw.push_back(uint8_t(c >> 16));
            raw.push_back(uint8_t(c >> 8));
            raw.push_back(uint8_t(c));
        }
    }
    // zlib stream of stored blocks
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(width));
    be32(ihdr, uint32_t(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

bool write_file(const std::string &path, const std::vector<uint8_t> &data) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace sawgod::emu
