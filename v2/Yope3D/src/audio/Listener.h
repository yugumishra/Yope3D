#pragma once
#include "../math/Vec3.h"

// ---------------------------------------------------------------------------
// Listener
//
// There is exactly one OpenAL listener (the camera). These free functions
// update its state each frame. Call setOrientation with camera forward + up
// so the 3D audio panning matches the visual view.
// ---------------------------------------------------------------------------

namespace Listener {
    void setPosition(math::Vec3 pos);
    void setVelocity(math::Vec3 vel);
    void setGain(float gain);
    // forward + up — 6 floats sent as AL_ORIENTATION.
    void setOrientation(math::Vec3 forward, math::Vec3 up);
}
