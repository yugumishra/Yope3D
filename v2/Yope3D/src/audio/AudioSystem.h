#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Source.h"

// ---------------------------------------------------------------------------
// AudioSystem
//
// Owns the OpenAL device + context, the sound-buffer cache, and all Source
// objects. Entire OpenAL lifetime lives here: context → buffers → sources
// are all torn down inside cleanup() in the correct order.
//
// Usage:
//   audio->init();
//   SoundBuffer* buf = audio->loadSound("audios/myfile.ogg");  // path relative to assets/
//   Source* src = audio->createSource(buf);
//   src->setPosition({0,0,0}); src->enableLooping(true); src->play();
//   // each frame: Listener::setPosition / setOrientation from Camera
//   audio->cleanup();   // or let destructor handle it
// ---------------------------------------------------------------------------

class AudioSystem {
public:
    // Lightweight RAII wrapper around an ALuint buffer.
    struct SoundBuffer {
        ALuint id = 0;
        SoundBuffer() = default;
        SoundBuffer(SoundBuffer&& o) noexcept : id(o.id) { o.id = 0; }
        SoundBuffer& operator=(SoundBuffer&&) = delete;
        SoundBuffer(const SoundBuffer&) = delete;
        SoundBuffer& operator=(const SoundBuffer&) = delete;
        ~SoundBuffer() { if (id) alDeleteBuffers(1, &id); }
    };

    AudioSystem() = default;
    ~AudioSystem() { cleanup(); }
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Open device + context, set distance model.
    void init();

    // Destroy all sources, then buffers, then context/device.
    void cleanup();

    // Decode + upload an OGG file (deduped by relative asset path).
    // Mirrors loadTexture's YOPE_EMBED_ASSETS / filesystem branching.
    SoundBuffer* loadSound(const std::string& path);

    // Create and own a new Source bound to the given buffer. Returns non-owning ptr.
    Source* createSource(SoundBuffer* buffer);

    // Pause all currently-playing sources, recording which were paused by the system.
    // resumeAll() only resumes those — sources already paused by user code are left alone.
    void pauseAll();
    void resumeAll();

    // Stop and remove a specific source. The pointer becomes invalid after this call.
    void removeSource(Source* src);

private:
    ALCdevice*  device_  = nullptr;
    ALCcontext* context_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<SoundBuffer>> buffers_;
    std::vector<std::unique_ptr<Source>>                          sources_;

    bool initialised_ = false;
};
