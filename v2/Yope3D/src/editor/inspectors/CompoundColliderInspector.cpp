#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "world/World.h"
#include "world/ColliderBaker.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include <imgui.h>
#include <filesystem>

// Removal goes through HullInspector's "Remove Physics Body" (detachPhysicsBody
// strips the whole body, CompoundCollider included) — there's no standalone
// "remove just this component" action, since a compound body without a
// Hull/Fixed makes no sense.
void drawCompoundColliderComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* cc = static_cast<ecs::CompoundCollider*>(comp);
    const char* header = cc->isStatic ? "Compound Collider (Static)" : "Compound Collider (Dynamic)";
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::TextDisabled("%s", cc->assetPath[0] ? cc->assetPath : "(unsaved)");
    if (cc->compiled)
        ImGui::Text("Sub-shapes: %zu   BVH nodes: %zu",
                    cc->compiled->subShapes.size(), cc->compiled->nodes.size());
    else
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.f), "Not loaded");
    if (cc->compiled && !cc->isStatic)
        ImGui::Text("Total mass: %.3f", cc->compiled->totalMass);

    ImGui::DragFloat("Density", &cc->density, 0.05f, 0.001f, 1000.0f, "%.3f");
    ImGui::Checkbox("Static", &cc->isStatic);

    if (ImGui::Button("Regenerate", ImVec2(-1, 0)) && ctx.world && cc->assetPath[0]) {
        std::string absPath = (std::filesystem::path(YOPE_ASSETS_DIR) / cc->assetPath).string();
        if (ColliderBaker::bakeToFile(*ctx.registry, e, absPath, /*leafSize=*/4, cc->density)) {
            physics::CompiledCollider* compiled =
                ctx.world->loadCompoundCollider(cc->assetPath, /*forceReload=*/true);
            ctx.world->attachCompoundCollider(e, compiled, cc->assetPath,
                                              /*mass=*/0.0f, cc->isStatic, cc->density);
            // Re-bake recentered sub-shapes to a (possibly new) mass-weighted
            // centroid — keep root + its direct children's rendered mesh in
            // sync with it (see ColliderBaker::applyPivotCompensation).
            if (compiled) ColliderBaker::applyPivotCompensation(*ctx.registry, e, compiled->pivotOffset);
            if (ctx.world->debugPhysics) ctx.world->rebuildDebugMeshes();
        }
    }
    ImGui::TextDisabled("Re-bakes from this entity's current mesh subtree with the settings above.");
}
#endif
