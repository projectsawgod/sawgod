// Minimal PNG writer (uncompressed deflate blocks): enough for screenshots and golden captures.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sawgod::emu {

// pixels: width*height values 0x00RRGGBB. Returns the PNG file bytes.
std::vector<uint8_t> encode_png(const uint32_t *pixels, int width, int height);
bool write_file(const std::string &path, const std::vector<uint8_t> &data);

} // namespace sawgod::emu
