#include "TextAtlas.h"
#include "gpu/GpuDevice.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stb_image_write.h>
#include <cstring>
#include <string>
#include <stdexcept>
#include <iostream>

static constexpr char kFirstChar = ' ';
static constexpr char kLastChar  = '~';

bool TextAtlas::init(GpuDevice& gpu, VkCommandPool commandPool,
                     VkDescriptorPool descriptorPool, VkDescriptorSetLayout textureLayout,
                     const std::string& fontPath, int pixelSize)
{
    // ---------------------------------------------------------------------------
    // 1. Initialize FreeType and load font face
    // ---------------------------------------------------------------------------

    std::string fullPath = std::string(YOPE_ASSETS_DIR) + "/" + fontPath;

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cerr << "[TextAtlas] FreeType init failed\n";
        return false;
    }

    FT_Face face;
    if (FT_New_Face(ft, fullPath.c_str(), 0, &face)) {
        std::cerr << "[TextAtlas] Failed to load font: " << fullPath << "\n";
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));
    pixelSize_  = pixelSize;
    lineHeight_ = static_cast<int>(face->size->metrics.height    >> 6);
    ascender_   = static_cast<int>(face->size->metrics.ascender  >> 6);

    // ---------------------------------------------------------------------------
    // 2. Build atlas pixel buffer (RGBA8, ATLAS_SIZE × ATLAS_SIZE)
    // ---------------------------------------------------------------------------

    pixels_.assign(kAtlasSize * kAtlasSize * 4, 0);
    auto& pixels = pixels_;

    int cursorX = 0, cursorY = 0, lineH = 0;

    for (char c = kFirstChar; c <= kLastChar; ++c) {
        if (FT_Load_Char(face, static_cast<FT_ULong>(c), FT_LOAD_RENDER)) {
            std::cerr << "[TextAtlas] Failed to load glyph '" << c << "'\n";
            continue;
        }

        FT_GlyphSlot g = face->glyph;
        int w = static_cast<int>(g->bitmap.width);
        int h = static_cast<int>(g->bitmap.rows);

        if (cursorX + w + 1 > kAtlasSize) {
            cursorX  = 0;
            cursorY += lineH + 1;
            lineH    = 0;
        }
        if (cursorY + h > kAtlasSize) {
            std::cerr << "[TextAtlas] Atlas full — some glyphs omitted\n";
            break;
        }

        // Copy grayscale bitmap into RGBA8 atlas: RGB=white, A=coverage.
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                uint8_t val = g->bitmap.buffer[row * g->bitmap.pitch + col];
                int idx = ((cursorY + row) * kAtlasSize + (cursorX + col)) * 4;
                pixels[idx + 0] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = val;   // correct: val>0 → visible (old Java had this inverted)
            }
        }

        GlyphInfo info{};
        info.atlasX   = cursorX;
        info.atlasY   = cursorY;
        info.width    = w;
        info.rows     = h;
        info.bearingX = g->bitmap_left;
        info.bearingY = g->bitmap_top;
        info.advance  = static_cast<int>(g->advance.x >> 6);
        glyphs_[c]    = info;

        lineH    = std::max(lineH, h);
        cursorX += w + 1;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // ---------------------------------------------------------------------------
    // 3. Upload atlas to GPU via Texture::load
    // ---------------------------------------------------------------------------

    texture_ = std::make_unique<Texture>(
        Texture::load(gpu, commandPool, textureLayout, descriptorPool,
                      pixels.data(), kAtlasSize, kAtlasSize)
    );

    return true;
}

bool TextAtlas::exportToPNG(const std::string& path) const {
    if (pixels_.empty()) {
        std::cerr << "[TextAtlas] exportToPNG: no pixel data (call after init)\n";
        return false;
    }
    int ok = stbi_write_png(path.c_str(), kAtlasSize, kAtlasSize, 4,
                            pixels_.data(), kAtlasSize * 4);
    if (!ok) {
        std::cerr << "[TextAtlas] exportToPNG: failed to write " << path << "\n";
        return false;
    }
    std::cerr << "[TextAtlas] Exported atlas to " << path << "\n";
    return true;
}

void TextAtlas::destroy(VkDevice device) {
    if (texture_) {
        texture_->destroy(device);
        texture_.reset();
    }
    glyphs_.clear();
}

const GlyphInfo* TextAtlas::glyph(char c) const {
    auto it = glyphs_.find(c);
    if (it == glyphs_.end()) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "[TextAtlas] Unknown glyph '" << c << "' (suppressing further warnings)\n";
            warned = true;
        }
        return nullptr;
    }
    return &it->second;
}
