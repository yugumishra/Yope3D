#pragma once
#include <AL/al.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct stb_vorbis;
class Source;
class AudioSystem;

// ---------------------------------------------------------------------------
// MusicStream
//
// Incrementally decodes a Vorbis file into a small ring of queued OpenAL
// buffers so a multi-minute track never needs its full PCM resident — only
// the compressed file bytes (a few MB) stay in memory, decoded a ~1-second
// chunk at a time via stb_vorbis's pull API. Owns its Source (stereo,
// AL_SOURCE_RELATIVE) and the decoder; AudioSystem owns the MusicStream
// itself and drives refill() once per frame from AudioSystem::update().
// ---------------------------------------------------------------------------

class MusicStream {
public:
    MusicStream(const std::string& path, bool loop, AudioSystem* owner);
    ~MusicStream();
    MusicStream(const MusicStream&) = delete;
    MusicStream& operator=(const MusicStream&) = delete;

    // Tops up any processed buffers with freshly-decoded PCM. Returns false
    // once a non-looping stream has fully drained and stopped playing —
    // AudioSystem drops the MusicStream when this happens.
    bool refill();

    Source* source() const { return source_.get(); }
    bool ownsSource(Source* s) const { return source_.get() == s; }

private:
    bool decodeChunkInto(ALuint bufferId);

    std::vector<uint8_t>    encoded_;       // compressed file bytes, resident
    stb_vorbis*              decoder_ = nullptr;
    std::unique_ptr<Source>  source_;
    ALuint                   bufferIds_[3] = {0, 0, 0};
    std::vector<int16_t>     scratch_;      // ~1s decode scratch buffer
    int                       channels_ = 0;
    int                       sampleRate_ = 0;
    bool                      loop_ = false;
    bool                      finished_ = false;  // decoder exhausted (non-looping)
};
