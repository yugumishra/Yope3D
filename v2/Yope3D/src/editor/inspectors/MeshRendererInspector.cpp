#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/CommandHistory.h"
#include "editor/FileDialog.h"
#include "editor/commands/SetAssetCommands.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "world/RenderMesh.h"
#include "assets/ObjLoader.h"
#include <imgui.h>
#include <cstdio>

static void applyMesh(EditorContext& ctx, ecs::Entity e, ecs::MeshRenderer* mr,
                      const char* absPath) {
    std::string before = (mr->mesh ? mr->mesh->sourcePath : "");
    try {
        LoadedMesh loaded = ObjLoader::load(absPath);
        if (!loaded.vertices.empty() && ctx.world) {
            RenderMesh* rm = ctx.world->attachMesh(e, loaded.vertices, loaded.indices);
            if (rm) {
                rm->sourcePath = absPath;
                if (ctx.history)
                    ctx.history->execute(ctx,
                        std::make_unique<SetMeshCommand>(e, before, absPath));
            }
        }
    } catch (...) {}
}

void drawMeshRendererComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* mr = static_cast<ecs::MeshRenderer*>(comp);
    if (!ImGui::CollapsingHeader("Mesh Renderer")) return;

    if (!mr->mesh) { ImGui::TextDisabled("(no mesh)"); return; }

    // Color — direct edit (visual-only, no physics side-effects)
    ImGui::ColorEdit3("Color", mr->mesh->color);

    // Visibility toggle
    ImGui::Checkbox("Transform Ready", &mr->mesh->transformReady);

    ImGui::Spacing();

    // Mesh path display + drag-drop target + browse button.
    // Show primitive type name if available, otherwise raw pointer address.
    const char* meshLabel = "custom mesh";
    switch (mr->mesh->primitiveType) {
        case PrimitiveType::Sphere:    meshLabel = "sphere";    break;
        case PrimitiveType::Icosphere: meshLabel = "icosphere"; break;
        case PrimitiveType::Cube:      meshLabel = "cube";      break;
        case PrimitiveType::Rect:      meshLabel = "rect";      break;
        case PrimitiveType::Plane:     meshLabel = "plane";     break;
        default: break;
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf), "Mesh: %s", meshLabel);
    ImGui::Selectable(buf, false, ImGuiSelectableFlags_None, ImVec2(0, 0));
    ImGui::SameLine();
    ImGui::TextDisabled("(drop .obj here)");
    ImGui::SameLine();
    if (ImGui::Button("...##mesh_pick")) {
        if (auto p = FileDialog::openFile({{"OBJ Mesh", "obj"}}))
            applyMesh(ctx, e, mr, p->c_str());
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            applyMesh(ctx, e, mr, static_cast<const char*>(payload->Data));
        ImGui::EndDragDropTarget();
    }
}
#endif
