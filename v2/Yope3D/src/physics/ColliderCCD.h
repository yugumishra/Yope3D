#pragma once

namespace physics {
class Hull;
struct Barrier;
struct BoundedBarrier;

namespace ColliderCCD {
    // CCD sphere-vs-plane collision. Retained from committed Java codebase.
    // TODO (Milestone 6b): implement CCD logic ported from Java Collider.java.
    void collideBarrier(Hull& one, const Barrier& b, float dt);
    void collideBarrier(Hull& one, const BoundedBarrier& b, float dt);
} // namespace ColliderCCD
} // namespace physics
