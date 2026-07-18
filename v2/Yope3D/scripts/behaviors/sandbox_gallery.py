"""Sandbox gallery — the 14 demo scenes from the pre-M13 C++ SandboxScript.

Ported 1:1 from src/scripting/SandboxScript.cpp (dropped from the build in M13);
every constant below matches the original, so the pyramids stack and the cloths
sag exactly the way they used to.

    0-2    Pyramid (Small / Medium / Large)  — 4, 7, 10-row OBB pyramids
    3      Mass-Ratio Stack — a 200x-heavier box on light ones (warm-start demo)
    4-12   Spring cloth, 3 anchor variants x 3 node shapes (sphere/AABB/OBB)
    13     Stress test — walled arena + a bulk body grid
    14     Doppler test — a looping audio source falling past the listener

Attach to any entity in a scene file (assets/scenes/sandboxGallery.json does it)
and press Play.

Controls: LEFT/RIGHT switch scene, UP/DOWN pick spawn shape, LMB fire it,
WASD+mouse fly, P collider overlay, H hide HUD, V cloth self-collision.

The cloth scenes default to self-collision OFF (a collision-layer filter — see
apply_cloth_filter): 400 nodes whose AABBs all overlap their neighbours' is what
made these scenes crawl. V toggles it live, with the cost visible in the HUD's
body/island counters.

paramsBlob keys (all optional): start_scene, fly_speed, sprint_mult, mouse_sens,
font, stress_n (bodies in the stress scene), stress_shape (sphere/aabb/obb),
cloth_self_collision (default false).
"""
import yope3d

from behaviors._gallery import GalleryBase

# ---- Constants (verbatim from SandboxScript.cpp) ----

PYR_HALF = 0.45
PYR_SPACING = 1.0

GRID_N = 20
GRID_STEP = 1.0
NODE_HALF = 0.45
NODE_MASS = 1.0
SPRING_K = 300.0

STRESS_HALF = 20.0
STRESS_CEILING = 25.0

# A PGS solver's iteration budget converges a uniform-mass stack in well under
# PGS_VELOCITY_ITERATIONS regardless of warm-starting — that's why the pyramid
# scenes don't show the toggle doing anything. A large mass ratio is the
# textbook counterexample: the heavy box's weight has to propagate down through
# every light-box contact below it, and each contact's effective mass (dominated
# by the LIGHTER of the pair) badly underestimates how much impulse the joint
# actually needs. Warm-starting carries last step's converged impulse forward so
# the light boxes are already "primed" to resist that weight; without it, every
# step restarts from zero and visibly sinks/jitters under the heavy box.
MR_HALF = 0.5
MR_LEVELS = 6          # 5 light boxes + 1 heavy one on top
MR_LIGHT_MASS = 1.0
MR_HEAVY_MASS = 200.0

FLOOR_COLOR = (0.32, 0.28, 0.24)
WALL_COLOR = (0.22, 0.22, 0.28)


ALL_LAYERS = 0xFFFFFFFF


