#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>
#include "gpu/Texture.h"

class GpuDevice;

// ---------------------------------------------------------------------------
// TextAtlas — MSDF (multi-channel signed distance field) glyph atlas.
//
// Loads a font atlas baked OFFLINE by tools/msdf_bake.cpp: a 3-channel MSDF
// PNG + a JSON glyph layout (see assets/fonts/generated/). The atlas is
// scale-free — one atlas renders crisp text at any size — so all metrics are
// stored em-normalized (emSize = 1) rather than in baked pixels.
//
// fontPath is the ORIGINAL .ttf path (e.g. "fonts/monaco.ttf"); the loader
// resolves it to "fonts/generated/monaco.{png,json}".
//
// The PNG is uploaded as a LINEAR (UNORM, non-sRGB) texture so the distance
// values are sampled unmodified. The fragment shader reconstructs coverage via
// median-of-three + screen-space anti-aliasing (state==2 / text3d.frag).
// ---------------------------------------------------------------------------

struct GlyphInfo {
    float advance = 0.0f;                      // pen advance, em units
    bool  hasQuad = false;                      // false for whitespace (advance only)
    float planeL = 0, planeB = 0, planeR = 0, planeT = 0;  // quad rect, em, Y-up, baseline origin
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;       // atlas UVs (v0 = top, v1 = bottom)
};

class TextAtlas {
public:
    TextAtlas() = default;
    ~TextAtlas() = default;

    // Non-copyable, non-movable (owns a Texture with GPU resources).
    TextAtlas(const TextAtlas&) = delete;
    TextAtlas& operator=(const TextAtlas&) = delete;

    // fontPath: original .ttf path relative to YOPE_ASSETS_DIR (e.g. "fonts/monaco.ttf").
    // pixelSize is accepted for call-site compatibility but ignored — the MSDF
    // atlas is resolution-independent.
    bool init(GpuDevice& gpu, VkCommandPool commandPool,
              VkDescriptorPool descriptorPool, VkDescriptorSetLayout textureLayout,
              const std::string& fontPath, int pixelSize = 0);

    void destroy(VkDevice device);

    // Returns nullptr if the character is not in the atlas (logs a warning).
    const GlyphInfo* glyph(char c) const;

    VkDescriptorSet descriptorSet() const { return texture_->getDescriptorSet(); }

    // All metrics em-normalized (emSize = 1).
    float lineHeight()    const { return lineHeight_; }
    float ascender()      const { return ascender_;   }
    float descender()     const { return descender_;  }
    float distanceRange() const { return distanceRange_; }  // atlas texels
    const std::string& fontPath() const { return fontPath_; }

private:
    std::unique_ptr<Texture>             texture_;
    std::unordered_map<char, GlyphInfo>  glyphs_;
    std::string fontPath_;
    float lineHeight_    = 0.0f;
    float ascender_      = 0.0f;
    float descender_     = 0.0f;
    float distanceRange_ = 4.0f;
};
