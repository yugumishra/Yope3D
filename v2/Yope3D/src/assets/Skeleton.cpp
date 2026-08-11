#include "Skeleton.h"
#include <algorithm>

namespace anim {

bool Skeleton::isTopologicallyOrdered() const {
    const int n = static_cast<int>(parent.size());
    if (static_cast<int>(bindLocal.size())   != n) return false;
    if (static_cast<int>(inverseBind.size()) != n) return false;
    for (int i = 0; i < n; ++i)
        if (parent[i] >= i || parent[i] < -1) return false;
    return true;
}

int findBone(const Skeleton& sk, const std::string& name) {
    for (size_t i = 0; i < sk.names.size(); ++i)
        if (sk.names[i] == name) return static_cast<int>(i);
    return -1;
}

void samplePose(const Skeleton& sk, const Clip& clip, float time,
                std::vector<Transform>& out) {
    out = sk.bindLocal;

    const int n = static_cast<int>(out.size());
    for (const Channel& ch : clip.channels) {
        if (ch.targetNode < 0 || ch.targetNode >= n) continue;
        if (ch.times.empty() || ch.values.empty())   continue;
        Transform& t = out[ch.targetNode];
        switch (ch.path) {
            case Path::Translation: t.position = sampleVec3Channel(ch, time); break;
            case Path::Rotation:    t.rotation = sampleQuatChannel(ch, time); break;
            case Path::Scale:       t.scale    = sampleVec3Channel(ch, time); break;
        }
    }
}

void blendPoses(const std::vector<Transform>& a, const std::vector<Transform>& b,
                float w, std::vector<Transform>& out) {
    const size_t n = a.size();
    if (&out != &a && &out != &b) out.resize(n);

    // Exact passthrough at the endpoints. Without this, w == 0 still runs a slerp
    // whose renormalisation perturbs the quaternion in the last couple of ULPs —
    // enough to make "cross-fade at rest reproduces the source pose" fail on an
    // exact comparison, and enough to jitter a held pose frame to frame.
    if (w <= 0.0f) { if (&out != &a) out.assign(a.begin(), a.end()); return; }
    if (w >= 1.0f && b.size() >= n) { out.assign(b.begin(), b.begin() + n); return; }

    for (size_t i = 0; i < n; ++i) {
        const Transform& ta = a[i];
        // A short `b` (mismatched skeletons) holds `a` for the missing tail rather
        // than reading out of bounds.
        if (i >= b.size()) { out[i] = ta; continue; }
        const Transform& tb = b[i];

        Transform r;
        r.position = ta.position + (tb.position - ta.position) * w;
        r.scale    = ta.scale    + (tb.scale    - ta.scale)    * w;
        r.rotation = math::Quat::slerp(ta.rotation, tb.rotation, w);
        out[i] = r;
    }
}

void buildWorldPose(const Skeleton& sk, const std::vector<Transform>& localPose,
                    std::vector<math::Mat4>& out) {
    const size_t n = std::min(sk.parent.size(), localPose.size());
    out.resize(n);

    // parent[i] < i (the topological-order invariant) guarantees out[parent[i]]
    // already holds the parent's world matrix by the time we read it, so one
    // forward sweep resolves the whole hierarchy with no recursion.
    for (size_t i = 0; i < n; ++i) {
        const math::Mat4 local = localPose[i].getModelMatrix();
        const int p = sk.parent[i];
        out[i] = (p < 0 || static_cast<size_t>(p) >= i) ? local : out[p] * local;
    }
}

void buildPalette(const Skeleton& sk, const std::vector<Transform>& localPose,
                  std::vector<math::Mat4>& out) {
    buildWorldPose(sk, localPose, out);

    // Right-multiply by the inverse binds as a SEPARATE pass. Folding it into the
    // world-space sweep above would feed children their parent's PALETTE matrix
    // instead of its world matrix — a wrong hierarchy that still looks plausible.
    const size_t ib = std::min(out.size(), sk.inverseBind.size());
    for (size_t i = 0; i < ib; ++i) out[i] = out[i] * sk.inverseBind[i];
}

} // namespace anim
