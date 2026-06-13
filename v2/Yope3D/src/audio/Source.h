#pragma once
#include <AL/al.h>
#include "../math/Vec3.h"

// ---------------------------------------------------------------------------
// Source
//
// Wraps one OpenAL source. AudioSystem owns Source objects; callers hold
// non-owning Source* pointers.  The underlying AL buffer is owned by
// AudioSystem::SoundBuffer and must outlive any Source that references it.
// ---------------------------------------------------------------------------

class Source {
public:
    explicit Source(ALuint bufferId);
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    void play();
    void pause();
    void stop();
    void rewind();

    void setGain(float gain);
    void setPitch(float pitch);
    void setPosition(math::Vec3 pos);
    void setVelocity(math::Vec3 vel);
    void setReferenceDistance(float dist);
    void enableLooping(bool loop);

    // Rebind the AL buffer (stops the source first). Used to recycle a finished
    // transient voice for a new one-shot instead of allocating another source.
    void setBuffer(ALuint bufferId);

    bool isPlaying() const;

    // Set by AudioSystem::pauseAll(); cleared by resumeAll().
    // Prevents resumeAll() from resuming sources the user deliberately paused.
    bool pausedBySystem = false;

    // True for one-shot voices created by yope.play_sound — eligible for reuse by
    // AudioSystem::playTransient once they stop, so footstep/impact spam can't leak.
    bool transient = false;

private:
    ALuint id_ = 0;
};
