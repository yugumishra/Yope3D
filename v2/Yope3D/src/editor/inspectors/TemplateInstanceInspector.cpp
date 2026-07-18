#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include <imgui.h>

// Read-only provenance display for a template-spawned subtree, with an
// "Unpack" escape hatch (just removes the marker component) for hand-editing
// an instance's structure beyond what field-level overrides support — once
// unpacked, "Save as Template"/"Save Scene" treats the subtree as plain data.
void drawTemplateInstanceComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* ti = static_cast<ecs::TemplateInstance*>(comp);
    if (!ImGui::CollapsingHeader("Template Instance", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextWrapped("Instance of: %s", ti->sourcePath);
    if (ImGui::Button("Unpack")) {
        ctx.history->execute(ctx,
            std::make_unique<RemoveComponentCommand<ecs::TemplateInstance>>(e, *ti, "Unpack Template Instance"));
    }
}
#endif
