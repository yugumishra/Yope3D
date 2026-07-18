#include "AddSpringConstraintCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"

void AddSpringConstraintCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_) || !ctx.registry->valid(target_)) return;
    if (ctx.registry->has<ecs::SpringConstraint>(src_)) return;  // idempotent
    ctx.registry->add<ecs::SpringConstraint>(src_, {target_, k_, restLength_});
    // Also create the physics spring so the constraint has physical effect.
    if (ctx.world) ctx.world->addSpringPhysics(src_, target_, k_, restLength_);
}

void AddSpringConstraintCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_)) return;
    if (ctx.registry->has<ecs::SpringConstraint>(src_))
        ctx.registry->remove<ecs::SpringConstraint>(src_);
    if (ctx.world) ctx.world->removeSpringBetween(src_, target_);
}
#endif
