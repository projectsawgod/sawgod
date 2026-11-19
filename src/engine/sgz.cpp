#include "engine/sgz.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

#include <miniz.h>

#include "engine/persist.h"

namespace sawgod {

namespace {

// The value after "key": in a flat JSON object -- a string or an integer. Enough for the stamp we write.
std::optional<std::string> json_field(const std::string &json, const std::string &key) {
    const std::string quoted = "\"" + key + "\"";
    size_t at = json.find(quoted);
    if (at == std::string::npos)
        return std::nullopt;
    at = json.find(':', at + quoted.size());
    if (at == std::string::npos)
        return std::nullopt;
    ++at;
    while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\n' || json[at] == '\r'))
        ++at;
    if (at >= json.size())
        return std::nullopt;
    if (json[at] == '"') {
        const size_t end = json.find('"', at + 1);
        if (end == std::string::npos)
            return std::nullopt;
        return json.substr(at + 1, end - at - 1);
    }
    size_t end = at;
    while (end < json.size() && (json[end] == '-' || (json[end] >= '0' && json[end] <= '9')))
        ++end;
    if (end == at)
        return std::nullopt;
    return json.substr(at, end - at);
}

std::vector<uint8_t> read_whole(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

} // namespace

std::string sgz_stamp_json(const SgzStamp &stamp) {
    return "{\"kind\": \"" + stamp.kind + "\", \"build\": \"" + stamp.build + "\", \"rom_sha1\": \"" +
           stamp.rom_sha1 + "\"}\n";
}

std::optional<SgzStamp> parse_sgz_stamp(const std::string &json) {
    const auto kind = json_field(json, "kind");
    const auto build = json_field(json, "build");
    const auto sha1 = json_field(json, "rom_sha1");
    if (!kind || !build || !sha1 || kind->empty() || build->empty() || sha1->size() != 40)
        return std::nullopt;
    return SgzStamp{*kind, *build, *sha1};
}

bool write_sgz(const std::string &path, const SgzStamp &stamp, const SgzFiles &files, std::string *error) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 20)) {
        if (error)
            *error = "cannot start a zip in memory";
        return false;
    }
    bool ok = true;
    const auto add = [&](const std::string &name, const void *data, size_t size) {
        if (ok && !mz_zip_writer_add_mem(&zip, name.c_str(), data, size, MZ_BEST_SPEED)) {
            ok = false;
            if (error)
                *error = path + ": cannot add " + name + " (" +
                         mz_zip_get_error_string(mz_zip_get_last_error(&zip)) + ")";
        }
    };
    const std::string json = sgz_stamp_json(stamp);
    add(kSgzStampFile, json.data(), json.size());
    for (const auto &[name, data] : files)
        if (name != kSgzStampFile)
            add(name, data.data(), data.size());
    void *buffer = nullptr;
    size_t size = 0;
    if (ok && !mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
        ok = false;
        if (error)
            *error = path + ": cannot finish the zip";
    }
    if (ok)
        ok = WriteFileAtomic(path, buffer, size, error);
    mz_zip_writer_end(&zip); // frees the heap buffer too
    return ok;
}

std::optional<SgzFiles> read_sgz(const std::string &path, std::string *error) {
    const std::vector<uint8_t> data = read_whole(path);
    mz_zip_archive zip{};
    if (data.empty() || !mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0)) {
        if (error)
            *error = path + ": not a readable archive";
        return std::nullopt;
    }
    SgzFiles files;
    bool ok = true;
    for (mz_uint i = 0; ok && i < mz_zip_reader_get_num_files(&zip); ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;
        char name[1024];
        mz_zip_reader_get_filename(&zip, i, name, sizeof name);
        size_t size = 0;
        void *p = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!p) {
            ok = false;
            if (error)
                *error = path + ": cannot read " + name;
            break;
        }
        files[name].assign(static_cast<uint8_t *>(p), static_cast<uint8_t *>(p) + size);
        mz_free(p);
    }
    mz_zip_reader_end(&zip);
    if (!ok)
        return std::nullopt;
    return files;
}

std::optional<SgzStamp> read_sgz_stamp(const std::string &path) {
    const std::vector<uint8_t> data = read_whole(path);
    mz_zip_archive zip{};
    if (data.empty() || !mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0))
        return std::nullopt;
    std::optional<SgzStamp> stamp;
    size_t size = 0;
    if (void *p = mz_zip_reader_extract_file_to_heap(&zip, kSgzStampFile, &size, 0)) {
        stamp = parse_sgz_stamp(std::string(static_cast<char *>(p), size));
        mz_free(p);
    }
    // the stamp alone isn't proof the rest is intact: check every entry's CRC too
    if (stamp && !mz_zip_validate_archive(&zip, 0))
        stamp.reset();
    mz_zip_reader_end(&zip);
    return stamp;
}

} // namespace sawgod
