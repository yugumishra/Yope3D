"""Tier-3 scripting-extensions smoketest — collision events + comms + timers.

Attach to ANY entity and press Play. It makes itself a static spherical target
(if it isn't already a physics body), then drops a projectile onto itself so the
on_collision_enter callback fires automatically — no aiming or input needed.

Watch the console:
  - [tier3] ENTER ...   when a projectile lands on the target  (on_collision_enter)
  - [tier3] event bus   the _events subscriber receiving the same hit
  - [tier3] 3s timer    a _timers.after callback
  - [tier3] EXIT ...    if the projectile bounces off  (on_collision_exit)
Press R to drop another projectile.

Exercises: on_collision_enter/exit, get_behavior, _events bus, _timers scheduler.
"""
import yope3d

try:
    from behaviors import _events, _timers
except ImportError:               # depending on how scripts/ is on sys.path
    import _events, _timers


class Tier3Smoketest:
    PARAMS = {}

    def init(self, world, entity, params):
        self.hits = 0
        # Make self a tangible, visible static target even on a bare entity.
        if not yope3d.reg_has(entity, "Hull"):
            world.attach_sphere_collider(entity, 0.0, 1.0, True)   # static, radius 1
            world.attach_sphere_mesh(entity, 1.0, 0.8, 0.2, 0.2)
        self.pos = yope3d.get_position(entity) or yope3d.Vec3(0, 2, 0)

        _events.subscribe("target_hit", self._on_any_hit)
        _timers.after(3.0, lambda: print("[tier3] 3s timer fired"))
        self._drop()
        print(f"[tier3] init target={entity} at {self.pos}")

    def _drop(self):
        p = self.pos
        proj = yope3d.world.add_sphere(1.0, 0.4, yope3d.Vec3(p.x, p.y + 8.0, p.z))
        yope3d.world.attach_sphere_mesh(proj, 0.4, 0.3, 0.5, 1.0)
        yope3d.reg_get(proj, "Hull").velocity = yope3d.Vec3(0, -6, 0)  # nudge down

    def on_collision_enter(self, world, entity, other):
        self.hits += 1
        world.set_mesh_color(entity, 0.2, 1.0, 0.2)
        print(f"[tier3] ENTER hit#{self.hits} {entity} <- {other} "
              f"behavior(other)={yope3d.get_behavior(other)}")
        _events.emit("target_hit", entity, self.hits)

    def on_collision_exit(self, world, entity, other):
        print(f"[tier3] EXIT {entity} <-/-> {other}")

    def _on_any_hit(self, target, n):
        print(f"[tier3] event bus delivered: target {target} total hits {n}")

    def update(self, world, entity, dt):
        _timers.tick(dt)
        if yope3d.input.is_key_pressed(yope3d.KEY_R):
            self._drop()

    def on_unload(self, world, entity):
        _events.unsubscribe("target_hit", self._on_any_hit)
