#pragma once
#include <AL/al.h>
#include "../math/Vec3.h"

class AudioSystem;

// ---------------------------------------------------------------------------
// Source
//
// Wraps one OpenAL source. AudioSystem owns Source objects; callers hold
// non-owning Source* pointers.  The underlying AL buffer is owned by
// AudioSystem::SoundBuffer and must outlive any Source that references it.
// ---------------------------------------------------------------------------

class Source {
public:
    // Volume-group tag for the AudioSystem mixer buses (see setBus()).
    enum class Bus { Music = 0, SFX = 1, Voice = 2 };

    // bufferId == 0 means "no buffer attached yet" — used by streaming
    // sources (MusicStream), which feed the source via alSourceQueueBuffers
    // instead of AL_BUFFER.
    Source(ALuint bufferId, AudioSystem* owner);
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    void play();
    void pause();
    void stop();
    void rewind();

    // Sets the source's base (pre-mix) gain and recomputes the live AL gain
    // as base * busGain * masterGain via the owning AudioSystem.
    void setGain(float gain);
    void setPitch(float pitch);
    void setPosition(math::Vec3 pos);
    void setVelocity(math::Vec3 vel);
    void setReferenceDistance(float dist);
    void enableLooping(bool loop);

    // Non-spatial ("2D") playback: position/distance attenuation/Doppler are
    // ignored; used for music and UI sounds. Position stays at the origin.
    void setRelative(bool relative);

    // Assigns this source's mixer bus and immediately recomputes AL_GAIN.
    void setBus(Bus bus);
    Bus  getBus() const { return bus_; }
    float baseGain() const { return baseGain_; }

    // Recomputes AL_GAIN from baseGain_ via owner_->effectiveGainFor(bus_, ...).
    // Called by AudioSystem when a bus/master gain changes.
    void refreshGain();

    // Rebind the AL buffer (stops the source first). Used to recycle a finished
    // transient voice for a new one-shot instead of allocating another source.
    void setBuffer(ALuint bufferId);

    bool isPlaying() const;

    // Raw AL source name — for MusicStream's direct alSourceQueueBuffers/
    // alGetSourcei calls, which don't have a Source-level equivalent.
    ALuint rawId() const { return id_; }

    // Set by AudioSystem::pauseAll(); cleared by resumeAll().
    // Prevents resumeAll() from resuming sources the user deliberately paused.
    bool pausedBySystem = false;

    // True for one-shot voices created by yope3d.play_sound — eligible for reuse by
    // AudioSystem::playTransient once they stop, so footstep/impact spam can't leak.
    bool transient = false;

private:
    ALuint       id_ = 0;
    AudioSystem* owner_ = nullptr;
    float        baseGain_ = 1.0f;
    Bus          bus_ = Bus::SFX;
};
