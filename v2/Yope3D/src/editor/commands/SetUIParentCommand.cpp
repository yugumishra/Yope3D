#include "editor/commands/SetUIParentCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "world/TransformHierarchy.h"
#include "ui/UIHierarchy.h"

namespace {
void applyUIParent(ecs::Registry& reg, ecs::Entity e, ecs::Entity parent) {
    if (parent == ecs::NullEntity) {
        if (reg.has<ecs::Parent>(e)) reg.remove<ecs::Parent>(e);
    } else if (auto* p = reg.get<ecs::Parent>(e)) {
        p->parent = parent;
    } else {
        reg.add<ecs::Parent>(e, ecs::Parent{parent});
    }
}
} // namespace

bool SetUIParentCommand::canReparent(ecs::Registry& reg, ecs::Entity entity, ecs::Entity newParent) {
    if (!reg.valid(entity) || !reg.has<ecs::UITransform>(entity)) return false;
    if (newParent == ecs::NullEntity) return true;
    if (!reg.valid(newParent) || !reg.has<ecs::UITransform>(newParent)) return false;
    if (newParent == entity) return false;
    // No cycles: newParent may not be entity or one of its descendants.
    if (hierarchy::isDescendantOf(reg, newParent, entity)) return false;
    return true;
}

void SetUIParentCommand::redo(EditorContext& ctx) {
    ecs::Registry& reg = *ctx.registry;
    if (!canReparent(reg, entity_, newParent_)) return;

    auto* tf = reg.get<ecs::UITransform>(entity_);
    if (!tf) return;

    if (!captured_) {
        oldParent_ = reg.has<ecs::Parent>(entity_) ? reg.get<ecs::Parent>(entity_)->parent
                                                    : ecs::NullEntity;
        before_ = *tf;

        ui::ResolvedUIRect worldRect = ui::resolveUIRectWorld(reg, entity_, screenW_, screenH_);
        ui::ResolvedUIRect parentRect = (newParent_ != ecs::NullEntity)
            ? ui::resolveUIRectWorld(reg, newParent_, screenW_, screenH_)
            : ui::ResolvedUIRect{}; // identity [0,1]x[0,1] screen rect

        math::Vec2 parentSize{parentRect.max.x - parentRect.min.x, parentRect.max.y - parentRect.min.y};
        after_ = before_;
        after_.anchor   = 0; // Free
        after_.sizeMode = 0; // Fraction
        if (parentSize.x > 1e-5f && parentSize.y > 1e-5f) {
            after_.minX = (worldRect.min.x - parentRect.min.x) / parentSize.x;
            after_.minY = (worldRect.min.y - parentRect.min.y) / parentSize.y;
            after_.maxX = (worldRect.max.x - parentRect.min.x) / parentSize.x;
            after_.maxY = (worldRect.max.y - parentRect.min.y) / parentSize.y;
        }
        captured_ = true;
    }

    applyUIParent(reg, entity_, newParent_);
    *tf = after_;
}

void SetUIParentCommand::undo(EditorContext& ctx) {
    ecs::Registry& reg = *ctx.registry;
    if (!reg.valid(entity_)) return;
    applyUIParent(reg, entity_, oldParent_);
    if (auto* t = reg.get<ecs::UITransform>(entity_)) *t = before_;
}
#endif
