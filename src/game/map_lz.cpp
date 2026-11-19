// Decodes the map data compression used by the original game.
#include "game/map_lz.h"

#include <algorithm>

namespace sawgod::game {

LzUnpacked lz_unpack(const uint8_t *src, size_t max_input, const std::vector<uint8_t> &initial) {
    LzUnpacked out;
    std::vector<uint8_t> ring = initial;
    ring.resize(4096, 0);
    std::vector<bool> written(4096, false);
    size_t pos = 0;
    int left = 0;
    uint8_t buffered = 0;
    auto bit = [&]() -> unsigned {
        if (left == 0) {
            if (pos >= max_input)
                return 0;
            buffered = src[pos++];
            left = 8;
        }
        --left;
        const unsigned b = buffered >> 7;
        buffered = uint8_t(buffered << 1);
        return b;
    };
    auto bits = [&](int n) {
        unsigned v = 0;
        for (int i = 0; i < n; ++i)
            v = v << 1 | bit();
        return v;
    };
    uint16_t cursor = 1;
    auto put = [&](uint8_t v) {
        out.bytes.push_back(v);
        ring[cursor] = v;
        written[cursor] = true;
        cursor = uint16_t((cursor + 1) & 0xFFF);
    };
    for (;;) {
        if (bit()) {
            put(uint8_t(bits(8)));
            continue;
        }
        uint16_t source = uint16_t(bits(12));
        if (source == 0)
            break;
        const unsigned count = bits(4) + 2;
        for (unsigned i = 0; i < count; ++i) {
            out.reads_before |= !written[source];
            put(ring[source]);
            source = uint16_t((source + 1) & 0xFFF);
        }
    }
    out.consumed = pos;
    return out;
}

// The original packer, reconstructed: Okumura's LZSS (binary-tree match search) with a 4 KB ring
// whose cursor starts at 1, matches of 2-17 bytes, an equal-length match found later in the tree replacing an
// earlier one, and position 0 as the tree's empty marker (never searched, so never a copy source). It packs
// every map layer of the ROM to the ROM's exact bytes (sawgod-headless --maps-check), so a map stored as a
// grid unpacks with the game's own end state.
std::vector<uint8_t> lz_pack(const std::vector<uint8_t> &data) {
    constexpr int N = 4096, F = 17, kThreshold = 1, kNil = N;
    std::vector<int> lson(N + 1, kNil), rson(N + 257, kNil), dad(N + 1, kNil);
    std::vector<uint8_t> text(N + F - 1, 0);
    std::vector<uint8_t> out;
    uint8_t acc = 0;
    int nbits = 0;
    auto put = [&](unsigned v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            acc = uint8_t(acc << 1 | (v >> i & 1));
            if (++nbits == 8)
                out.push_back(acc), nbits = 0, acc = 0;
        }
    };
    int mpos = 0, mlen = 0;
    auto insert = [&](int r) {
        if (r == 0) { // the empty marker: not a node
            mlen = 0;
            return;
        }
        int cmp = 1, p = N + 1 + text[size_t(r)];
        rson[size_t(r)] = lson[size_t(r)] = kNil;
        mlen = 0;
        for (;;) {
            if (cmp >= 0) {
                if (rson[size_t(p)] != kNil)
                    p = rson[size_t(p)];
                else {
                    rson[size_t(p)] = r, dad[size_t(r)] = p;
                    return;
                }
            } else {
                if (lson[size_t(p)] != kNil)
                    p = lson[size_t(p)];
                else {
                    lson[size_t(p)] = r, dad[size_t(r)] = p;
                    return;
                }
            }
            int i = 1;
            for (; i < F; ++i)
                if ((cmp = int(text[size_t(r + i)]) - int(text[size_t(p + i)])) != 0)
                    break;
            if (i >= mlen) {
                mpos = p, mlen = i;
                if (mlen >= F)
                    break;
            }
        }
        dad[size_t(r)] = dad[size_t(p)], lson[size_t(r)] = lson[size_t(p)], rson[size_t(r)] = rson[size_t(p)];
        dad[size_t(lson[size_t(p)])] = r, dad[size_t(rson[size_t(p)])] = r;
        if (rson[size_t(dad[size_t(p)])] == p)
            rson[size_t(dad[size_t(p)])] = r;
        else
            lson[size_t(dad[size_t(p)])] = r;
        dad[size_t(p)] = kNil;
    };
    auto erase = [&](int p) {
        if (dad[size_t(p)] == kNil)
            return;
        int q;
        if (rson[size_t(p)] == kNil)
            q = lson[size_t(p)];
        else if (lson[size_t(p)] == kNil)
            q = rson[size_t(p)];
        else {
            q = lson[size_t(p)];
            if (rson[size_t(q)] != kNil) {
                while (rson[size_t(q)] != kNil)
                    q = rson[size_t(q)];
                rson[size_t(dad[size_t(q)])] = lson[size_t(q)], dad[size_t(lson[size_t(q)])] = dad[size_t(q)];
                lson[size_t(q)] = lson[size_t(p)], dad[size_t(lson[size_t(p)])] = q;
            }
            rson[size_t(q)] = rson[size_t(p)], dad[size_t(rson[size_t(p)])] = q;
        }
        dad[size_t(q)] = dad[size_t(p)];
        if (rson[size_t(dad[size_t(p)])] == p)
            rson[size_t(dad[size_t(p)])] = q;
        else
            lson[size_t(dad[size_t(p)])] = q;
        dad[size_t(p)] = kNil;
    };
    int r = 1, s = (1 + F) % N, len = 0;
    size_t idx = 0;
    for (; len < F && idx < data.size(); ++len)
        text[size_t(r + len)] = data[idx++];
    insert(r);
    while (len > 0) {
        if (mlen > len)
            mlen = len;
        if (mlen <= kThreshold) {
            mlen = 1;
            put(1, 1);
            put(text[size_t(r)], 8);
        } else {
            put(0, 1);
            put(unsigned(mpos), 12);
            put(unsigned(mlen - 2), 4);
        }
        const int last = mlen;
        int i = 0;
        for (; i < last && idx < data.size(); ++i) {
            erase(s);
            const uint8_t c = data[idx++];
            text[size_t(s)] = c;
            if (s < F - 1)
                text[size_t(s + N)] = c;
            s = (s + 1) % N, r = (r + 1) % N;
            insert(r);
        }
        for (; i < last; ++i) {
            erase(s);
            s = (s + 1) % N, r = (r + 1) % N;
            if (--len)
                insert(r);
        }
    }
    put(0, 1);
    put(0, 12); // end
    if (nbits)
        put(0, 8 - nbits);
    return out;
}

} // namespace sawgod::game
