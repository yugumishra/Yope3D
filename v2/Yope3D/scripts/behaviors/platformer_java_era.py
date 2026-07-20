"""
Trailer demo — recreation of the old Java engine's Platformer (parkour) scene.

Ported from old_src_java/src/scripts/Platformer.java, matching the original's
constants and quirks rather than the later C++ PlatformerScript remake (which
deliberately re-tuned them). Java pieces that no longer exist map as follows:

  - CSphere(a, b, pos, vel, rot, omega): a = mass, b = radius (arg 2 equals
    the caller's visual radius/scale at every old call site).
  - Barrier(up-normal, point (0,1,0)) — the infinite floor plane at y=1 —
    becomes a wide static slab with its top face at y=1.
  - Launch.window.pause() on win becomes world.set_paused(True) plus a text
    overlay (the Java win-screen call was commented out in the source).

Gameplay, verbatim from the Java update():
  - WASD adds camera-relative velocity impulses (magnitude move_speed, x2
    holding SHIFT), then x/z velocity decays by 0.8 EVERY frame. The impulse
    direction keeps the original's quirks: forward is the pitched camera
    forward flattened (so looking up/down walks slightly slower), and the
    push is only normalized when BOTH |x| and |z| exceed 0.001.
  - SPACE adds +move_speed up-velocity per frame while near the floor
    (y < 2.1) or standing on a course platform (down-raycast < radius+0.25).
    LEFT CTRL pushes down. Both scale with SHIFT and dash.
  - F dashes: move_speed x4 for 31 frames, 240-frame cooldown, FOV lerping
    up by 0.5/60ths of the current FOV per frame, back down after.
  - 50 spiral platforms (4x4x4 fixed cubes, random colors): radius
    15 + 0.5/step, angle 0.3/step, y = 0.5 + 2*step. A 51st platform at
    (-50, 65, 0) carries the star (gold, bobbing 69 + sin(3t), spinning
    4 rad/s, a solid fixed sphere) — reachable only by dashing.
  - Landing on the star platform collects it: 50 gold star particles burst
    from the player using the original's exact (unnormalized) hemisphere
    formula, each a real rigid body with velocity 0.75*u and spin 3*u.
  - Landing on spiral platform 49 (the top) ends the run: 1 = won (star
    collected, under 90 s), 2 = no star, 3 = over 90 s.

Java stepped this logic once per frame at 60 fps, so every frame-based
constant (0.8 decay, 31-frame dash, %4 gates) runs on a fixed 60 Hz
accumulator here, independent of display rate. Mouselook, camera follow and
the star animation run per render frame.

paramsBlob keys (optional):
  seed  int  1337 — RNG seed for platform colors + particle directions

Controls: WASD move · mouse look · SPACE jump · SHIFT sprint · CTRL descend ·
F dash · H toggle HUD
"""
import math, random, yope3d

FONT = "fonts/monaco.ttf"

# ---- Fixed-rate logic (the Java frame rate) ---------------------------- #
LOGIC_DT  = 1.0 / 60.0
MAX_STEPS = 4                   # cap catch-up after a hitch

# ---- Movement (Platformer.java constants) ------------------------------ #
MOVE_SPEED = 1.0                # loop.getCamera().setMoveSpeed(1f)
DECAY      = 0.8625                # per-frame x/z velocity decay
SPRINT_MUL = 2.0
PITCH_CAP  = math.pi / 2.0
MOUSE_SENS = 0.002
LOW_JUMP_Y = 2.1                # jump allowed near the floor...
GROUND_EPS = 0.25               # ...or when down-raycast < radius + this

# ---- Dash -------------------------------------------------------------- #
DASH_MULT     = 4.0
DASH_FRAMES   = 31
DASH_COOLDOWN = 240
FOV_INC_FRAC  = 0.5 / 60.0      # fovIncrement = 0.5 * FOV / 60

# ---- Course ------------------------------------------------------------ #
PLAT_COUNT   = 50               # spiral platforms; index 50 = star platform
PLAT_HALF    = 2.0              # 4x4x4 cubes (Mesh.cube() +-1, setScale(2))
INIT_RADIUS  = 15.0
RADIUS_GROW  = 0.5
ANGLE_STEP   = 0.3
HEIGHT_STEP  = 2.0
STAR_PLAT    = (-50.0, 65.0, 0.0)
FLOOR_TOP    = 1.0              # the Java Barrier plane sat at y=1
FLOOR_HALF   = 50.0             # visual plane was scaled 50
FLOOR_COLOR  = (0.2, 0.8, 0.9)

