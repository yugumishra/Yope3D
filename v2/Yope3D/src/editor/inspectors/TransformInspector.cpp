#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "editor/commands/CompoundCommand.h"
#include "editor/commands/TransformEditSession.h"
#include "world/Transform.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include <imgui.h>
#include <cmath>

void drawTransformComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* tf = static_cast<Transform*>(comp);
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

    // Single shared anchor across all three drags. ImGui::DragFloat3 widgets
    // are exclusive — only one can be active at a time — so reusing the same
    // anchor is fine and means every Transform mutation flows through one
    // canonical begin/apply/commit pipeline (the same one the viewport gizmo
    // uses).
    static TransformEditAnchor anchor;

    // ---- Position ----
    float pos[3] = { tf->position.x, tf->position.y, tf->position.z };
    ImGui::DragFloat3("Position", pos, 0.05f);
    if (ImGui::IsItemActivated()) transform_edit::begin(anchor, e, *ctx.registry);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        tf->position = { pos[0], pos[1], pos[2] };
        transform_edit::commit(anchor, e, ctx, "Edit Position");
    } else if (ImGui::IsItemActive()) {
        tf->position = { pos[0], pos[1], pos[2] };
    }

    // ---- Rotation (display as Euler degrees) ----
    auto& q = tf->rotation;
    float sinr = 2.f*(q.w*q.x + q.y*q.z), cosr = 1.f - 2.f*(q.x*q.x + q.y*q.y);
    float sinp = 2.f*(q.w*q.y - q.z*q.x);
    float siny = 2.f*(q.w*q.z + q.x*q.y), cosy = 1.f - 2.f*(q.y*q.y + q.z*q.z);
    float eu[3] = {
        std::atan2(sinr, cosr) * 57.2957795f,
        std::asin(std::fabs(sinp) >= 1.f ? std::copysign(1.f, sinp) : sinp) * 57.2957795f,
        std::atan2(siny, cosy) * 57.2957795f
    };
    ImGui::DragFloat3("Rotation", eu, 0.5f);
    auto eulerToQuat = [](const float eu[3]) {
        float rx = eu[0]*0.0174532925f, ry = eu[1]*0.0174532925f, rz = eu[2]*0.0174532925f;
        float cx = std::cos(rx*.5f), sx = std::sin(rx*.5f);
        float cy = std::cos(ry*.5f), sy = std::sin(ry*.5f);
        float cz = std::cos(rz*.5f), sz = std::sin(rz*.5f);
        return math::Quat{ sx*cy*cz - cx*sy*sz, cx*sy*cz + sx*cy*sz,
                           cx*cy*sz - sx*sy*cz, cx*cy*cz + sx*sy*sz };
    };
    if (ImGui::IsItemActivated()) transform_edit::begin(anchor, e, *ctx.registry);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        tf->rotation = eulerToQuat(eu);
        transform_edit::commit(anchor, e, ctx, "Edit Rotation");
    } else if (ImGui::IsItemActive()) {
        tf->rotation = eulerToQuat(eu);
    }

    // ---- Scale ----
    // Uniform if entity has SphereForm. applyScaleRatio inside the anchor
    // pipeline scales any present collider proportionally to its pre-edit
    // size, so a snapped (5, 0.1, 5) OBB on a custom mesh stays correctly
    // fitted as the user drags scale (e.g. 2× → (10, 0.2, 10)) instead of
    // collapsing back to tf.scale.
    bool uniformScale = ctx.registry && ctx.registry->has<ecs::SphereForm>(e);
    if (uniformScale) {
        float s = tf->scale.x;
        ImGui::DragFloat("Scale", &s, 0.02f, 0.001f, 1000.f);
        if (ImGui::IsItemActivated()) transform_edit::begin(anchor, e, *ctx.registry);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            tf->scale = { s, s, s };
            transform_edit::applyScaleRatio(anchor, e, *ctx.registry);
            transform_edit::commit(anchor, e, ctx, "Edit Scale");
        } else if (ImGui::IsItemActive()) {
            tf->scale = { s, s, s };
            transform_edit::applyScaleRatio(anchor, e, *ctx.registry);
        }
    } else {
        float sc[3] = { tf->scale.x, tf->scale.y, tf->scale.z };
        ImGui::DragFloat3("Scale", sc, 0.02f, 0.001f, 1000.f);
        if (ImGui::IsItemActivated()) transform_edit::begin(anchor, e, *ctx.registry);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            tf->scale = { sc[0], sc[1], sc[2] };
            transform_edit::applyScaleRatio(anchor, e, *ctx.registry);
            transform_edit::commit(anchor, e, ctx, "Edit Scale");
        } else if (ImGui::IsItemActive()) {
            tf->scale = { sc[0], sc[1], sc[2] };
            transform_edit::applyScaleRatio(anchor, e, *ctx.registry);
        }
    }

    // Wake sleeping entity on Transform edit (only on edit commit).
    if (!ctx.registry->has<ecs::Fixed>(e) && ctx.registry->has<ecs::Sleeping>(e)) {
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ctx.registry->remove<ecs::Sleeping>(e);
            if (auto* h = ctx.registry->get<ecs::Hull>(e)) { h->sleepFrames = 0; h->velocity = {}; h->omega = {}; }
        }
    }
}
#endif
