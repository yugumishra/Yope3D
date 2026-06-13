"""Tier-2 scripting-extensions smoketest — self-contained, no scene setup needed.

Attach this to ANY single entity (an empty, the floor, whatever) and press Play.
It spawns its own targets directly along the camera's forward axis, so the
center-screen crosshair ray passes straight through them — you do NOT need a
character controller or any look-around to test it.

Each frame it raycasts from the camera (origin = eye, dir = forward) and draws a
small world-space marker on whatever the crosshair is pointing at (a forward ray
drawn from the eye would be degenerate — it collapses to a dot at screen center,
which is the "ray isn't centered" confusion). Press LMB to recolor + punch the hit.

Exercises: raycast, apply_impulse, wake, draw_line, find_entity, time, tick_count,
reg_add/reg_remove, Quat from_euler/slerp/rotate, look_at.

For a *real* game you'd split this: CharacterController on the player capsule
(it drives yope3d.camera) + gameplay behaviors on other entities. One ScriptComponent
per entity, so put each behavior on its own object.
"""
import yope3d


def _draw_marker(pt, color, size=0.4):
    """Draw a 3-axis world-space cross at `pt` (visible from any angle)."""
    yope3d.draw_line(pt - yope3d.Vec3(size, 0, 0), pt + yope3d.Vec3(size, 0, 0), color)
    yope3d.draw_line(pt - yope3d.Vec3(0, size, 0), pt + yope3d.Vec3(0, size, 0), color)
    yope3d.draw_line(pt - yope3d.Vec3(0, 0, size), pt + yope3d.Vec3(0, 0, size), color)


class Tier2Smoketest:
    PARAMS = {}

    def init(self, world, entity, params):
        # Spawn targets straight down the camera's current aim line, so the
        # center-screen ray crosses every one with no look-around required.
        cam = yope3d.camera
        o = cam.position
        d = cam.get_forward()
        self.targets = []
        for i in range(5):
            pos = o + d * (6.0 + i * 2.5)
            e = world.add_sphere(1.0, 0.5, pos)
            world.attach_sphere_mesh(e, 0.5, 0.8, 0.2, 0.2)
            yope3d.reg_get(e, "Name").value = f"target_{i}"
            yope3d.reg_get(e, "Hull").gravity = False   # hover until punched
            self.targets.append(e)

        # Quat math + lookup sanity (these ran fine for you already).
        q = yope3d.Quat.from_euler(0.0, yope3d.to_radians(90.0), 0.0)
        print(f"[tier2] init t={yope3d.time():.2f} aim={d} "
              f"rotated={q.rotate(yope3d.Vec3(0,0,1))} "
              f"found={yope3d.find_entity('target_0')} "
              f"look={yope3d.look_at(yope3d.Vec3(1,0,0))}")

    def update(self, world, entity, dt):
        cam = yope3d.camera
        o = cam.position
        d = cam.get_forward()

        # Live "what am I aiming at" feedback — a marker at the crosshair hit.
        hit, e, point, normal, t = yope3d.raycast(o, d, 100.0, entity)
        if hit:
            _draw_marker(point, yope3d.Vec3(0.2, 0.9, 1.0))

        if yope3d.input.is_mouse_pressed(yope3d.MOUSE_LEFT) and hit:
            world.set_mesh_color(e, 0.2, 1.0, 0.2)
            world.apply_impulse(e, d * 8.0)            # also wakes the body
            _draw_marker(point, yope3d.Vec3(1, 1, 0), 0.8)
            print(f"[tier2] hit {e} t={t:.2f} tick={world.tick_count}")
