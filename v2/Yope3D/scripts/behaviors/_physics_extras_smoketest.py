"""Physics-extras smoketest (limitations.md §4.2 / §4.3 / §4.5).

One process, one scene. A single Coordinator entity builds the whole test in
init(), lets the sim run, then asserts once at t>3.5s. Watch the console for
[phyx] lines; a final [phyx] ALL PASS / FAILURES ABOVE summarizes.

What it proves:

  * §4.3 Moving platforms — a kinematic box (add_kinematic_box) driven by a
    script-set Hull.velocity carries a dynamic rider up with it: the rider rises
    with the platform and stays resting on top (never falls through or is left
    behind), and the platform itself moved (kinematic integration works).

  * §4.2 Contact data — the Coordinator gets a dynamic collider that falls onto
    the floor; its own 5-arg on_collision_enter(..., contact) receives a nonzero
    impulse and a contact point/normal (the arity-aware optional 5th arg).

  * §4.5 Global observer + layer scoping — yope3d.observe_collisions(LAYER_DEBRIS,
    cb) fires cb(contact) for a scriptless sphere-vs-box pair opted in via
    Hull.observe_layers=LAYER_DEBRIS (proving scriptless pairs are visible AND
    contact data reaches the observer), while a second scriptless pair on a
    DIFFERENT observe channel never reaches the callback. Crucially, the floor /
    platform / rider / coordinator all keep the default observe_layers=0 and are
    NEVER observed despite their default all-bits collision layers — that opt-in
    default is the fix for the "subscribing observes every default body" footgun.
"""
import math
import yope3d
from yope3d import Vec3

LAYER_DEBRIS = 1 << 5
LAYER_OTHER  = 1 << 6

_STATE = {"ran": False}


def _observe(entity, layer):
    """Opt a body into observation on `layer`. Hull.observe_layers defaults to 0
    (never observed), so bodies we DON'T call this on — the coordinator, floor,
    platform, rider, all on default all-bits collision layers — stay invisible to
    the LAYER_DEBRIS observer. That is the whole point of the dedicated field."""
    h = yope3d.reg_get(entity, "Hull")
    if h:
        h.observe_layers = layer


