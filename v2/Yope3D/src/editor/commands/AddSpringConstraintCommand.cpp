#include "AddSpringConstraintCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"

void AddSpringConstraintCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_) || !ctx.registry->valid(target_)) return;
    if (ctx.registry->has<ecs::SpringConstraint>(src_)) return;  // idempotent
    ctx.registry->add<ecs::SpringConstraint>(src_, {target_, k_, restLength_});
}

void AddSpringConstraintCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_)) return;
    if (ctx.registry->has<ecs::SpringConstraint>(src_))
        ctx.registry->remove<ecs::SpringConstraint>(src_);
}
#endif
