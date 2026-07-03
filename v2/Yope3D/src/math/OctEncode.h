#pragma once
#include <cstdint>
#include <cmath>
#include "Vec3.h"

// ---------------------------------------------------------------------------
// Octahedral unit-vector encoding (Cigolle et al. 2014).
//
// A unit vector has only 2 degrees of freedom, so it can be stored as two
// scalars by projecting the sphere onto an octahedron unfolded into the
// [-1,1]^2 square. Paired with snorm16 quantisation this packs a normal (or
// tangent) into 4 bytes with ~0.001 deg worst-case angular error — visually
// lossless, and small enough that a 32-byte GPU vertex can carry both a normal
// and a tangent alongside float32 position + uv. See the Milestone 15 plan for
// the full bit-budget derivation.
//
// These are pure inline helpers (no YOPE_MATH_IMPL dependency) so they can be
// used from any translation unit. Vec3 is treated as a plain {x,y,z} aggregate.
// ---------------------------------------------------------------------------

namespace math {

// Map a unit vector to octahedral coordinates in [-1,1]^2.
inline void octEncode(float nx, float ny, float nz, float& ox, float& oy) {
    const float invL1 = 1.0f / (std::fabs(nx) + std::fabs(ny) + std::fabs(nz));
    nx *= invL1; ny *= invL1; nz *= invL1;
    if (nz < 0.0f) {
        const float x = nx, y = ny;
        nx = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        ny = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    ox = nx; oy = ny;
}

// Inverse of octEncode: octahedral coordinates -> unit vector.
inline Vec3 octDecode(float ox, float oy) {
    float x = ox, y = oy;
    const float z = 1.0f - std::fabs(ox) - std::fabs(oy);
    const float t = z < 0.0f ? -z : 0.0f;          // max(0, -z)
    x += (x >= 0.0f) ? -t : t;
    y += (y >= 0.0f) ? -t : t;
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0f) { const float inv = 1.0f / len; return Vec3{x * inv, y * inv, z * inv}; }
    return Vec3{0.0f, 0.0f, 1.0f};
}

// snorm16 quantisation matching the Vulkan VK_FORMAT_R16G16_SNORM convert rule:
//   encode: round(clamp(v,-1,1) * 32767)
//   decode: max(v / 32767, -1)
inline int16_t floatToSnorm16(float v) {
    if (v < -1.0f) v = -1.0f; else if (v > 1.0f) v = 1.0f;
    return static_cast<int16_t>(std::lround(v * 32767.0f));
}
inline float snorm16ToFloat(int16_t v) {
    const float f = static_cast<float>(v) / 32767.0f;
    return f < -1.0f ? -1.0f : f;
}

inline void octEncodeSnorm16(const Vec3& n, int16_t out[2]) {
    float ox, oy;
    octEncode(n.x, n.y, n.z, ox, oy);
    out[0] = floatToSnorm16(ox);
    out[1] = floatToSnorm16(oy);
}
inline Vec3 octDecodeSnorm16(const int16_t in[2]) {
    return octDecode(snorm16ToFloat(in[0]), snorm16ToFloat(in[1]));
}

} // namespace math
