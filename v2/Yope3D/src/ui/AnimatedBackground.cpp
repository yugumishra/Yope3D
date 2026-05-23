#include "AnimatedBackground.h"
#include <cmath>

AnimatedBackground::AnimatedBackground(math::Vec2 min, math::Vec2 max, math::Vec4 tint,
                                       int depth, std::vector<Texture*> frames, float fps)
    : TexturedBackground(min, max, tint, depth, frames.empty() ? nullptr : frames[0]),
      frames_(std::move(frames)), fps_(fps) {}

void AnimatedBackground::update(float dt) {
    if (frames_.empty() || fps_ <= 0.0f) return;
    elapsed_ += dt;
    float frameDur = 1.0f / fps_;
    while (elapsed_ >= frameDur) {
        elapsed_ -= frameDur;
        frame_ = reverse_
            ? (frame_ - 1 + static_cast<int>(frames_.size())) % static_cast<int>(frames_.size())
            : (frame_ + 1) % static_cast<int>(frames_.size());
    }
    texture_ = frames_[frame_];
}
