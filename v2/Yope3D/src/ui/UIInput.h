#pragma once
#include <vector>
#include "ecs/Entity.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"

// ---------------------------------------------------------------------------
// UIInputRouter — per-frame pointer routing for ECS UI entities.
//
// Hit-tests view<UITransform>() against the cursor (topmost by depth wins,
// mirroring the depth-sort in Renderer::buildECSUIGeometry), diffs hover/press
// state frame-to-frame, and emits UIInputEvents for World/Engine to dispatch to
// each target entity's ScriptComponent. Runs on the main thread only — no
// locking beyond what the caller already holds around the registry.
// ---------------------------------------------------------------------------

struct UIInputEvent {
    enum class Type { Press, Release, Enter, Leave, Click };
    Type        type;
    ecs::Entity entity;
    int         button;
};

class UIInputRouter {
public:
    // cx,cy: cursor position in [0,1] screen fractions. screenW,screenH: pixel
    // dimensions (needed to resolve anchored/pixel-sized UITransforms the same
    // way the renderer does — see ui::resolveUITransformRect). pressedEdge/
    // releasedEdge: per-button one-shot transition flags for this frame
    // (index = GLFW button code, length numButtons). Appends events to outEvents.
    void update(ecs::Registry& reg, float cx, float cy, float screenW, float screenH,
                const bool* pressedEdge, const bool* releasedEdge, int numButtons,
                std::vector<UIInputEvent>& outEvents);

    // Re-runs the topmost point-in-rect test only (no state mutation) — backs
    // the polled World::uiHitTest / yope3d.ui_hit_test binding.
    static ecs::Entity hitTest(ecs::Registry& reg, float cx, float cy, float screenW, float screenH);

    ecs::Entity hovered() const { return hovered_; }
    ecs::Entity pressed() const { return pressed_; }
    bool        consumedClick() const { return consumedThisFrame_; }

    // Focus (text input, Phase 2).
    ecs::Entity focused() const { return focused_; }
    void        setFocus(ecs::Entity e) { focused_ = e; }

private:
    ecs::Entity hovered_ = ecs::NullEntity;
    ecs::Entity pressed_ = ecs::NullEntity;
    ecs::Entity focused_ = ecs::NullEntity;
    bool        consumedThisFrame_ = false;
};
