// Reading .7z archives for the ROM screen (rom_check.h): the 7-Zip project's C decoder (Igor Pavlov, public
// domain; fetched by CMake, tag 26.01), reading from memory.
#include <algorithm>
#include <cstring>

#include "engine/rom_check.h"

extern "C" {
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
}

namespace sawgod {

namespace {

// An ISeekInStream over a byte buffer. `vt` comes first, so the decoder's pointer to it is a pointer to this.
struct MemStream {
    ISeekInStream vt;
    const uint8_t *data;
    size_t size, pos;
};

SRes mem_read(ISeekInStreamPtr p, void *buf, size_t *size) {
    auto *s = reinterpret_cast<MemStream *>(const_cast<ISeekInStream *>(p));
    const size_t n = std::min(*size, s->size - s->pos);
    std::memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    *size = n;
    return SZ_OK;
}

SRes mem_seek(ISeekInStreamPtr p, Int64 *pos, ESzSeek origin) {
    auto *s = reinterpret_cast<MemStream *>(const_cast<ISeekInStream *>(p));
    Int64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? Int64(s->pos) : Int64(s->size);
    const Int64 to = base + *pos;
    if (to < 0 || to > Int64(s->size))
        return SZ_ERROR_PARAM;
    s->pos = size_t(to);
    *pos = to;
    return SZ_OK;
}

std::string utf8(const UInt16 *w) {
    std::string out;
    for (; *w; ++w) {
        uint32_t c = *w;
        if (c >= 0xD800 && c < 0xDC00 && w[1] >= 0xDC00 && w[1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (w[1] - 0xDC00);
            ++w;
        }
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

} // namespace

bool read_7z(const std::vector<uint8_t> &data, std::vector<ArchiveEntry> *entries) {
    static const ISzAlloc alloc = {SzAlloc, SzFree};
    static const ISzAlloc alloc_temp = {SzAllocTemp, SzFreeTemp};
    static const bool crc_ready = [] {
        CrcGenerateTable();
        return true;
    }();
    (void)crc_ready;

    MemStream mem{{mem_read, mem_seek}, data.data(), data.size(), 0};
    CLookToRead2 look;
    LookToRead2_CreateVTable(&look, False);
    constexpr size_t kBuf = size_t(1) << 18;
    std::vector<Byte> buf(kBuf);
    look.buf = buf.data();
    look.bufSize = kBuf;
    look.realStream = &mem.vt;
    LookToRead2_INIT(&look)

        CSzArEx db;
    SzArEx_Init(&db);
    SRes res = SzArEx_Open(&db, &look.vt, &alloc, &alloc_temp);
    UInt32 block = 0xFFFFFFFF;
    Byte *out = nullptr;
    size_t out_size = 0;
    std::vector<UInt16> name;
    for (UInt32 i = 0; res == SZ_OK && i < db.NumFiles; ++i) {
        if (SzArEx_IsDir(&db, i))
            continue;
        const UInt64 size = SzArEx_GetFileSize(&db, i);
        if (size > (UInt64(64) << 20))
            continue; // larger than any WSC ROM
        name.assign(SzArEx_GetFileNameUtf16(&db, i, nullptr), 0);
        SzArEx_GetFileNameUtf16(&db, i, name.data());
        size_t offset = 0, processed = 0;
        res = SzArEx_Extract(&db, &look.vt, i, &block, &out, &out_size, &offset, &processed, &alloc,
                             &alloc_temp);
        if (res == SZ_OK)
            entries->push_back(
                {utf8(name.data()), std::vector<uint8_t>(out + offset, out + offset + processed)});
    }
    ISzAlloc_Free(&alloc, out);
    SzArEx_Free(&db, &alloc);
    return res == SZ_OK;
}

} // namespace sawgod
