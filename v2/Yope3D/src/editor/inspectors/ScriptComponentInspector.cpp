#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include "scripting/Script.h"
#include <imgui.h>
#include <cstring>

void drawScriptComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* sc = static_cast<ecs::ScriptComponent*>(comp);
    if (!ImGui::CollapsingHeader("Script Component", ImGuiTreeNodeFlags_DefaultOpen)) return;

    char classBuf[64];
    std::strncpy(classBuf, sc->scriptClass, sizeof(classBuf));
    ImGui::LabelText("Script Class", "%s", classBuf);

    // paramsBlob editable as a multiline text area
    static char paramsBuf[2048];
    static ecs::ScriptComponent beforeEdit{};
    ImGui::Text("Params (JSON):");
    std::strncpy(paramsBuf, sc->paramsBlob, sizeof(paramsBuf));
    if (ImGui::InputTextMultiline("##params", paramsBuf, sizeof(paramsBuf),
                                  ImVec2(-1, 80))) {
        std::strncpy(sc->paramsBlob, paramsBuf, sizeof(sc->paramsBlob) - 1);
    }
    if (ImGui::IsItemActivated()) beforeEdit = *sc;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
            e, beforeEdit, *sc, "Edit Script Params"));

    if (sc->instance) {
        ImGui::Spacing();
        sc->instance->drawInspector(ctx);
    }
}
#endif
