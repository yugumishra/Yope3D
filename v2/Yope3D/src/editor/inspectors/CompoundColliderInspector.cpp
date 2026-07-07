#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "world/World.h"
#include "world/ColliderBaker.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include <imgui.h>
#include <filesystem>

// Read-only view of a baked static level collider. Removal goes through
// HullInspector's "Remove Physics Body" (detachPhysicsBody strips the whole
// body, CompoundCollider included) — there's no standalone "remove just this
// component" action, since a compound body without a Hull/Fixed makes no sense.
void drawCompoundColliderComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* cc = static_cast<ecs::CompoundCollider*>(comp);
    if (!ImGui::CollapsingHeader("Compound Collider (Static Level)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::TextDisabled("%s", cc->assetPath[0] ? cc->assetPath : "(unsaved)");
    if (cc->compiled)
        ImGui::Text("Sub-shapes: %zu   BVH nodes: %zu",
                    cc->compiled->subShapes.size(), cc->compiled->nodes.size());
    else
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.f), "Not loaded");

    if (ImGui::Button("Regenerate", ImVec2(-1, 0)) && ctx.world && cc->assetPath[0]) {
        std::string absPath = (std::filesystem::path(YOPE_ASSETS_DIR) / cc->assetPath).string();
        if (ColliderBaker::bakeToFile(*ctx.registry, e, absPath)) {
            cc->compiled = ctx.world->loadCompoundCollider(cc->assetPath, /*forceReload=*/true);
            if (ctx.world->debugPhysics) ctx.world->rebuildDebugMeshes();
        }
    }
    ImGui::TextDisabled("Re-bakes from this entity's current mesh subtree.");
}
#endif
