#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Zero-copy reader for the packed loading-logo binary (tools/logo_pack.py).
//
// The whole file is read once into one buffer; the clip views are std::spans
// INTO that buffer — no per-frame parsing, no per-number heap. Coords are uint16
// (coord / 65535 -> [0,1]); a frame's coords are coords[frameStart[i] ..
// frameStart[i+1]) as groups of 4 ([x, y, x, y]).
struct LogoClipView {
    float fps        = 60.0f;
    float refAspect  = 16.0f / 9.0f;
    int   frameCount = 0;
    float coordLo    = 0.0f;                 // uint16 0..65535 maps linearly to
    float coordHi    = 1.0f;                 //   [coordLo, coordHi] (allows off-screen)
    std::span<const uint32_t> frameStart;   // frameCount+1 entries (coord indices)
    std::span<const uint16_t> coords;       // uint16 grid

    float durationSec() const { return fps > 0.0f ? frameCount / fps : 0.0f; }
    // Decode a raw uint16 coord to its cam-view value.
    float decode(uint16_t c) const { return coordLo + (c * (1.0f / 65535.0f)) * (coordHi - coordLo); }
};

struct LogoBundle {
    std::unique_ptr<uint8_t[]> buf;
    LogoClipView part1;
    LogoClipView part2;
    bool valid = false;

    bool load(const std::string& path) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 64) { std::fclose(f); return false; }
        buf.reset(new uint8_t[static_cast<size_t>(sz)]);   // operator new[] => 16-aligned
        size_t rd = std::fread(buf.get(), 1, static_cast<size_t>(sz), f);
        std::fclose(f);
        if (rd != static_cast<size_t>(sz)) return false;
        return parseBuf(static_cast<size_t>(sz));
    }

    // Same format, but bytes already resolved (embedded asset or filesystem —
    // see assets::readBytes). Takes ownership of a copy in the 16-aligned buf.
    bool loadFromMemory(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 64) return false;
        buf.reset(new uint8_t[bytes.size()]);   // operator new[] => 16-aligned
        std::memcpy(buf.get(), bytes.data(), bytes.size());
        return parseBuf(bytes.size());
    }

private:
    bool parseBuf(size_t sz) {
        const uint8_t* p = buf.get();
        uint32_t magic = 0, clipCount = 0;
        std::memcpy(&magic, p + 0, 4);
        std::memcpy(&clipCount, p + 8, 4);
        if (magic != 0x31474C59u || clipCount < 2) return false;

        auto readClip = [&](int i, LogoClipView& c) {
            const uint8_t* d = p + 16 + 32 * i;
            uint32_t fc, cc, fso, coo; float fps, ra, lo, hi;
            std::memcpy(&fc,  d + 0,  4);
            std::memcpy(&fps, d + 4,  4);
            std::memcpy(&ra,  d + 8,  4);
            std::memcpy(&cc,  d + 12, 4);
            std::memcpy(&fso, d + 16, 4);
            std::memcpy(&coo, d + 20, 4);
            std::memcpy(&lo,  d + 24, 4);
            std::memcpy(&hi,  d + 28, 4);
            c.frameCount = static_cast<int>(fc);
            c.fps        = fps;
            c.refAspect  = ra;
            c.coordLo    = lo;
            c.coordHi    = hi;
            // Offsets are 4-aligned by the packer, buffer base is 16-aligned.
            c.frameStart = { reinterpret_cast<const uint32_t*>(p + fso), fc + 1u };
            c.coords     = { reinterpret_cast<const uint16_t*>(p + coo), cc };
        };
        readClip(0, part1);
        readClip(1, part2);
        valid = part1.frameCount > 0 && part2.frameCount > 0;
        return valid;
    }

public:
    void clear() {
        buf.reset();
        part1 = LogoClipView{};
        part2 = LogoClipView{};
        valid = false;
    }
};
