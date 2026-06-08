#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "math/Vec3.h"
#include "world/Transform.h"

// Adds a physics body (Hull + shape) to an entity that currently has none.
// Undo restores the entity to its physics-free state and rolls back the
// Transform scale change that attach writes.
struct AddColliderCommand : ICommand {
    enum class Shape { Sphere, AABB, OBB, Capsule, Cylinder };

    AddColliderCommand(Shape shape, ecs::Entity entity,
                       float mass, math::Vec3 extent, bool isStatic = false)
        : shape_(shape), entity_(entity),
          mass_(mass), extent_(extent), isStatic_(isStatic) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Add Collider"; }

private:
    Shape       shape_;
    ecs::Entity entity_;
    float       mass_;
    math::Vec3  extent_;    // extent.x = radius for spheres
    bool        isStatic_;

    // Captured on first redo so undo can restore the exact pre-add transform
    Transform   prevTransform_{};
    bool        capturedTransform_ = false;
};
#endif
