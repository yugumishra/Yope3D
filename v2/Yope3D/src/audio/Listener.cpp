#include "Listener.h"
#include <AL/al.h>

void Listener::setPosition(math::Vec3 pos) {
    alListener3f(AL_POSITION, pos.x, pos.y, pos.z);
}

void Listener::setVelocity(math::Vec3 vel) {
    alListener3f(AL_VELOCITY, vel.x, vel.y, vel.z);
}

void Listener::setGain(float gain) {
    alListenerf(AL_GAIN, gain);
}

void Listener::setOrientation(math::Vec3 forward, math::Vec3 up) {
    float buf[6] = { forward.x, forward.y, forward.z, up.x, up.y, up.z };
    alListenerfv(AL_ORIENTATION, buf);
}
