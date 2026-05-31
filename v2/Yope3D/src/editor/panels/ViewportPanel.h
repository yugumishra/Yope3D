#pragma once
#include "editor/EditorPanel.h"
#include "editor/commands/TransformEditSession.h"
#include "world/Transform.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"
#include <imgui.h>
#include <ImGuizmo.h>

class ViewportPanel : public EditorPanel {
public:
    const char* name() const override { return "Viewport"; }
    void draw(EditorContext& ctx) override;
    bool wantsKeyboard() const override { return true; }

    float sensitivity = 0.002f;
    float speed       = 5.0f;

private:
    bool   prevTabDown_  = false;
    double prevCursorX_  = 0.0;
    double prevCursorY_  = 0.0;

    // Gizmo state
    ImGuizmo::OPERATION gizmoOp_       = ImGuizmo::TRANSLATE;
    bool                gizmoWasUsing_ = false;
    Transform           gizmoDragStart_{};
    ecs::Entity         gizmoDragEntity_ = ecs::NullEntity;

    // Saved LightSource state for light-position drag undo
    ecs::LightSource lightDragStart_{};

    // Pre-edit anchor for the Transform-path gizmo drag. Shared pipeline with
    // TransformInspector's DragFloat3 widgets — same begin/applyScaleRatio/commit
    // semantics, so a snapped collider survives a SCALE-mode gizmo drag and
    // every gizmo edit produces the same kind of undoable compound command.
    TransformEditAnchor gizmoAnchor_{};

    // GLFW edge-detection for gizmo mode keys (Q/E/R)
    bool prevQDown_ = false;
    bool prevEDown_ = false;
    bool prevRDown_ = false;

    // Cached ImGui descriptor sets for the world-space sprite icons drawn over
    // lights and audio sources. Loaded lazily on first frame; nullptr if the
    // texture is missing (e.g. user hasn't placed assets/icons/light.png yet).
    void* iconLight_   = nullptr;
    void* iconSpeaker_ = nullptr;
    bool  iconsLoaded_ = false;
};