class SandboxGallery(GalleryBase):
    PARAMS = dict(GalleryBase.PARAMS)
    PARAMS.update({
        "stress_n": {"type": "int", "default": 400, "label": "Stress scene body count"},
        "stress_shape": {"type": "str", "default": "sphere",
                         "label": "Stress scene shape (sphere/aabb/obb)"},
        "cloth_self_collision": {"type": "bool", "default": False,
                                 "label": "Cloth nodes collide with each other"},
    })

    # ---- Cloth self-collision filter -------------------------------------
    # A 20x20 cloth is 400 bodies whose AABBs all overlap their neighbours', so
    # node-vs-node contacts dominate narrowphase and fuse the whole sheet into one
    # giant island — that is where the framerate went. The springs alone hold the
    # sheet together, so self-collision is a *visual* upgrade (the cloth can no
    # longer pass through itself), not a structural requirement. Default OFF; V
    # toggles it live so the cost is legible on the HUD.
    #
    # Contacts need (A.layer & B.mask) && (B.layer & A.mask). Nodes sit on their own
    # layer and mask it out of themselves, so they still collide with the floor and
    # with anything you fire at them (both of which keep layer/mask = ALL).

    def cloth_bit(self, world):
        if not world.layers.has("cloth"):
            return world.layers.add("cloth")
        return world.layers["cloth"]

    def apply_cloth_filter(self, world):
        bit = self.cloth_bit(world)
        mask = ALL_LAYERS if self.self_collide else (ALL_LAYERS & ~bit)
        for e in self.cloth_nodes:
            hull = yope3d.reg_get(e, "Hull")
            if hull is None:
                continue
            hull.collision_layer = bit
            hull.collision_mask = mask
            world.wake(e)   # a sleeping island would ignore the new filter

    # ---- Pyramids --------------------------------------------------------

    def build_pyramid(self, world, base_n):
        half_floor = (base_n + 15) * 1.0
        self.static_box(world, (0, -0.5, 0), (half_floor, 0.5, 15.0), FLOOR_COLOR)

        for row in range(base_n):
            count = base_n - row
            y = PYR_HALF + row * (2.0 * PYR_HALF - 0.012)
            t = row / (base_n - 1) if base_n > 1 else 0.5
            color = (0.2 + t * 0.7, 0.45 - t * 0.15, 0.9 - t * 0.7)
            for j in range(count):
                x = -(count - 1) * PYR_SPACING * 0.5 + j * PYR_SPACING
                self.obb(world, (PYR_HALF, PYR_HALF, PYR_HALF), 1.0, (x, y, 0.0), color)

        self.set_camera((0.0, base_n * 0.7, base_n + 4.0))

    # ---- Mass-ratio stack --------------------------------------------------

    def build_mass_ratio_stack(self, world):
        self.static_box(world, (0.0, -0.5, 0.0), (6.0, 0.5, 6.0), FLOOR_COLOR)

        # A visible air gap between levels (unlike the pyramid's near-zero spawn
        # overlap) so this reads as 6 discrete boxes dropping and settling, not
        # one solid column — the short fall + impact is also part of the demo,
        # not just the resting sink.
        spacing = 2.0 * MR_HALF + 0.35
        for level in range(MR_LEVELS):
            heavy = level == MR_LEVELS - 1
            mass = MR_HEAVY_MASS if heavy else MR_LIGHT_MASS
            color = (0.85, 0.15, 0.1) if heavy else (0.25, 0.55, 0.85)
            y = MR_HALF + level * spacing
            self.box(world, (MR_HALF, MR_HALF, MR_HALF), mass, (0.0, y, 0.0), color)

        self.set_camera((4.0, MR_LEVELS * spacing * 0.6, 5.0), pitch=-0.15, yaw=0.7)

    # ---- Spring cloth ----------------------------------------------------
    # variant:  0 = top row fixed, 1 = 4 corners fixed, 2 = 2 top corners fixed
    # shape:    0 = sphere, 1 = AABB, 2 = OBB
    # Variant 1 hangs the sheet horizontally (a trampoline); 0 and 2 hang it
    # vertically (a curtain).

    def build_cloth(self, world, variant, shape):
        fh = (GRID_N + 1) * GRID_STEP * 2.0
        self.static_box(world, (0.0, -5.0, 0.0), (fh, 5.0, fh), FLOOR_COLOR)

        horizontal = variant == 1
        half_w = (GRID_N - 1) * GRID_STEP * 0.5
        top_y = 25.0 if horizontal else (GRID_N - 1) * GRID_STEP + 25.0

        grid = [[None] * GRID_N for _ in range(GRID_N)]
        half = (NODE_HALF, NODE_HALF, NODE_HALF)

        for j in range(GRID_N):
            for i in range(GRID_N):
                if horizontal:
                    pos = (-half_w + i * GRID_STEP, top_y, -half_w + j * GRID_STEP)
                else:
                    pos = (-half_w + i * GRID_STEP, top_y - j * GRID_STEP, 0.0)

                fi = i / (GRID_N - 1)
                fj = j / (GRID_N - 1)

                if shape == 0:
                    color = (0.9 - 0.4 * fi, 0.3 + 0.4 * fj, 0.15 + 0.3 * fi)
                    e = self.sphere(world, NODE_MASS, NODE_HALF, pos, color)
                elif shape == 1:
                    color = (0.1 + 0.2 * fj, 0.5 + 0.3 * fi, 0.8 - 0.3 * fj)
                    e = self.box(world, half, NODE_MASS, pos, color)
                else:
                    color = (fi, fj, 0.4 + 0.3 * (fi + fj) * 0.5)
                    e = self.obb(world, half, NODE_MASS, pos, color)

                grid[i][j] = e
                self.cloth_nodes.append(e)

                if variant == 0:
                    fix = j == 0
                elif variant == 1:
                    fix = i in (0, GRID_N - 1) and j in (0, GRID_N - 1)
                else:
                    fix = j == 0 and i in (0, GRID_N - 1)
                if fix:
                    world.fix_entity(e)

        for j in range(GRID_N):
            for i in range(GRID_N - 1):
                self.spring(world, grid[i][j], grid[i + 1][j], SPRING_K, GRID_STEP)
        for i in range(GRID_N):
            for j in range(GRID_N - 1):
                self.spring(world, grid[i][j], grid[i][j + 1], SPRING_K, GRID_STEP)

        self.apply_cloth_filter(world)

        dist = (GRID_N - 1) * GRID_STEP
        if horizontal:
            self.set_camera((0.0, top_y + dist, dist * 0.7), pitch=-0.8)
        else:
            self.set_camera((0.0, top_y * 0.5, dist + 5.0))

    # ---- Stress ----------------------------------------------------------

    def build_stress(self, world):
        self.static_box(world, (0.0, -0.5, 0.0), (STRESS_HALF, 0.5, STRESS_HALF),
                        FLOOR_COLOR)

        wh = STRESS_CEILING * 0.5
        for pos, ext in (
            ((-STRESS_HALF - 0.4, wh, 0.0), (0.4, wh, STRESS_HALF)),
            ((STRESS_HALF + 0.4, wh, 0.0), (0.4, wh, STRESS_HALF)),
            ((0.0, wh, -STRESS_HALF - 0.4), (STRESS_HALF, wh, 0.4)),
            ((0.0, wh, STRESS_HALF + 0.4), (STRESS_HALF, wh, 0.4)),
        ):
            self.static_box(world, pos, ext, WALL_COLOR)

        n = int(self.params.get("stress_n", 400))
        shape = self.params.get("stress_shape", "sphere")

        # 36x36 columns, stacked upward — same packing the Phase E sweep used, so
        # the stack stays under STRESS_CEILING well past 40k bodies.
        kx, kz, spacing, half = 36, 36, 1.05, 0.5
        x0 = -((kx - 1) * 0.5) * spacing
        z0 = -((kz - 1) * 0.5) * spacing
        color = (0.7, 0.4, 0.3)

        for i in range(max(n, 0)):
            ix = i % kx
            iz = (i // kx) % kz
            iy = i // (kx * kz)
            pos = (x0 + ix * spacing, 1.0 + iy * spacing, z0 + iz * spacing)
            if shape == "aabb":
                self.box(world, (half, half, half), 1.0, pos, color)
            elif shape == "obb":
                self.obb(world, (half, half, half), 1.0, pos, color)
            else:
                self.sphere(world, 1.0, half, pos, color)

        self.set_camera((0.0, 3.5, STRESS_HALF - 2.0))

    # ---- Doppler ---------------------------------------------------------

    def build_doppler(self, world):
        # Floor sits 100 m down so the ball keeps falling past the listener long
        # enough to hear the pitch sweep.
        self.static_box(world, (0.0, -100.5, 0.0), (50.0, 0.5, 50.0), FLOOR_COLOR)

        self.doppler_ent = self.sphere(world, 1.0, 0.5, (0.0, 30.0, 0.0),
                                       (1.0, 0.35, 0.1), vel=(0.0, -40.0, 0.0))

        buf = yope3d.audio.load_sound("audios/test.ogg")
        if buf is not None:
            self.doppler_src = yope3d.audio.create_source(buf)
            self.doppler_src.enable_looping(True)
            self.doppler_src.play()

        self.set_camera((0.0, 2.0, 8.0))

    # ---- shell hooks -----------------------------------------------------

    def init(self, world, entity, params):
        self.self_collide = bool(params.get("cloth_self_collision", False))
        self.cloth_nodes = []
        self.prev_v = False
        GalleryBase.init(self, world, entity, params)

    def on_update(self, world, dt):
        v = yope3d.input.is_key_down(yope3d.KEY_V)
        if v and not self.prev_v and self.cloth_nodes:
            self.self_collide = not self.self_collide
            self.apply_cloth_filter(world)
            self.refresh_stats(world)
        self.prev_v = v

        # The audio source is not an entity, so it needs a manual per-frame sync of
        # position + velocity (velocity is what OpenAL derives the Doppler shift from).
        src = getattr(self, "doppler_src", None)
        ent = getattr(self, "doppler_ent", None)
        if src is None or ent is None:
            return
        tf = yope3d.reg_get(ent, "Transform")
        hull = yope3d.reg_get(ent, "Hull")
        if tf is not None and hull is not None:
            src.set_position(tf.position)
            src.set_velocity(hull.velocity)

    def controls_extra(self):
        return "   V self-collide" if self.cloth_nodes else ""

    def stats_extra(self, world):
        if not self.cloth_nodes:
            return ""
        return "  self-collision %s" % ("ON" if self.self_collide else "OFF")

    def on_unload_scene(self, world):
        src = getattr(self, "doppler_src", None)
        if src is not None:
            src.stop()
        self.doppler_src = None
        self.doppler_ent = None
        self.cloth_nodes = []


SandboxGallery.SCENES = [
    ("Pyramid (Small)", lambda s, w: s.build_pyramid(w, 4)),
    ("Pyramid (Medium)", lambda s, w: s.build_pyramid(w, 7)),
    ("Pyramid (Large)", lambda s, w: s.build_pyramid(w, 10)),
    ("Mass-Ratio Stack (warm-start demo)", lambda s, w: s.build_mass_ratio_stack(w)),
    ("Spring [Sphere] - Top Row Fixed", lambda s, w: s.build_cloth(w, 0, 0)),
    ("Spring [AABB]   - Top Row Fixed", lambda s, w: s.build_cloth(w, 0, 1)),
    ("Spring [OBB]    - Top Row Fixed", lambda s, w: s.build_cloth(w, 0, 2)),
    ("Spring [Sphere] - 4 Corners", lambda s, w: s.build_cloth(w, 1, 0)),
    ("Spring [AABB]   - 4 Corners", lambda s, w: s.build_cloth(w, 1, 1)),
    ("Spring [OBB]    - 4 Corners", lambda s, w: s.build_cloth(w, 1, 2)),
    ("Spring [Sphere] - 2 Top Corners", lambda s, w: s.build_cloth(w, 2, 0)),
    ("Spring [AABB]   - 2 Top Corners", lambda s, w: s.build_cloth(w, 2, 1)),
    ("Spring [OBB]    - 2 Top Corners", lambda s, w: s.build_cloth(w, 2, 2)),
    ("Stress Test", lambda s, w: s.build_stress(w)),
    ("Doppler Test", lambda s, w: s.build_doppler(w)),
]
