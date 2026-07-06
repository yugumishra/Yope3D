#include "editor/inspectors/InspectorRegistry.h"
#include "assets/GltfLoader.h"
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
#include <iostream>
#include <cstring>
#include <filesystem>
#include <vector>

// Swap the mesh on an existing entity. For multi-primitive glTF, only the first
// primitive is used — importing a full model (all primitives + materials) is the
// job of the "Import Model" menu / viewport drop (ImportModelCommand).
static void applyMesh(EditorContext& ctx, ecs::Entity e, ecs::MeshRenderer* mr,
                      const char* absPath) {
    std::string before = (mr->mesh ? mr->mesh->sourcePath : "");

    try {
        std::string ext = std::filesystem::path(absPath).extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(c));

        LoadedMesh loaded;
        if (ext == ".glb" || ext == ".gltf") {
            std::vector<LoadedMesh> meshes = GltfLoader::load(absPath);
            if (!meshes.empty()) loaded = std::move(meshes[0]);
        } else {
            loaded = ObjLoader::load(absPath);
        }
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
    ImGui::TextDisabled("(drop model here)");
    ImGui::SameLine();
    if (ImGui::Button("...##mesh_pick")) {
        if (auto p = FileDialog::openFile({{"Model", "obj,glb,gltf"}}))
            applyMesh(ctx, e, mr, p->c_str());
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            applyMesh(ctx, e, mr, static_cast<const char*>(payload->Data));
        ImGui::EndDragDropTarget();
    }
}
#endif
