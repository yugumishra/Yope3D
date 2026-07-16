"""Shared shell for the scene galleries (pure Python, no C++).

`behaviors.sandbox_gallery` and `behaviors.physics_gallery` are both "a list of
procedurally-built scenes plus a viewer". The viewer half — fly camera, LEFT/RIGHT
scene cycling, HUD, LMB projectile spawning — lives here so the galleries stay
pure scene content.

A gallery subclasses GalleryBase and sets:

    SCENES = [("Scene name", builder_fn), ...]

where `builder_fn(self, world)` creates entities through the tracked factories
(`self.sphere/box/obb/static_box/ramp/spring`). Every entity those return is
recorded, so switching scenes can tear the previous one down again.

Teardown is per-entity (`world.remove_entity`) and NOT `world.reset_physics()`:
reset rebuilds the whole registry and deletes every live script instance — including
the gallery script that called it, which is a use-after-free. Anything a builder
creates outside the tracked factories (audio sources, HUD text) must be cleaned up
by overriding `on_unload_scene`.
"""
import math
import random

import yope3d

SPAWN_SPEED = 18.0
SPAWN_RATE = 0.05
SPAWN_HALF = 0.65

_SPAWN_NAMES = ("Sphere", "AABB", "OBB")

# Q/E step through these rather than scaling continuously — a filmable slow-mo
# wants repeatable, nameable settings, not whatever the key-repeat landed on.
TIME_SCALES = (0.05, 0.1, 0.25, 0.5, 1.0, 2.0)
DEFAULT_TIME_SCALE_IDX = 4


