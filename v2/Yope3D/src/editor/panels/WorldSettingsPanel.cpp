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

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Normal Bias", &ctx.world->shadowNormalBias, 0.0f, 0.5f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##shadowNormalBias")) ctx.world->shadowNormalBias = 0.035f;
        ImGui::TextDisabled("World-space offset along the surface normal (main acne fix)");

        ImGui::SliderFloat("Depth Bias", &ctx.world->shadowBias, 0.0f, 0.02f, "%.4f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##shadowBias")) ctx.world->shadowBias = 0.0006f;
        ImGui::TextDisabled("Extra safety margin on top of normal bias");

        ImGui::SliderFloat("PCF Radius", &ctx.world->shadowPcfRadius, 0.0f, 4.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##shadowPcfRadius")) ctx.world->shadowPcfRadius = 1.0f;
        ImGui::TextDisabled("Softening kernel spread, in shadow-texel multiples");

        ImGui::SliderFloat("Ortho Half-Extent", &ctx.world->shadowOrthoHalfExtent, 2.0f, 100.0f, "%.1f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##shadowOrthoHalfExtent")) ctx.world->shadowOrthoHalfExtent = 20.0f;
        ImGui::TextDisabled("Directional caster's shadow box size (camera-centered); smaller = sharper");

        ImGui::SliderFloat("Ortho Far", &ctx.world->shadowOrthoFar, 1.0f, 200.0f, "%.1f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##shadowOrthoFar")) ctx.world->shadowOrthoFar = 40.0f;

        ImGui::SliderFloat("Spot Near", &ctx.world->shadowSpotNear, 0.05f, 10.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##shadowSpotNear")) ctx.world->shadowSpotNear = 1.0f;
        ImGui::TextDisabled("Spot caster's near plane; keep as large as the scene allows for depth precision");

        ImGui::SliderFloat("Spot Far", &ctx.world->shadowSpotFar, 1.0f, 200.0f, "%.1f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##shadowSpotFar")) ctx.world->shadowSpotFar = 30.0f;
        ImGui::TextDisabled("Spot caster's far plane; no larger than the light's actual reach");
    }

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
