#include "Source.h"

Source::Source(ALuint bufferId) {
    alGenSources(1, &id_);
    alSourcei(id_, AL_BUFFER,           static_cast<ALint>(bufferId));
    alSourcei(id_, AL_SOURCE_RELATIVE,  AL_FALSE);   // absolute world-space position
    alSourcef(id_, AL_REFERENCE_DISTANCE, 1.0f);     // full volume within 1 unit
    alSourcef(id_, AL_ROLLOFF_FACTOR,     1.0f);
    alSourcef(id_, AL_MAX_DISTANCE,       1e9f);
}

Source::~Source() {
    if (id_) {
        alSourceStop(id_);
        alDeleteSources(1, &id_);
    }
}

void Source::play()   { alSourcePlay(id_);   }
void Source::pause()  { alSourcePause(id_);  }
void Source::stop()   { alSourceStop(id_);   }
void Source::rewind() { alSourceRewind(id_); }

void Source::setGain(float gain)   { alSourcef(id_, AL_GAIN,  gain);  }
void Source::setPitch(float pitch) { alSourcef(id_, AL_PITCH, pitch); }

void Source::setPosition(math::Vec3 pos) {
    alSource3f(id_, AL_POSITION, pos.x, pos.y, pos.z);
}
void Source::setVelocity(math::Vec3 vel) {
    alSource3f(id_, AL_VELOCITY, vel.x, vel.y, vel.z);
}

void Source::setReferenceDistance(float dist) {
    alSourcef(id_, AL_REFERENCE_DISTANCE, dist);
}

void Source::enableLooping(bool loop) {
    alSourcei(id_, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
}

void Source::setBuffer(ALuint bufferId) {
    alSourceStop(id_);   // AL_BUFFER can only be set on a non-playing source
    alSourcei(id_, AL_BUFFER, static_cast<ALint>(bufferId));
}

bool Source::isPlaying() const {
    ALint state = AL_STOPPED;
    alGetSourcei(id_, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}