class PhysicsExtrasSmoketest:
    def init(self, world, entity, params):
        self.t = 0.0
        self.checked = False

        # --- §4.2: give ourselves a falling dynamic body so our own 5-arg
        #     on_collision_enter receives contact data when we hit the floor. ---
        self.self_impulse = 0.0
        self.self_point = None
        self.self_normal = None
        world.attach_aabb_collider(entity, 1.0, Vec3(0.5, 0.5, 0.5), False)
        tf = yope3d.reg_get(entity, "Transform")
        if tf:
            tf.position = Vec3(-4, 3, 0)

        # Floor (static). Left on default collision layers AND observe_layers=0 —
        # its (many) contacts must never reach the LAYER_DEBRIS observer. If the
        # dedicated observe field weren't opt-in, every all-layers body hitting the
        # floor would show up as a foreign observed pair (the old footgun).
        world.add_static_aabb(Vec3(0, -1, 0), Vec3(30, 1, 30))

        # --- §4.3: moving platform + dynamic rider. Both default (unobserved). ---
        self.platform = world.add_kinematic_box(Vec3(0, 1, 0), Vec3(2, 0.25, 2))
        world.attach_box_mesh(self.platform, Vec3(2, 0.25, 2), 0.6, 0.6, 0.7)
        self.platform_start_y = 1.0

        self.rider = world.add_aabb(Vec3(0.5, 0.5, 0.5), 1.0, Vec3(0, 2.0, 0))
        world.attach_box_mesh(self.rider, Vec3(0.5, 0.5, 0.5), 0.9, 0.4, 0.2)

        # --- §4.5: two isolated scriptless pairs, one observed, one not. ---
        self.debris_hits = 0
        self.debris_impulse = 0.0
        self.debris_point = None
        self.observed_foreign = False   # set True if a non-debris entity ever appears
        yope3d.observe_collisions(LAYER_DEBRIS, self.on_debris)

        # Debris pair (observe_layers=LAYER_DEBRIS): sphere drops onto its own box.
        # Default collision layers (they physically collide with the floor etc. —
        # doesn't matter, the floor isn't observed).
        d_sphere = world.add_sphere(1.0, 0.4, Vec3(8, 4, 0))
        world.attach_sphere_mesh(d_sphere, 0.4, 0.3, 0.8, 0.9)
        d_box = world.add_static_aabb(Vec3(8, 1.0, 0), Vec3(0.8, 0.5, 0.8))
        self.debris_ids = {d_sphere.id, d_box.id}
        for e in (d_sphere, d_box):
            _observe(e, LAYER_DEBRIS)

        # Foreign pair (observe_layers=LAYER_OTHER): identical setup, different
        # observe channel. Must never reach the LAYER_DEBRIS observer.
        f_sphere = world.add_sphere(1.0, 0.4, Vec3(-8, 4, 0))
        f_box = world.add_static_aabb(Vec3(-8, 1.0, 0), Vec3(0.8, 0.5, 0.8))
        self.foreign_ids = {f_sphere.id, f_box.id}
        for e in (f_sphere, f_box):
            _observe(e, LAYER_OTHER)

    # ---- callbacks ----
    def on_debris(self, contact):
        if not contact.enter:
            return
        # Scoping: every observed entity must be a debris entity.
        if contact.a.id not in self.debris_ids or contact.b.id not in self.debris_ids:
            self.observed_foreign = True
        self.debris_hits += 1
        if contact.impulse > self.debris_impulse:
            self.debris_impulse = contact.impulse
            self.debris_point = contact.point

    def on_collision_enter(self, world, entity, other, contact):
        # §4.2 per-entity 5-arg path: record our landing impulse.
        if contact.impulse > self.self_impulse:
            self.self_impulse = contact.impulse
            self.self_point = contact.point
            self.self_normal = contact.normal

    # ---- drive + assert ----
    def update(self, world, entity, dt):
        self.t += dt
        h = yope3d.reg_get(self.platform, "Hull")
        if h:
            h.velocity = Vec3(0, 0.4, 0)   # steady rise; solver carries the rider

        if self.checked or _STATE["ran"] or self.t < 3.5:
            return
        self.checked = True
        _STATE["ran"] = True
        self._run(world)

    def _run(self, world):
        ok = True
        ptf = yope3d.reg_get(self.platform, "Transform")
        rtf = yope3d.reg_get(self.rider, "Transform")
        py = ptf.position.y if ptf else None
        ry = rtf.position.y if rtf else None

        # §4.3 moving platform
        ok &= self._c("platform integrated upward", py is not None and py > self.platform_start_y + 0.8, True)
        ok &= self._c("rider carried up (not left behind)", ry is not None and ry > 2.6, True)
        ok &= self._c("rider rests on top (not fallen through)",
                      py is not None and ry is not None and (ry - py) > 0.4, True)

        # §4.2 contact data (per-entity 5-arg)
        ok &= self._c("self landing impulse > 0", self.self_impulse > 0.0, True)
        ok &= self._c("self contact point present", self.self_point is not None, True)
        ok &= self._c("self contact normal present", self.self_normal is not None, True)

        # §4.5 global observer + scoping + contact data
        ok &= self._c("observer saw scriptless debris pair", self.debris_hits >= 1, True)
        ok &= self._c("observer contact impulse > 0", self.debris_impulse > 0.0, True)
        ok &= self._c("observer contact point present", self.debris_point is not None, True)
        ok &= self._c("observer never saw foreign (off-mask) pair", self.observed_foreign, False)

        print(f"[phyx] {'ALL PASS' if ok else 'FAILURES ABOVE'}")

    def _c(self, label, got, want):
        if got == want:
            print(f"[phyx]   ok   {label}: {got!r}")
            return True
        print(f"[phyx]   FAIL {label}: got {got!r}, want {want!r}")
        return False
