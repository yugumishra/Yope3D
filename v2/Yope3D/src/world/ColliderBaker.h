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
// subtree (nothing to bake).
physics::CompiledCollider bakeFromEntity(ecs::Registry& reg, ecs::Entity root, int leafSize = 4);

// Convenience: bake + write to an absolute .bcbvh path. Returns false if the
// subtree contains no mesh geometry, or on IO error.
bool bakeToFile(ecs::Registry& reg, ecs::Entity root, const std::string& absPath, int leafSize = 4);

} // namespace ColliderBaker
