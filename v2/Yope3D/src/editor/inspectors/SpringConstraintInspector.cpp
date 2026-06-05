#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "editor/commands/AddSpringConstraintCommand.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "world/World.h"
#include <imgui.h>
#include <cstdio>

void drawSpringConstraintComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* sc = static_cast<ecs::SpringConstraint*>(comp);
    bool removeRequested = false;
    if (!componentHeader("Spring Constraint", "X##sprrem", &removeRequested)) {
        if (removeRequested && ctx.history && ctx.world) {
            ctx.world->removeSpringBetween(e, sc->target);
            ctx.registry->remove<ecs::SpringConstraint>(e);
        }
        return;
    }
    if (removeRequested && ctx.history && ctx.world) {
        ctx.world->removeSpringBetween(e, sc->target);
        ctx.registry->remove<ecs::SpringConstraint>(e);
        return;
    }

    static ecs::SpringConstraint before{};

    // Other entity display (read-only)
    if (ctx.registry && ctx.registry->valid(sc->target)) {
        char label[64] = "Entity #???";
        if (auto* n = ctx.registry->get<ecs::Name>(sc->target))
            std::snprintf(label, sizeof(label), "%s", n->value);
        else
            std::snprintf(label, sizeof(label), "Entity #%u", sc->target.id);
        ImGui::TextDisabled("Target: %s", label);
    } else {
        ImGui::TextDisabled("Target: (none)");
    }

    ImGui::DragFloat("Spring K",     &sc->k,          0.1f,  0.f, 10000.f);
    if (ImGui::IsItemActivated()) before = *sc;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::SpringConstraint>>(e, before, *sc, "Edit Spring K"));

    ImGui::DragFloat("Rest Length",  &sc->restLength,  0.01f, 0.f, 100.f);
    if (ImGui::IsItemActivated()) before = *sc;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::SpringConstraint>>(e, before, *sc, "Edit Rest Length"));
}
#endif
