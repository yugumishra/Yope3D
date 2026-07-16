#pragma once
#include "../math/Vec3.h"
#include "../math/Quat.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Rigid (node-TRS) animation clip data — the asset side of glTF `animations`
// import. Deliberately NOT an ECS component: keyframe arrays are variable
// length, which archetype storage (trivially-relocatable, memcpy-on-resize)
// cannot hold. Clips live in a World-side store keyed by name (see
// World::animationClips_); ecs::AnimationPlayer only carries the key + POD
// playback state. See GltfLoader.h (import) and World::updateAnimations
// (sampling into local Transforms).
// ---------------------------------------------------------------------------
namespace anim {

enum class Path   { Translation, Rotation, Scale };
enum class Interp { Step, Linear, CubicSpline };

// One glTF animation channel targeting a single node's TRS path.
// `values` layout depends on `interp`:
//   Step / Linear  -> comps floats per keyframe:      [v0, v1, ...]
//   CubicSpline    -> 3*comps floats per keyframe (glTF in-tangent, value,
//                     out-tangent triplets): [a0,v0,b0, a1,v1,b1, ...]
// comps = 3 for Translation/Scale, 4 for Rotation.
struct Channel {
    int    targetNode = -1;   // index into LoadedModel::nodes / World's per-root binding table
    Path   path        = Path::Translation;
    Interp interp      = Interp::Linear;
    std::vector<float> times;
    std::vector<float> values;
};

struct Clip {
    std::string          name;
    float                 duration = 0.f;   // max keyframe time across all channels
    std::vector<Channel> channels;
};

namespace detail {

// Locate the keyframe segment [k, k+1] containing `t`; `s` is the segment-local
// parameter in [0,1], `segDur` the segment's time span (0 for a degenerate/
// single-keyframe channel). Clamps outside the keyframe range.
inline void findSegment(const std::vector<float>& times, float t, int& k, float& s, float& segDur) {
    int n = static_cast<int>(times.size());
    if (n <= 1 || t <= times.front()) { k = 0; s = 0.f; segDur = 0.f; return; }
    if (t >= times.back()) {
        k = n - 2;
        segDur = times[n-1] - times[n-2];
        s = 1.f;
        return;
    }
    for (int i = 0; i < n - 1; ++i) {
        if (t >= times[i] && t <= times[i+1]) {
            k = i;
            segDur = times[i+1] - times[i];
            s = segDur > 0.f ? (t - times[i]) / segDur : 0.f;
            return;
        }
    }
    k = n - 2; s = 1.f; segDur = times[n-1] - times[n-2];   // unreachable fallback
}

// Component-wise sample of a channel into `out[0..comps)`.
inline void sampleComponents(const Channel& ch, float t, int comps, float* out) {
    int k; float s, segDur;
    findSegment(ch.times, t, k, s, segDur);
    bool hasNext = ch.times.size() > 1;
    int  k1 = k + (hasNext ? 1 : 0);

    if (ch.interp == Interp::Step) {
        const float* v = &ch.values[static_cast<size_t>(k) * comps];
        for (int c = 0; c < comps; ++c) out[c] = v[c];
        return;
    }
    if (ch.interp == Interp::Linear) {
        const float* v0 = &ch.values[static_cast<size_t>(k)  * comps];
        const float* v1 = &ch.values[static_cast<size_t>(k1) * comps];
        for (int c = 0; c < comps; ++c) out[c] = v0[c] + s * (v1[c] - v0[c]);
        return;
    }
    // CubicSpline: glTF Hermite spline, 3*comps floats per key (inTangent, value, outTangent).
    int stride = 3 * comps;
    const float* key0 = &ch.values[static_cast<size_t>(k)  * stride];
    const float* key1 = &ch.values[static_cast<size_t>(k1) * stride];
    const float* v0 = key0 + comps;       // value at k
    const float* b0 = key0 + 2 * comps;   // out-tangent at k
    const float* a1 = key1;               // in-tangent at k+1
    const float* v1 = key1 + comps;       // value at k+1
    float s2 = s * s, s3 = s2 * s;
    float h00 =  2*s3 - 3*s2 + 1;
    float h10 =      s3 - 2*s2 + s;
    float h01 = -2*s3 + 3*s2;
    float h11 =      s3 -   s2;
    for (int c = 0; c < comps; ++c)
        out[c] = h00*v0[c] + segDur*h10*b0[c] + h01*v1[c] + segDur*h11*a1[c];
}

} // namespace detail

inline math::Vec3 sampleVec3Channel(const Channel& ch, float t) {
    float v[3] = {0.f, 0.f, 0.f};
    detail::sampleComponents(ch, t, 3, v);
    return { v[0], v[1], v[2] };
}

// Rotation: Linear interpolation uses slerp (component-wise lerp would not stay
// unit-length along the arc); Step/CubicSpline sample component-wise and
// renormalize (CubicSpline's Hermite blend does not preserve unit length exactly).
inline math::Quat sampleQuatChannel(const Channel& ch, float t) {
    if (ch.interp == Interp::Linear && ch.times.size() > 1) {
        int k; float s, segDur;
        detail::findSegment(ch.times, t, k, s, segDur);
        const float* v0 = &ch.values[static_cast<size_t>(k)     * 4];
        const float* v1 = &ch.values[static_cast<size_t>(k + 1) * 4];
        return math::Quat::slerp({v0[0], v0[1], v0[2], v0[3]},
                                  {v1[0], v1[1], v1[2], v1[3]}, s);
    }
    float v[4] = {0.f, 0.f, 0.f, 1.f};
    detail::sampleComponents(ch, t, 4, v);
    float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2] + v[3]*v[3]);
    if (len > 1e-8f) { v[0] /= len; v[1] /= len; v[2] /= len; v[3] /= len; }
    return { v[0], v[1], v[2], v[3] };
}

} // namespace anim
