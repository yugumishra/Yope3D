#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Source.h"
#include "../ui/Tween.h"

class MusicStream;

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

    // Both defined in the .cpp, not defaulted here: an inline-defaulted
    // special member is instantiated at first ODR-use (e.g. Engine.cpp's
    // make_unique<AudioSystem>()), which would need MusicStream's complete
    // type for musicStreams_'s exception-safety/destruction paths — but
    // MusicStream.h is only forward-declared in this header.
    AudioSystem();
    ~AudioSystem();
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

    // Fire-and-forget one-shot (yope3d.play_sound): reuses a finished transient voice
    // (rebinding its buffer + resetting gain/pitch/loop) before allocating a new one,
    // so footstep/impact spam can't exhaust the OpenAL voice pool. The returned handle
    // is valid until the sound stops, after which it may be recycled.
    Source* playTransient(SoundBuffer* buffer);

    // Pause all currently-playing sources, recording which were paused by the system.
    // resumeAll() only resumes those — sources already paused by user code are left alone.
    void pauseAll();
    void resumeAll();

    // Stop all sources without removing them (used on play-mode exit and scene unload).
    void stopAll();

    // Stop and remove a specific source. The pointer becomes invalid after this call.
    void removeSource(Source* src);

    // ---- Mixer buses (Music/SFX/Voice) + master ----
    // Effective AL gain for a source = baseGain * busGain[bus] * masterGain.
    // Source::setGain/refreshGain call this to recompute their live AL_GAIN.
    float effectiveGainFor(Source::Bus bus, float baseGain) const {
        return baseGain * busGain_[static_cast<size_t>(bus)] * masterGain_;
    }
    void setBusGain(Source::Bus bus, float gain);
    void setMasterGain(float gain);

    // ---- Gain fades (used by playMusic's fade_in and manual fade-outs) ----
    void fadeGain(Source* src, float target, float durationSeconds,
                  ui::Ease ease = ui::Ease::Linear);

    // Advances gain fades and refills streaming music buffers. Call once per
    // frame (Engine::update) — there is no other per-frame audio tick.
    void update(float dt);

    // Decode + upload an OGG file without down-mixing to mono (stereo stays
    // stereo); cached separately from loadSound's mono buffers_ cache so the
    // same path can't collide between the two gamma^H^H^H mix modes.
    // For short stereo one-shots; long tracks should use playMusic(stream=true).
    SoundBuffer* loadMusicBuffer(const std::string& path);

    // Non-spatial ("2D") music playback, tagged to the Music bus.
    //   stream=true  -> incremental decode via MusicStream (low memory, for
    //                    long tracks).
    //   stream=false -> full-decode stereo SoundBuffer (loadMusicBuffer), for
    //                    short stingers/jingles where streaming is overkill.
    // fadeInSeconds > 0 ramps gain from 0 to 1 instead of starting at full volume.
    Source* playMusic(const std::string& path, bool loop = false,
                       float fadeInSeconds = 0.0f, bool stream = true);

private:
    ALCdevice*  device_  = nullptr;
    ALCcontext* context_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<SoundBuffer>> buffers_;
    std::unordered_map<std::string, std::unique_ptr<SoundBuffer>> musicBuffers_;
    std::vector<std::unique_ptr<Source>>                          sources_;
    std::vector<std::unique_ptr<MusicStream>>                     musicStreams_;

    struct GainFade {
        Source* src;
        float from, to, duration, elapsed;
        ui::Ease ease;
    };
    std::vector<GainFade> fades_;

    float masterGain_ = 1.0f;
    std::array<float, 3> busGain_ = {1.0f, 1.0f, 1.0f};  // indexed by Source::Bus

    bool initialised_ = false;
};
