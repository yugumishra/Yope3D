#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"
#include "math/Vec3.h"

namespace ecs { class Registry; }

// Removes the physics body (Hull + shape form) from an entity.
// Captures enough state to fully restore it on undo.
struct RemoveColliderCommand : ICommand {
    enum class Shape { Sphere, AABB, OBB, Unknown };

    RemoveColliderCommand(ecs::Entity e, ecs::Registry& reg);

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Remove Physics Body"; }

private:
    ecs::Entity entity_;
    ecs::Hull   hull_;
    Shape       shape_   = Shape::Unknown;
    float       radius_  = 0.f;
    math::Vec3  extent_  = {1.f, 1.f, 1.f};
    bool        isFixed_ = false;
};
#endif
