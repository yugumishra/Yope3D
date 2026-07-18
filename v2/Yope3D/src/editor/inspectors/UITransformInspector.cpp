#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include <imgui.h>
#include <climits>

void drawUITransformComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* t = static_cast<ecs::UITransform*>(comp);
    if (!ImGui::CollapsingHeader("UI Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::UITransform before{};

    float min2[2] = {t->minX, t->minY};
    float max2[2] = {t->maxX, t->maxY};

    if (ImGui::DragFloat2("Min (TL)", min2, 0.005f, 0.0f, 1.0f, "%.3f")) {
        t->minX = min2[0]; t->minY = min2[1];
    }
    if (ImGui::IsItemActivated()) before = *t;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
            e, before, *t, "Edit UI Transform Min"));

    if (ImGui::DragFloat2("Max (BR)", max2, 0.005f, 0.0f, 1.0f, "%.3f")) {
        t->maxX = max2[0]; t->maxY = max2[1];
    }
    if (ImGui::IsItemActivated()) before = *t;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
            e, before, *t, "Edit UI Transform Max"));

    if (ImGui::DragInt("Depth", &t->depth, 1)) {}
    if (ImGui::IsItemActivated()) before = *t;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
            e, before, *t, "Edit UI Depth"));

    // Send To Back / Backward / Forward / To Front buttons
    if (ctx.registry && ctx.history) {
        int minDepth = INT_MAX, maxDepth = INT_MIN, prevDepth = INT_MIN, nextDepth = INT_MAX;
        for (auto [oe, ot] : ctx.registry->view<ecs::UITransform>()) {
            if (oe == e) continue;
            if (ot.depth < minDepth) minDepth = ot.depth;
            if (ot.depth > maxDepth) maxDepth = ot.depth;
            if (ot.depth < t->depth && ot.depth > prevDepth) prevDepth = ot.depth;
            if (ot.depth > t->depth && ot.depth < nextDepth) nextDepth = ot.depth;
        }
        bool hasOthers = (minDepth != INT_MAX);

        ImGui::Spacing();
        if (!hasOthers) ImGui::BeginDisabled();
        auto applyDepth = [&](int newDepth) {
            ecs::UITransform nb = *t;
            ecs::UITransform na = *t; na.depth = newDepth;
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
                e, nb, na, "UI Depth Change"));
            t->depth = newDepth;
        };
        if (ImGui::SmallButton("To Back"))    applyDepth(minDepth - 1);
        ImGui::SameLine();
        if (prevDepth == INT_MIN) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Backward"))   applyDepth(prevDepth - 1);
        if (prevDepth == INT_MIN) ImGui::EndDisabled();
        ImGui::SameLine();
        if (nextDepth == INT_MAX) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Forward"))    applyDepth(nextDepth + 1);
        if (nextDepth == INT_MAX) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("To Front"))   applyDepth(maxDepth + 1);
        if (!hasOthers) ImGui::EndDisabled();
    }

    ImGui::Checkbox("Visible", &t->visible);

    ImGui::Spacing();
    ImGui::SeparatorText("Anchor / Sizing");

    static const char* kAnchorNames[] = {
        "Free", "Top Left", "Top Right", "Bottom Left", "Bottom Right", "Center",
        "Center Top", "Center Bottom", "Center Left", "Center Right"
    };
    int anchor = t->anchor;
    if (ImGui::Combo("Anchor", &anchor, kAnchorNames, IM_ARRAYSIZE(kAnchorNames))) {
        before = *t;
        t->anchor = anchor;
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
            e, before, *t, "Edit UI Anchor"));
    }

    if (t->anchor != 0) {
        static const char* kSizeModeNames[] = { "Fraction (Min/Max)", "Fixed Pixels" };
        int sizeMode = t->sizeMode;
        if (ImGui::Combo("Size Mode", &sizeMode, kSizeModeNames, IM_ARRAYSIZE(kSizeModeNames))) {
            before = *t;
            t->sizeMode = sizeMode;
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
                e, before, *t, "Edit UI Size Mode"));
        }

        if (t->sizeMode == 1) {
            float px[2] = {t->pixelWidth, t->pixelHeight};
            if (ImGui::DragFloat2("Pixel Size", px, 1.0f, 0.0f, 8192.0f, "%.0f")) {
                t->pixelWidth = px[0]; t->pixelHeight = px[1];
            }
            if (ImGui::IsItemActivated()) before = *t;
            if (ImGui::IsItemDeactivatedAfterEdit())
                ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
                    e, before, *t, "Edit UI Pixel Size"));
        }

        float offPx[2] = {t->offsetXPx, t->offsetYPx};
        if (ImGui::DragFloat2("Offset (px)", offPx, 1.0f, -8192.0f, 8192.0f, "%.0f")) {
            t->offsetXPx = offPx[0]; t->offsetYPx = offPx[1];
        }
        if (ImGui::IsItemActivated()) before = *t;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITransform>>(
                e, before, *t, "Edit UI Anchor Offset"));
    }
}
#endif
