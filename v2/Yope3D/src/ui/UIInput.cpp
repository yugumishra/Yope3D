#include "UIInput.h"
#include "UILayout.h"
#include "UIHierarchy.h"

namespace {

// Topmost (highest depth) visible UITransform entity whose resolved rect
// contains (cx, cy) — same resolution (anchor/pixel-size, Parent-chain
// composition) and depth-priority the renderer uses, so clicks always land
// where the element is drawn.
ecs::Entity topmostAt(ecs::Registry& reg, float cx, float cy, float screenW, float screenH) {
    ecs::Entity best = ecs::NullEntity;
    int         bestDepth = 0;
    bool        found = false;

    for (auto [e, tf] : reg.view<ecs::UITransform>()) {
        if (!tf.visible) continue;
        if (const auto* btn = reg.get<ecs::UIButton>(e); btn && !btn->enabled) continue;
        // Plain text has no interaction of its own — it's routinely layered on
        // top of (or inside) a button/background purely for display, and being
        // hit-testable would silently steal every click/hover from whatever's
        // underneath it (e.g. a button label parented above its own button,
        // one depth higher so it draws on top, would otherwise "eat" the
        // button's clicks). Wrap text in a UIButton for interactive text.
        if (reg.has<ecs::UIText>(e)) continue;
        ui::ResolvedUIRect resolved = ui::resolveUIRectWorld(reg, e, screenW, screenH);
        if (!resolved.visible) continue;
        if (cx < resolved.min.x || cx > resolved.max.x ||
            cy < resolved.min.y || cy > resolved.max.y) continue;
        if (!found || tf.depth >= bestDepth) {
            best = e;
            bestDepth = tf.depth;
            found = true;
        }
    }
    return best;
}

} // namespace

ecs::Entity UIInputRouter::hitTest(ecs::Registry& reg, float cx, float cy, float screenW, float screenH) {
    return topmostAt(reg, cx, cy, screenW, screenH);
}

void UIInputRouter::update(ecs::Registry& reg, float cx, float cy, float screenW, float screenH,
                            const bool* pressedEdge, const bool* releasedEdge, int numButtons,
                            std::vector<UIInputEvent>& outEvents) {
    consumedThisFrame_ = false;

    ecs::Entity hit = topmostAt(reg, cx, cy, screenW, screenH);

    if (hit != hovered_) {
        if (hovered_ != ecs::NullEntity)
            outEvents.push_back({UIInputEvent::Type::Leave, hovered_, -1});
        if (hit != ecs::NullEntity)
            outEvents.push_back({UIInputEvent::Type::Enter, hit, -1});
        hovered_ = hit;
    }

    for (int b = 0; b < numButtons; ++b) {
        if (pressedEdge[b]) {
            // Any press moves focus: onto the pressed element, or away from
            // whatever was focused if the press lands off any UI (click-away).
            focused_ = hit;
            if (hit != ecs::NullEntity) {
                pressed_ = hit;
                outEvents.push_back({UIInputEvent::Type::Press, hit, b});
                consumedThisFrame_ = true;
            }
        }
        if (releasedEdge[b] && pressed_ != ecs::NullEntity) {
            outEvents.push_back({UIInputEvent::Type::Release, pressed_, b});
            if (pressed_ == hit)
                outEvents.push_back({UIInputEvent::Type::Click, pressed_, b});
            pressed_ = ecs::NullEntity;
        }
    }
}
