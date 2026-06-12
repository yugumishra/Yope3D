#include "editor/panels/ViewportPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "editor/CommandHistory.h"
#include "editor/commands/SetComponentCommand.h"
#include "editor/commands/CompoundCommand.h"
#include "editor/commands/TransformEditSession.h"
#include "editor/picking/IdBufferPass.h"
#include "rendering/ViewportTarget.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "Engine.h"
#include "assets/AssetManager.h"
#include "gpu/Texture.h"
#include "platform/Window.h"
#include "rendering/Camera.h"
#include "math/Mat4.h"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <cmath>
#include <cstring>

// Project a world-space point through the editor camera into pixel coordinates
// inside the viewport image. Returns false if the point is behind the camera.
static bool projectWorldToScreen(const math::Vec3& world,
                                 const math::Mat4& view, const math::Mat4& proj,
                                 ImVec2 imageOrigin, float imageW, float imageH,
                                 ImVec2& outPixel) {
    math::Mat4 vp = proj * view;
    // Column-major Mat4 — treat world as homogeneous (w=1).
    float x = vp.m[0]*world.x + vp.m[4]*world.y + vp.m[8] *world.z + vp.m[12];
    float y = vp.m[1]*world.x + vp.m[5]*world.y + vp.m[9] *world.z + vp.m[13];
    float w = vp.m[3]*world.x + vp.m[7]*world.y + vp.m[11]*world.z + vp.m[15];
    if (w <= 0.0001f) return false;   // behind camera
    float ndcX =  x / w;
    float ndcY = -y / w;   // Vulkan clip-space Y is flipped vs. screen
    outPixel.x = imageOrigin.x + (ndcX * 0.5f + 0.5f) * imageW;
    outPixel.y = imageOrigin.y + (ndcY * 0.5f + 0.5f) * imageH;
    return true;
}

// Try to load an icon texture and wrap it in an ImGui descriptor set.
// Returns nullptr if the file is missing (we just skip drawing that icon).
static void* tryLoadIcon(EditorContext& ctx, const char* relPath) {
    if (!ctx.engine || !ctx.engine->assets || !ctx.engine->gpu) return nullptr;
    Texture* tex = nullptr;
    try { tex = ctx.engine->assets->loadTexture(*ctx.engine->gpu, relPath); }
    catch (...) { return nullptr; }
    if (!tex) return nullptr;
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        tex->getSampler(), tex->getImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return reinterpret_cast<void*>(ds);
}

