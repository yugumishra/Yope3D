"""
Article 2 demo — tick-rate smoothness ladder.

Callback to the classic v1 bouncing-ball demo: a cannon fires terra-cotta
spheres from ONE point with ONE fixed velocity on a fixed interval, so the
in-flight stream traces the trajectory like a long-exposure photo.

The balls are NOT rigid bodies — they are plain visual entities driven by the
barest possible fixed-step integrator, right here in Python:

    vy -= G * h;  x += vx * h;  y += vy * h
    if y < r and vy < 0:  y = r;  vy *= -BOUNCE     # the whole "collision"

stepped once per elapsed physics tick (h = 1 / physics_hz), so the temporal
quantization of the arc is set purely by the tick rate: exponential max-height
decay from the bounce factor, and a visibly chunky arc at low Hz. The ladder
starts DELIBERATELY low (10 Hz) so the choppiness is unmissable before 240 Hz
makes it vanish. Wall-clock pacing never changes (world.physics_hz alters
step size, not time scale).

paramsBlob keys (all optional):
  n_balls  int   900  — cannon pool size

Controls: LEFT/RIGHT step the tick-rate ladder (the only way Hz changes —
each step restarts the stream so the arc always starts from a clean muzzle
shot at the new rate), SPACE restarts the stream in place.
"""
import yope3d

RATES = [1.0, 5.0, 10.0, 24.0, 60.0, 240.0]
FONT  = "fonts/monaco.ttf"
ARENA = 24.0
G     = 9.80665

# One trajectory, shared by every ball the cannon ever fires.
BALL_R     = 0.45
LAUNCH_POS = (-ARENA + 1.5, 10)       # (x, y); z comes from the cannon lane
LAUNCH_V   = (4, 0)                # (vx, vy) — spans the floor in ~6.5 s
BOUNCE     = 0.75                      # vy multiplier at each floor hit
EMIT_DT    = 0.04                      # seconds between shots
BALL_COLOR = (0.62, 0.33, 0.22)        # terra-cotta, like the old demo


def build_brick_floor(world, arena):
    """4x4 grid of brick-textured static tiles, top face at y = 0.

    Purely visual as far as the balls are concerned — their "floor" is the
    y-check in Cannon.step. Each tile carries the full brick.jpg (rect UVs
    are 0-1 per face), so the grid itself is what tiles the texture.
    """
    half = arena / 4.0
    for ix in range(4):
        for iz in range(4):
            cx = -arena + half + ix * 2.0 * half
            cz = -arena + half + iz * 2.0 * half
            e = world.add_static_aabb(yope3d.Vec3(cx, -1.5, cz),
                                      yope3d.Vec3(half, 1.5, half))
            world.attach_box_mesh(e, yope3d.Vec3(half, 1.5, half), 1.0, 1.0, 1.0)
            yope3d.reg_add(e, "Material")
            m = yope3d.reg_get(e, "Material")
            m.albedo_map = "textures/brick.jpg"
            m.metallic = 0.0
            m.roughness = 0.85


