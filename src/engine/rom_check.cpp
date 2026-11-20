#include "engine/rom_check.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <miniz.h>

#include "engine/assets.h"

namespace sawgod {

namespace {

bool starts_with(const std::vector<uint8_t> &d, const char *magic, size_t n) {
    return d.size() >= n && std::memcmp(d.data(), magic, n) == 0;
}

bool is_zip(const std::vector<uint8_t> &d) {
    return starts_with(d, "PK\x03\x04", 4) || starts_with(d, "PK\x05\x06", 4);
}
bool is_7z(const std::vector<uint8_t> &d) { return starts_with(d, "7z\xBC\xAF\x27\x1C", 6); }

// Name order when an archive holds several candidates: .wsc first.
int rank(const std::string &name) {
    std::string ext = std::filesystem::path(name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".wsc" ? 0 : ext == ".ws" || ext == ".bin" ? 1 : 2;
}

// Verdict severity when no entry is Ok: the most telling one wins.
int telling(RomVerdict v) {
    switch (v) {
    case RomVerdict::Modified:
        return 3;
    case RomVerdict::GameBoy:
        return 2;
    case RomVerdict::Unsupported:
        return 1;
    default:
        return 0;
    }
}

RomCheck check_entries(std::vector<ArchiveEntry> entries) {
    std::stable_sort(entries.begin(), entries.end(), [](const ArchiveEntry &a, const ArchiveEntry &b) {
        return rank(a.name) < rank(b.name);
    });
    RomCheck best;
    best.verdict = RomVerdict::NoRomInArchive;
    for (ArchiveEntry &e : entries) {
        // nested archives aren't opened; tiny files (readme, nfo) aren't ROMs
        if (e.data.size() < 0x8000 || is_zip(e.data) || is_7z(e.data))
            continue;
        std::string sha1;
        const RomVerdict v = classify_rom(e.data, &sha1);
        const std::string name = std::filesystem::path(e.name).filename().string();
        if (v == RomVerdict::Ok)
            return RomCheck{v, std::move(e.data), name, sha1};
        if (best.verdict == RomVerdict::NoRomInArchive || telling(v) > telling(best.verdict))
            best = RomCheck{v, {}, name, sha1};
    }
    // an archive of only non-ROM-like files is "no ROM inside", not "unsupported"
    if (best.verdict == RomVerdict::Unsupported && rank(best.name) == 2)
        best.verdict = RomVerdict::NoRomInArchive;
    return best;
}

bool read_zip(const std::vector<uint8_t> &data, std::vector<ArchiveEntry> *entries) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0))
        return false;
    bool ok = true;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_uncomp_size > (64u << 20))
            continue; // larger than any WSC ROM
        size_t size = 0;
        void *p = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!p) {
            ok = false;
            break;
        }
        entries->push_back({st.m_filename, std::vector<uint8_t>(static_cast<uint8_t *>(p),
                                                                static_cast<uint8_t *>(p) + size)});
        mz_free(p);
    }
    mz_zip_reader_end(&zip);
    return ok;
}

} // namespace

RomCheck check_rom_bytes(const std::string &name, const std::vector<uint8_t> &data) {
    if (data.empty())
        return RomCheck{RomVerdict::Unreadable, {}, name, {}};
    if (is_zip(data) || is_7z(data)) {
        std::vector<ArchiveEntry> entries;
        if (!(is_zip(data) ? read_zip(data, &entries) : read_7z(data, &entries)))
            return RomCheck{RomVerdict::Unreadable, {}, name, {}};
        return check_entries(std::move(entries));
    }
    RomCheck c;
    c.name = std::filesystem::path(name).filename().string();
    c.verdict = classify_rom(data, &c.sha1);
    if (c.verdict == RomVerdict::Ok)
        c.rom = data;
    return c;
}

RomCheck check_rom_file(const std::string &path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in)
        return RomCheck{RomVerdict::Unreadable, {}, std::filesystem::path(path).filename().string(), {}};
    std::vector<uint8_t> data(std::istreambuf_iterator<char>(in), {});
    return check_rom_bytes(path, data);
}

} // namespace sawgod
