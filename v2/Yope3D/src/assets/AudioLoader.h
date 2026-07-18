#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LoadedAudio {
    std::vector<int16_t> samples;  // interleaved if stereo
    int sampleRate = 0;
    int channels   = 0;            // 1 = mono, 2 = stereo
};

namespace AudioLoader {
    // absPath: full filesystem path to .ogg file.
    // Throws std::runtime_error on failure.
    LoadedAudio load(const std::string& absPath);

    // Embed-mode variant: decode from in-memory buffer.
    LoadedAudio loadFromMemory(const uint8_t* data, int len);
}
