#pragma once
#ifdef YOPE_PYTHON
#include "ecs/Entity.h"
#include "physics/ContactInfo.h"

// The Python-facing collision payload, bound as `yope3d.Contact` (see
// bindings_ecs.cpp). Constructed both by PythonScript (for the arity-aware
// on_collision_enter/exit 5th argument) and by the global collision-observer
// dispatch (bindings_world.cpp / CollisionObservers). Read-only from Python.
//
//   a, b     — the colliding entity pair. For a per-entity callback, `a` is the
//              receiving entity (`self`) and `b` is `other`, and `normal` is
//              oriented from `b` toward `a` (i.e. the direction that separates
//              self from other). For the global observer, `a`/`b` are the pair in
//              the solver's stored order and `normal` points from `a` toward `b`.
//   enter    — True on a contact-begin event, False on contact-end.
//   point    — deepest contact point (world space); zero on exit.
//   normal   — unit contact normal (see orientation note above); zero on exit.
//   impulse  — accumulated normal impulse this tick (impact strength); 0 on exit
//              and for trigger overlaps (which never reach the solver).
struct PyContact {
    ecs::Entity          a{};
    ecs::Entity          b{};
    bool                 enter = false;
    physics::ContactInfo info{};
};
#endif // YOPE_PYTHON