void ViewportPanel::draw(EditorContext& ctx) {
    // M maximizes / restores the viewport (route-global so it fires from any focus).
    if (ImGui::Shortcut(ImGuiKey_M, ImGuiInputFlags_RouteGlobal)) {
        isMaximized_ = !isMaximized_;
        if (ctx.onViewportMaximize) ctx.onViewportMaximize(isMaximized_);
    }

    if (isMaximized_) {
        ImGuiViewport* mv = ImGui::GetMainViewport();
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowPos(mv->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(mv->WorkSize, ImGuiCond_Always);
        ImGui::SetNextWindowViewport(mv->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##vp_max", nullptr,
            ImGuiWindowFlags_NoDecoration  | ImGuiWindowFlags_NoMove   |
            ImGuiWindowFlags_NoResize      | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        drawContent(ctx);
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();
    drawContent(ctx);
    ImGui::End();
}

void ViewportPanel::drawContent(EditorContext& ctx) {
    bool playing = ctx.playMode && *ctx.playMode;

    GLFWwindow* win = ctx.engine && ctx.engine->window
                    ? ctx.engine->window->getHandle() : nullptr;
    bool cursorCaptured = win &&
        glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

    // Disable ImGui keyboard nav while in mouse-look so Space/Enter can't activate
    // focused widgets (e.g. the Play/Stop button that Tab navigation lands on).
    ImGuiIO& io = ImGui::GetIO();
    if (cursorCaptured)
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    else
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Play / Stop button
    if (ImGui::Button(playing ? "Stop" : "  Play  ")) {
        if (ctx.onTogglePlay) ctx.onTogglePlay();
    }
    ImGui::SameLine();
    if (cursorCaptured)
        ImGui::TextDisabled("MOUSE LOOK  |  Tab to release  Space: up");
    else
        ImGui::TextDisabled(playing ? "PLAYING  |  WASD: fly  Tab: mouse look  Y: stop"
                                    : "EDITING  |  WASD: fly  Tab: mouse look  Y: play");
    // Maximize / restore button
    ImGui::SameLine(0, 16);
    if (ImGui::Button(isMaximized_ ? " [-] " : " [+] ")) {
        isMaximized_ = !isMaximized_;
        if (ctx.onViewportMaximize) ctx.onViewportMaximize(isMaximized_);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(isMaximized_ ? "Restore viewport (M)" : "Maximize viewport (M)");

    // Y toggles play/stop globally (Space is reserved for camera up in mouse-look mode)
    if (!cursorCaptured &&
        ImGui::Shortcut(ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) &&
        ctx.onTogglePlay)
        ctx.onTogglePlay();

    // Viewport texture
    ImVec2 avail = ImGui::GetContentRegionAvail();
    uint32_t w = static_cast<uint32_t>(avail.x > 1 ? avail.x : 1);
    uint32_t h = static_cast<uint32_t>(avail.y > 1 ? avail.y : 1);
    if (ctx.viewportTarget) {
        if (ctx.onViewportResize &&
            (w != ctx.viewportTarget->width() || h != ctx.viewportTarget->height()))
            ctx.onViewportResize(w, h);
        ImTextureID texId = (ImTextureID)(void*)(ctx.viewportTarget->imguiDescriptor());
        ImGui::Image(texId,
            ImVec2(static_cast<float>(ctx.viewportTarget->width()),
                   static_cast<float>(ctx.viewportTarget->height())));
    }

    // Cache image rect immediately after Image — IsItemHovered/GetItemRectMin
    // must be called before any other item is rendered.
    bool   imageHovered = ImGui::IsItemHovered();
    ImVec2 imgTopLeft   = ImGui::GetItemRectMin();

    // Determined by icon hit-test below; suppresses ID buffer readback so the
    // mesh under the icon doesn't overwrite the icon-driven selection a frame later.
    bool iconConsumedClick = false;

    // ---- Hit-test screen-space icons (lights + audio sources) BEFORE the ID
    // buffer click so an icon under the cursor consumes the click. The actual
    // icon draw happens further down (after gizmo overlay) using the same loop.
    if (!playing && ctx.registry && ctx.editorCamera && ctx.viewportTarget &&
        imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGuizmo::IsOver())
    {
        math::Mat4 vw = ctx.editorCamera->genViewMatrix();
        math::Mat4 pr = ctx.editorCamera->genProjectionMatrix();
        pr.m[5] = -pr.m[5];
        float fw = static_cast<float>(ctx.viewportTarget->width());
        float fh = static_cast<float>(ctx.viewportTarget->height());
        const float iconHalf = 12.0f;
        ImVec2 mouse = ImGui::GetMousePos();
        auto hit = [&](const math::Vec3& world) {
            ImVec2 px;
            if (!projectWorldToScreen(world, vw, pr, imgTopLeft, fw, fh, px)) return false;
            return mouse.x >= px.x - iconHalf && mouse.x <= px.x + iconHalf &&
                   mouse.y >= px.y - iconHalf && mouse.y <= px.y + iconHalf;
        };
        bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
        for (auto [le, ls] : ctx.registry->view<ecs::LightSource>()) {
            if (ls.type == 1) continue;
            math::Vec3 p{ls.position[0], ls.position[1], ls.position[2]};
            if (hit(p)) {
                if (ctx.selection) {
                    if (additive) ctx.selection->add(le);
                    else          ctx.selection->set(le);
                }
                iconConsumedClick = true;
                break;
            }
        }
        if (!iconConsumedClick) {
            for (auto [ae, tf, _as] : ctx.registry->view<Transform, ecs::AudioSource>()) {
                if (hit(tf.position)) {
                    if (ctx.selection) {
                        if (additive) ctx.selection->add(ae);
                        else          ctx.selection->set(ae);
                    }
                    iconConsumedClick = true;
                    break;
                }
            }
        }
    }

    // ---- Viewport click → ID buffer picking (skipped when an icon was hit) ----
    if (!playing && ctx.idBufferPass && ctx.viewportTarget && !iconConsumedClick) {
        if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGuizmo::IsOver())
        {
            ImVec2 mousePos  = ImGui::GetMousePos();
            int lx = static_cast<int>(mousePos.x - imgTopLeft.x);
            int ly = static_cast<int>(mousePos.y - imgTopLeft.y);
            if (lx >= 0 && ly >= 0 &&
                static_cast<uint32_t>(lx) < ctx.viewportTarget->width() &&
                static_cast<uint32_t>(ly) < ctx.viewportTarget->height())
            {
                bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                ctx.idBufferPass->scheduleReadback(static_cast<uint32_t>(lx),
                                                   static_cast<uint32_t>(ly),
                                                   additive);
            }
        }
    }

    // ---- Gizmo mode shortcuts (Q/E/R) — GLFW edge-detection to bypass ImGui routing issues.
    // W is skipped (conflicts with WASD camera forward). Fly mode suppresses shortcuts.
    if (!playing && !cursorCaptured && win &&
        (ImGui::IsWindowFocused() || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)))
    {
        bool qDown = glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS;
        bool eDown = glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS;
        bool rDown = glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS;
        if (qDown && !prevQDown_) gizmoOp_ = ImGuizmo::TRANSLATE;
        if (eDown && !prevEDown_) gizmoOp_ = ImGuizmo::ROTATE;
        if (rDown && !prevRDown_) gizmoOp_ = ImGuizmo::SCALE;
        prevQDown_ = qDown; prevEDown_ = eDown; prevRDown_ = rDown;
    } else {
        prevQDown_ = prevEDown_ = prevRDown_ = false;
    }

    // ---- Escape: cancel in-progress gizmo drag OR deselect ----
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (gizmoWasUsing_ && ctx.registry && ctx.registry->valid(gizmoDragEntity_)) {
            if (auto* tf = ctx.registry->get<Transform>(gizmoDragEntity_)) *tf = gizmoDragStart_;
            // Restore anchored collider sizes too — same axes the gizmo would
            // have mutated during a scale drag.
            if (gizmoAnchor_.active && gizmoAnchor_.entity == gizmoDragEntity_) {
                if (gizmoAnchor_.hasSphere)
                    if (auto* sf = ctx.registry->get<ecs::SphereForm>(gizmoDragEntity_)) *sf = gizmoAnchor_.sphereBefore;
                if (gizmoAnchor_.hasAABB)
                    if (auto* a  = ctx.registry->get<ecs::AABBForm>(gizmoDragEntity_))   *a  = gizmoAnchor_.aabbBefore;
                if (gizmoAnchor_.hasOBB)
                    if (auto* o  = ctx.registry->get<ecs::OBBForm>(gizmoDragEntity_))    *o  = gizmoAnchor_.obbBefore;
            }
            gizmoAnchor_.active = false;
            gizmoWasUsing_      = false;
            gizmoDragEntity_    = ecs::NullEntity;
        } else if (ctx.selection) {
            ctx.selection->clear();
        }
    }

    // ---- ImGuizmo overlay ----
    if (!playing && ctx.selection && ctx.registry && ctx.editorCamera) {
        ecs::Entity sel = ctx.selection->primary();
        if (ctx.registry->valid(sel)) {
            // Shared view/proj setup — used by both the Transform and Light gizmo paths.
            math::Mat4 view = ctx.editorCamera->genViewMatrix();
            math::Mat4 proj = ctx.editorCamera->genProjectionMatrix();

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            // Use the image rect (not the full panel window) so gizmos align
            // with the 3D scene and ImGuizmo hit-tests match the visible handles.
            ImGuizmo::SetRect(imgTopLeft.x, imgTopLeft.y,
                              static_cast<float>(ctx.viewportTarget->width()),
                              static_cast<float>(ctx.viewportTarget->height()));

            float viewF[16], projF[16];
            std::memcpy(viewF, &view, sizeof(float)*16);
            std::memcpy(projF, &proj, sizeof(float)*16);
            projF[5] = -projF[5];   // OpenGL Y-up convention for ImGuizmo

            // --- Path A: entity has a Transform (physics bodies, render objects) ---
            if (auto* tf = ctx.registry->get<Transform>(sel)) {
                auto selSpan = ctx.selection->get();
                bool isMulti = (selSpan.size() > 1);

                // Compute centroid of all selected Transform entities.
                math::Vec3 centroid{0.f, 0.f, 0.f};
                int tfCount = 0;
                for (auto e : selSpan) {
                    if (auto* t = ctx.registry->get<Transform>(e)) {
                        centroid = centroid + t->position;
                        ++tfCount;
                    }
                }
                if (tfCount > 0) centroid = centroid * (1.f / float(tfCount));

                // Build a translation-only model matrix at the centroid.
                // Rotate/scale gizmos still work on the primary entity only.
                float modelF[16];
                if (isMulti && gizmoOp_ == ImGuizmo::TRANSLATE) {
                    Transform centTf{};
                    centTf.position = centroid;
                    math::Mat4 m = centTf.getModelMatrix();
                    std::memcpy(modelF, &m, sizeof(float)*16);
                } else {
                    math::Mat4 model = tf->getModelMatrix();
                    std::memcpy(modelF, &model, sizeof(float)*16);
                }

                ImGuizmo::Manipulate(viewF, projF, gizmoOp_, ImGuizmo::WORLD, modelF);
                bool isUsing = ImGuizmo::IsUsing();

                if (isUsing) {
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(modelF, t, r, s);

                    if (isMulti && gizmoOp_ == ImGuizmo::TRANSLATE) {
                        // Multi-select translate: snapshot all on drag start.
                        if (!gizmoWasUsing_) {
                            multiDragStarts_.clear();
                            for (auto e : selSpan) {
                                if (auto* et = ctx.registry->get<Transform>(e))
                                    multiDragStarts_.push_back({e, *et});
                            }
                            multiCentroidStart_ = centroid;
                            gizmoDragEntity_ = sel;
                        }
                        // Apply delta from centroid start to all selected entities.
                        math::Vec3 newCentroid{t[0], t[1], t[2]};
                        math::Vec3 delta = newCentroid - multiCentroidStart_;
                        for (auto& [e, startTf] : multiDragStarts_) {
                            if (auto* et = ctx.registry->get<Transform>(e))
                                et->position = startTf.position + delta;
                        }
                    } else {
                        // Single-entity drag (or rotate/scale even in multi mode).
                        if (!gizmoWasUsing_) {
                            gizmoDragStart_  = *tf;
                            gizmoDragEntity_ = sel;
                            transform_edit::begin(gizmoAnchor_, sel, *ctx.registry);
                        }

                        tf->position = { t[0], t[1], t[2] };
                        float rx = r[0]*0.0174532925f, ry = r[1]*0.0174532925f, rz = r[2]*0.0174532925f;
                        float cx = std::cos(rx*.5f), sx = std::sin(rx*.5f);
                        float cy = std::cos(ry*.5f), sy = std::sin(ry*.5f);
                        float cz = std::cos(rz*.5f), sz = std::sin(rz*.5f);
                        tf->rotation = { sx*cy*cz - cx*sy*sz, cx*sy*cz + sx*cy*sz,
                                         cx*cy*sz - sx*sy*cz, cx*cy*cz + sx*sy*sz };
                        if (ctx.registry->has<ecs::SphereForm>(sel)) {
                            float ddx = std::abs(s[0] - gizmoDragStart_.scale.x);
                            float ddy = std::abs(s[1] - gizmoDragStart_.scale.y);
                            float ddz = std::abs(s[2] - gizmoDragStart_.scale.z);
                            float u = (ddx >= ddy && ddx >= ddz) ? s[0] : (ddy >= ddz ? s[1] : s[2]);
                            u = (u < 0) ? (-std::max(-u, 0.001f)) : (std::max(u, 0.001f));
                            tf->scale = { u, u, u };
                        } else {
                            tf->scale = { s[0], s[1], s[2] };
                        }
                        transform_edit::applyScaleRatio(gizmoAnchor_, sel, *ctx.registry);
                    }
                }

                if (gizmoWasUsing_ && !isUsing) {
                    if (isMulti && gizmoOp_ == ImGuizmo::TRANSLATE && !multiDragStarts_.empty()) {
                        // Commit as a compound of one SetComponentCommand per entity.
                        auto compound = std::make_unique<CompoundCommand>("Gizmo Translate");
                        for (auto& [e, startTf] : multiDragStarts_) {
                            if (auto* et = ctx.registry->get<Transform>(e)) {
                                compound->add(std::make_unique<SetComponentCommand<Transform>>(
                                    e, startTf, *et, "Gizmo Translate"));
                            }
                        }
                        if (ctx.history) ctx.history->execute(ctx, std::move(compound));
                        multiDragStarts_.clear();
                    } else {
                        const char* label =
                            (gizmoOp_ == ImGuizmo::TRANSLATE) ? "Gizmo Translate" :
                            (gizmoOp_ == ImGuizmo::ROTATE)    ? "Gizmo Rotate"    :
                                                                "Gizmo Scale";
                        transform_edit::commit(gizmoAnchor_, gizmoDragEntity_, ctx, label);
                    }
                    gizmoDragEntity_ = ecs::NullEntity;
                }
                gizmoWasUsing_ = isUsing;
            }
            // --- Path B: point or spot light (LightSource, no Transform) ---
            else if (auto* ls = ctx.registry->get<ecs::LightSource>(sel)) {
                if (ls->type == 0 || ls->type == 2) {  // point or spot
                    // Spot supports translate + rotate; point translate only; scale never.
                    bool isSpot = (ls->type == 2);
                    ImGuizmo::OPERATION lightOp =
                        (isSpot && gizmoOp_ == ImGuizmo::ROTATE) ? ImGuizmo::ROTATE
                                                                  : ImGuizmo::TRANSLATE;

                    float modelF[16];
                    if (lightOp == ImGuizmo::ROTATE) {
                        // Build a rotation matrix whose local -Y axis points along ls->direction.
                        // col1 of the matrix = -direction, so extracting col1 after drag = new -dir.
                        float dx = ls->direction[0], dy = ls->direction[1], dz = ls->direction[2];
                        float dl = std::sqrt(dx*dx + dy*dy + dz*dz);
                        if (dl > 0.0001f) { dx /= dl; dy /= dl; dz /= dl; }

                        float c1x = -dx, c1y = -dy, c1z = -dz;

                        // World up: switch to X if direction is near-vertical
                        float wx = 0, wy = 1, wz = 0;
                        if (std::abs(dy) > 0.99f) { wx = 1; wy = 0; wz = 0; }

                        float c2x = c1y*wz - c1z*wy, c2y = c1z*wx - c1x*wz, c2z = c1x*wy - c1y*wx;
                        float c2l = std::sqrt(c2x*c2x + c2y*c2y + c2z*c2z);
                        if (c2l > 0.0001f) { c2x /= c2l; c2y /= c2l; c2z /= c2l; }

                        float c0x = c1y*c2z - c1z*c2y;
                        float c0y = c1z*c2x - c1x*c2z;
                        float c0z = c1x*c2y - c1y*c2x;

                        float tx = ls->position[0], ty = ls->position[1], tz = ls->position[2];
                        modelF[0] =c0x; modelF[1] =c0y; modelF[2] =c0z; modelF[3] =0;
                        modelF[4] =c1x; modelF[5] =c1y; modelF[6] =c1z; modelF[7] =0;
                        modelF[8] =c2x; modelF[9] =c2y; modelF[10]=c2z; modelF[11]=0;
                        modelF[12]=tx;  modelF[13]=ty;  modelF[14]=tz;  modelF[15]=1;
                    } else {
                        Transform lightTf{};
                        lightTf.position = { ls->position[0], ls->position[1], ls->position[2] };
                        math::Mat4 model = lightTf.getModelMatrix();
                        std::memcpy(modelF, &model, sizeof(float)*16);
                    }

                    ImGuizmo::Manipulate(viewF, projF, lightOp, ImGuizmo::WORLD, modelF);
                    bool isUsing = ImGuizmo::IsUsing();

                    if (isUsing) {
                        if (!gizmoWasUsing_) {
                            lightDragStart_  = *ls;
                            gizmoDragEntity_ = sel;
                        }

                        if (lightOp == ImGuizmo::TRANSLATE) {
                            float t[3], r[3], s[3];
                            ImGuizmo::DecomposeMatrixToComponents(modelF, t, r, s);
                            ls->position[0] = t[0];
                            ls->position[1] = t[1];
                            ls->position[2] = t[2];
                        } else {
                            // New direction = -col1 of result (col1 = modelF[4..6])
                            float nx = -modelF[4], ny = -modelF[5], nz = -modelF[6];
                            float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
                            if (nl > 0.001f) { nx /= nl; ny /= nl; nz /= nl; }
                            ls->direction[0] = nx;
                            ls->direction[1] = ny;
                            ls->direction[2] = nz;
                        }
                    }

                    if (gizmoWasUsing_ && !isUsing) {
                        if (ctx.history && ctx.registry->valid(gizmoDragEntity_)) {
                            if (auto* lsNow = ctx.registry->get<ecs::LightSource>(gizmoDragEntity_)) {
                                ctx.history->execute(ctx,
                                    std::make_unique<SetComponentCommand<ecs::LightSource>>(
                                        gizmoDragEntity_, lightDragStart_, *lsNow, "Edit Light"));
                            }
                        }
                        gizmoDragEntity_ = ecs::NullEntity;
                    }
                    gizmoWasUsing_ = isUsing;
                }
            }
        }
    }

    // ---- 2D UI gizmo (UITransform entities only) ----
    // Draws a bounding-box outline + 9 drag handles in screen space.
    // Handles: 0=center (translate), 1=TL, 2=TR, 3=BR, 4=BL (resize corners),
    //          5=Top, 6=Right, 7=Bottom, 8=Left (resize edge midpoints).
    if (!playing && ctx.selection && ctx.registry && ctx.viewportTarget) {
        ecs::Entity sel = ctx.selection->primary();
        if (ctx.registry->valid(sel) &&
            !ctx.registry->has<Transform>(sel) &&   // 2D path — skip if 3D entity
            ctx.registry->has<ecs::UITransform>(sel))
        {
            auto* uiTf = ctx.registry->get<ecs::UITransform>(sel);
            float vpW = static_cast<float>(ctx.viewportTarget->width());
            float vpH = static_cast<float>(ctx.viewportTarget->height());

            // Convert [0,1] UITransform coords → ImGui screen pixels.
            auto vpToScreen = [&](float u, float v) -> ImVec2 {
                return { imgTopLeft.x + u * vpW,
                         imgTopLeft.y + v * vpH };
            };
            // Reverse: screen pixels → [0,1] viewport-normalized.
            auto screenToVP = [&](float sx, float sy, float& u, float& v) {
                u = (sx - imgTopLeft.x) / vpW;
                v = (sy - imgTopLeft.y) / vpH;
            };

            float x0 = uiTf->minX, y0 = uiTf->minY;
            float x1 = uiTf->maxX, y1 = uiTf->maxY;
            float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;

            ImVec2 handles[9] = {
                vpToScreen(mx, my),  // 0: center
                vpToScreen(x0, y0),  // 1: TL
                vpToScreen(x1, y0),  // 2: TR
                vpToScreen(x1, y1),  // 3: BR
                vpToScreen(x0, y1),  // 4: BL
                vpToScreen(mx, y0),  // 5: Top
                vpToScreen(x1, my),  // 6: Right
                vpToScreen(mx, y1),  // 7: Bottom
                vpToScreen(x0, my),  // 8: Left
            };

            ImVec2 tl = vpToScreen(x0, y0);
            ImVec2 br = vpToScreen(x1, y1);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float kHandleR  = 5.0f;
            const float kHoverR   = 8.0f;
            ImU32 colBox  = IM_COL32(100, 200, 255, 200);
            ImU32 colHandleNorm = IM_COL32(255, 255, 255, 220);
            ImU32 colHandleHov  = IM_COL32(100, 220, 255, 255);

            // Bounding rectangle outline
            dl->AddRect(tl, br, colBox, 0.0f, 0, 1.5f);

            ImVec2 mouse = ImGui::GetMousePos();

            // Detect hover and drag-start
            bool mouseOverViewport = imageHovered;
            bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

            // Find which handle (if any) the mouse is over
            int hoveredHandle = -1;
            if (mouseOverViewport) {
                for (int i = 0; i < 9; ++i) {
                    float dx = mouse.x - handles[i].x;
                    float dy = mouse.y - handles[i].y;
                    if (dx*dx + dy*dy <= kHoverR*kHoverR) {
                        hoveredHandle = i;
                        break;
                    }
                }
            }

            // Drag start
            if (mouseClicked && hoveredHandle >= 0 && uiDragHandle_ < 0) {
                uiDragHandle_  = hoveredHandle;
                uiDragEntity_  = sel;
                uiDragStart_   = *uiTf;
                uiDragMinX_    = x0; uiDragMinY_ = y0;
                uiDragMaxX_    = x1; uiDragMaxY_ = y1;
                float originU, originV;
                screenToVP(mouse.x, mouse.y, originU, originV);
                uiDragOriginX_ = originU;
                uiDragOriginY_ = originV;
            }

            // Live drag
            if (uiDragHandle_ >= 0 && uiDragEntity_ == sel && mouseDown) {
                float curU, curV;
                screenToVP(mouse.x, mouse.y, curU, curV);
                float du = curU - uiDragOriginX_;
                float dv = curV - uiDragOriginY_;

                auto clamp01 = [](float v){ return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

                switch (uiDragHandle_) {
                    case 0:  // translate all
                        uiTf->minX = clamp01(uiDragMinX_ + du);
                        uiTf->minY = clamp01(uiDragMinY_ + dv);
                        uiTf->maxX = clamp01(uiDragMaxX_ + du);
                        uiTf->maxY = clamp01(uiDragMaxY_ + dv);
                        break;
                    case 1:  // TL
                        uiTf->minX = clamp01(uiDragMinX_ + du);
                        uiTf->minY = clamp01(uiDragMinY_ + dv);
                        break;
                    case 2:  // TR
                        uiTf->maxX = clamp01(uiDragMaxX_ + du);
                        uiTf->minY = clamp01(uiDragMinY_ + dv);
                        break;
                    case 3:  // BR
                        uiTf->maxX = clamp01(uiDragMaxX_ + du);
                        uiTf->maxY = clamp01(uiDragMaxY_ + dv);
                        break;
                    case 4:  // BL
                        uiTf->minX = clamp01(uiDragMinX_ + du);
                        uiTf->maxY = clamp01(uiDragMaxY_ + dv);
                        break;
                    case 5:  // Top edge
                        uiTf->minY = clamp01(uiDragMinY_ + dv);
                        break;
                    case 6:  // Right edge
                        uiTf->maxX = clamp01(uiDragMaxX_ + du);
                        break;
                    case 7:  // Bottom edge
                        uiTf->maxY = clamp01(uiDragMaxY_ + dv);
                        break;
                    case 8:  // Left edge
                        uiTf->minX = clamp01(uiDragMinX_ + du);
                        break;
                }

                // Recalculate handles for current frame draw with updated uiTf
                x0 = uiTf->minX; y0 = uiTf->minY;
                x1 = uiTf->maxX; y1 = uiTf->maxY;
                mx = (x0+x1)*0.5f; my = (y0+y1)*0.5f;
                handles[0] = vpToScreen(mx, my);
                handles[1] = vpToScreen(x0, y0); handles[2] = vpToScreen(x1, y0);
                handles[3] = vpToScreen(x1, y1); handles[4] = vpToScreen(x0, y1);
                handles[5] = vpToScreen(mx, y0); handles[6] = vpToScreen(x1, my);
                handles[7] = vpToScreen(mx, y1); handles[8] = vpToScreen(x0, my);
                tl = handles[1]; br = handles[3];
                dl->AddRect(tl, br, colBox, 0.0f, 0, 1.5f);
            }

            // Drag end — commit undoable command
            if (!mouseDown && uiDragHandle_ >= 0 && uiDragEntity_ == sel) {
                if (ctx.history && ctx.registry->valid(uiDragEntity_)) {
                    if (auto* t = ctx.registry->get<ecs::UITransform>(uiDragEntity_)) {
                        ctx.history->execute(ctx,
                            std::make_unique<SetComponentCommand<ecs::UITransform>>(
                                uiDragEntity_, uiDragStart_, *t, "Move UI Element"));
                    }
                }
                uiDragHandle_ = -1;
                uiDragEntity_ = ecs::NullEntity;
            }

            // Draw handles on top of the outline
            for (int i = 0; i < 9; ++i) {
                ImU32 col = (i == hoveredHandle || i == uiDragHandle_)
                            ? colHandleHov : colHandleNorm;
                // Center handle: circle (translate); others: squares (resize)
                if (i == 0) {
                    dl->AddCircleFilled(handles[i], kHandleR, col);
                    dl->AddCircle(handles[i], kHandleR, IM_COL32(0,0,0,180), 0, 1.5f);
                } else {
                    ImVec2 a{handles[i].x - kHandleR, handles[i].y - kHandleR};
                    ImVec2 b{handles[i].x + kHandleR, handles[i].y + kHandleR};
                    dl->AddRectFilled(a, b, col);
                    dl->AddRect(a, b, IM_COL32(0,0,0,180), 0.0f, 0, 1.5f);
                }
            }
        }
    }

    // ---- Screen-space icons (lights + audio sources) ----
    // Drawn via ImGui draw list so they overlay the viewport image. Clickable
    // for selection. Loaded lazily — missing icons silently no-op.
    // Suppressed in play mode — these are editor-only authoring affordances.
    if (!playing && ctx.viewportTarget && ctx.editorCamera && ctx.registry) {
        if (!iconsLoaded_) {
            iconLight_   = tryLoadIcon(ctx, "icons/light.png");
            iconSpeaker_ = tryLoadIcon(ctx, "icons/speaker.png");
            iconsLoaded_ = true;
        }

        math::Mat4 view = ctx.editorCamera->genViewMatrix();
        math::Mat4 proj = ctx.editorCamera->genProjectionMatrix();
        proj.m[5] = -proj.m[5];  // match viewport's flipped-Y convention

        float fw = static_cast<float>(ctx.viewportTarget->width());
        float fh = static_cast<float>(ctx.viewportTarget->height());
        const float iconSize = 24.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        auto drawIcon = [&](const math::Vec3& worldPos, void* tex,
                            ecs::Entity entity, ImU32 fallbackColor) {
            ImVec2 px;
            if (!projectWorldToScreen(worldPos, view, proj, imgTopLeft, fw, fh, px))
                return;
            // Clip to viewport rect
            if (px.x < imgTopLeft.x || px.x > imgTopLeft.x + fw ||
                px.y < imgTopLeft.y || px.y > imgTopLeft.y + fh) return;

            bool selected = ctx.selection && ctx.selection->contains(entity);
            ImVec2 a{px.x - iconSize*0.5f, px.y - iconSize*0.5f};
            ImVec2 b{px.x + iconSize*0.5f, px.y + iconSize*0.5f};
            if (tex) {
                ImU32 tint = selected ? IM_COL32(255, 220, 80, 255)
                                      : IM_COL32(255, 255, 255, 230);
                dl->AddImage((ImTextureID)tex, a, b, ImVec2(0,0), ImVec2(1,1), tint);
            } else {
                // Fallback: filled circle so icons are still visible if PNGs are missing.
                ImU32 col = selected ? IM_COL32(255, 220, 80, 230) : fallbackColor;
                dl->AddCircleFilled(px, iconSize*0.5f, col);
                dl->AddCircle(px, iconSize*0.5f, IM_COL32(0,0,0,160), 0, 1.5f);
            }

            // Click-to-select handled earlier (see iconConsumedClick block).
            (void)entity;
        };

        for (auto [le, ls] : ctx.registry->view<ecs::LightSource>()) {
            // Directional lights have no position; skip them.
            if (ls.type == 1) continue;
            math::Vec3 pos{ls.position[0], ls.position[1], ls.position[2]};
            drawIcon(pos, iconLight_, le, IM_COL32(255, 230, 120, 230));
        }
        for (auto [ae, tf, as] : ctx.registry->view<Transform, ecs::AudioSource>()) {
            drawIcon(tf.position, iconSpeaker_, ae, IM_COL32(120, 200, 255, 230));
        }
    }

    // ---- Camera controls ----
    if (!ctx.editorCamera || !win || !ctx.engine || !ctx.engine->input) {
        ImGui::End();
        return;
    }

    // Tab: toggle mouse look (cursor capture). glfwGetKey bypasses ImGui routing.
    bool tabDown      = glfwGetKey(win, GLFW_KEY_TAB) == GLFW_PRESS;
    bool tabJustPressed = tabDown && !prevTabDown_;
    prevTabDown_      = tabDown;

    if (tabJustPressed) {
        if (cursorCaptured) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            int winW, winH;
            glfwGetWindowSize(win, &winW, &winH);
            double targetX = winW * 0.5, targetY = winH * 0.5;
            glfwSetCursorPos(win, targetX, targetY);
            prevCursorX_ = targetX;
            prevCursorY_ = targetY;
            cursorCaptured = false;
            skipDeltaFrames_ = 2;
        } else if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(win, &prevCursorX_, &prevCursorY_);
            cursorCaptured = true;
            // 2 frames: frame 0 covers the glfwGetCursorPos baseline mismatch (GLFW's
            // virtual cursor resets to a different origin than the physical pos we just
            // read), frame 1 covers any OS event-queue lag that delivers buffered
            // movement after the mode switch.
            skipDeltaFrames_ = 2;
        }
    }

    // Sample cursor position directly — ImGui's GLFW backend early-returns its
    // cursor-pos callback (without chaining) when GLFW_CURSOR_DISABLED is set,
    // so Input::getMouseDelta() would always be zero in mouse-look mode.
    double cx, cy;
    glfwGetCursorPos(win, &cx, &cy);
    bool skip = skipDeltaFrames_ > 0;
    double dx = skip ? 0.0 : (cx - prevCursorX_);
    double dy = skip ? 0.0 : (cy - prevCursorY_);
    if (skipDeltaFrames_ > 0) --skipDeltaFrames_;
    prevCursorX_ = cx;
    prevCursorY_ = cy;

    float dt  = ImGui::GetIO().DeltaTime;
    auto& cam = *ctx.editorCamera;
    (void)dt; // used by WASD below

    // Mouse look — only when cursor is captured
    if (cursorCaptured) {
        math::Vec3 rot = cam.getRotation();
        rot.y += static_cast<float>(dx) * -sensitivity;
        rot.x += static_cast<float>(dy) * -sensitivity;
        constexpr float kPitchLimit = 1.5607963f;
        if (rot.x >  kPitchLimit) rot.x =  kPitchLimit;
        if (rot.x < -kPitchLimit) rot.x = -kPitchLimit;
        cam.setRotation(rot);
    }

    // WASD movement — fires whenever the viewport window has ImGui focus
    // OR when cursor is captured (so mouse-look mode keeps movement active).
    // glfwGetKey reads raw hardware state, bypassing ImGui's keyboard routing.
    if (ImGui::IsWindowFocused() || cursorCaptured) {
        math::Vec3 rot = cam.getRotation();
        float sy = std::sin(rot.y), cy = std::cos(rot.y);
        math::Vec3 forward{-sy, 0.f, -cy};
        math::Vec3 right  { cy, 0.f, -sy};
        math::Vec3 move{};

        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) move = move + forward;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) move = move - forward;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) move = move + right;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) move = move - right;
        // Up/down only in mouse-look mode so Space doesn't fight the play/stop toggle
        if (cursorCaptured) {
            if (glfwGetKey(win, GLFW_KEY_SPACE)      == GLFW_PRESS) move.y += 1.f;
            if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) move.y -= 1.f;
        }

        float len = std::sqrt(move.x*move.x + move.y*move.y + move.z*move.z);
        if (len > 0.f) {
            float s = speed * dt / len;
            cam.setPosition(cam.getPosition() + move * s);
        }
    }
}
