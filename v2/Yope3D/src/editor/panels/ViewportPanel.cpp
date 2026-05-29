#include "editor/panels/ViewportPanel.h"
#include "editor/EditorContext.h"
#include "rendering/ViewportTarget.h"
#include "Engine.h"
#include "platform/Window.h"
#include "rendering/Camera.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <cmath>

void ViewportPanel::draw(EditorContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    bool playing = ctx.playMode && *ctx.playMode;

    GLFWwindow* win = ctx.engine && ctx.engine->window
                    ? ctx.engine->window->getHandle() : nullptr;
    bool cursorCaptured = win &&
        glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

    // Play / Stop button
    if (ImGui::Button(playing ? "Stop" : "  Play  ")) {
        if (ctx.onTogglePlay) ctx.onTogglePlay();
    }
    ImGui::SameLine();
    if (cursorCaptured)
        ImGui::TextDisabled("MOUSE LOOK  |  Tab to release");
    else
        ImGui::TextDisabled(playing ? "PLAYING  |  WASD: fly  Tab: mouse look"
                                    : "EDITING  |  WASD: fly  Tab: mouse look");

    // Space toggles play/stop when NOT in mouse-look mode
    if (!cursorCaptured && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Space))
        if (ctx.onTogglePlay) ctx.onTogglePlay();

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
            cursorCaptured = false;
        } else if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            // Seed previous position so the first frame has zero delta.
            glfwGetCursorPos(win, &prevCursorX_, &prevCursorY_);
            cursorCaptured = true;
        }
    }

    // Sample cursor position directly — ImGui's GLFW backend early-returns its
    // cursor-pos callback (without chaining) when GLFW_CURSOR_DISABLED is set,
    // so Input::getMouseDelta() would always be zero in mouse-look mode.
    double cx, cy;
    glfwGetCursorPos(win, &cx, &cy);
    double dx = cx - prevCursorX_;
    double dy = cy - prevCursorY_;
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

    ImGui::End();
}
