"""Trigger volume demo — Hull.is_trigger + world.add_trigger_box / add_trigger_sphere.

Attach to any entity with a Transform (in a scene file, so it gets a live script
instance) and press Play. Watch the console + viewport:

  - [trigger] ENTER self         <- <entity>   a falling sphere entered self's box
  - [trigger] EXIT  self         <- <entity>   it fell out the bottom
  - [trigger] ENTER free_trigger <- <entity>   (same, for the freestanding sphere)
  - [trigger] EXIT  free_trigger <- <entity>

Important per-entity-script constraint (see CLAUDE.md "Scripting System"):
on_collision_enter/exit only dispatches to entities that already carry a live
ScriptComponent instance, and instances are created once at scene load — a
dynamically spawned entity (world.add_sphere, world.add_trigger_box, ...) never
gets one. So:

  - self (already scripted in the scene) is turned into the primary trigger via
    attach_aabb_collider + flipping Hull.is_trigger — the realistic path for
    retrofitting an existing scene entity into a trigger, and the one that gets
    real on_collision_enter/exit dispatch for free.
  - free_trigger is a second, freestanding volume spawned via
    world.add_trigger_sphere purely to exercise that factory. It has no script of
    its own, so it can't receive native events — instead update() polls the
    balls *this script spawned* against it each frame (plain distance check) and
    synthesizes the same ENTER/EXIT log + tint. This is exactly the manual
    capsule_overlap-polling workaround limitations.md describes triggers as
    replacing — shown here because free_trigger has nothing else driving it.

Exercises: world.add_trigger_box (via self), world.add_trigger_sphere (freestanding),
Hull.is_trigger, on_collision_enter/on_collision_exit firing with zero physical
response (spheres are never deflected, only the amber tint changes).
"""
import yope3d

IDLE_COLOR = (1.0, 0.85, 0.15)   # amber
HIT_COLOR  = (0.3, 1.0, 0.3)     # green


class TriggerVolumeDemo:
    PARAMS = {
        "drop_interval": {"type": "float", "default": 2.0, "label": "Seconds between drops"},
    }

    def init(self, world, entity, params):
        self.t = 0.0
        self.interval = params.get("drop_interval", 2.0)
        self.drops = 0
        self.pos = yope3d.get_position(entity) or yope3d.Vec3(0, 3, 0)

        # Static floor so dropped spheres visibly come to rest below the triggers,
        # proving they weren't stopped or pushed by them on the way down.
        floor = world.add_static_aabb(
            yope3d.Vec3(self.pos.x, -0.5, self.pos.z), yope3d.Vec3(6, 0.5, 6))
        world.attach_box_mesh(floor, yope3d.Vec3(6, 0.5, 6), 0.5, 0.5, 0.55)

        # Turn self into a trigger: attach a static AABB collider, then flip
        # Hull.is_trigger. Orthogonal to `tangible` (stays True) -- this is the
        # low-level path for making an *existing* scripted entity a trigger.
        if not yope3d.reg_has(entity, "Hull"):
            world.attach_aabb_collider(entity, 0.0, yope3d.Vec3(1, 1, 1), True)
        yope3d.reg_get(entity, "Hull").is_trigger = True
        world.attach_box_mesh(entity, yope3d.Vec3(1, 1, 1), *IDLE_COLOR)

        # Freestanding trigger sphere via the new factory -- exercises
        # world.add_trigger_sphere end to end (create -> mesh -> visible in editor).
        # No script of its own -> see the manual poll in update() below.
        self.free_trigger_pos    = yope3d.Vec3(self.pos.x + 3.0, self.pos.y, self.pos.z)
        self.free_trigger_radius = 1.0
        self.free_trigger = world.add_trigger_sphere(self.free_trigger_pos, self.free_trigger_radius)
        world.attach_sphere_mesh(self.free_trigger, self.free_trigger_radius, *IDLE_COLOR)

        self.ball_radius = 0.3
        self.balls = []                # every entity this script has spawned
        self.free_trigger_inside = set()

        self._drop(world)
        print(f"[trigger] init: self={entity} (trigger box) "
              f"free_trigger={self.free_trigger} (trigger sphere, polled manually)")

    def _drop(self, world):
        self.drops += 1
        # Alternate which trigger the projectile falls through.
        x = self.pos.x if self.drops % 2 else self.pos.x + 3.0
        proj = world.add_sphere(1.0, self.ball_radius, yope3d.Vec3(x, self.pos.y + 5.0, self.pos.z))
        world.attach_sphere_mesh(proj, self.ball_radius, 0.3, 0.6, 1.0)
        self.balls.append(proj)

    def on_collision_enter(self, world, entity, other):
        world.set_mesh_color(entity, *HIT_COLOR)
        print(f"[trigger] ENTER self <- {other}")

    def on_collision_exit(self, world, entity, other):
        world.set_mesh_color(entity, *IDLE_COLOR)
        print(f"[trigger] EXIT  self <- {other}")

    def _poll_free_trigger(self, world):
        r_sum = self.free_trigger_radius + self.ball_radius
        still_inside = set()
        for ball in self.balls:
            p = yope3d.get_position(ball)
            if p is None:
                continue
            if (p - self.free_trigger_pos).length() <= r_sum:
                still_inside.add(ball)
                if ball not in self.free_trigger_inside:
                    print(f"[trigger] ENTER free_trigger <- {ball}")

        for ball in self.free_trigger_inside - still_inside:
            print(f"[trigger] EXIT  free_trigger <- {ball}")

        if still_inside and not self.free_trigger_inside:
            world.set_mesh_color(self.free_trigger, *HIT_COLOR)
        elif not still_inside and self.free_trigger_inside:
            world.set_mesh_color(self.free_trigger, *IDLE_COLOR)

        self.free_trigger_inside = still_inside

    def update(self, world, entity, dt):
        self.t += dt
        if self.t >= self.interval:
            self.t = 0.0
            self._drop(world)
        self._poll_free_trigger(world)
