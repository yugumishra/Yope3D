#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/ReparentCommand.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include <imgui.h>
#include <cstdio>
#include <memory>

// Read-only view of the transform-hierarchy parent, plus a Detach button.
// Parenting itself happens via drag-drop in the Hierarchy panel (no Add-Component
// entry), so this inspector only shows the current parent and lets you clear it.
void drawParentComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* p = static_cast<ecs::Parent*>(comp);
    if (!ImGui::CollapsingHeader("Parent", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (ctx.registry && ctx.registry->valid(p->parent)) {
        char label[80] = "Entity #???";
        if (auto* n = ctx.registry->get<ecs::Name>(p->parent))
            std::snprintf(label, sizeof(label), "%s", n->value);
        else
            std::snprintf(label, sizeof(label), "Entity #%u", p->parent.id);
        ImGui::TextDisabled("Parent: %s", label);
    } else {
        ImGui::TextDisabled("Parent: (none)");
    }

    if (ImGui::Button("Detach to Root")) {
        if (ctx.history && ReparentCommand::canReparent(*ctx.registry, e, ecs::NullEntity))
            ctx.history->execute(ctx, std::make_unique<ReparentCommand>(e, ecs::NullEntity));
    }
}
#endif
