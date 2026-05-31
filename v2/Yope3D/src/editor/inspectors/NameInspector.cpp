#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include <imgui.h>
#include <cstring>

void drawNameComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* n = static_cast<ecs::Name*>(comp);
    if (!ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::Name before{};
    ImGui::InputText("##name", n->value, sizeof(n->value));
    if (ImGui::IsItemActivated()) before = *n;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::Name>>(e, before, *n, "Rename Entity"));
}
#endif
