#pragma once
#include "TexturedBackground.h"
#include <vector>

// ---------------------------------------------------------------------------
// AnimatedBackground — cycles through a sequence of textures at a given fps.
// ---------------------------------------------------------------------------

class AnimatedBackground : public TexturedBackground {
public:
    AnimatedBackground(math::Vec2 min, math::Vec2 max, math::Vec4 tint, int depth,
                       std::vector<Texture*> frames, float fps);
    ~AnimatedBackground() override = default;

    void update(float dt) override;

    void setReverse(bool r) { reverse_ = r; }
    void setFps(float fps)  { fps_ = fps;   }

private:
    std::vector<Texture*> frames_;
    float   fps_     = 12.0f;
    float   elapsed_ = 0.0f;
    int     frame_   = 0;
    bool    reverse_ = false;
};
