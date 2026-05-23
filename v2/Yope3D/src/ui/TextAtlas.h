#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include "gpu/Texture.h"

class GpuDevice;

// ---------------------------------------------------------------------------
// TextAtlas — FreeType-backed glyph atlas.
//
// Renders the full printable ASCII range (' ' through '~') into a 512×512
// RGBA8 texture.  Pixels are (255,255,255,coverage) so text can be tinted
// via vertex color in the UI shader (state = 2: use texture alpha as mask).
//
// Metrics are stored in screen pixels at the requested pixelSize.
// ---------------------------------------------------------------------------

struct GlyphInfo {
    int atlasX, atlasY;   // top-left corner in the 512×512 atlas (pixels)
    int width, rows;      // bitmap dimensions (pixels)
    int bearingX;         // horizontal offset from pen to glyph left edge
    int bearingY;         // vertical offset from baseline to glyph top
    int advance;          // pen advance after this glyph (pixels)
};

class TextAtlas {
public:
    static constexpr int kAtlasSize = 1024;

    TextAtlas() = default;
    ~TextAtlas() = default;

    // Non-copyable, non-movable (owns a Texture with GPU resources).
    TextAtlas(const TextAtlas&) = delete;
    TextAtlas& operator=(const TextAtlas&) = delete;

    // fontPath: path relative to YOPE_ASSETS_DIR (e.g. "fonts/roboto.ttf").
    // pixelSize: render height in pixels.
    bool init(GpuDevice& gpu, VkCommandPool commandPool,
              VkDescriptorPool descriptorPool, VkDescriptorSetLayout textureLayout,
              const std::string& fontPath, int pixelSize);

    void destroy(VkDevice device);

    // Returns nullptr if the character is not in the atlas (logs a warning).
    const GlyphInfo* glyph(char c) const;

    // Debug: write the atlas pixels to a PNG file (path relative to cwd).
    bool exportToPNG(const std::string& path) const;

    VkDescriptorSet descriptorSet() const { return texture_->getDescriptorSet(); }
    int pixelSize()  const { return pixelSize_;  }
    int lineHeight() const { return lineHeight_; }
    int ascender()   const { return ascender_;   }

private:
    std::unique_ptr<Texture>             texture_;
    std::unordered_map<char, GlyphInfo>  glyphs_;
    std::vector<uint8_t>                 pixels_;   // retained for exportToPNG
    int pixelSize_  = 0;
    int lineHeight_ = 0;
    int ascender_   = 0;
};
