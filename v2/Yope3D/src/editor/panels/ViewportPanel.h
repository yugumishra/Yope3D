#pragma once
#include "editor/EditorPanel.h"
#include "editor/commands/TransformEditSession.h"
#include "world/Transform.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <utility>
#include <vector>

class ViewportPanel : public EditorPanel {
public:
    const char* name() const override { return "Viewport"; }
    void draw(EditorContext& ctx) override;
    bool wantsKeyboard() const override { return true; }

    float sensitivity = 0.002f;
    float speed       = 5.0f;

private:
    void drawContent(EditorContext& ctx);

    bool isMaximized_ = false;

    bool   prevTabDown_  = false;
    int    skipDeltaFrames_ = 0;  // zero delta for N frames after a cursor mode switch
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

    // Multi-select gizmo: full Transform of every selected entity at drag start,
    // keyed by entity. Translate applies a uniform delta; rotate/scale apply the
    // gizmo delta about multiCentroidStart_ (the group pivot).
    std::vector<std::pair<ecs::Entity, Transform>> multiDragStarts_;
    math::Vec3 multiCentroidStart_{};
    // Parallel per-entity anchors captured at drag start for a multi-select SCALE,
    // so collider Forms resize with the Transform (mirrors the single-entity path)
    // and the whole group commits as one undoable compound.
    std::vector<TransformEditAnchor> multiAnchors_;

    // ---- 2D UI gizmo state ----
    // Active drag handle index (see UIHandleIndex enum in .cpp) or -1 if none.
    int             uiDragHandle_   = -1;
    ecs::Entity     uiDragEntity_   = ecs::NullEntity;
    ecs::UITransform uiDragStart_  {};
    // Mouse position in viewport-normalised [0,1] coords at drag start.
    float           uiDragOriginX_ = 0.f;
    float           uiDragOriginY_ = 0.f;
    // Bounds snapshot at drag start for delta computation.
    float           uiDragMinX_    = 0.f, uiDragMinY_ = 0.f;
    float           uiDragMaxX_    = 0.f, uiDragMaxY_ = 0.f;
    // Immediate parent's resolved world rect at drag start (identity if root) —
    // mouse-delta (world/screen space) is divided by this to get the LOCAL
    // fraction delta actually written to minX/minY/maxX/maxY.
    math::Vec2      dragParentMin_  {0.f, 0.f};
    math::Vec2      dragParentSize_ {1.f, 1.f};
};
