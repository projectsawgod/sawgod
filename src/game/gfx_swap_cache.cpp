#include "engine/assets.h"
#include "engine/derived_cache.h"
#include "game/gfx_swap.h"
#include "game/maps.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace sawgod::game {
namespace {
struct Reader {
    const std::vector<uint8_t> &bytes;
    size_t at = 0;
    bool ok = true;
    uint32_t word() {
        if (!ok || bytes.size() - at < 4) {
            ok = false;
            return 0;
        }
        uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8)
            value |= uint32_t(bytes[at++]) << shift;
        return value;
    }
};
void word(std::vector<uint8_t> &bytes, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(uint8_t(value >> shift));
}
} // namespace
bool GfxSwap::load_field_maps_cached(const std::map<std::string, std::vector<uint8_t>> &files,
                                     const std::string &cache_path, std::vector<std::string> *errors) {
    const char *code_id =
#include "resource_id.inc"
        ;
    std::string key = std::string(code_id) + ":gb-field-maps-v1:";
    if (!cache_path.empty())
        for (int id = 0; id < kMapCount; ++id)
            for (const auto &name : field_map_files(id)) {
                const auto found = files.find(name);
                key += std::to_string(name.size()) + ":" + name;
                key += found == files.end() ? "missing;"
                                            : to_hex(sha1(found->second.data(), found->second.size()));
            }
    if (auto bytes = read_derived_cache(cache_path, key)) {
        Reader in{*bytes};
        std::map<int, Map> maps;
        const auto count = in.word();
        if (count > kMapCount)
            in.ok = false;
        for (unsigned n = 0; in.ok && n < count; ++n) {
            const auto id = in.word(), tiles = in.word();
            if (!in.ok || id >= kMapCount || tiles == 0 || tiles > 128 ||
                tiles * 256 > bytes->size() - in.at) {
                in.ok = false;
                break;
            }
            Map map;
            map.metatiles.resize(tiles);
            for (auto &tile : map.metatiles) {
                std::copy_n(bytes->begin() + in.at, tile.size(), tile.begin());
                in.at += tile.size();
            }
            for (auto &entry : map.gb) {
                const auto value = in.word();
                if (value != std::numeric_limits<uint32_t>::max() && value >= tiles)
                    in.ok = false;
                entry = value == std::numeric_limits<uint32_t>::max() ? -1 : int(value);
            }
            auto mappings = [&](Lookup &target, uint32_t limit) {
                const auto size = in.word();
                if (size > limit) {
                    in.ok = false;
                    return;
                }
                target.entries.reserve(size);
                for (unsigned i = 0; in.ok && i < size; ++i) {
                    const auto position = in.word(), value = in.word();
                    if (position >= limit || value >= tiles ||
                        !target.append_sorted(int(position), int(value)))
                        in.ok = false;
                }
            };
            mappings(map.by_cell, 65536);
            mappings(map.by_type, 8192);
            if (!maps.emplace(int(id), std::move(map)).second)
                in.ok = false;
        }
        if (in.ok && in.at == bytes->size()) {
            maps_ = std::move(maps);
            if (!maps_.empty())
                install_hooks();
            return true;
        }
    }
    maps_.clear();
    const auto read = [&](const std::string &name) {
        const auto found = files.find(name);
        return found == files.end() ? std::vector<uint8_t>() : found->second;
    };
    bool valid = true;
    for (int id = 0; id < kMapCount; ++id) {
        std::string error;
        load_field_map(id, read, &error);
        if (!error.empty()) {
            valid = false;
            char label[32];
            std::snprintf(label, sizeof label, "GB field map %03d: ", id);
            if (errors)
                errors->push_back(label + error);
        }
    }
    if (valid && !cache_path.empty()) {
        std::vector<uint8_t> bytes;
        word(bytes, uint32_t(maps_.size()));
        for (const auto &[id, map] : maps_) {
            word(bytes, unsigned(id));
            word(bytes, uint32_t(map.metatiles.size()));
            for (const auto &tile : map.metatiles)
                bytes.insert(bytes.end(), tile.begin(), tile.end());
            for (const auto entry : map.gb)
                word(bytes, uint32_t(entry));
            for (const auto *mappings : {&map.by_cell, &map.by_type}) {
                word(bytes, uint32_t(mappings->size()));
                for (const auto &[position, value] : *mappings) {
                    word(bytes, unsigned(position));
                    word(bytes, unsigned(value));
                }
            }
        }
        write_derived_cache(cache_path, key, bytes);
    }
    return false;
}
} // namespace sawgod::game
