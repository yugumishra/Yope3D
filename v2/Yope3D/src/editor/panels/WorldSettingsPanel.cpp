#include "editor/panels/WorldSettingsPanel.h"
#include "editor/EditorContext.h"
#include "world/World.h"
#include "Engine.h"
#include <imgui.h>

void WorldSettingsPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("World Settings", &visible);

    if (!ctx.world) { ImGui::End(); return; }

    float g[3] = { ctx.world->gravity.x, ctx.world->gravity.y, ctx.world->gravity.z };
    if (ImGui::DragFloat3("Gravity", g, 0.1f))
        ctx.world->gravity = { g[0], g[1], g[2] };

    ImGui::SliderFloat("Exposure", &ctx.world->exposure, 0.0f, 4.0f, "%.2f");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##exposure")) ctx.world->exposure = 1.0f;

    if (ctx.engine) {
        const char* modes[] = { "RASTER", "RAYTRACE" };
        int mode = static_cast<int>(ctx.engine->renderMode_);
        if (ImGui::Combo("Render Mode", &mode, modes, 2))
            ctx.engine->renderMode_ = static_cast<RenderMode>(mode);
    }

    if (ImGui::Checkbox("Debug Physics", &ctx.world->debugPhysics)) {
        if (ctx.world->debugPhysics)
            ctx.world->rebuildDebugMeshes();
    }

    ImGui::End();
}