class Cannon:
    """Fixed-point, fixed-velocity ball emitter over a recycled pool.

    Balls are mesh-only entities (physics body detached at spawn); their
    state lives in plain Python lists and is integrated in step(). Parked
    balls sit invisible; emission makes one visible at the muzzle with the
    launch velocity. A ball recycles when it crosses the far floor edge.
    """

    def __init__(self, world, z, n):
        self.z = z
        self.timer = 0.0
        self.active = []               # [entity, x, y, vx, vy]
        self.ready = []
        muzzle = yope3d.Vec3(LAUNCH_POS[0], LAUNCH_POS[1], z)
        for _ in range(n):
            e = world.add_sphere(1.0, BALL_R, muzzle)
            world.attach_sphere_mesh(e, BALL_R, *BALL_COLOR)
            world.detach_physics_body(e)
            world.set_mesh_visible(e, False)
            self.ready.append(e)

    def _park(self, world, e):
        """Hide a ball AND snap it back to the muzzle so a parked ball never
        sits at its last mid-flight position — otherwise the next reuse can
        show that stale spot for a moment before the muzzle reset lands."""
        tf = yope3d.reg_get(e, "Transform")
        tf.position = yope3d.Vec3(LAUNCH_POS[0], LAUNCH_POS[1], self.z)
        world.set_mesh_visible(e, False)

    def restart(self, world):
        for ball in self.active:
            self._park(world, ball[0])
            self.ready.append(ball[0])
        self.active.clear()
        self.timer = 0.0

    def step(self, h):
        """One fixed step of the whole stream — the article's entire physics."""
        for ball in self.active:
            ball[4] -= G * h           # vy
            ball[1] += ball[3] * h     # x
            ball[2] += ball[4] * h     # y
            if ball[2] < BALL_R and ball[4] < 0.0:
                ball[2] = BALL_R
                ball[4] *= -BOUNCE

    def update(self, world, dt_wall, nticks, h):
        # Advance every in-flight ball by the ticks that actually happened.
        for _ in range(min(nticks, 240)):
            self.step(h)
        # Recycle finished runs, then write transforms for the rest.
        still = []
        for ball in self.active:
            if ball[1] > ARENA - 0.6:
                self._park(world, ball[0])
                self.ready.append(ball[0])
            else:
                tf = yope3d.reg_get(ball[0], "Transform")
                tf.position = yope3d.Vec3(ball[1], ball[2], self.z)
                still.append(ball)
        self.active = still
        # Fire on the fixed wall-clock interval.
        self.timer += dt_wall
        while self.timer >= EMIT_DT and self.ready:
            self.timer -= EMIT_DT
            e = self.ready.pop(0)
            tf = yope3d.reg_get(e, "Transform")
            tf.position = yope3d.Vec3(LAUNCH_POS[0], LAUNCH_POS[1], self.z)
            world.set_mesh_visible(e, True)
            self.active.append([e, LAUNCH_POS[0], LAUNCH_POS[1],
                                LAUNCH_V[0], LAUNCH_V[1]])


class Smoothness:
    PARAMS = {
        "n_balls": {"type": "int",   "default": 900, "label": "Cannon Pool"},
    }

    def init(self, world, entity, params):
        yope3d.set_profile_scene("Article2 Smoothness")

        build_brick_floor(world, ARENA)
        self.cannon = Cannon(world, 0.0, int(params.get("n_balls", 900)))

        # Side view: the whole trajectory in frame, arcs reading left→right.
        yope3d.camera.set_position(yope3d.Vec3(0.0, 5.0, 20.0))
        yope3d.camera.set_rotation(yope3d.Vec3(-0.12, 0.0, 0.0))

        # ------------------------------ HUD ------------------------------ #
        self.hud = world.add_ui_text(FONT, "", yope3d.Vec2(0.12, 0.04),
                                     yope3d.Vec2(0.88, 0.12), 2)

        self.rate_idx  = 0
        self.fps_ema   = 0.0
        self.sim_last  = world.tick_count
        self.last_tick = world.tick_count
        self.tick_rate = 0.0
        self._apply_rate(world)

    def _apply_rate(self, world):
        world.physics_hz = RATES[self.rate_idx]
        self.cannon.restart(world)

    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        inp = yope3d.input
        if inp.is_key_pressed(yope3d.KEY_RIGHT) and self.rate_idx < len(RATES) - 1:
            self.rate_idx += 1
            self._apply_rate(world)
        if inp.is_key_pressed(yope3d.KEY_LEFT) and self.rate_idx > 0:
            self.rate_idx -= 1
            self._apply_rate(world)
        if inp.is_key_pressed(yope3d.KEY_SPACE):
            self.cannon.restart(world)

        # Drive the stream by the physics ticks that actually elapsed.
        ticks = world.tick_count
        nticks = ticks - self.sim_last
        self.sim_last = ticks
        self.cannon.update(world, dt, nticks, 1.0 / world.physics_hz)

        # Measured rates (EMA-smoothed so the HUD is readable on camera).
        if dt > 0.0:
            a = 0.06
            self.fps_ema = (1.0 - a) * self.fps_ema + a * (1.0 / dt)
            inst = (ticks - self.last_tick) / dt
            self.last_tick = ticks
            self.tick_rate = (1.0 - a) * self.tick_rate + a * inst

        # --------------------------- HUD text --------------------------- #
        yope3d.set_text(self.hud,
                        "PHYSICS %d Hz   |   render %3.0f fps   |   measured %3.0f ticks/s" %
                        (int(RATES[self.rate_idx]), self.fps_ema, self.tick_rate))
