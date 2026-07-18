#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "world/Transform.h"
#include "math/Vec3.h"
#include <string>
#include <vector>
#include <utility>

// Bakes a compound collider (ColliderBaker::bakeToFile) from `entity`'s mesh
// subtree to a `.bcbvh` asset under assets/colliders/, then attaches it
// (Hull + ecs::CompoundCollider, +Fixed if isStatic) to `entity` — the
// "Generate Compound Collider" button on the root inspector for imported
// levels/props. `density` drives per-sub-shape mass (analytic for known
// primitives, inferred mesh volume otherwise); `isStatic=true` (default)
// preserves the original "walk-through-walls" static-level behavior,
// `isStatic=false` produces a real dynamic body driven by the baked
// mass/COM/inertia. Mirrors AddColliderCommand: physics bodies must be
// hierarchy roots, so redo() auto-unparents (snapshotting the pre-attach
// state for undo).
struct GenerateColliderCommand : ICommand {
    explicit GenerateColliderCommand(ecs::Entity entity, float density = 1.0f, bool isStatic = true)
        : entity_(entity), density_(density), isStatic_(isStatic) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override {
        return isStatic_ ? "Generate Static Collider" : "Generate Dynamic Collider";
    }

private:
    ecs::Entity entity_;
    float       density_;
    bool        isStatic_;
    std::string assetPath_;   // asset-relative .bcbvh path (empty until first redo)

    Transform   prevTransform_{};
    bool        capturedTransform_ = false;
    ecs::Entity oldParent_{};
    // Direct children's pre-bake local positions — the COM-recentering pivot
    // compensation (ColliderBaker::applyPivotCompensation) shifts these, so
    // undo() must put them back alongside prevTransform_.
    std::vector<std::pair<ecs::Entity, math::Vec3>> childPrevPositions_;
};
#endif
