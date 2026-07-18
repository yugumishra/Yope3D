#pragma once
#include "ecs/Entity.h"
#include "physics/ContactInfo.h"
#include <cstdint>

// Global collision observers (limitations.md §4.5): a central listener that can
// see collisions between bodies carrying no ScriptComponent — for "spawn a
// particle / play a thud wherever these two kinds of thing collide" systems that
// per-entity callbacks can't express. Registered from Python via
//   yope3d.observe_collisions(layer_mask, cb)   # cb(contact) -> None
// where a pair is observed only if (hullA.collisionLayer | hullB.collisionLayer)
// intersects layer_mask — so the observed set stays bounded to the layers you
// subscribe to and never fires on every pair in the scene.
//
// The py::function storage + registry live in the pybind TU (bindings_world.cpp);
// this header stays pybind-free so core (Engine, SceneManager) can include it.
namespace collisionobs {

// OR of every registered observer's layer mask (0 when none). Engine feeds this to
// World::setCollisionObserveMask each frame so detectCollisionEvents lets the
// matching pairs into the diff (and stays a pure no-op when nobody is subscribed).
uint32_t observerMask();

// Invoke every observer whose mask intersects `layers` (= layerA | layerB of the
// colliding pair). Cheap no-op when no observers are registered. Call on the main
// thread with the GIL available (Engine's collision-drain loop qualifies).
void dispatch(ecs::Entity a, ecs::Entity b, bool enter,
              const physics::ContactInfo& contact, uint32_t layers);

// Drop all observers — called when scripts are torn down (scene unload / editor
// Stop) so registrations don't leak or double up across scene loads.
void clear();

} // namespace collisionobs
