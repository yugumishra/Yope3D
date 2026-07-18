"""
Phase E stress-test scenes, ported from the pre-M13 SandboxScript::loadStressTest.
Attach as a ScriptComponent on any entity (assets/scenes/stress.json does this).

Two scenarios, both inside the same walled arena (floor + 4 walls):

  grid    — the classic Phase E workload: a 36x36xN grid of resting bodies.
            Constants match the old C++ scene exactly so profile CSVs stay
            comparable with the original Phase E data.
  funnel  — 4 angled static OBB plates form a square funnel above the floor;
            bodies spawn jittered above the mouth with small random velocities
            and drain through the throat into one large pile. Sustained island
            merge/split churn + non-grid-aligned broadphase distribution.

Shape "mixed" picks a random shape per body (sphere/aabb/obb) — exercises the
cross-shape narrowphase buckets that single-shape sweeps never hit. Capsules /
cylinders are excluded: they have no live narrowphase until GJK/EPA lands.

paramsBlob keys (all optional):
  scenario    str  "grid"   — "grid" or "funnel"
  n           int    0      — body count; 0 spawns only the arena (+funnel)
  shape       str  "sphere" — "sphere", "aabb", "obb", or "mixed"
  fly_speed   float 12.0    — camera speed (m/s)
  sprint_mult float  3.0    — speed multiplier while Left Shift held
  mouse_sens  float  0.002  — mouse look sensitivity

Env overrides (the sweep harness contract, takes precedence over params):
  YOPE_STRESS_N          — body count
  YOPE_STRESS_SHAPE      — sphere (default) / aabb / obb / mixed
  YOPE_STRESS_SCENARIO   — grid (default) / funnel

Controls: WASD move, mouse look, Space up, Left Ctrl down, Left Shift sprint.
"""
import os, math, random, yope3d

STRESS_HALF    = 20.0   # arena half-extent
STRESS_CEILING = 25.0   # wall top

# grid scenario (constants verbatim from the old C++ scene)
GRID_KX        = 36     # bodies per grid row (X)
GRID_KZ        = 36     # bodies per grid row (Z)
SPACING        = 1.05   # just over diameter (extent=0.5)
HALF_EXT       = 0.5
MASS           = 1.0

# funnel scenario
FUNNEL_THROAT  = 1.5    # throat (bottom opening) half-width
FUNNEL_MOUTH   = 10.0   # mouth (top opening) half-width
FUNNEL_BOTTOM  = 10.0   # throat height above the floor
FUNNEL_TOP     = 18.0   # mouth height
PLATE_THICK    = 0.3    # plate half-thickness
SPAWN_COLS     = 12     # spawn columns per axis above the mouth
SPAWN_STEP     = 1.5    # spawn lattice spacing (loose — bodies rain, not stack)
RNG_SEED       = 1234   # fixed seed: reproducible spawns across sweep runs

# body colors: single-shape runs keep the old uniform color for parity;
# mixed runs color per shape so the shapes read on camera.
COLOR_UNIFORM  = (0.7, 0.4, 0.3)
COLOR_BY_SHAPE = {"sphere": (0.7, 0.4, 0.3),
                  "aabb":   (0.35, 0.5, 0.75),
                  "obb":    (0.45, 0.7, 0.4)}