class GalleryBase:
    SCENES = []

    PARAMS = {
        "start_scene": {"type": "int", "default": 0, "label": "Scene index at Play"},
        "fly_speed": {"type": "float", "default": 12.0, "label": "Camera speed (m/s)"},
        "sprint_mult": {"type": "float", "default": 3.0, "label": "Left Shift multiplier"},
        "mouse_sens": {"type": "float", "default": 0.002, "label": "Mouse sensitivity"},
        "font": {"type": "str", "default": "fonts/monaco.ttf", "label": "HUD font"},
        "hud_px": {"type": "int", "default": 20, "label": "HUD glyph height (reference px)"},
    }

    # ---- lifecycle -------------------------------------------------------

    def init(self, world, entity, params):
        self.speed = params.get("fly_speed", 12.0)
        self.sprint_mult = params.get("sprint_mult", 3.0)
        self.sens = params.get("mouse_sens", 0.002)
        self.params = params

        self.spawned = []
        self.spawn_type = 0
        self.spawn_cooldown = 0.0
        self.yaw = 0.0
        self.pitch = 0.0
        self.hud_visible = True

        self.prev_left = False
        self.prev_right = False
        self.prev_up = False
        self.prev_down = False
        self.prev_h = False
        self.prev_p = False
        self.prev_r = False
        self.prev_f = False
        self.prev_q = False
        self.prev_e = False

        self.fps = 0
        self.fps_accum = 0.0
        self.fps_frames = 0
        self.phys_hz = 0
        self.prev_ticks = world.tick_count

        # Solver instrumentation starts at engine defaults so a scene looks normal
        # until you deliberately break it.
        world.warm_start = True
        world.debug_contacts = False
        self.time_idx = DEFAULT_TIME_SCALE_IDX
        world.time_scale = TIME_SCALES[self.time_idx]

        # auto_size lets the renderer (which has the atlas metrics) snap each box to
        # the text's natural extent — the max corner below is a placeholder it
        # overwrites. Guessing bounds here just truncates long scene names.
        font = params.get("font", "fonts/monaco.ttf")
        px = int(params.get("hud_px", 20))
        self.title = self.hud_line(world, font, 0.02, px)
        self.stats = self.hud_line(world, font, 0.06, px)
        self.solver = self.hud_line(world, font, 0.10, px)
        self.status = self.hud_line(world, font, 0.14, px)
        self.refresh_solver(world)

        self.index = int(params.get("start_scene", 0)) % max(len(self.SCENES), 1)
        self.load(world, self.index)

    def hud_line(self, world, font, y, px):
        e = world.add_ui_text(font, " ", yope3d.Vec2(0.01, y), yope3d.Vec2(0.99, y + 0.04), 1)
        text = yope3d.reg_get(e, "UIText")
        text.display_px = px
        text.auto_size = True
        return e

    def on_unload(self, world, entity):
        self.clear(world)

    # ---- scene switching -------------------------------------------------

    def load(self, world, index):
        self.clear(world)
        self.index = index % len(self.SCENES)
        name, builder = self.SCENES[self.index]
        yope3d.set_profile_scene(name)
        builder(self, world)
        self.refresh_hud(name)

    def clear(self, world):
        self.on_unload_scene(world)
        for e in self.spawned:
            world.remove_entity(e)
        self.spawned = []

    def on_unload_scene(self, world):
        """Override for non-entity resources (audio sources, etc.)."""

    def refresh_hud(self, name):
        yope3d.set_text(self.title, "[%d/%d] %s" % (self.index + 1, len(self.SCENES), name))
        yope3d.set_text(
            self.status,
            "LEFT/RIGHT scene   UP/DOWN spawn type (%s)   LMB fire   WASD+mouse fly   "
            "P colliders   R warm-start   F contacts   Q/E time scale   H hide%s"
            % (_SPAWN_NAMES[self.spawn_type], self.controls_extra()),
        )

    def refresh_solver(self, world):
        yope3d.set_text(
            self.solver,
            "warm-start %s   contacts %s   time %.2fx"
            % ("ON" if world.warm_start else "OFF",
               "ON" if world.debug_contacts else "OFF",
               world.time_scale),
        )

    def controls_extra(self):
        """Override to append scene-specific keys to the controls line."""
        return ""

    def stats_extra(self, world):
        """Override to append scene-specific readouts to the stats line."""
        return ""

    def refresh_stats(self, world):
        # get_hull_count() counts every physics body, including the floor/walls the
        # scene builder made — that IS the solver's workload, so report it as-is.
        # Points, not manifolds, is the number the PGS loop iterates, so it's the
        # one that tracks solve cost (a box-box pair is 1 manifold, up to 4 points).
        yope3d.set_text(
            self.stats,
            "FPS %d   physics %d Hz   bodies %d   pairs %d   contacts %d   islands %d%s"
            % (self.fps, self.phys_hz, world.get_hull_count(),
               world.get_pair_count(), world.get_contact_point_count(),
               world.get_island_count(), self.stats_extra(world)),
        )

    # ---- tracked entity factories ---------------------------------------

    def track(self, e):
        self.spawned.append(e)
        return e

    def sphere(self, world, mass, radius, pos, color, vel=None, fixed=False):
        e = self.track(world.add_sphere(mass, radius, yope3d.Vec3(*pos)))
        world.attach_sphere_mesh(e, radius, *color)
        if vel is not None:
            yope3d.reg_get(e, "Hull").velocity = yope3d.Vec3(*vel)
        if fixed:
            world.fix_entity(e)
        return e

    def box(self, world, half, mass, pos, color, vel=None, fixed=False):
        e = self.track(world.add_aabb(yope3d.Vec3(*half), mass, yope3d.Vec3(*pos)))
        world.attach_box_mesh(e, yope3d.Vec3(*half), *color)
        if vel is not None:
            yope3d.reg_get(e, "Hull").velocity = yope3d.Vec3(*vel)
        if fixed:
            world.fix_entity(e)
        return e

    def obb(self, world, half, mass, pos, color, vel=None, rot=None, fixed=False):
        e = self.track(world.add_obb(yope3d.Vec3(*half), mass, yope3d.Vec3(*pos)))
        world.attach_box_mesh(e, yope3d.Vec3(*half), *color)
        if rot is not None:
            yope3d.reg_get(e, "Transform").rotation = rot
        if vel is not None:
            yope3d.reg_get(e, "Hull").velocity = yope3d.Vec3(*vel)
        if fixed:
            # Orient first, then fix: fix_entity zeroes mass/velocity and adds the
            # Fixed tag. init() runs before the physics thread ticks, so no step
            # ever sees this body as dynamic.
            world.fix_entity(e)
        return e

    def static_box(self, world, pos, half, color):
        e = self.track(world.add_static_aabb(yope3d.Vec3(*pos), yope3d.Vec3(*half)))
        world.attach_box_mesh(e, yope3d.Vec3(*half), *color)
        return e

    def spring(self, world, a, b, k, rest):
        world.add_spring(a, b, k, rest)

    # ---- camera ----------------------------------------------------------

    def set_camera(self, pos, pitch=0.0, yaw=0.0):
        self.pitch = pitch
        self.yaw = yaw
        yope3d.camera.set_position(yope3d.Vec3(*pos))
        yope3d.camera.set_rotation(yope3d.Vec3(pitch, yaw, 0.0))

    # ---- update ----------------------------------------------------------

    def update(self, world, entity, dt):
        inp = yope3d.input

        self.fps_accum += dt
        self.fps_frames += 1
        if self.fps_accum >= 0.25:
            self.fps = int(self.fps_frames / self.fps_accum + 0.5)
            # Physics runs on its own thread at a fixed 240 Hz, so render FPS says
            # nothing about solver cost (it's vsync-capped anyway). Ticks/second is
            # the number that moves: when a step costs more than its 1/240 s budget,
            # the accumulator's substep clamp kicks in and the sim runs in slow motion.
            ticks = world.tick_count
            self.phys_hz = int((ticks - self.prev_ticks) / self.fps_accum + 0.5)
            self.prev_ticks = ticks
            self.fps_accum = 0.0
            self.fps_frames = 0
            self.refresh_stats(world)

        left = inp.is_key_down(yope3d.KEY_LEFT)
        right = inp.is_key_down(yope3d.KEY_RIGHT)
        if right and not self.prev_right:
            self.load(world, self.index + 1)
        elif left and not self.prev_left:
            self.load(world, self.index - 1)
        self.prev_left, self.prev_right = left, right

        up = inp.is_key_down(yope3d.KEY_UP)
        down = inp.is_key_down(yope3d.KEY_DOWN)
        if up and not self.prev_up:
            self.spawn_type = (self.spawn_type + 1) % 3
            self.refresh_hud(self.SCENES[self.index][0])
        if down and not self.prev_down:
            self.spawn_type = (self.spawn_type + 2) % 3
            self.refresh_hud(self.SCENES[self.index][0])
        self.prev_up, self.prev_down = up, down

        h = inp.is_key_down(yope3d.KEY_H)
        if h and not self.prev_h:
            self.hud_visible = not self.hud_visible
            for e in (self.title, self.stats, self.solver, self.status):
                yope3d.reg_get(e, "UITransform").visible = self.hud_visible
        self.prev_h = h

        p = inp.is_key_down(yope3d.KEY_P)
        if p and not self.prev_p:
            world.debug_physics = not world.debug_physics
        self.prev_p = p

        r = inp.is_key_down(yope3d.KEY_R)
        if r and not self.prev_r:
            world.warm_start = not world.warm_start
            self.refresh_solver(world)
        self.prev_r = r

        f = inp.is_key_down(yope3d.KEY_F)
        if f and not self.prev_f:
            world.debug_contacts = not world.debug_contacts
            self.refresh_solver(world)
        self.prev_f = f

        q = inp.is_key_down(yope3d.KEY_Q)
        e_key = inp.is_key_down(yope3d.KEY_E)
        if q and not self.prev_q:
            self.set_time_scale(world, self.time_idx - 1)
        if e_key and not self.prev_e:
            self.set_time_scale(world, self.time_idx + 1)
        self.prev_q, self.prev_e = q, e_key

        self.spawn_cooldown -= dt
        if inp.is_lmb_down() and self.spawn_cooldown <= 0.0:
            self.fire(world)
            self.spawn_cooldown = SPAWN_RATE

        self.fly(dt)
        self.on_update(world, dt)

    def set_time_scale(self, world, idx):
        self.time_idx = max(0, min(len(TIME_SCALES) - 1, idx))
        world.time_scale = TIME_SCALES[self.time_idx]
        self.refresh_solver(world)

    def on_update(self, world, dt):
        """Override for per-frame scene work (e.g. Doppler source sync)."""

    def fly(self, dt):
        inp = yope3d.input
        dx, dy = inp.get_mouse_delta()
        self.yaw -= dx * self.sens
        self.pitch = max(-1.5, min(1.5, self.pitch - dy * self.sens))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

        fwd = yope3d.camera.get_forward()
        right = yope3d.Vec3(math.cos(self.yaw), 0.0, -math.sin(self.yaw))
        move = yope3d.Vec3(0.0, 0.0, 0.0)
        if inp.is_key_down(yope3d.KEY_W):
            move = move + fwd
        if inp.is_key_down(yope3d.KEY_S):
            move = move - fwd
        if inp.is_key_down(yope3d.KEY_D):
            move = move + right
        if inp.is_key_down(yope3d.KEY_A):
            move = move - right
        if inp.is_key_down(yope3d.KEY_SPACE):
            move = move + yope3d.Vec3(0, 1, 0)
        if inp.is_key_down(yope3d.KEY_LEFT_CONTROL):
            move = move - yope3d.Vec3(0, 1, 0)

        speed = self.speed * (self.sprint_mult
                              if inp.is_key_down(yope3d.KEY_LEFT_SHIFT) else 1.0)
        p = yope3d.camera.position
        yope3d.camera.set_position(yope3d.Vec3(p.x + move.x * speed * dt,
                                               p.y + move.y * speed * dt,
                                               p.z + move.z * speed * dt))

    def fire(self, world):
        fwd = yope3d.camera.get_forward()
        cam = yope3d.camera.position
        origin = (cam.x + fwd.x * 1.5, cam.y + fwd.y * 1.5, cam.z + fwd.z * 1.5)
        vel = (fwd.x * SPAWN_SPEED, fwd.y * SPAWN_SPEED, fwd.z * SPAWN_SPEED)
        s = SPAWN_HALF

        if self.spawn_type == 0:
            self.sphere(world, 1.0, s, origin, (0.2, 0.5, 1.0), vel=vel)
        elif self.spawn_type == 1:
            self.box(world, (s, s, s), 1.0, origin, (0.3, 0.85, 0.4), vel=vel)
        else:
            sy = s * random.uniform(0.5, 1.8)
            axis = yope3d.Vec3(random.uniform(-1, 1), random.uniform(-1, 1),
                               random.uniform(-1, 1)).normalize()
            rot = yope3d.Quat.from_axis_angle(axis, random.uniform(0.0, 2.0 * math.pi))
            self.obb(world, (s, sy, s), 1.0, origin, (1.0, 0.5, 0.1), vel=vel, rot=rot)
