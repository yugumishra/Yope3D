#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "world/RenderMesh.h"
#include "assets/ObjLoader.h"
#include <imgui.h>
#include <cstdio>

void drawMeshRendererComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* mr = static_cast<ecs::MeshRenderer*>(comp);
    if (!ImGui::CollapsingHeader("Mesh Renderer")) return;

    if (!mr->mesh) { ImGui::TextDisabled("(no mesh)"); return; }

    // Color — direct edit (visual-only, no physics side-effects)
    ImGui::ColorEdit3("Color", mr->mesh->color);

    // Visibility toggle
    ImGui::Checkbox("Transform Ready", &mr->mesh->transformReady);

    ImGui::Spacing();

    // Mesh path display + drag-drop target.
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

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const char* absPath = static_cast<const char*>(payload->Data);
            try {
                LoadedMesh loaded = ObjLoader::load(absPath);
                if (!loaded.vertices.empty() && ctx.world) {
                    // Use the raw vertex/index overload — this skips setPrimitiveInfo so
                    // the mesh stays PrimitiveType::Custom and cpuVertices/cpuIndices are
                    // retained for raytracer triangle intersection regardless of the OBJ's
                    // internal object name (e.g. Blender exports "o Plane" for any plane).
                    RenderMesh* rm = ctx.world->attachMesh(e, loaded.vertices, loaded.indices);
                    if (rm) rm->sourcePath = absPath;
                }
            } catch (...) {
                // load failure — leave mesh unchanged
            }
        }
        ImGui::EndDragDropTarget();
    }
}
#endif
