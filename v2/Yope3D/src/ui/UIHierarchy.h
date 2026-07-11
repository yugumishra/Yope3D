#pragma once
#include "UILayout.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include <vector>

// ---------------------------------------------------------------------------
// UI rect composition over ecs::Parent.
//
// ecs::Parent already links 3D Transforms (see world/TransformHierarchy.h);
// it is reused here for UI grouping too — a UI entity with a Parent that also
// carries a UITransform has its own UITransform resolved as a [0,1] rect
// *local to the parent's resolved rect* (not the full screen), so moving,
// hiding, or fading the parent moves/hides/fades the whole subtree. Root
// entities (no UI-parent) resolve against the real screen, unchanged from
// pre-hierarchy behavior.
//
// Anchor/pixel-size mode (UILayout.h) is resolved against the full screen at
// every level, so a non-Free anchor on a *nested* child anchors to the
// screen, not the parent — anchoring is intended for root-level HUD pinning;
// nested children should stick to Free/fraction mode.
// ---------------------------------------------------------------------------

namespace ui {

struct ResolvedUIRect {
    math::Vec2 min{0.0f, 0.0f}, max{1.0f, 1.0f};
    bool       visible = true;
    float      opacity = 1.0f;
};

inline ResolvedUIRect resolveUIRectWorld(ecs::Registry& reg, ecs::Entity e,
                                          float screenW, float screenH) {
    constexpr int kMaxDepth = 64; // mirrors TransformHierarchy's cap

    // Walk up to the root, collecting the chain leaf-to-root (cycle-safe).
    std::vector<ecs::Entity> chain;
    ecs::Entity cur = e;
    for (int i = 0; i < kMaxDepth && reg.valid(cur); ++i) {
        chain.push_back(cur);
        const auto* p = reg.get<ecs::Parent>(cur);
        if (!p || p->parent == ecs::NullEntity || !reg.has<ecs::UITransform>(p->parent)) break;
        cur = p->parent;
    }

    // Compose root-to-leaf: each level's local rect is normalized into the
    // accumulated space (the "screen" starts as the unit rect [0,1]x[0,1]).
    ResolvedUIRect acc{};
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto* tf = reg.get<ecs::UITransform>(*it);
        if (!tf) continue;

        math::Vec2 localMin, localMax;
        resolveUITransformRect(*tf, screenW, screenH, localMin, localMax);

        math::Vec2 parentSize{acc.max.x - acc.min.x, acc.max.y - acc.min.y};
        math::Vec2 newMin{acc.min.x + localMin.x * parentSize.x, acc.min.y + localMin.y * parentSize.y};
        math::Vec2 newMax{acc.min.x + localMax.x * parentSize.x, acc.min.y + localMax.y * parentSize.y};

        acc.min     = newMin;
        acc.max     = newMax;
        acc.visible = acc.visible && tf->visible;
        acc.opacity *= tf->opacity;
    }
    return acc;
}

} // namespace ui
