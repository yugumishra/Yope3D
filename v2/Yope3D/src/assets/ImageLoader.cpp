#include "ImageLoader.h"

#include <stb_image.h>
#include <stdexcept>
#include <mutex>

namespace {
// stbi_set_flip_vertically_on_load is a process-global flag — guard it so
// TextureStreamer's worker thread can decode concurrently with any main-thread
// ImageLoader call without racing another thread's flip setting.
std::mutex g_stbiMutex;
}

LoadedImage ImageLoader::load(const std::string& absPath, bool flipVertically) {
    std::lock_guard<std::mutex> lk(g_stbiMutex);
    stbi_set_flip_vertically_on_load(flipVertically);

    int w, h, ch;
    uint8_t* pixels = stbi_load(absPath.c_str(), &w, &h, &ch, 4);

    stbi_set_flip_vertically_on_load(false);

    if (!pixels) {
        throw std::runtime_error("ImageLoader: failed to load " + absPath + " — " + stbi_failure_reason());
    }

    LoadedImage result;
    result.width = w;
    result.height = h;
    result.channels = 4;
    result.pixels.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);

    return result;
}

LoadedImage ImageLoader::loadFromMemory(const uint8_t* data, int len, bool flipVertically) {
    std::lock_guard<std::mutex> lk(g_stbiMutex);
    stbi_set_flip_vertically_on_load(flipVertically);

    int w, h, ch;
    uint8_t* pixels = stbi_load_from_memory(data, len, &w, &h, &ch, 4);

    stbi_set_flip_vertically_on_load(false);

    if (!pixels) {
        throw std::runtime_error("ImageLoader: failed to decode memory buffer — " + std::string(stbi_failure_reason()));
    }

    LoadedImage result;
    result.width = w;
    result.height = h;
    result.channels = 4;
    result.pixels.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);

    return result;
}
