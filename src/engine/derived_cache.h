// Disposable, content-addressed derived bytes. Callers include every input and algorithm version in key.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace sawgod {
std::optional<std::vector<uint8_t>> read_derived_cache(const std::string &path, const std::string &key);
void write_derived_cache(const std::string &path, const std::string &key, const std::vector<uint8_t> &bytes);
} // namespace sawgod
