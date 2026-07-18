#pragma once
#include <string>
#include "../ecs/Entity.h"
#include "../physics/CompoundShape.h"

namespace ecs { class Registry; }

// Bakes a static compound collider (physics::CompiledCollider) from an entity's
// mesh subtree — one OBB sub-shape per descendant MeshRenderer, pose expressed
// relative to `root`'s frame, with a BVH built over the result. This is the
// runtime half of the Blender-authored-level workflow: model walls as boxes,
// import via glTF, then bake a single collider covering the whole level so the
// player can't walk through geometry.
//
// Assumes `root`'s own Transform has unit scale at both bake time and runtime
// (see ecs::CompoundCollider) — the baked sub-shape poses are expressed relative
// to root's rotation+position only.
namespace ColliderBaker {

// Walks root + every descendant (via ecs::Parent, hierarchy::collectSubtree),
// building one OBB per entity with a non-empty MeshRenderer mesh. Returns a
// CompiledCollider with empty subShapes/nodes if no mesh was found in the
// subtree (nothing to bake). `density` (kg/m^3, single global scalar) drives
// per-sub-shape mass (analytic volume for known primitives, physics::meshVolume
// for Custom meshes); the resulting shapes are recentered to their mass-weighted
// COM and combined into totalMass/inverseInertiaLocal (physics::computeCompoundMassProperties)
// before the BVH is built. Static (Fixed) bodies ignore these fields at runtime,
// so `density`'s default is harmless for the pre-existing static-level use case.
physics::CompiledCollider bakeFromEntity(ecs::Registry& reg, ecs::Entity root, int leafSize = 4,
                                         float density = 1.0f);

// Convenience: bake + write to an absolute .bcbvh path. Returns false if the
// subtree contains no mesh geometry, or on IO error.
bool bakeToFile(ecs::Registry& reg, ecs::Entity root, const std::string& absPath, int leafSize = 4,
                float density = 1.0f);

// The mass/COM pass above recenters sub-shapes to the mass-weighted centroid —
// `root`'s LOCAL origin becomes the compiled collider's origin — but the mesh
// hierarchy under `root` (and root's own Transform) is authored around the OLD
// (pre-recentering) pivot and knows nothing about the shift. Call this once
// right after loading the freshly-(re)baked CompiledCollider (with `pivotOffset`
// = compiled->pivotOffset) to keep the two in sync: shifts root's own
// Transform.position in world space to the new origin, and shifts every DIRECT
// child's local Transform.position by the inverse so their absolute world
// positions — and therefore the rendered mesh — don't move. (Only direct
// children need adjusting: grandchildren are expressed relative to an
// intermediate parent whose own transform is untouched, so their world
// position is preserved transitively.) No-op if `root` has no Transform or
// `pivotOffset` is ~zero (e.g. a v1-loaded static collider, or an already-
// recentered regenerate).
void applyPivotCompensation(ecs::Registry& reg, ecs::Entity root, const math::Vec3& pivotOffset);

} // namespace ColliderBaker
