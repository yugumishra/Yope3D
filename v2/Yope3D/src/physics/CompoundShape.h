#pragma once
#include "../math/Vec3.h"
#include "../math/Mat3.h"
#include <vector>
#include <string>
#include <cstdint>

// Baked compound collider data — a static rigid body made of many convex
// sub-shapes with a flat AABB BVH ("mid-phase") over them. Authored offline
// (from a glTF import) and cooked to a `.bcbvh` file (magic 'BCVH'), or built
// in-memory for tests. Immutable at runtime; one CompiledCollider is shared by
// every ecs::CompoundCollider that references the same asset path, so it holds
// NO per-instance (world) state — all sub-shape transforms are body-LOCAL and
// composed against the owning entity's Transform inside detectCompound().
namespace physics {

// Sub-shape type ordinals — MUST match ColliderDiscrete::ShapeVariant::index()
// (0=Sphere 1=AABB 2=OBB 3=Capsule 4=Cylinder) so the narrowphase can dispatch.
enum class SubShapeType : uint32_t {
    Sphere = 0, AABB = 1, OBB = 2, Capsule = 3, Cylinder = 4
};

// One convex primitive in body-local space. `extent` packing:
//   Sphere   : extent.x = radius
//   AABB/OBB : extent = half-extents
//   Capsule/ : extent.x = radius, extent.y = halfHeight
//   Cylinder
// aabbMin/aabbMax are the local enclosing AABB (BVH leaf key); precomputed at bake.
struct SubShape {
    SubShapeType type      = SubShapeType::OBB;
    math::Vec3   localPos  {};              // center relative to body origin
    math::Mat3   localRot  {};              // identity for Sphere/AABB
    math::Vec3   extent    {1.0f, 1.0f, 1.0f};
    math::Vec3   aabbMin   {};
    math::Vec3   aabbMax   {};
    float        mass      = 0.0f;          // baked (not density) — pure number on the hot path
};

// Flat BVH node. count>0 => leaf spanning subShapes[first, first+count);
// count==0 => internal node with children at [left] and [right].
struct BvhNode {
    math::Vec3 aabbMin {};
    math::Vec3 aabbMax {};
    int32_t    left    = -1;
    int32_t    right   = -1;
    int32_t    first   = 0;
    int32_t    count   = 0;
};

struct CompiledCollider {
    std::vector<SubShape> subShapes;   // reordered so BVH leaves are contiguous
    std::vector<BvhNode>  nodes;       // nodes[0] is the root (empty => no shapes)
    math::Vec3            localMin {}; // overall local AABB (root bounds; broadphase)
    math::Vec3            localMax {};

