#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "editor/commands/AddPointJointCommand.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "world/World.h"
#include <imgui.h>
#include <cstdio>

void drawPointJointConstraintComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* pj = static_cast<ecs::PointJointConstraint*>(comp);
    bool removeRequested = false;
    if (!componentHeader("Point Joint", "X##ptjrem", &removeRequested)) {
        if (removeRequested && ctx.history && ctx.world) {
            ctx.world->removeJointBetween(e, pj->target);
            ctx.registry->remove<ecs::PointJointConstraint>(e);
        }
        return;
    }
    if (removeRequested && ctx.history && ctx.world) {
        ctx.world->removeJointBetween(e, pj->target);
        ctx.registry->remove<ecs::PointJointConstraint>(e);
        return;
    }

    static ecs::PointJointConstraint before{};

    // Other entity display (read-only)
    if (ctx.registry && ctx.registry->valid(pj->target)) {
        char label[64] = "Entity #???";
        if (auto* n = ctx.registry->get<ecs::Name>(pj->target))
            std::snprintf(label, sizeof(label), "%s", n->value);
        else
            std::snprintf(label, sizeof(label), "Entity #%u", pj->target.id);
        ImGui::TextDisabled("Target: %s", label);
    } else {
        ImGui::TextDisabled("Target: (none)");
    }

    ImGui::DragFloat3("Local Anchor A", &pj->localAnchorA.x, 0.01f);
    if (ImGui::IsItemActivated()) before = *pj;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::PointJointConstraint>>(e, before, *pj, "Edit Joint Anchor A"));
        if (ctx.world) ctx.world->resyncPointJoint(e, *pj);
    }

    ImGui::DragFloat3("Local Anchor B", &pj->localAnchorB.x, 0.01f);
    if (ImGui::IsItemActivated()) before = *pj;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::PointJointConstraint>>(e, before, *pj, "Edit Joint Anchor B"));
        if (ctx.world) ctx.world->resyncPointJoint(e, *pj);
    }
}
#endif
