#include "MeshBuild.h"
#include "../world/RenderMesh.h"
#include "../math/OctEncode.h"
#include "../math/Vec3.h"
#include <cmath>

namespace meshbuild {

std::vector<math::Vec4> computeTangents(const std::vector<Vertex>&   verts,
                                        const std::vector<uint32_t>& indices) {
    const size_t n = verts.size();
    std::vector<math::Vec3> tan(n, math::Vec3{0, 0, 0});
    std::vector<math::Vec3> bit(n, math::Vec3{0, 0, 0});

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= n || b >= n || c >= n) continue;

        const Vertex& v0 = verts[a];
        const Vertex& v1 = verts[b];
        const Vertex& v2 = verts[c];

        const math::Vec3 p0{v0.position[0], v0.position[1], v0.position[2]};
        const math::Vec3 p1{v1.position[0], v1.position[1], v1.position[2]};
        const math::Vec3 p2{v2.position[0], v2.position[1], v2.position[2]};
        const math::Vec3 e1 = p1 - p0;
        const math::Vec3 e2 = p2 - p0;

        const float du1 = v1.uv[0] - v0.uv[0], dv1 = v1.uv[1] - v0.uv[1];
        const float du2 = v2.uv[0] - v0.uv[0], dv2 = v2.uv[1] - v0.uv[1];
        const float denom = du1 * dv2 - du2 * dv1;
        const float r = (std::fabs(denom) > 1e-12f) ? (1.0f / denom) : 0.0f;

        const math::Vec3 t = (e1 * dv2 - e2 * dv1) * r;
        const math::Vec3 bvec = (e2 * du1 - e1 * du2) * r;

        tan[a] += t; tan[b] += t; tan[c] += t;
        bit[a] += bvec; bit[b] += bvec; bit[c] += bvec;
    }

    std::vector<math::Vec4> out(n);
    for (size_t i = 0; i < n; ++i) {
        math::Vec3 nrm{verts[i].normal[0], verts[i].normal[1], verts[i].normal[2]};
        nrm = nrm.normalize();

        // Gram-Schmidt: t' = normalize(t - n*(n.t))
        math::Vec3 t = tan[i];
        t = t - nrm * nrm.dot(t);
        const float tl = t.length();
        if (tl > 1e-8f) {
            t = t * (1.0f / tl);
        } else {
            // Degenerate (no usable UV gradient): arbitrary perpendicular of n.
            const math::Vec3 ref = (std::fabs(nrm.x) < 0.9f) ? math::Vec3{1, 0, 0}
                                                             : math::Vec3{0, 1, 0};
            t = nrm.cross(ref).normalize();
        }

        // Handedness: sign of (n x t) . accumulated bitangent.
        const float w = (nrm.cross(t).dot(bit[i]) < 0.0f) ? -1.0f : 1.0f;
        out[i] = math::Vec4{t.x, t.y, t.z, w};
    }
    return out;
}

std::vector<PackedVertex> packVertices(const std::vector<Vertex>&     verts,
                                       const std::vector<math::Vec4>& tangents) {
    std::vector<PackedVertex> out(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const Vertex& v = verts[i];
        PackedVertex& p = out[i];

        p.position[0] = v.position[0];
        p.position[1] = v.position[1];
        p.position[2] = v.position[2];
        p.uv[0] = v.uv[0];
        p.uv[1] = v.uv[1];

        const math::Vec3 nrm = math::Vec3{v.normal[0], v.normal[1], v.normal[2]}.normalize();
        math::octEncodeSnorm16(nrm, p.normalOct);

        const math::Vec4 tg = (i < tangents.size()) ? tangents[i] : math::Vec4{1, 0, 0, 1};
        math::octEncodeSnorm16(math::Vec3{tg.x, tg.y, tg.z}, p.tangentOct);
        p.handedness = (tg.w < 0.0f) ? -1.0f : 1.0f;
    }
    return out;
}

std::vector<PackedVertex> buildPacked(const std::vector<Vertex>&   verts,
                                      const std::vector<uint32_t>& indices) {
    return packVertices(verts, computeTangents(verts, indices));
}

} // namespace meshbuild
