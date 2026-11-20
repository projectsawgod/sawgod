#include "engine/assets.h"
#include <cstring>

namespace sawgod {
namespace {
uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

} // namespace
Sha1 sha1(const uint8_t *data, size_t size) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> msg(data, data + size);
    uint64_t bits = uint64_t(size) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(uint8_t(bits >> (i * 8)));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(msg[off + i * 4]) << 24 | uint32_t(msg[off + i * 4 + 1]) << 16 |
                   uint32_t(msg[off + i * 4 + 2]) << 8 | msg[off + i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)
                f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40)
                f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60)
                f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else
                f = b ^ c ^ d, k = 0xCA62C1D6;
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    Sha1 out;
    for (int i = 0; i < 20; ++i)
        out[i] = uint8_t(h[i / 4] >> (24 - 8 * (i % 4)));
    return out;
}

std::string to_hex(const Sha1 &digest) {
    static const char *hex = "0123456789abcdef";
    std::string s;
    for (uint8_t b : digest) {
        s += hex[b >> 4];
        s += hex[b & 15];
    }
    return s;
}

} // namespace sawgod
