#include "engine/derived_cache.h"
#include "engine/assets.h"
#include "engine/persist.h"
#include <fstream>
namespace sawgod {
namespace {
std::string header(const std::string &key) {
    return "sawgod-derived-v1\n" + to_hex(sha1(reinterpret_cast<const uint8_t *>(key.data()), key.size())) +
           "\n";
}
} // namespace
std::optional<std::vector<uint8_t>> read_derived_cache(const std::string &path, const std::string &key) {
    if (path.empty())
        return std::nullopt;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    const auto prefix = header(key);
    const size_t offset = prefix.size() + 41;
    if (!in || in.tellg() < std::streamoff(offset) || in.tellg() > 64 * 1024 * 1024)
        return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
        return std::nullopt;
    if (std::string(bytes.begin(), bytes.begin() + prefix.size()) != prefix)
        return std::nullopt;
    const auto digest = to_hex(sha1(bytes.data() + offset, bytes.size() - offset));
    if (std::string(bytes.begin() + prefix.size(), bytes.begin() + offset) != digest + "\n")
        return std::nullopt;
    return std::vector<uint8_t>(bytes.begin() + offset, bytes.end());
}
void write_derived_cache(const std::string &path, const std::string &key, const std::vector<uint8_t> &bytes) {
    if (path.empty())
        return;
    const auto prefix = header(key) + to_hex(sha1(bytes.data(), bytes.size())) + "\n";
    std::vector<uint8_t> out(prefix.begin(), prefix.end());
    out.insert(out.end(), bytes.begin(), bytes.end());
    WriteFileAtomic(path, out, nullptr);
}
} // namespace sawgod
