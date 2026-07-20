"""
Trailer demo — recreation of the old Java engine's Shooter scene.

The old-vs-new teaser cuts archival Java-era footage against the new engine
running the SAME SCENE TYPE at far higher body counts. This is the new side,
rebuilt from old_src_java/src/scripts/Shooter.java plus the footage itself:

  - a walled chamber with visible side barriers, camera up high with mouselook
  - the big WHITE SPHERE, driven around with WASD (SPACE/SHIFT for up/down)
  - controlled sprays only: hold F to fire 25-sphere bursts off the white
    sphere's surface (velocity dir*5, random RGB — the old spawn formula)
  - J (or LMB) pulls every sphere toward the white one with an inverse-square
    force, K (or RMB) pushes them away (the old mult/d^2 impulse; scroll
    wheel scales it) — J/K exist because trackpads make held-buttons awkward
  - Y deletes every sprayed sphere

The spheres are deliberately large relative to the chamber, like the old
footage — a few hundred visibly start filling the bottom of the box. The
difference the trailer is selling: these are real rigid bodies in the 240 Hz
solver, so keep spraying far past what the Java engine could hold.

paramsBlob keys (all optional):
  burst_size  int   25    — spheres per burst (old demo: 25)
  force       float 2000  — attract/repel strength A in accel = A / d^2
  seed        int   1337  — RNG seed for spray directions/colors

Controls: WASD white sphere (camera-relative) · SPACE/LSHIFT up/down ·
mouse look · hold F spray · hold J/LMB attract, K/RMB repel · scroll force ·
Y delete all · H toggle HUD
"""
import math, random, yope3d

FONT = "fonts/monaco.ttf"

# ---- Chamber ----------------------------------------------------------- #
HALF      = 26.0                # floor half-extent — 52 x 52 chamber
FLOOR_TOP = -4.0                # floor sits lower so the box reads deeper
WALL_TOP  = 28.0                # side-barrier top
FLOOR_COL = (0.24, 0.24, 0.27)

# ---- The white sphere (Shooter.java's immovable "star") ---------------- #
# Kinematic: contacts can't move it (the old Float.MAX_VALUE mass) and it
# ignores gravity, so the driven velocity is the only thing that moves it.
# Drive model: input accelerates a SCRIPT-SIDE velocity with exponential
# decay (never read back from the hull), written to the body every frame —
# snappy launch, short glide to a stop. Top speed ~= ACCEL / DAMP.
STAR_R     = 4.0
STAR_ACCEL = 48.0               # input acceleration (m/s^2)
STAR_DAMP  = 4.0                # decay rate (1/s): vel *= exp(-DAMP * dt)
STAR_START = (0.0, 6.0, 0.0)

# ---- Spray (verbatim Shooter formulas where they exist) ---------------- #
BURST      = 25                 # spheres per burst, every 4th frame while F held
SPHERE_R   = 2.5
SPHERE_M   = 1.0
MUZZLE_SPD = 5.0                # velocity = dir * 5
SUBDIV     = 1                  # icosphere detail: 80 tris — no instancing and
                                # one draw call per sphere, so detail is the
                                # only per-body render knob we have
# Cull spheres that tunnel through the floor: once the center is a full
# radius (plus slack) below the floor's top face the slab already hides it,
# so removal is never visible on camera.
CULL_Y     = FLOOR_TOP - SPHERE_R - 1.0
CULL_EVERY = 10                 # frames between cull sweeps

CAM_POS   = (0.0, 28.0, 40.0)   # up high, looking down into the chamber
MOUSE_SENS = 0.002


