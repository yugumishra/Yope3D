// ---------------------------------------------------------------------------
// msdf_bake — offline MSDF font-atlas generator (developer tool).
//
// Builds a multi-channel signed-distance-field atlas (PNG) + glyph-layout (JSON)
// from a .ttf, using the msdfgen library. Run via the CMake `bake_fonts` target
// (gated behind -DYOPE_BAKE_FONTS=ON); the engine never depends on this tool —
// it loads the committed PNG+JSON at runtime (see src/ui/TextAtlas).
//
//   msdf_bake <font.ttf> <out.png> <out.json> [bakeSize=48] [pxRange=4]
//
// Conventions baked into the output (loader must match):
//   - planeBounds: em units, Y-UP, baseline at origin (the padded glyph quad).
//   - atlasBounds: pixels, TOP-LEFT origin (top = smaller row); V increases down.
//   - advance / metrics: em units (emSize = 1).
//   - distanceRange: in atlas texels (= pxRange) — feeds the shader screenPxRange.
// ---------------------------------------------------------------------------

#include <msdfgen.h>
#include <msdfgen-ext.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace msdfgen;

namespace {

struct BakedGlyph {
    unsigned code     = 0;
    double   advance  = 0.0;   // em
    bool     hasQuad  = false;
    double   pl, pb, pr, pt;   // plane bounds (em, Y-up, baseline origin)
    int      ax = 0, ay = 0;   // atlas placement (px, top-left origin)
    int      aw = 0, ah = 0;   // glyph bitmap dims (px)
    std::vector<float> px;     // RGB float, row-major, top-down
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
            "usage: msdf_bake <font.ttf> <out.png> <out.json> [bakeSize=48] [pxRange=4]\n");
        return 2;
    }
    const char* fontPath = argv[1];
    const char* outPng   = argv[2];
    const char* outJson  = argv[3];
    const int    bakeSize = argc > 4 ? std::atoi(argv[4]) : 48;
    const double pxRange  = argc > 5 ? std::atof(argv[5]) : 4.0;
    const int    atlasW   = 512;
    const int    pad      = 1;

    FreetypeHandle* ft = initializeFreetype();
    if (!ft) { std::fprintf(stderr, "msdf_bake: FreeType init failed\n"); return 1; }
    FontHandle* font = loadFont(ft, fontPath);
    if (!font) {
        std::fprintf(stderr, "msdf_bake: cannot load font '%s'\n", fontPath);
        deinitializeFreetype(ft);
        return 1;
    }

    FontMetrics fm{};
    getFontMetrics(fm, font, FONT_SCALING_EM_NORMALIZED);

    const double rangeEm = pxRange / bakeSize;   // distance range in em
    const double margin  = 0.5 * rangeEm;        // padding around the glyph each side

    // --- Generate per-glyph MSDF bitmaps -------------------------------------
    // Printable ASCII, then Latin-1 Supplement + Latin Extended-A ("Western +
    // Central European" coverage: accents, ñ, ø, ł, ...). The gap between the
    // two skips DEL (0x7F) and the C1 control block (0x80-0x9F), which have no
    // printable form. A codepoint the font doesn't carry is simply skipped by
    // the loadGlyph check below, so this range is safe for any Latin font.
    static constexpr struct { unsigned first, last; } kRanges[] = {
        { 0x20, 0x7E },
        { 0xA0, 0x17F },
    };

    std::vector<BakedGlyph> glyphs;
    for (const auto& range : kRanges)
    for (unsigned c = range.first; c <= range.last; ++c) {
        Shape shape;
        double advance = 0.0;
        if (!loadGlyph(shape, font, c, FONT_SCALING_EM_NORMALIZED, &advance)) continue;

        BakedGlyph g;
        g.code = c;
        g.advance = advance;

        if (shape.contours.empty()) {        // whitespace: advance only, no quad
            glyphs.push_back(std::move(g));
            continue;
        }

        shape.normalize();
        edgeColoringSimple(shape, 3.0);
        Shape::Bounds b = shape.getBounds();

        const double bxl = b.l - margin, bxr = b.r + margin;
        const double byb = b.b - margin, byt = b.t + margin;
        const int w = static_cast<int>(std::ceil((bxr - bxl) * bakeSize));
        const int h = static_cast<int>(std::ceil((byt - byb) * bakeSize));
        if (w <= 0 || h <= 0) { glyphs.push_back(std::move(g)); continue; }

        Bitmap<float, 3> msdf(w, h);
        generateMSDF(msdf, shape, Range(rangeEm),
                     Vector2(bakeSize, bakeSize), Vector2(-bxl, -byb));

        g.hasQuad = true;
        g.aw = w; g.ah = h;
        g.pl = bxl; g.pr = bxr; g.pb = byb; g.pt = byt;
        g.px.resize(static_cast<size_t>(w) * h * 3);
        // msdfgen bitmaps are Y-UP (row 0 = bottom); store top-down.
        for (int r = 0; r < h; ++r) {
            const int sy = h - 1 - r;
            for (int x = 0; x < w; ++x) {
                const float* p = msdf(x, sy);
                float* d = &g.px[(static_cast<size_t>(r) * w + x) * 3];
                d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
            }
        }
        glyphs.push_back(std::move(g));
    }

    destroyFont(font);
    deinitializeFreetype(ft);

    // --- Shelf-pack into a fixed-width atlas ---------------------------------
    int cursorX = pad, cursorY = pad, shelfH = 0;
    for (auto& g : glyphs) {
        if (!g.hasQuad) continue;
        if (cursorX + g.aw + pad > atlasW) { cursorX = pad; cursorY += shelfH + pad; shelfH = 0; }
        g.ax = cursorX;
        g.ay = cursorY;
        cursorX += g.aw + pad;
        shelfH = std::max(shelfH, g.ah);
    }
    const int atlasH = cursorY + shelfH + pad;

    // --- Composite atlas (top-down RGB8); 0 = far-outside = transparent ------
    std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * atlasH * 3, 0);
    auto toByte = [](float f) -> uint8_t {
        f = std::min(1.0f, std::max(0.0f, f));
        return static_cast<uint8_t>(std::lround(f * 255.0f));
    };
    for (const auto& g : glyphs) {
        if (!g.hasQuad) continue;
        for (int r = 0; r < g.ah; ++r) {
            for (int x = 0; x < g.aw; ++x) {
                const float* s = &g.px[(static_cast<size_t>(r) * g.aw + x) * 3];
                uint8_t* d = &atlas[((static_cast<size_t>(g.ay + r) * atlasW) + (g.ax + x)) * 3];
                d[0] = toByte(s[0]); d[1] = toByte(s[1]); d[2] = toByte(s[2]);
            }
        }
    }

    if (!stbi_write_png(outPng, atlasW, atlasH, 3, atlas.data(), atlasW * 3)) {
        std::fprintf(stderr, "msdf_bake: failed to write PNG '%s'\n", outPng);
        return 1;
    }

    // --- JSON layout ---------------------------------------------------------
    std::ofstream js(outJson);
    if (!js) { std::fprintf(stderr, "msdf_bake: failed to write JSON '%s'\n", outJson); return 1; }
    js.setf(std::ios::fixed);
    js.precision(6);
    js << "{\n";
    js << "  \"atlas\": { \"distanceRange\": " << pxRange
       << ", \"size\": " << bakeSize
       << ", \"width\": " << atlasW
       << ", \"height\": " << atlasH
       << ", \"yOrigin\": \"top\" },\n";
    js << "  \"metrics\": { \"emSize\": 1.0"
       << ", \"lineHeight\": " << fm.lineHeight
       << ", \"ascender\": " << fm.ascenderY
       << ", \"descender\": " << fm.descenderY << " },\n";
    js << "  \"glyphs\": [\n";
    for (size_t i = 0; i < glyphs.size(); ++i) {
        const auto& g = glyphs[i];
        js << "    { \"unicode\": " << g.code << ", \"advance\": " << g.advance;
        if (g.hasQuad) {
            js << ", \"planeBounds\": { \"left\": " << g.pl << ", \"bottom\": " << g.pb
               << ", \"right\": " << g.pr << ", \"top\": " << g.pt << " }";
            js << ", \"atlasBounds\": { \"left\": " << g.ax << ", \"top\": " << g.ay
               << ", \"right\": " << (g.ax + g.aw) << ", \"bottom\": " << (g.ay + g.ah) << " }";
        }
        js << " }" << (i + 1 < glyphs.size() ? "," : "") << "\n";
    }
    js << "  ]\n}\n";
    js.close();

    std::printf("msdf_bake: %s -> %s (%dx%d) + %s, %zu glyphs\n",
                fontPath, outPng, atlasW, atlasH, outJson, glyphs.size());
    return 0;
}
