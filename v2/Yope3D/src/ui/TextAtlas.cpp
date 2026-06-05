#include "TextAtlas.h"
#include "gpu/GpuDevice.h"
#include "scene/serialization/JsonParser.h"
#include <stb_image.h>
#include <filesystem>
#include <iostream>

namespace {

// "fonts/monaco.ttf" -> ("fonts/generated/monaco.png", "fonts/generated/monaco.json")
// relative to YOPE_ASSETS_DIR. The bake step (tools/msdf_bake.cpp) writes the
// generated atlases into a sibling "generated/" directory keyed by font stem.
void resolveBakedPaths(const std::string& fontPath,
                       std::string& outPng, std::string& outJson) {
    std::filesystem::path p(fontPath);
    std::string stem = p.stem().string();           // "monaco"
    std::filesystem::path dir = p.parent_path();     // "fonts"
    std::filesystem::path gen = dir / "generated";
    outPng  = (gen / (stem + ".png")).string();
    outJson = (gen / (stem + ".json")).string();
}

} // namespace

bool TextAtlas::init(GpuDevice& gpu, VkCommandPool commandPool,
                     VkDescriptorPool descriptorPool, VkDescriptorSetLayout textureLayout,
                     const std::string& fontPath, int /*pixelSize*/)
{
    fontPath_ = fontPath;

    std::string pngRel, jsonRel;
    resolveBakedPaths(fontPath, pngRel, jsonRel);
    std::string pngPath  = std::string(YOPE_ASSETS_DIR) + "/" + pngRel;
    std::string jsonPath = std::string(YOPE_ASSETS_DIR) + "/" + jsonRel;

    // ---------------------------------------------------------------------------
    // 1. Parse glyph layout JSON
    // ---------------------------------------------------------------------------
    JsonNode root;
    try {
        root = parseJsonFile(jsonPath.c_str());
    } catch (const std::exception& e) {
        std::cerr << "[TextAtlas] Failed to parse '" << jsonPath << "': " << e.what()
                  << "\n  (did you run the 'bake_fonts' target? see CMakeLists / tools/msdf_bake.cpp)\n";
        return false;
    }

    const JsonNode& atlas   = root["atlas"];
    const JsonNode& metrics = root["metrics"];
    distanceRange_ = atlas["distanceRange"].asFloat();
    float atlasW   = atlas["width"].asFloat();
    float atlasH   = atlas["height"].asFloat();
    lineHeight_    = metrics["lineHeight"].asFloat();
    ascender_      = metrics["ascender"].asFloat();
    descender_     = metrics["descender"].asFloat();

    for (const JsonNode& g : root["glyphs"].asArray()) {
        int code = g["unicode"].asInt();
        if (code < 0 || code > 127) continue;     // ASCII-only atlas

        GlyphInfo info{};
        info.advance = g["advance"].asFloat();

        if (g.contains("planeBounds") && g.contains("atlasBounds")) {
            const JsonNode& pb = g["planeBounds"];
            info.planeL = pb["left"].asFloat();
            info.planeB = pb["bottom"].asFloat();
            info.planeR = pb["right"].asFloat();
            info.planeT = pb["top"].asFloat();

            const JsonNode& ab = g["atlasBounds"];   // pixels, top-left origin
            info.u0 = ab["left"].asFloat()   / atlasW;
            info.u1 = ab["right"].asFloat()  / atlasW;
            info.v0 = ab["top"].asFloat()    / atlasH;   // v0 = top
            info.v1 = ab["bottom"].asFloat() / atlasH;   // v1 = bottom
            info.hasQuad = true;
        }

        glyphs_[static_cast<char>(code)] = info;
    }

    // ---------------------------------------------------------------------------
    // 2. Load MSDF PNG and upload as a LINEAR (non-sRGB), no-mipmap texture
    // ---------------------------------------------------------------------------
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(pngPath.c_str(), &w, &h, &ch, 4);   // force RGBA
    if (!pixels) {
        std::cerr << "[TextAtlas] Failed to load MSDF PNG '" << pngPath << "'\n";
        return false;
    }

    // generateMipmaps=false → LINEAR filter + CLAMP_TO_EDGE (correct for distance
    // fields); srgb=false → raw UNORM distance values (no gamma decode).
    texture_ = std::make_unique<Texture>(
        Texture::load(gpu, commandPool, textureLayout, descriptorPool,
                      pixels, w, h, /*generateMipmaps=*/false, /*srgb=*/false)
    );

    stbi_image_free(pixels);

    // Confirmation that the MSDF path (not the old coverage atlas) is in use.
    std::cerr << "[TextAtlas] MSDF atlas loaded: " << pngRel
              << " (" << w << "x" << h << "), " << glyphs_.size()
              << " glyphs, distanceRange=" << distanceRange_
              << ", lineHeight(em)=" << lineHeight_ << "  [font=" << fontPath << "]\n";
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
