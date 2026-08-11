#include "SkeletonBlob.h"
#include <cstring>

namespace skelblob {
namespace {

constexpr uint8_t kVersion = 1;

// ---- write helpers ----
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));       b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}
void putI32(std::vector<uint8_t>& b, int32_t v) { putU32(b, static_cast<uint32_t>(v)); }
void putF32(std::vector<uint8_t>& b, float v) {
    uint32_t u; std::memcpy(&u, &v, 4); putU32(b, u);
}
void putStr(std::vector<uint8_t>& b, const std::string& s) {
    putU32(b, static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
void putFloats(std::vector<uint8_t>& b, const std::vector<float>& v) {
    putU32(b, static_cast<uint32_t>(v.size()));
    for (float f : v) putF32(b, f);
}

// ---- read cursor ----
struct Cursor {
    const std::vector<uint8_t>& d;
    size_t o = 0;
    bool ok = true;

    bool need(size_t n) { if (!ok || o + n > d.size()) { ok = false; return false; } return true; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = uint32_t(d[o]) | (uint32_t(d[o+1]) << 8) |
                     (uint32_t(d[o+2]) << 16) | (uint32_t(d[o+3]) << 24);
        o += 4; return v;
    }
    int32_t i32()  { return static_cast<int32_t>(u32()); }
    float   f32()  { uint32_t u = u32(); float f; std::memcpy(&f, &u, 4); return f; }
    std::string str() {
        uint32_t n = u32();
        // A corrupt length must not be trusted into a huge allocation.
        if (!need(n)) return {};
        std::string s(reinterpret_cast<const char*>(d.data() + o), n);
        o += n; return s;
    }
    std::vector<float> floats() {
        uint32_t n = u32();
        if (!need(size_t(n) * 4)) return {};
        std::vector<float> v(n);
        for (uint32_t i = 0; i < n; ++i) v[i] = f32();
        return v;
    }
};

} // namespace

std::vector<uint8_t> encode(const Payload& p) {
    std::vector<uint8_t> b;
    if (p.empty()) return b;

    const char magic[4] = {'Y', 'S', 'K', 'L'};
    b.insert(b.end(), magic, magic + 4);
    b.push_back(kVersion); b.push_back(0); b.push_back(0); b.push_back(0);

    putU32(b, static_cast<uint32_t>(p.skeletons.size()));
    for (const NamedSkeleton& ns : p.skeletons) {
        const anim::Skeleton& sk = ns.skeleton;
        const uint32_t n = static_cast<uint32_t>(sk.parent.size());
        putStr(b, ns.key);
        putU32(b, n);
        for (uint32_t i = 0; i < n; ++i) putI32(b, sk.parent[i]);
        for (uint32_t i = 0; i < n; ++i) {
            const Transform& t = sk.bindLocal[i];
            putF32(b, t.position.x); putF32(b, t.position.y); putF32(b, t.position.z);
            putF32(b, t.rotation.x); putF32(b, t.rotation.y);
            putF32(b, t.rotation.z); putF32(b, t.rotation.w);
            putF32(b, t.scale.x);    putF32(b, t.scale.y);    putF32(b, t.scale.z);
        }
        for (uint32_t i = 0; i < n; ++i)
            for (int k = 0; k < 16; ++k) putF32(b, sk.inverseBind[i].m[k]);
        for (uint32_t i = 0; i < n; ++i)
            putStr(b, i < sk.names.size() ? sk.names[i] : std::string{});
    }

    putU32(b, static_cast<uint32_t>(p.clips.size()));
    for (const NamedClip& nc : p.clips) {
        const anim::Clip& c = nc.clip;
        putStr(b, nc.key);
        putF32(b, c.duration);
        putU32(b, static_cast<uint32_t>(c.channels.size()));
        for (const anim::Channel& ch : c.channels) {
            putI32(b, ch.targetNode);
            putI32(b, static_cast<int32_t>(ch.path));
            putI32(b, static_cast<int32_t>(ch.interp));
            putFloats(b, ch.times);
            putFloats(b, ch.values);
        }
    }
    return b;
}

bool decode(const std::vector<uint8_t>& bytes, Payload& out) {
    if (bytes.size() < 8) return false;
    if (bytes[0] != 'Y' || bytes[1] != 'S' || bytes[2] != 'K' || bytes[3] != 'L') return false;
    if (bytes[4] != kVersion) return false;

    Cursor c{bytes, 8};

    const uint32_t skelCount = c.u32();
    for (uint32_t s = 0; s < skelCount && c.ok; ++s) {
        NamedSkeleton ns;
        ns.key = c.str();
        const uint32_t n = c.u32();
        if (!c.ok) break;

        anim::Skeleton& sk = ns.skeleton;
        sk.parent.resize(n);
        sk.bindLocal.resize(n);
        sk.inverseBind.resize(n);
        sk.names.resize(n);

        for (uint32_t i = 0; i < n; ++i) sk.parent[i] = c.i32();
        for (uint32_t i = 0; i < n; ++i) {
            Transform& t = sk.bindLocal[i];
            t.position = { c.f32(), c.f32(), c.f32() };
            t.rotation = { c.f32(), c.f32(), c.f32(), c.f32() };
            t.scale    = { c.f32(), c.f32(), c.f32() };
        }
        for (uint32_t i = 0; i < n; ++i)
            for (int k = 0; k < 16; ++k) sk.inverseBind[i].m[k] = c.f32();
        for (uint32_t i = 0; i < n; ++i) sk.names[i] = c.str();

        if (!c.ok) break;
        out.skeletons.push_back(std::move(ns));
    }

    const uint32_t clipCount = c.u32();
    for (uint32_t i = 0; i < clipCount && c.ok; ++i) {
        NamedClip nc;
        nc.key = c.str();
        nc.clip.name     = nc.key;
        nc.clip.duration = c.f32();
        const uint32_t chCount = c.u32();
        if (!c.ok) break;
        for (uint32_t k = 0; k < chCount && c.ok; ++k) {
            anim::Channel ch;
            ch.targetNode = c.i32();
            ch.path       = static_cast<anim::Path>(c.i32());
            ch.interp     = static_cast<anim::Interp>(c.i32());
            ch.times      = c.floats();
            ch.values     = c.floats();
            if (!c.ok) break;
            nc.clip.channels.push_back(std::move(ch));
        }
        if (!c.ok) break;
        out.clips.push_back(std::move(nc));
    }

    return c.ok;
}

} // namespace skelblob
