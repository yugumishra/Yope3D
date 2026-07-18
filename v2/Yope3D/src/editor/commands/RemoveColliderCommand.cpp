#include "RemoveColliderCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "world/World.h"

RemoveColliderCommand::RemoveColliderCommand(ecs::Entity e, ecs::Registry& reg)
    : entity_(e) {
    if (auto* h = reg.get<ecs::Hull>(e))      hull_    = *h;
    isFixed_ = reg.has<ecs::Fixed>(e);
    if (auto* sf = reg.get<ecs::SphereForm>(e)) { shape_ = Shape::Sphere; radius_ = sf->radius; }
    else if (auto* af = reg.get<ecs::AABBForm>(e)) { shape_ = Shape::AABB;   extent_ = af->extent; }
    else if (auto* of = reg.get<ecs::OBBForm>(e))  { shape_ = Shape::OBB;    extent_ = of->extent; }
}

void RemoveColliderCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(entity_)) return;
    ctx.world->detachPhysicsBody(entity_);
}

void RemoveColliderCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(entity_)) return;
    switch (shape_) {
        case Shape::Sphere: ctx.world->attachSphereCollider(entity_, hull_.mass, radius_,  isFixed_); break;
        case Shape::AABB:   ctx.world->attachAABBCollider  (entity_, hull_.mass, extent_, isFixed_); break;
        case Shape::OBB:    ctx.world->attachOBBCollider   (entity_, hull_.mass, extent_, isFixed_); break;
        default: return;
    }
    // Restore damping, friction, restitution, gravity — the attach* helpers only
    // set mass/inertia, so we overwrite the full hull after.
    if (auto* h = ctx.registry->get<ecs::Hull>(entity_)) *h = hull_;
}
#endif
