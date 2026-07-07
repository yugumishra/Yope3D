#include "CompoundShape.h"
#include <algorithm>
#include <limits>
#include <fstream>
#include <type_traits>

namespace physics {

namespace {
    inline void expand(math::Vec3& mn, math::Vec3& mx, const math::Vec3& p) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }
    constexpr float kInf = std::numeric_limits<float>::max();

    // Recursive median split on the longest axis of the centroid bounds.
    // Operates on the index range [begin, end) of `shapes`; appends nodes to
    // `out.nodes` and returns the index of the node it created.
    int32_t buildNode(std::vector<SubShape>& shapes, CompiledCollider& out,
                      int begin, int end, int leafSize)
    {
        int32_t nodeIdx = static_cast<int32_t>(out.nodes.size());
        out.nodes.emplace_back();

        // Bounds over the shapes in this range.
        math::Vec3 mn{kInf, kInf, kInf}, mx{-kInf, -kInf, -kInf};
        math::Vec3 cmn{kInf, kInf, kInf}, cmx{-kInf, -kInf, -kInf};
        for (int i = begin; i < end; ++i) {
            expand(mn, mx, shapes[i].aabbMin);
            expand(mn, mx, shapes[i].aabbMax);
            math::Vec3 c = (shapes[i].aabbMin + shapes[i].aabbMax) * 0.5f;
            expand(cmn, cmx, c);
        }

        int count = end - begin;
        if (count <= leafSize) {
            BvhNode& n = out.nodes[nodeIdx];
            n.aabbMin = mn; n.aabbMax = mx;
            n.left = -1; n.first = begin; n.count = count;
            return nodeIdx;
        }

        // Longest centroid axis.
        math::Vec3 d = cmx - cmn;
        int axis = (d.x >= d.y && d.x >= d.z) ? 0 : (d.y >= d.z ? 1 : 2);
        auto axisOf = [axis](const SubShape& s) {
            math::Vec3 c = (s.aabbMin + s.aabbMax) * 0.5f;
            return axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
        };

        int mid = begin + count / 2;
        std::nth_element(shapes.begin() + begin, shapes.begin() + mid, shapes.begin() + end,
                         [&](const SubShape& a, const SubShape& b) { return axisOf(a) < axisOf(b); });

        // Degenerate split (all centroids coincide) => make a leaf regardless.
        if (mid == begin || mid == end) {
            BvhNode& n = out.nodes[nodeIdx];
            n.aabbMin = mn; n.aabbMax = mx;
            n.left = -1; n.first = begin; n.count = count;
            return nodeIdx;
        }

        // Capture both child indices explicitly: the left subtree appends an
        // arbitrary number of nodes before the right child is created, so the
        // right child is NOT leftChild+1.
        int32_t leftChild  = buildNode(shapes, out, begin, mid, leafSize);
        int32_t rightChild = buildNode(shapes, out, mid, end, leafSize);

        BvhNode& n = out.nodes[nodeIdx];
        n.aabbMin = mn; n.aabbMax = mx;
        n.left = leftChild; n.right = rightChild; n.count = 0;
        return nodeIdx;
    }
}

void buildCompoundBvh(std::vector<SubShape>& shapes, CompiledCollider& out, int leafSize) {
    out.nodes.clear();
    if (shapes.empty()) {
        out.subShapes.clear();
        out.localMin = {}; out.localMax = {};
        return;
    }
    if (leafSize < 1) leafSize = 1;
    buildNode(shapes, out, 0, static_cast<int>(shapes.size()), leafSize);
    out.subShapes = shapes;               // now reordered to match leaf ranges
    out.localMin = out.nodes[0].aabbMin;
    out.localMax = out.nodes[0].aabbMax;
}

static_assert(std::is_trivially_copyable<SubShape>::value, "SubShape must be POD for verbatim IO");
static_assert(std::is_trivially_copyable<BvhNode>::value,  "BvhNode must be POD for verbatim IO");

namespace {
    template <class T> void writePod(std::ostream& os, const T& v) {
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <class T> bool readPod(std::istream& is, T& v) {
        return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(T)));
    }
    template <class T> void writeVec(std::ostream& os, const std::vector<T>& v) {
        uint32_t n = static_cast<uint32_t>(v.size());
        writePod(os, n);
        if (n) os.write(reinterpret_cast<const char*>(v.data()), sizeof(T) * n);
    }
    template <class T> bool readVec(std::istream& is, std::vector<T>& v) {
        uint32_t n = 0;
        if (!readPod(is, n)) return false;
        v.resize(n);
        if (n) return static_cast<bool>(is.read(reinterpret_cast<char*>(v.data()), sizeof(T) * n));
        return true;
    }
}

bool writeBcbvh(const std::string& absPath, const CompiledCollider& col) {
    std::ofstream os(absPath, std::ios::binary);
    if (!os) return false;
    writePod(os, BCBVH_MAGIC);
    writePod(os, BCBVH_VERSION);
    writeVec(os, col.subShapes);
    writeVec(os, col.nodes);
    writePod(os, col.localMin);
    writePod(os, col.localMax);
    return static_cast<bool>(os);
}

bool readBcbvh(const std::string& absPath, CompiledCollider& out) {
    std::ifstream is(absPath, std::ios::binary);
    if (!is) return false;
    uint32_t magic = 0, version = 0;
    if (!readPod(is, magic) || !readPod(is, version)) return false;
    if (magic != BCBVH_MAGIC || version != BCBVH_VERSION) return false;
    if (!readVec(is, out.subShapes)) return false;
    if (!readVec(is, out.nodes)) return false;
    if (!readPod(is, out.localMin) || !readPod(is, out.localMax)) return false;
    return true;
}

float meshVolume(const std::vector<math::Vec3>& positions, const std::vector<uint32_t>& indices) {
    double vol = 0.0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const math::Vec3& a = positions[indices[i]];
        const math::Vec3& b = positions[indices[i + 1]];
        const math::Vec3& c = positions[indices[i + 2]];
        double crossX = (double)b.y * c.z - (double)b.z * c.y;
        double crossY = (double)b.z * c.x - (double)b.x * c.z;
        double crossZ = (double)b.x * c.y - (double)b.y * c.x;
        vol += (double)a.x * crossX + (double)a.y * crossY + (double)a.z * crossZ;
    }
    return static_cast<float>(std::fabs(vol) / 6.0);
}

namespace {
    constexpr float kSphereVolumeRatio = 0.5236f;   // pi/6
    constexpr float kVolumeRatioTol    = 0.08f;     // +/- absolute band around the target ratio
    constexpr float kUniformAspectTol  = 1.15f;     // max/min half-extent to call a shape "round"
}

bool classifyAsSphere(const std::vector<math::Vec3>& positions, const std::vector<uint32_t>& indices,
                      const math::Vec3& halfExtentMesh) {
    float exMax = std::max({halfExtentMesh.x, halfExtentMesh.y, halfExtentMesh.z});
    float exMin = std::min({halfExtentMesh.x, halfExtentMesh.y, halfExtentMesh.z});
    if (exMin <= 1e-6f || exMax / exMin > kUniformAspectTol) return false;

    float aabbVol = 8.0f * halfExtentMesh.x * halfExtentMesh.y * halfExtentMesh.z;
    if (aabbVol <= 1e-9f) return false;
    float ratio = meshVolume(positions, indices) / aabbVol;
    return std::fabs(ratio - kSphereVolumeRatio) < kVolumeRatioTol;
}

} // namespace physics
