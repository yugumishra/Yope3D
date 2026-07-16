#include "MusicStream.h"
#include "Source.h"
#include "AudioSystem.h"
#include "../assets/AssetResolve.h"

#include <stdexcept>

// Declarations only — the implementation is compiled exactly once, in
// stb_impl.cpp (which doesn't define STB_VORBIS_HEADER_ONLY). This TU only
// needs the declaration section, which is the same physical struct layout as
// that translation unit's.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

MusicStream::MusicStream(const std::string& path, bool loop, AudioSystem* owner)
    : loop_(loop) {
    encoded_ = assets::readBytes(path);
    if (encoded_.empty())
        throw std::runtime_error("MusicStream: failed to load: " + path);

    int error = 0;
    decoder_ = stb_vorbis_open_memory(encoded_.data(), static_cast<int>(encoded_.size()),
                                       &error, nullptr);
    if (!decoder_)
        throw std::runtime_error("MusicStream: failed to open vorbis stream: " + path);

    stb_vorbis_info info = stb_vorbis_get_info(decoder_);
    channels_   = info.channels;
    sampleRate_ = static_cast<int>(info.sample_rate);
    scratch_.resize(static_cast<size_t>(sampleRate_) * channels_);  // ~1s scratch

    source_ = std::make_unique<Source>(0, owner);  // 0 = no static buffer, queued instead
    alGenBuffers(3, bufferIds_);

    // Prime the queue before playback starts so there's no initial underrun.
    for (ALuint id : bufferIds_) {
        if (!decodeChunkInto(id)) break;  // very short/empty file
    }
}

MusicStream::~MusicStream() {
    ALuint id = source_->rawId();
    alSourceStop(id);

    ALint queued = 0;
    alGetSourcei(id, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        ALuint buf = 0;
        alSourceUnqueueBuffers(id, 1, &buf);
    }
    alDeleteBuffers(3, bufferIds_);

    if (decoder_) stb_vorbis_close(decoder_);
    // source_ destructs after this body (stops + deletes the AL source).
}

bool MusicStream::decodeChunkInto(ALuint bufferId) {
    int samplesPerChannel = stb_vorbis_get_samples_short_interleaved(
        decoder_, channels_, scratch_.data(), static_cast<int>(scratch_.size()));

    if (samplesPerChannel == 0) {
        if (!loop_) { finished_ = true; return false; }
        stb_vorbis_seek_start(decoder_);
        samplesPerChannel = stb_vorbis_get_samples_short_interleaved(
            decoder_, channels_, scratch_.data(), static_cast<int>(scratch_.size()));
        if (samplesPerChannel == 0) { finished_ = true; return false; }  // empty file
    }

    ALenum format = channels_ == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    alBufferData(bufferId, format, scratch_.data(),
                 static_cast<ALsizei>(samplesPerChannel * channels_ * sizeof(int16_t)),
                 static_cast<ALsizei>(sampleRate_));

    ALuint id = source_->rawId();
    alSourceQueueBuffers(id, 1, &bufferId);
    return true;
}

bool MusicStream::refill() {
    if (!finished_) {
        ALuint id = source_->rawId();
        ALint processed = 0;
        alGetSourcei(id, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0) {
            ALuint buf = 0;
            alSourceUnqueueBuffers(id, 1, &buf);
            if (!decodeChunkInto(buf)) break;
        }
    }
    // Once the decoder is exhausted and the queued tail has finished playing,
    // there's nothing left to do — tell AudioSystem to drop this stream.
    if (finished_ && !source_->isPlaying()) return false;
    return true;
}