# ---- Player / star / particles ----------------------------------------- #
PLAYER_R     = 1.0
PLAYER_SPAWN = (0.0, 120.0, 10.0)
PLAYER_FRICTION = 0.025          # near-zero so contact friction doesn't fight
                                # the WASD impulses (old contacts had none)
STAR_R       = 1.0
STAR_BASE_Y  = 69.0
STAR_COLOR   = (1.0, 0.813437, 0.0)
PARTICLES    = 50
WIN_TIME     = 90.0


class Platformer:
    PARAMS = {
        "seed": {"type": "int", "default": 1337, "label": "RNG Seed"},
    }

    # ------------------------------------------------------------------ #

    def init(self, world, entity, params):
        yope3d.set_profile_scene("Trailer Platformer")
        self.rng = random.Random(int(params.get("seed", 1337)))

        # Floor: slab whose top face is the old barrier plane at y=1.
        floor = world.add_static_aabb(yope3d.Vec3(0.0, FLOOR_TOP - 1.0, 0.0),
                                      yope3d.Vec3(FLOOR_HALF, 1.0, FLOOR_HALF))
        world.attach_box_mesh(floor, yope3d.Vec3(FLOOR_HALF, 1.0, FLOOR_HALF),
                              *FLOOR_COLOR)

        # Spiral course + star platform. plat_idx maps entity id -> course
        # index for jumpValid (star check = 50, win check = 49).
        self.plat_idx = {}
        radius = INIT_RADIUS
        for i in range(PLAT_COUNT):
            x = radius * math.cos(ANGLE_STEP * i)
            z = radius * math.sin(ANGLE_STEP * i)
            y = 0.5 + HEIGHT_STEP * i
            self._platform(world, i, (x, y, z))
            radius += RADIUS_GROW
        self._platform(world, PLAT_COUNT, STAR_PLAT)

        # Player: invisible physics sphere (Java setDraw(false) — no mesh).
        self.player = world.add_sphere(1.0, PLAYER_R, yope3d.Vec3(*PLAYER_SPAWN))
        hull = yope3d.reg_get(self.player, "Hull")
        hull.sleeping_enabled = False   # velocity is written every tick
        hull.linear_damping = 0.0       # decay is the script's 0.8, not the solver's
        hull.friction = PLAYER_FRICTION

        # Star: solid fixed sphere (the Java hull) carrying the OBJ visual.
        self.star_body = world.add_sphere(
            1.0, STAR_R, yope3d.Vec3(STAR_PLAT[0], STAR_BASE_Y, STAR_PLAT[2]))
        world.fix_entity(self.star_body)
        self.star_visual = world.add_model("models/star.obj")[0]
        world.set_mesh_color(self.star_visual, *STAR_COLOR)
        yope3d.reg_add(self.star_visual, "Parent")
        yope3d.reg_get(self.star_visual, "Parent").parent = self.star_body
        tf = yope3d.reg_get(self.star_visual, "Transform")
        tf.position = yope3d.Vec3(0.0, 0.0, 0.0)

        # Camera: at the ball, looking straight down while falling in.
        self.pitch = -PITCH_CAP + 1e-3
        self.yaw = 0.0
        yope3d.camera.set_position(yope3d.Vec3(*PLAYER_SPAWN))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

        # State (names follow the Java fields).
        self.move_speed = MOVE_SPEED
        self.dashing = False
        self.start_frame = 0
        self.fov_lerp = 0
        self.fov_inc = 0.0
        self.winning_variable = -1
        self.star_collected = False
        self.won = False
        self.tick = 0
        self.acc = 0.0
        self.total_time = 0.0

        # HUD: run timer (H toggles) + hidden centered win overlay.
        self.hud_timer = world.add_ui_text(FONT, "", yope3d.Vec2(0.80, 0.03),
                                           yope3d.Vec2(0.98, 0.09), 2)
        self.hud_win = world.add_ui_text(FONT, "", yope3d.Vec2(0.20, 0.42),
                                         yope3d.Vec2(0.80, 0.58), 3)
        self.hud_on = True

    def _platform(self, world, index, pos):
        e = world.add_static_aabb(yope3d.Vec3(*pos),
                                  yope3d.Vec3(PLAT_HALF, PLAT_HALF, PLAT_HALF))
        world.attach_box_mesh(e, yope3d.Vec3(PLAT_HALF, PLAT_HALF, PLAT_HALF),
                              self.rng.random(), self.rng.random(),
                              self.rng.random())
        self.plat_idx[e.id] = index

    # ------------------------------------------------------------------ #

    def _jump_valid(self, pos):
        """The Java jumpValid(): which course platform is underfoot (-1 = none).
        One closest-hit raycast replaces the old per-platform AABB loop; the
        plat_idx filter keeps non-course hits (floor, particles) returning -1."""
        hit, e, _point, _normal, _t = yope3d.raycast(
            pos, yope3d.Vec3(0.0, -1.0, 0.0), PLAYER_R + GROUND_EPS, self.player)
        if hit and e is not None:
            return self.plat_idx.get(e.id, -1)
        return -1

    def _logic_step(self, world, inp):
        """One 60 Hz Java frame."""
        self.tick += 1
        pos = yope3d.reg_get(self.player, "Transform").position
        platform = self._jump_valid(pos)
        hull = yope3d.reg_get(self.player, "Hull")

        # ---- Movement (the big key-gate if, verbatim) ------------------ #
        if (inp.is_key_down(yope3d.KEY_W) or inp.is_key_down(yope3d.KEY_A)
                or inp.is_key_down(yope3d.KEY_S) or inp.is_key_down(yope3d.KEY_D)
                or inp.is_key_down(yope3d.KEY_SPACE)
                or inp.is_key_down(yope3d.KEY_LEFT_SHIFT)):
            mul = SPRINT_MUL if inp.is_key_down(yope3d.KEY_LEFT_SHIFT) else 1.0
            if (inp.is_key_down(yope3d.KEY_SPACE)
                    and (pos.y < LOW_JUMP_Y or platform != -1)):
                hull.velocity.y += self.move_speed * mul
            if inp.is_key_down(yope3d.KEY_LEFT_CONTROL):
                hull.velocity.y -= self.move_speed * mul

            # Camera-relative push: pitched forward flattened (magnitude
            # cos(pitch) — original quirk), right stays horizontal unit.
            fwd = yope3d.camera.get_forward()
            rx, rz = math.cos(self.yaw), -math.sin(self.yaw)
            px = pz = 0.0
            if inp.is_key_down(yope3d.KEY_W):
                px += fwd.x; pz += fwd.z
            if inp.is_key_down(yope3d.KEY_S):
                px -= fwd.x; pz -= fwd.z
            if inp.is_key_down(yope3d.KEY_D):
                px += rx; pz += rz
            if inp.is_key_down(yope3d.KEY_A):
                px -= rx; pz -= rz

            # Java only normalizes when both components are non-negligible.
            if abs(px) > 0.001 and abs(pz) > 0.001:
                n = math.sqrt(px * px + pz * pz)
                px, pz = px / n, pz / n

            hull.velocity.x += px * mul * self.move_speed
            hull.velocity.z += pz * mul * self.move_speed

        # ---- Frictional decay (every frame, x/z only) ------------------ #
        hull.velocity.x *= DECAY
        hull.velocity.z *= DECAY

        # ---- Dash ------------------------------------------------------ #
        if (inp.is_key_down(yope3d.KEY_F) and not self.dashing
                and self.tick - self.start_frame > DASH_COOLDOWN
                and self.tick % 4 == 0):
            self.dashing = True
            self.start_frame = self.tick
            self.move_speed *= DASH_MULT
            self.fov_inc = yope3d.camera.fov * FOV_INC_FRAC
        if self.dashing and self.fov_lerp < DASH_FRAMES:
            yope3d.camera.fov += self.fov_inc
            self.fov_lerp += 1
        if self.dashing and self.tick - self.start_frame > DASH_FRAMES:
            self.move_speed /= DASH_MULT
            yope3d.camera.fov -= self.fov_inc
            self.fov_lerp -= 1
            self.dashing = False
        if self.fov_lerp > 0 and not self.dashing:
            yope3d.camera.fov -= self.fov_inc
            self.fov_lerp -= 1

        # ---- Star collection (landing on the star platform) ------------ #
        if platform == PLAT_COUNT and not self.star_collected:
            self._collect(world, pos)

        # ---- Win (landing on the top spiral platform) ------------------ #
        if platform == PLAT_COUNT - 1:
            self._finish(world)

    # ------------------------------------------------------------------ #

    def _collect(self, world, player_pos):
        """Star burst — the Java particle loop, quirks intact: theta in
        [pi/4, 3pi/4], nar = sin(theta) (NOT cos — the direction u is
        deliberately left unnormalized, |u| in [1, sqrt(2)])."""
        self.star_collected = True
        world.set_mesh_visible(self.star_visual, False)
        # (Like the original, the star's fixed collider stays in the world.)

        px, py, pz = player_pos.x, player_pos.y, player_pos.z
        for _ in range(PARTICLES):
            theta = self.rng.random() * math.pi * 0.5 + math.pi / 4.0
            phi = self.rng.random() * math.pi * 2.0
            nar = math.sin(theta)
            u = (math.cos(phi) * nar, math.sin(theta), math.sin(phi) * nar)

            body = world.add_sphere(1.0, 0.2, yope3d.Vec3(px + 3.0 * u[0],
                                                          py + 3.0 * u[1],
                                                          pz + 3.0 * u[2]))
            visual = world.add_model("models/star.obj")[0]
            world.set_mesh_color(visual, *STAR_COLOR)
            yope3d.reg_add(visual, "Parent")
            yope3d.reg_get(visual, "Parent").parent = body
            tf = yope3d.reg_get(visual, "Transform")
            tf.position = yope3d.Vec3(0.0, 0.0, 0.0)
            tf.scale = yope3d.Vec3(0.2, 0.2, 0.2)

            yope3d.set_velocity(body, yope3d.Vec3(0.75 * u[0], 0.75 * u[1],
                                                  0.75 * u[2]))
            yope3d.reg_get(body, "Hull").omega = yope3d.Vec3(
                3.0 * u[0], 3.0 * u[1], 3.0 * u[2])

    def _finish(self, world):
        """winningVariable + the window pause, with an overlay standing in
        for the commented-out Java win screen."""
        self.won = True
        if self.star_collected and self.total_time < WIN_TIME:
            self.winning_variable = 1
            msg = "YOU WIN!  %.1fs" % self.total_time
        elif not self.star_collected:
            self.winning_variable = 2
            msg = "INCOMPLETE - get the star first"
        else:
            self.winning_variable = 3
            msg = "TOO SLOW - %.1fs (need < %ds)" % (self.total_time, WIN_TIME)
        yope3d.set_text(self.hud_win, msg)
        world.set_paused(True)

    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        if self.won:
            return                      # Launch.window.pause(): everything freezes
        self.total_time += dt
        inp = yope3d.input

        # Mouselook (per render frame, pitch capped at +-pi/2 like Java).
        dx, dy = inp.get_mouse_delta()
        self.yaw -= dx * MOUSE_SENS
        self.pitch = max(-PITCH_CAP, min(PITCH_CAP, self.pitch - dy * MOUSE_SENS))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

        # Fixed 60 Hz gameplay ticks.
        self.acc = min(self.acc + dt, MAX_STEPS * LOGIC_DT)
        while self.acc >= LOGIC_DT:
            self.acc -= LOGIC_DT
            self._logic_step(world, inp)
            if self.won:
                break

        # Star bob + spin (69 + sin(3t), 4 rad/s yaw).
        if not self.star_collected:
            yope3d.set_position(self.star_body, yope3d.Vec3(
                STAR_PLAT[0], STAR_BASE_Y + math.sin(3.0 * self.total_time),
                STAR_PLAT[2]))
            tf = yope3d.reg_get(self.star_body, "Transform")
            tf.rotation = yope3d.Quat.from_axis_angle(
                yope3d.Vec3(0.0, 1.0, 0.0), 4.0 * self.total_time)

        # Camera rides the (invisible) ball's center, exactly like Java.
        p = yope3d.reg_get(self.player, "Transform").position
        yope3d.camera.set_position(p)

        # HUD.
        if inp.is_key_pressed(yope3d.KEY_H):
            self.hud_on = not self.hud_on
            if not self.hud_on:
                yope3d.set_text(self.hud_timer, "")
        if self.hud_on:
            star = "*" if self.star_collected else " "
            yope3d.set_text(self.hud_timer, "%s %5.1fs" % (star, self.total_time))