class JavaEra:
    PARAMS = {
        "burst_size": {"type": "int",   "default": BURST, "label": "Burst Size"},
        "force":      {"type": "float", "default": 2000.0, "label": "Attract Force"},
        "seed":       {"type": "int",   "default": 1337,  "label": "RNG Seed"},
    }

    # ------------------------------------------------------------------ #

    def init(self, world, entity, params):
        yope3d.set_profile_scene("Trailer JavaEra")

        self.burst_size = int(params.get("burst_size", BURST))
        self.force      = float(params.get("force", 2000.0))
        self.rng        = random.Random(int(params.get("seed", 1337)))

        # Floor (top face at FLOOR_TOP) and four INVISIBLE side barriers — no
        # mesh, like the old Barrier planes, so the outside camera sees in.
        floor = world.add_static_aabb(yope3d.Vec3(0, FLOOR_TOP - 1, 0),
                                      yope3d.Vec3(HALF, 1.0, HALF))
        world.attach_box_mesh(floor, yope3d.Vec3(HALF, 1.0, HALF), *FLOOR_COL)
        wh = (WALL_TOP - FLOOR_TOP) / 2.0
        wy = FLOOR_TOP + wh
        for pos, half in (((+HALF + 0.5, wy, 0), (0.5, wh, HALF)),
                          ((-HALF - 0.5, wy, 0), (0.5, wh, HALF)),
                          ((0, wy, +HALF + 0.5), (HALF, wh, 0.5)),
                          ((0, wy, -HALF - 0.5), (HALF, wh, 0.5))):
            world.add_static_aabb(yope3d.Vec3(*pos), yope3d.Vec3(*half))
        # Invisible ceiling so repel blasts stay in the chamber.
        world.add_static_aabb(yope3d.Vec3(0, WALL_TOP + 1, 0),
                              yope3d.Vec3(HALF, 1.0, HALF))

        # The white sphere.
        self.star = world.add_kinematic_sphere(yope3d.Vec3(*STAR_START), STAR_R)
        world.attach_sphere_mesh(self.star, STAR_R, 1.0, 1.0, 1.0)
        self.vel = [0.0, 0.0, 0.0]      # script-side drive velocity

        self.balls = []                 # every sprayed sphere, for forces + Y

        # Camera: fixed high vantage, free mouselook.
        yope3d.camera.set_position(yope3d.Vec3(*CAM_POS))
        yope3d.camera.look_at(yope3d.Vec3(0, FLOOR_TOP + 4, 0))
        r = yope3d.camera.rotation
        self.pitch, self.yaw = r.x, r.y

        # ------------------------------ HUD ----------------------------- #
        self.hud_count = world.add_ui_text(FONT, "", yope3d.Vec2(0.30, 0.04),
                                           yope3d.Vec2(0.70, 0.12), 2)
        self.hud_info  = world.add_ui_text(FONT, "", yope3d.Vec2(0.14, 0.13),
                                           yope3d.Vec2(0.86, 0.17), 2)
        self.hud_on   = True
        self.frame    = 0
        self.fps_ema  = 0.0

    # ------------------------------------------------------------------ #

    def _burst(self, world):
        """Shooter.java's spawn: random direction, spawn on the star's
        surface (star_r + r + 0.1 out), velocity dir * 5, random color."""
        sp = yope3d.reg_get(self.star, "Transform").position
        for _ in range(self.burst_size):
            theta = self.rng.random() * 2.0 * math.pi
            phi   = self.rng.random() * 2.0 * math.pi
            nar   = math.cos(phi)
            d     = (math.cos(theta) * nar, math.sin(phi), math.sin(theta) * nar)
            off   = STAR_R + SPHERE_R + 0.1
            e = world.add_sphere(SPHERE_M, SPHERE_R,
                                 yope3d.Vec3(sp.x + d[0] * off,
                                             sp.y + d[1] * off,
                                             sp.z + d[2] * off))
            world.attach_sphere_mesh(e, SPHERE_R,
                                     self.rng.random(),
                                     self.rng.random(),
                                     self.rng.random(),
                                     subdivisions=SUBDIV)
            yope3d.set_velocity(e, yope3d.Vec3(d[0] * MUZZLE_SPD,
                                               d[1] * MUZZLE_SPD,
                                               d[2] * MUZZLE_SPD))
            self.balls.append(e)

    def _delete_all(self, world):
        for e in self.balls:
            world.remove_entity(e)
        self.balls.clear()

    def _cull_fallthrough(self, world):
        """Remove spheres that tunneled through the floor (below CULL_Y the
        floor slab already occludes them, so they vanish off-camera)."""
        keep = []
        for e in self.balls:
            if yope3d.reg_get(e, "Transform").position.y < CULL_Y:
                world.remove_entity(e)
            else:
                keep.append(e)
        self.balls = keep

    def _apply_force(self, world, sign, dt):
        """The old LMB/RMB pain loop: inverse-square impulse toward (+1) or
        away from (-1) the white sphere. accel = force / d^2, d clamped."""
        sp = yope3d.reg_get(self.star, "Transform").position
        for e in self.balls:
            p  = yope3d.reg_get(e, "Transform").position
            dx, dy, dz = sp.x - p.x, sp.y - p.y, sp.z - p.z
            d  = math.sqrt(dx * dx + dy * dy + dz * dz)
            if d < 2.0:
                d = 2.0
            k = sign * self.force / (d * d * d) * SPHERE_M * dt
            world.apply_impulse(e, yope3d.Vec3(dx * k, dy * k, dz * k))

    def _drive_star(self, world, inp, dt):
        """WASD accelerates the WHITE SPHERE (camera-yaw-relative), SPACE /
        LSHIFT up/down. The drive velocity lives HERE (never read back from
        the hull): input adds STAR_ACCEL, then it decays exponentially, then
        it's written to the kinematic body. wake() first — a parked body
        ignores direct velocity writes (Hull.asleep)."""
        fx, fz = -math.sin(self.yaw), -math.cos(self.yaw)   # camera forward, XZ
        rx, rz = math.cos(self.yaw), -math.sin(self.yaw)    # camera right
        ax = ay = az = 0.0
        if inp.is_key_down(yope3d.KEY_W): ax += fx; az += fz
        if inp.is_key_down(yope3d.KEY_S): ax -= fx; az -= fz
        if inp.is_key_down(yope3d.KEY_D): ax += rx; az += rz
        if inp.is_key_down(yope3d.KEY_A): ax -= rx; az -= rz
        n = math.sqrt(ax * ax + az * az)
        if n > 0.0:
            ax, az = ax / n, az / n
        if inp.is_key_down(yope3d.KEY_SPACE):      ay += 1.0
        if inp.is_key_down(yope3d.KEY_LEFT_SHIFT): ay -= 1.0

        decay = math.exp(-STAR_DAMP * dt)
        self.vel[0] = (self.vel[0] + ax * STAR_ACCEL * dt) * decay
        self.vel[1] = (self.vel[1] + ay * STAR_ACCEL * dt) * decay
        self.vel[2] = (self.vel[2] + az * STAR_ACCEL * dt) * decay

        # Floor cap: kinematic-vs-static pairs don't collide, so clamp the
        # star to resting height (center one radius above the floor) by hand.
        floor_y = FLOOR_TOP + STAR_R
        pos = yope3d.reg_get(self.star, "Transform").position
        if pos.y <= floor_y:
            if self.vel[1] < 0.0:
                self.vel[1] = 0.0
            if pos.y < floor_y:
                yope3d.set_position(self.star,
                                    yope3d.Vec3(pos.x, floor_y, pos.z))

        world.wake(self.star)
        yope3d.set_velocity(self.star, yope3d.Vec3(*self.vel))

    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        inp = yope3d.input
        self.frame += 1

        # Mouselook (camera position stays fixed).
        dx, dy = inp.get_mouse_delta()
        self.yaw   -= dx * MOUSE_SENS
        self.pitch  = max(-1.5, min(1.5, self.pitch - dy * MOUSE_SENS))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

        self._drive_star(world, inp, dt)

        # Controlled sprays only: hold F, old cadence (every 4th frame).
        if inp.is_key_down(yope3d.KEY_F) and self.frame % 4 == 0:
            self._burst(world)
        if inp.is_key_pressed(yope3d.KEY_Y):
            self._delete_all(world)
        if self.frame % CULL_EVERY == 0:
            self._cull_fallthrough(world)

        # J/K are the trackpad-friendly aliases for held LMB/RMB.
        if inp.is_lmb_down() or inp.is_key_down(yope3d.KEY_J):
            self._apply_force(world, +1.0, dt)
        elif inp.is_rmb_down() or inp.is_key_down(yope3d.KEY_K):
            self._apply_force(world, -1.0, dt)

        # Scroll scales the force, like the old scrolled() handler.
        sy = inp.get_scroll_y()
        if sy != 0.0:
            self.force *= math.pow(1.1, sy)

        if inp.is_key_pressed(yope3d.KEY_H):
            self.hud_on = not self.hud_on
            if not self.hud_on:
                yope3d.set_text(self.hud_count, "")
                yope3d.set_text(self.hud_info, "")

        if dt > 0.0:
            self.fps_ema = 0.94 * self.fps_ema + 0.06 * (1.0 / dt)

        if self.hud_on:
            yope3d.set_text(self.hud_count, "BODIES  %d" % len(self.balls))
            yope3d.set_text(self.hud_info,
                            "%3.0f fps | 240 Hz | [F] spray  [J/K] pull/push  [Y] clear" %
                            self.fps_ema)
