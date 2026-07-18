#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LoadedImage {
    std::vector<uint8_t> pixels;
    int width    = 0;
    int height   = 0;
    int channels = 0;  // always 4 (RGBA) — forced at load time
};

namespace ImageLoader {
    // absPath: full filesystem path to the image file.
    // flipVertically: pass true to flip rows (matches stbi_set_flip_vertically_on_load).
    // Throws std::runtime_error on failure.
    LoadedImage load(const std::string& absPath, bool flipVertically = false);

    // Embed-mode variant: decode from an in-memory buffer.
    LoadedImage loadFromMemory(const uint8_t* data, int len, bool flipVertically = false);
}
