#include "AudioLoader.h"
#include "../stb_vorbis_wrapper.h"

#include <cstring>
#include <stdexcept>

LoadedAudio AudioLoader::load(const std::string& absPath) {
    int channels, sampleRate;
    short* output = nullptr;
    int sampleCount = stb_vorbis_decode_filename(absPath.c_str(), &channels, &sampleRate, &output);

    if (sampleCount < 0) {
        throw std::runtime_error("AudioLoader: failed to decode " + absPath);
    }

    if (!output) {
        throw std::runtime_error("AudioLoader: no audio data decoded from " + absPath);
    }

    LoadedAudio result;
    result.sampleRate = sampleRate;
    result.channels = channels;
    result.samples.assign(output, output + (sampleCount * channels));
    free(output);

    return result;
}

LoadedAudio AudioLoader::loadFromMemory(const uint8_t* data, int len) {
    int channels, sampleRate;
    short* output = nullptr;
    int sampleCount = stb_vorbis_decode_memory(data, len, &channels, &sampleRate, &output);

    if (sampleCount < 0) {
        throw std::runtime_error("AudioLoader: failed to decode memory buffer");
    }

    if (!output) {
        throw std::runtime_error("AudioLoader: no audio data decoded from memory buffer");
    }

    LoadedAudio result;
    result.sampleRate = sampleRate;
    result.channels = channels;
    result.samples.assign(output, output + (sampleCount * channels));
    free(output);

    return result;
}