class StressTest:
    PARAMS = {
        "scenario":    {"type": "enum",  "default": "grid",
                        "options": ["grid", "funnel"],        "label": "Scenario"},
        "n":           {"type": "int",   "default": 0,        "label": "Body Count"},
        "shape":       {"type": "enum",  "default": "sphere",
                        "options": ["sphere", "aabb", "obb", "mixed"],
                                                              "label": "Shape"},
        "fly_speed":   {"type": "float", "default": 12.0,     "label": "Fly Speed (m/s)"},
        "sprint_mult": {"type": "float", "default": 3.0,      "label": "Sprint Multiplier"},
        "mouse_sens":  {"type": "float", "default": 0.002,    "label": "Mouse Sensitivity"},
    }

    def init(self, world, entity, params):
        n        = int(os.environ.get("YOPE_STRESS_N",       params.get("n", 0)))
        shape    = os.environ.get("YOPE_STRESS_SHAPE",       params.get("shape", "sphere"))
        scenario = os.environ.get("YOPE_STRESS_SCENARIO",    params.get("scenario", "grid"))
        if shape not in ("aabb", "obb", "mixed"):
            shape = "sphere"
        if scenario != "funnel":
            scenario = "grid"

        yope3d.set_profile_scene("Stress Funnel" if scenario == "funnel"
                                 else "Stress Test")

        self.speed       = params.get("fly_speed",   12.0)
        self.sprint_mult = params.get("sprint_mult",  3.0)
        self.sens        = params.get("mouse_sens",   0.002)

        rng = random.Random(RNG_SEED)
        self._build_arena(world)
        if scenario == "funnel":
            self._build_funnel(world)
            self._spawn_rain(world, n, shape, rng)
            self.pitch, self.yaw = -0.25, 0.0
            yope3d.camera.set_position(yope3d.Vec3(0.0, 17.0, 36.0))
        else:
            self._spawn_grid(world, n, shape, rng)
            self.pitch, self.yaw = 0.0, 0.0
            yope3d.camera.set_position(yope3d.Vec3(0.0, 3.5, STRESS_HALF - 2.0))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

    # ------------------------------------------------------------------ #
    # Static geometry
    # ------------------------------------------------------------------ #

    def _build_arena(self, world):
        floor = world.add_static_aabb(yope3d.Vec3(0.0, -0.5, 0.0),
                                      yope3d.Vec3(STRESS_HALF, 0.5, STRESS_HALF))
        world.attach_box_mesh(floor, yope3d.Vec3(STRESS_HALF, 0.5, STRESS_HALF),
                              0.32, 0.28, 0.24)

        wh = STRESS_CEILING * 0.5
        walls = [
            ((-STRESS_HALF - 0.4, wh, 0.0), (0.4, wh, STRESS_HALF)),
            (( STRESS_HALF + 0.4, wh, 0.0), (0.4, wh, STRESS_HALF)),
            ((0.0, wh, -STRESS_HALF - 0.4), (STRESS_HALF, wh, 0.4)),
            ((0.0, wh,  STRESS_HALF + 0.4), (STRESS_HALF, wh, 0.4)),
        ]
        for pos, ext in walls:
            e = world.add_static_aabb(yope3d.Vec3(*pos), yope3d.Vec3(*ext))
            world.attach_box_mesh(e, yope3d.Vec3(*ext), 0.22, 0.22, 0.28)

    def _static_plate(self, world, pos, ext, quat):
        # Rotated static OBB: no add_static_obb factory, so spawn dynamic,
        # orient, then fix_entity (zero mass/velocity + Fixed tag). init()
        # runs before the physics thread starts, so no tick sees it dynamic.
        e = world.add_obb(yope3d.Vec3(*ext), 1.0, yope3d.Vec3(*pos))
        tf = yope3d.reg_get(e, "Transform")
        tf.rotation = quat
        world.fix_entity(e)
        world.attach_box_mesh(e, yope3d.Vec3(*ext), 0.35, 0.35, 0.42)

    def _build_funnel(self, world):
        run  = FUNNEL_MOUTH - FUNNEL_THROAT          # horizontal slope run
        rise = FUNNEL_TOP - FUNNEL_BOTTOM            # vertical slope rise
        alpha = math.atan2(run, rise)                # plate tilt from vertical
        hl    = 0.5 * math.hypot(run, rise)          # plate half-length
        mid_r = 0.5 * (FUNNEL_THROAT + FUNNEL_MOUTH)
        mid_y = 0.5 * (FUNNEL_BOTTOM + FUNNEL_TOP)

        ax_z = yope3d.Vec3(0, 0, 1)
        ax_x = yope3d.Vec3(1, 0, 0)
        # Local +Y maps to the slope direction (throat edge -> mouth edge);
        # plates span the full mouth width so the corners overlap and seal.
        ext_x = (PLATE_THICK, hl, FUNNEL_MOUTH)      # +X / -X plates
        ext_z = (FUNNEL_MOUTH, hl, PLATE_THICK)      # +Z / -Z plates
        self._static_plate(world, ( mid_r, mid_y, 0), ext_x,
                           yope3d.Quat.from_axis_angle(ax_z, -alpha))
        self._static_plate(world, (-mid_r, mid_y, 0), ext_x,
                           yope3d.Quat.from_axis_angle(ax_z,  alpha))
        self._static_plate(world, (0, mid_y,  mid_r), ext_z,
                           yope3d.Quat.from_axis_angle(ax_x,  alpha))
        self._static_plate(world, (0, mid_y, -mid_r), ext_z,
                           yope3d.Quat.from_axis_angle(ax_x, -alpha))

    # ------------------------------------------------------------------ #
    # Body spawns
    # ------------------------------------------------------------------ #

    def _spawn_body(self, world, shape, pos, rng):
        s = rng.choice(("sphere", "aabb", "obb")) if shape == "mixed" else shape
        color = COLOR_BY_SHAPE[s] if shape == "mixed" else COLOR_UNIFORM
        ext = yope3d.Vec3(HALF_EXT, HALF_EXT, HALF_EXT)
        if s == "aabb":
            e = world.add_aabb(ext, MASS, pos)
            world.attach_box_mesh(e, ext, *color)
        elif s == "obb":
            e = world.add_obb(ext, MASS, pos)
            world.attach_box_mesh(e, ext, *color)
        else:
            e = world.add_sphere(MASS, HALF_EXT, pos)
            world.attach_sphere_mesh(e, HALF_EXT, *color)
        return e

    def _spawn_grid(self, world, n, shape, rng):
        x0 = -((GRID_KX - 1) * 0.5) * SPACING
        z0 = -((GRID_KZ - 1) * 0.5) * SPACING
        for i in range(n):
            ix =  i % GRID_KX
            iz = (i // GRID_KX) % GRID_KZ
            iy =  i // (GRID_KX * GRID_KZ)
            pos = yope3d.Vec3(x0 + ix * SPACING,
                              1.0 + iy * SPACING,
                              z0 + iz * SPACING)
            self._spawn_body(world, shape, pos, rng)

    def _spawn_rain(self, world, n, shape, rng):
        # Jittered lattice above the funnel mouth; everything spawns at once so
        # object_count is constant for the whole run (keeps the analyzer's
        # N-from-filename semantics honest). Small random velocities break the
        # lattice symmetry so bodies don't fall in lockstep columns.
        per_layer = SPAWN_COLS * SPAWN_COLS
        x0 = -((SPAWN_COLS - 1) * 0.5) * SPAWN_STEP
        y0 = FUNNEL_TOP + 1.5
        for i in range(n):
            ix =  i % SPAWN_COLS
            iz = (i // SPAWN_COLS) % SPAWN_COLS
            iy =  i // per_layer
            pos = yope3d.Vec3(x0 + ix * SPAWN_STEP + rng.uniform(-0.25, 0.25),
                              y0 + iy * SPAWN_STEP,
                              x0 + iz * SPAWN_STEP + rng.uniform(-0.25, 0.25))
            e = self._spawn_body(world, shape, pos, rng)
            h = yope3d.reg_get(e, "Hull")
            if h is not None:
                h.velocity = yope3d.Vec3(rng.uniform(-1.0, 1.0),
                                         rng.uniform(-2.0, 0.0),
                                         rng.uniform(-1.0, 1.0))

    # ------------------------------------------------------------------ #
    # Free-fly camera
    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        inp = yope3d.input

        dx, dy = inp.get_mouse_delta()
        self.yaw   -= dx * self.sens
        self.pitch  = max(-1.5, min(1.5, self.pitch - dy * self.sens))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

        fwd   = yope3d.camera.get_forward()
        right = yope3d.Vec3(math.cos(self.yaw), 0.0, -math.sin(self.yaw))
        move  = yope3d.Vec3(0.0, 0.0, 0.0)
        if inp.is_key_down(yope3d.KEY_W): move = move + fwd
        if inp.is_key_down(yope3d.KEY_S): move = move - fwd
        if inp.is_key_down(yope3d.KEY_D): move = move + right
        if inp.is_key_down(yope3d.KEY_A): move = move - right
        if inp.is_key_down(yope3d.KEY_SPACE):        move = move + yope3d.Vec3(0, 1, 0)
        if inp.is_key_down(yope3d.KEY_LEFT_CONTROL): move = move - yope3d.Vec3(0, 1, 0)

        speed = self.speed * (self.sprint_mult
                              if inp.is_key_down(yope3d.KEY_LEFT_SHIFT) else 1.0)
        p = yope3d.camera.position
        yope3d.camera.set_position(yope3d.Vec3(p.x + move.x * speed * dt,
                                               p.y + move.y * speed * dt,
                                               p.z + move.z * speed * dt))
