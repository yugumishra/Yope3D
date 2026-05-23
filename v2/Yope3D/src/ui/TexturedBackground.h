#pragma once
#include "Background.h"
#include "gpu/Texture.h"

// ---------------------------------------------------------------------------
// TexturedBackground — Background with a texture covering its bounds.
// The texture is modulated by color_ (use {1,1,1,1} for unmodified texture).
// ---------------------------------------------------------------------------

class TexturedBackground : public Background {
public:
    TexturedBackground(math::Vec2 min, math::Vec2 max, math::Vec4 tint, int depth, Texture* tex);
    ~TexturedBackground() override = default;

    void buildMesh(UIBuffer& buf, float screenW, float screenH) override;

    void setTexture(Texture* tex) { texture_ = tex; }

protected:
    Texture* texture_ = nullptr;
};
