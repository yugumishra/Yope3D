#include "editor/panels/StatsPanel.h"
#include "editor/EditorContext.h"
#include "Engine.h"
#include "world/World.h"
#include "ecs/Registry.h"
#include <imgui.h>
#include <GLFW/glfw3.h>

void StatsPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("Stats", &visible);

    if (ctx.engine) {
        ImGui::Text("FPS: %d", ctx.engine->displayFps);

        float dt = ctx.engine->fpsFrames > 0
            ? ctx.engine->fpsAccum / ctx.engine->fpsFrames
            : 0.0f;
        frameHistory_[histIdx_] = dt * 1000.0f;
        histIdx_ = (histIdx_ + 1) % kHistoryLen;
        ImGui::PlotLines("Frame ms", frameHistory_, kHistoryLen, histIdx_,
                         nullptr, 0.0f, 33.3f, ImVec2(0, 50));
    }

    ImGui::Separator();

    if (ctx.registry)
        ImGui::Text("Entities: %zu", ctx.registry->entityCount());

    if (ctx.world) {
        ImGui::Text("Islands: %d", ctx.world->getIslandCount());
        ImGui::Text("Hulls:   %d", ctx.world->getHullCount());
        ImGui::Text("Archetypes: %zu", ctx.registry ? ctx.registry->archetypeCount() : 0);
    }

    ImGui::End();
}