    // Mass/inertia data (v2). For a compound built via computeCompoundMassProperties(),
    // the sub-shapes are recentered so the body-local origin IS the center of mass —
    // centerOfMassLocal is then ~{0,0,0} by construction (kept as an explicit
    // self-consistency field rather than assumed). pivotOffset is the shift that was
    // applied to get there (== the centroid in the shapes' originally-authored frame),
    // kept for reconstructing the original pivot if ever needed (e.g. rendering).
    // Static compounds (baked pre-M-dynamic, or loaded from a v1 .bcbvh) leave these
    // at their zero defaults — harmless, since attachCompoundCollider forces mass=0 /
    // zero inertia for static bodies regardless of what's baked here.
    float                  totalMass           = 0.0f;
    math::Vec3             centerOfMassLocal   {};
    math::Mat3             inverseInertiaLocal = math::Mat3::zero();
    math::Vec3             pivotOffset         {};
};

// `.bcbvh` on-disk header magic / version (little-endian POD blob follows).
// v2 adds SubShape::mass and CompiledCollider's mass/COM/inertia fields; v1 files
// (pre-dating dynamic compounds) are still readable — they load with those fields
// zeroed, which is correct for static (Fixed, mass-0) bodies.
constexpr uint32_t BCBVH_MAGIC   = 0x48564342u; // 'BCVH'
constexpr uint32_t BCBVH_VERSION = 2u;

// Build a median-split AABB BVH over `shapes` (reorders `shapes` in place so
// leaves are contiguous) and populate `out.nodes` + overall local bounds.
// `leafSize` = max sub-shapes per leaf. Header-only so tests + the baker share it.
void buildCompoundBvh(std::vector<SubShape>& shapes, CompiledCollider& out, int leafSize = 4);

// Cook / load the flat POD blob to/from a `.bcbvh` file (absolute path).
// Returns false on IO error or magic/version mismatch. SubShape and BvhNode are
// trivially copyable, so the arrays are written/read verbatim.
bool writeBcbvh(const std::string& absPath, const CompiledCollider& col);
bool readBcbvh (const std::string& absPath, CompiledCollider& out);

// ---- Shape inference (used by ColliderBaker; exposed here so it's testable
// without RenderMesh/GPU — takes raw positions, not the engine's Vertex type) ----

// Enclosed volume of a closed, consistently-wound triangle mesh via the
// divergence theorem (signed tetrahedron decomposition from the origin — works
// for any mesh-local origin as long as the mesh is watertight).
float meshVolume(const std::vector<math::Vec3>& positions, const std::vector<uint32_t>& indices);

// Shape classification, driven by one scale-invariant discriminator: the ratio
// of a mesh's enclosed volume to its own local AABB's volume.
//   Box             -> ratio ~1.0     (the mesh IS its bounding box)
//   Sphere/ellipsoid -> ratio ~pi/6 (~0.524), independent of radius/aspect —
//                       volume(a,b,c ellipsoid) / (2a * 2b * 2c) = pi/6 always.
// Anything else (capsule, cylinder, arbitrary level geometry) falls through to
// the OBB default — this is the extension point for capsule/cylinder inference:
// a cylinder's ratio is pi/4 (~0.785), also constant regardless of r/h (same
// scale-invariance trick), so it slots into this same if/else-if chain as
// another constant-ratio band. Capsule's ratio depends on the radius/half-height
// mix (no single constant), so it needs an extra parameter (e.g. compare against
// the AABB's aspect ratio too) rather than a single volume-ratio threshold.
// `halfExtentMesh` is the shape's own local AABB half-extents.
bool classifyAsSphere(const std::vector<math::Vec3>& positions, const std::vector<uint32_t>& indices,
                      const math::Vec3& halfExtentMesh);

// ---- Mass/inertia math (shared by ColliderBaker and the Python sphere-compound
// builder — one implementation, no duplicated tensor math) ----

// Solid sphere inertia about its own center: diagonal 2/5 m r^2.
math::Mat3 sphereInertia(float mass, float radius);

// Solid box inertia about its own center (half-extents), axis-aligned local frame:
// I_x = (1/3) m (ey^2+ez^2), etc.
math::Mat3 boxInertia(float mass, const math::Vec3& halfExtent);

// Parallel-axis theorem: shifts a body-frame inertia tensor computed about a
// sub-shape's own center to one about a point `offsetFromCOM` away, i.e. adds
// mass * (|d|^2 * I3 - d (x) d).
math::Mat3 parallelAxisShift(const math::Mat3& Ilocal, float mass, const math::Vec3& offsetFromCOM);

// Recenters `shapes` in place (localPos/aabbMin/aabbMax) so the body-local origin
// becomes the mass-weighted centroid — SubShape::mass must already be populated.
// Sums each sub-shape's own inertia (parallel-axis shifted to the new origin;
// Sphere uses sphereInertia, everything else approximates as an oriented box via
// boxInertia + localRot) into a combined tensor and inverts it. Returns the
// applied shift (pivotOffset, i.e. the centroid in the shapes' original frame);
// fills outTotalMass / outInverseInertiaLocal. No-op-safe on an empty/zero-mass
// input (returns {0,0,0}, outTotalMass=0, outInverseInertiaLocal=zero()).
math::Vec3 computeCompoundMassProperties(std::vector<SubShape>& shapes,
                                          float& outTotalMass,
                                          math::Mat3& outInverseInertiaLocal);

} // namespace physics
