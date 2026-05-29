#include "editor/panels/InspectorPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "world/RenderMesh.h"
#include <imgui.h>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

static bool drawTransform(Transform& tf, bool uniformScale = false) {
    bool changed = false;
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return false;

    float pos[3] = { tf.position.x, tf.position.y, tf.position.z };
    if (ImGui::DragFloat3("Position", pos, 0.05f)) {
        tf.position = { pos[0], pos[1], pos[2] };
        changed = true;
    }

    // Quaternion → Euler (degrees) for display; write back as quat
    auto& q = tf.rotation;
    float sinr = 2.0f * (q.w*q.x + q.y*q.z);
    float cosr = 1.0f - 2.0f*(q.x*q.x + q.y*q.y);
    float sinp = 2.0f * (q.w*q.y - q.z*q.x);
    float siny = 2.0f * (q.w*q.z + q.x*q.y);
    float cosy = 1.0f - 2.0f*(q.y*q.y + q.z*q.z);
    float eu[3] = {
        std::atan2(sinr, cosr) * 57.2957795f,
        std::asin(std::fabs(sinp) >= 1.f ? std::copysign(1.f, sinp) : sinp) * 57.2957795f,
        std::atan2(siny, cosy) * 57.2957795f
    };
    if (ImGui::DragFloat3("Rotation", eu, 0.5f)) {
        float rx = eu[0]*0.0174532925f, ry = eu[1]*0.0174532925f, rz = eu[2]*0.0174532925f;
        float cx = std::cos(rx*.5f), sx = std::sin(rx*.5f);
        float cy = std::cos(ry*.5f), sy = std::sin(ry*.5f);
        float cz = std::cos(rz*.5f), sz = std::sin(rz*.5f);
        tf.rotation = { sx*cy*cz - cx*sy*sz, cx*sy*cz + sx*cy*sz,
                        cx*cy*sz - sx*sy*cz, cx*cy*cz + sx*sy*sz };
        changed = true;
    }

    if (uniformScale) {
        float s = tf.scale.x;
        if (ImGui::DragFloat("Scale", &s, 0.02f, 0.001f, 1000.f)) {
            tf.scale = { s, s, s };
            changed = true;
        }
    } else {
        float sc[3] = { tf.scale.x, tf.scale.y, tf.scale.z };
        if (ImGui::DragFloat3("Scale", sc, 0.02f, 0.001f, 1000.f)) {
            tf.scale = { sc[0], sc[1], sc[2] };
            changed = true;
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Hull (rigid body)
// ---------------------------------------------------------------------------

static bool drawHull(ecs::Hull& h, ecs::Entity e, ecs::Registry* reg) {
    bool changed = false;
    if (!ImGui::CollapsingHeader("Hull (Rigid Body)", ImGuiTreeNodeFlags_DefaultOpen)) return false;

    if (reg) {
        bool isFixed = reg->has<ecs::Fixed>(e);
        if (ImGui::Checkbox("Fixed (Static)", &isFixed)) {
            if (isFixed) {
                reg->add<ecs::Fixed>(e);
                // add<> migrates the entity to a new archetype — h is now a dangling
                // reference into the old archetype's storage. Re-fetch before writing.
                if (auto* h2 = reg->get<ecs::Hull>(e)) {
                    h2->inverseMass    = 0.0f;
                    h2->inverseInertia = math::Mat3::zero();
                    h2->gravity        = false;
                    h2->velocity       = {};
                    h2->omega          = {};
                }
                // Also strip any lingering Sleeping tag (same migration caveat; do it after Fixed).
                if (reg->has<ecs::Sleeping>(e))
                    reg->remove<ecs::Sleeping>(e);
            } else {
                reg->remove<ecs::Fixed>(e);
                // remove<> also migrates. Re-fetch after.
                if (auto* h2 = reg->get<ecs::Hull>(e)) {
                    h2->inverseMass = (h2->mass > 0.0f) ? 1.0f / h2->mass : 0.0f;
                    h2->gravity     = true;
                }
            }
            changed = true;
            // h is stale after migration — bail out of this draw call.
            // The next frame will start fresh from the new archetype.
            return changed;
        }
        ImGui::SameLine();
    }
    if (ImGui::Checkbox("Gravity", &h.gravity)) changed = true;

    if (ImGui::DragFloat("Mass",            &h.mass,           0.05f, 0.f, 1000.f)) changed = true;
    if (ImGui::DragFloat("Linear Damping",  &h.linearDamping,  0.001f, 0.f, 1.f))   changed = true;
    if (ImGui::DragFloat("Angular Damping", &h.angularDamping, 0.001f, 0.f, 1.f))   changed = true;
    if (ImGui::DragFloat("Friction",        &h.friction,       0.01f,  0.f, 1.f))   changed = true;
    if (ImGui::DragFloat("Restitution",     &h.restitution,    0.01f,  0.f, 1.f))   changed = true;
    if (ImGui::Checkbox("Tangible", &h.tangible))                                    changed = true;

    float vel[3] = { h.velocity.x, h.velocity.y, h.velocity.z };
    if (ImGui::DragFloat3("Velocity", vel, 0.05f)) {
        h.velocity = { vel[0], vel[1], vel[2] };
        changed = true;
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Collider shapes
// ---------------------------------------------------------------------------

static void drawSphereForm(ecs::SphereForm& s) {
    if (!ImGui::CollapsingHeader("Sphere Collider")) return;
    ImGui::TextDisabled("Edit via Transform Scale");
    ImGui::InputFloat("Radius", &s.radius, 0.f, 0.f, "%.3f", ImGuiInputTextFlags_ReadOnly);
}

static void drawAABBForm(ecs::AABBForm& a) {
    if (!ImGui::CollapsingHeader("AABB Collider")) return;
    ImGui::TextDisabled("Edit via Transform Scale");
    float e[3] = { a.extent.x, a.extent.y, a.extent.z };
    ImGui::InputFloat3("Half Extents", e, "%.3f", ImGuiInputTextFlags_ReadOnly);
}

static void drawOBBForm(ecs::OBBForm& o) {
    if (!ImGui::CollapsingHeader("OBB Collider")) return;
    ImGui::TextDisabled("Edit via Transform Scale");
    float e[3] = { o.extent.x, o.extent.y, o.extent.z };
    ImGui::InputFloat3("Half Extents", e, "%.3f", ImGuiInputTextFlags_ReadOnly);
}

// ---------------------------------------------------------------------------
// Light source — shared color/intensity block + type-specific sub-sections
// ---------------------------------------------------------------------------

static void drawLightColorBlock(ecs::LightSource& ls) {
    if (!ImGui::CollapsingHeader("Color & Intensity", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::ColorEdit3("Color", ls.color);
    ImGui::DragFloat("Intensity", &ls.intensity, 0.05f, 0.f, 20.f);
}

static void drawLightPosition(ecs::LightSource& ls) {
    if (!ImGui::CollapsingHeader("Position", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::DragFloat3("##pos", ls.position, 0.05f);
}

static void drawLightDirection(ecs::LightSource& ls) {
    if (!ImGui::CollapsingHeader("Direction", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::DragFloat3("##dir", ls.direction, 0.01f, -1.f, 1.f);
    // Normalize on edit
    float len = std::sqrt(ls.direction[0]*ls.direction[0]
                         +ls.direction[1]*ls.direction[1]
                         +ls.direction[2]*ls.direction[2]);
    if (len > 1e-6f) {
        ls.direction[0] /= len; ls.direction[1] /= len; ls.direction[2] /= len;
    }
    ImGui::TextDisabled("(normalized)");
}

static void drawLightAttenuation(ecs::LightSource& ls) {
    if (!ImGui::CollapsingHeader("Attenuation")) return;
    ImGui::DragFloat("Constant",  &ls.constant,  0.01f, 0.f, 10.f);
    ImGui::DragFloat("Linear",    &ls.linear,    0.001f, 0.f, 1.f);
    ImGui::DragFloat("Quadratic", &ls.quadratic, 0.001f, 0.f, 1.f);
}

static void drawLightCone(ecs::LightSource& ls) {
    if (!ImGui::CollapsingHeader("Cone Angles")) return;
    float inner = ls.innerConeAngle * 57.2957795f;
    float outer = ls.outerConeAngle * 57.2957795f;
    if (ImGui::DragFloat("Inner (deg)", &inner, 0.5f, 0.f, 89.f))
        ls.innerConeAngle = inner * 0.0174532925f;
    if (ImGui::DragFloat("Outer (deg)", &outer, 0.5f, 0.f, 90.f))
        ls.outerConeAngle = outer * 0.0174532925f;
    if (ls.outerConeAngle < ls.innerConeAngle)
        ls.outerConeAngle = ls.innerConeAngle;
}

static void drawLightSource(ecs::LightSource& ls) {
    const char* typeName = ls.type == 0 ? "Point Light"
                         : ls.type == 1 ? "Directional Light"
                         : ls.type == 2 ? "Spot Light"
                         :                "Flash Light";
    if (!ImGui::CollapsingHeader(typeName, ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Indent();
    drawLightColorBlock(ls);
    if (ls.type == 0) { drawLightPosition(ls);  drawLightAttenuation(ls); }
    if (ls.type == 1) { drawLightDirection(ls); }
    if (ls.type == 2) { drawLightPosition(ls);  drawLightDirection(ls);
                        drawLightAttenuation(ls); drawLightCone(ls); }
    if (ls.type == 3) { drawLightAttenuation(ls); drawLightCone(ls); }
    ImGui::Unindent();
}

// ---------------------------------------------------------------------------
// Name / MeshRenderer
// ---------------------------------------------------------------------------

static void drawName(ecs::Name& n) {
    if (!ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::InputText("##name", n.value, sizeof(n.value));
}

static void drawMeshRenderer(ecs::MeshRenderer& mr) {
    if (!ImGui::CollapsingHeader("Mesh Renderer")) return;
    if (mr.mesh) {
        ImGui::TextDisabled("ptr: %p", static_cast<void*>(mr.mesh));
        ImGui::Checkbox("Transform Ready", &mr.mesh->transformReady);
        ImGui::ColorEdit3("Color", mr.mesh->color);
    } else {
        ImGui::TextDisabled("(no mesh)");
    }
}

// ---------------------------------------------------------------------------
// InspectorPanel::draw
// ---------------------------------------------------------------------------

void InspectorPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("Inspector", &visible);

    if (!ctx.selection || !ctx.registry) { ImGui::End(); return; }

    ecs::Entity e = ctx.selection->primary();
    if (!ctx.registry->valid(e)) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Entity %u  gen %u", e.id, e.generation);
    ImGui::Separator();

    bool anyChanged = false;
    bool hasSphere  = ctx.registry->has<ecs::SphereForm>(e);

    if (auto* n  = ctx.registry->get<ecs::Name>(e))         drawName(*n);
    if (auto* tf = ctx.registry->get<Transform>(e))         anyChanged |= drawTransform(*tf, hasSphere);
    if (auto* h  = ctx.registry->get<ecs::Hull>(e))         anyChanged |= drawHull(*h, e, ctx.registry);
    if (auto* s  = ctx.registry->get<ecs::SphereForm>(e))   drawSphereForm(*s);
    if (auto* a  = ctx.registry->get<ecs::AABBForm>(e))     drawAABBForm(*a);
    if (auto* o  = ctx.registry->get<ecs::OBBForm>(e))      drawOBBForm(*o);
    if (auto* mr = ctx.registry->get<ecs::MeshRenderer>(e)) drawMeshRenderer(*mr);
    if (auto* ls = ctx.registry->get<ecs::LightSource>(e))  drawLightSource(*ls);

    // Keep collider shapes in lock-step with Transform.scale.
    if (auto* tf = ctx.registry->get<Transform>(e)) {
        if (auto* sf = ctx.registry->get<ecs::SphereForm>(e)) sf->radius = tf->scale.x;
        if (auto* a  = ctx.registry->get<ecs::AABBForm>(e))   a->extent  = tf->scale;
        if (auto* o  = ctx.registry->get<ecs::OBBForm>(e))    o->extent  = tf->scale;
    }

    // Wake sleeping entities on any inspector edit. The physics sleep-skip skips
    // sleeping bodies entirely, so moving a sleeping object in the editor would
    // leave it frozen in place when Play is pressed.
    if (anyChanged && !ctx.registry->has<ecs::Fixed>(e) &&
        ctx.registry->has<ecs::Sleeping>(e))
    {
        ctx.registry->remove<ecs::Sleeping>(e);
        if (auto* h = ctx.registry->get<ecs::Hull>(e)) {
            h->sleepFrames = 0;
            h->velocity    = {};
            h->omega       = {};
        }
    }

    ImGui::End();
}
