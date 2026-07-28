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
starts DELIBERATELY low (1 Hz) so the choppiness is unmissable before 240 Hz
makes it vanish. Wall-clock pacing never changes (world.physics_hz alters
step size, not time scale).

Two coupled details the low rungs depend on:
  * physics_hz below ~4 Hz needs max_backlog raised with it — the physics
    thread's accumulator is capped at 0.25 s by default, so a 1 s step can
    never be reached and the sim just stops. See _apply_rate.
  * the cannon fires on SIMULATED time, not wall time, one shot per frame.
    Ball position is a function of tick age alone, so shots that share a tick
    share a point; wall-clock firing only stacks duplicates. See Cannon.update.

STILL MODE (still_mode=True) runs all six rungs at once instead of one at a
time, so the whole ladder fits in a single frame — the shot that survives being
delivered through a 60 fps medium, where 60 Hz and 240 Hz are pixel-identical in
motion but obviously different as bead spacing in a still.

All six rates at once in a 2 x 3 grid, each lane a short window of the same
trajectory standing on its own shallow brick plinth; a ball wraps back to its
lane's muzzle once it has travelled STILL_WRAP in x. Rows are separated in Y,
never in Z — see STILL_COLS for why depth staggering cannot work here.

It runs as a three-phase loop, built to be recorded in one take:

  solo    one ball per lane, no trail, STILL_SOLO_PASSES laps. Choppiness is
          only a visible percept while a single ball is trackable.
  stream  every lane emits one ball per step of ITS OWN rate, so a lane's bead
          count IS its tick count and the trail accumulates into a long
          exposure: 1 Hz reads as a couple of dots, 240 Hz as a solid tube.
  hold    frozen for STILL_HOLD s. One ball wraps as one is born, so nothing
          appears to move. THIS FINAL FRAME IS THE STILL.

That ordering is the article's sentence in order: the choppiness of decipherable
rates first, then sampling below Nyquist obscuring the signal.

The engine cannot run six physics rates simultaneously — physics_hz is one
global. It does not have to: the integrator above is Python, so the engine is
pinned at 240 Hz and each lane consumes every Nth tick with its own h. Every
rung divides 240 exactly (240, 48, 24, 10, 4, 1 ticks per step), so all six
lanes are exactly real-time and phase-locked to the same wall clock.

Six across is a ~8:1 strip. The camera fits itself to the ACTUAL window aspect
(yope3d.window), so setting width/height in yope3d.cfg to something wide — e.g.
1920x260 — fills the frame instead of letterboxing a 16:9 one.

paramsBlob keys (all optional):
  n_balls     int   900    — cannon pool size (ladder mode; still mode sizes
                             its own pool from the rates it has to fill)
  still_mode  bool  False  — six-lane single-frame comparison

Controls: LEFT/RIGHT step the tick-rate ladder (ladder mode only — the only way
Hz changes; each step restarts the stream so the arc always starts from a clean
muzzle shot at the new rate), SPACE or R restarts every stream in place.
"""
import math

import yope3d

RATES = [1.0, 5.0, 10.0, 24.0, 60.0, 240.0]
FONT  = "fonts/monaco.ttf"
ARENA = 24.0
G     = 9.80665
# Whole demo — floor, lanes, labels, camera — sits on this z plane rather than
# at z = 0, so the world origin is out of shot.
SCENE_Z = 20.0
# Where an idle ball waits. Must be OUT OF FRAME, not merely hidden: a pooled
# ball's transform is whatever it was last set to, so any moment where it is
# visible before its new position lands renders it at the park spot. Parking in
# frame (the old behaviour parked at the muzzle, and fresh pool balls sat at
# 0, STILL_H, SCENE_Z — dead centre of the strip) turns that into a flash the
# audience sees; parking below the world turns it into one missing frame.
# The shared pool makes this worse, not better: lane A parks a ball, lane B
# emits it, so the stale position is a NEIGHBOURING lane's muzzle.
PARK_Y = -1000.0

# One trajectory, shared by every ball the cannon ever fires.
BALL_R     = 0.45
LAUNCH_POS = (-ARENA + 1.5, 10)       # (x, y); z comes from the cannon lane
LAUNCH_V   = (4, 0)                # (vx, vy) — spans the floor in ~6.5 s
BOUNCE     = 0.75                      # vy multiplier at each floor hit
EMIT_DT    = 0.04                      # seconds between shots
BALL_COLOR = (0.62, 0.33, 0.22)        # terra-cotta, like the old demo

# ---- Still mode ---------------------------------------------------------- #
# Engine rate every lane subsamples. Must be an exact integer multiple of every
# entry in RATES or the lanes drift out of real time.
STILL_HZ    = 240.0
STILL_SPAN  = 5.0      # x travel the eye is asked to read per lane
STILL_WRAP  = 5.1      # ... and where the ball recycles to the muzzle
STILL_H     = 2.5      # launch height above the lane's own floor
STILL_PITCH = 5.8      # lane origin spacing in x (SPAN + gutter)
STILL_LABEL_EM = 0.45  # world height of one em on the per-lane "N Hz" label

# 2 rows x 3 columns, all six rates at once. Rows are separated in Y, never in
# Z: depth separation only works via perspective, and that same foreshortening
# scales bead spacing per lane, biasing the density comparison the layout exists
# to make. Same z means uniform scale across every lane, and rows stacked in y
# cannot occlude one another.
STILL_COLS  = 3
STILL_ROW_H = 4.4      # y spacing between row floors (arc + label + gutter)

# The brick plinth each row stands on. Depth is in Z, and deliberately shallow:
# a floor that runs off toward the camera adds nothing but a receding slab.
# Enough to read as ground and give the eye one perspective cue, no more.
BRICK_Z     = 1.6      # plinth depth along z
BRICK_THICK = 0.22     # plinth half-height; top face sits exactly on the lane floor

# ---- Making both rows sit at the same angle ------------------------------ #
# The grazing angle onto a horizontal plane is atan((cam_y - plane_y) / dist),
# so two rows at different heights are seen differently by construction. Framed
# close (dist ~12) with the camera at the grid's centre, the bottom plinth reads
# +18 deg (top face, properly 3D) and the top plinth -2 deg — ABOVE the camera,
# so you see its underside nearly edge-on. Different faces, not just different
# angles.
#
# A long lens fixes it: as distance grows both angles converge on the tilt, and
# tilting down puts both plinths below the camera so both show their top face.
#     dist  12, tilt 0 deg  ->  +18.1 / -2.4   (what this looked like)
#     dist  80, tilt 8 deg  ->  +10.8 / +7.7
#     dist 120, tilt 8 deg  ->   +9.8 / +7.8
#     dist 240, tilt 8 deg  ->   +8.9 / +8.0
# Anything past ~80 already has both rows on their top face within ~3 deg, which
# is imperceptible — so distance beyond that buys nothing and costs depth
# precision, which is the real constraint here.
#
# The engine draws with NEAR_PLANE 0.1 / FAR_PLANE 2000 (Camera.h, and it is
# perspective-only — this setup is the script-side stand-in for a true ortho
# cam). With the near plane that close, essentially the whole depth buffer is
# spent before the content even starts, and precision degrades as dist^2:
#     dist 240 -> 0.0343     dist 120 -> 0.0086     dist 80 -> 0.0038
# against a bead DIAMETER of 0.208. Overlapping beads in the dense lanes differ
# in depth by far less than their spacing, so 0.034 visibly z-fights. 120 cuts
# that 4x. The complete fix is raising NEAR_PLANE (a settable near/far would put
# this at ~1e-5), not moving the camera.
STILL_TILT_DEG   = 8.0
STILL_LENS_D     = 120.0
STILL_FIT_MARGIN = 1.06

# Video phases. Solo shows ONE ball per lane with no trail — the only state in
# which "choppiness" is a visible percept, because a single ball is trackable.
# Stream then accumulates the long exposure, and hold freezes it: that final
# frame IS the still.
STILL_SOLO_PASSES = 3  # solo laps per lane before the trail turns on
STILL_HOLD        = 5.0

# ---- What actually sets the on-screen size of a bead --------------------- #
# Apparent radius is r / strip_width, and r is capped by the bead spacing
# STILL_SPAN / (STILL_T * rate) — so shrinking STILL_SPAN changes NOTHING:
# spacing, radius and camera distance all shrink together. The x geometry is
# scale-invariant. Only two things move the needle:
#
#   STILL_T — seconds of flight shown per lane, i.e. bead COUNT. The 24 Hz lane
#     holds 24 * T beads across one sixth of the frame, so at 1920 wide that is
#     1920 / (6 * 24 * T) px per bead. T is the price of arc: a full fall plus
#     one bounce needs 2.5 * sqrt(2H/g), which is 1.79 s at H = 2.5. Below that
#     the rebound gets clipped; above it beads shrink for no extra information.
#   STILL_BEAD_FILL — bead diameter as a fraction of that spacing. 0.5 is
#     exactly touching. Past 1.0 beads overlap, but overlap is not the legibility
#     cliff: a lit sphere chain keeps a scalloped silhouette and separate
#     speculars until roughly 2x, so filling past touching buys real pixels
#     before anything is lost.
#
# STILL_SEP_HZ is the fastest rung that must still read as discrete beads;
# everything above it is meant to converge into a continuous curve. No radius
# separates 60 from 240 — past that the only remaining signal is arc shape,
# which is the honest limit of a long-exposure figure.
STILL_T          = 1.70
STILL_SEP_HZ     = 24.0
STILL_BEAD_FILL  = 0.85
STILL_VX    = STILL_SPAN / STILL_T
STILL_R      = STILL_BEAD_FILL * STILL_VX / STILL_SEP_HZ

def build_brick_strip(world, x_min, x_max, y_top, depth):
    """A shallow brick plinth: top face at y_top, `depth` deep along z.

    Purely visual as far as the balls are concerned — their "floor" is the
    y-check in Cannon.step. Each tile carries the full brick.jpg (rect UVs are
    0-1 per face), so the tiling is what repeats the texture; tiles are made
    square in the top face (width == depth) to keep the brick undistorted.
    """
    half_d = depth * 0.5
    span   = x_max - x_min
    n      = max(1, int(round(span / depth)))
    half_w = span / (2.0 * n)
    for i in range(n):
        cx = x_min + half_w + i * 2.0 * half_w
        e  = world.add_static_aabb(
            yope3d.Vec3(cx, y_top - BRICK_THICK, SCENE_Z),
            yope3d.Vec3(half_w, BRICK_THICK, half_d))
        world.attach_box_mesh(e, yope3d.Vec3(half_w, BRICK_THICK, half_d),
                              1.0, 1.0, 1.0)
        yope3d.reg_add(e, "Material")
        m = yope3d.reg_get(e, "Material")
        m.albedo_map = "textures/brick.jpg"
        m.metallic = 0.0
        m.roughness = 0.85


class BallPool:
    """Shared, recycled stock of mesh-only spheres.

    One pool serves every cannon on screen — still mode's six lanes hand balls
    back and forth through this single free list rather than reserving six
    fixed sub-pools, so a lane can be resized without stranding entities.
    Balls are visual only (physics body detached at spawn); their motion is the
    Python integrator in Cannon.step, never the engine solver.
    """

    def __init__(self, world, n, radius, park):
        self.ready = []
        for _ in range(n):
            e = world.add_sphere(1.0, radius, park)
            world.attach_sphere_mesh(e, radius, *BALL_COLOR)
            world.detach_physics_body(e)
            world.set_mesh_visible(e, False)
            self.ready.append(e)


class Cannon:
    """Fixed-point, fixed-velocity ball emitter over one lane of a shared pool.

    A lane owns its rate, its muzzle, its floor and its x window. `div` is how
    many ENGINE ticks make one step of this lane's rate: 1 in ladder mode
    (engine runs at the lane's rate), 240/rate in still mode (engine pinned at
    STILL_HZ, lane subsamples). Because a ball's position depends only on how
    many steps it has lived, a lane is a strobe photo of its own tick grid.
    """

    def __init__(self, pool, rate, div, muzzle, vx, wrap_x, radius, z,
                 mode, floor_y=0.0):
        self.pool     = pool
        self.rate     = rate
        self.h        = 1.0 / rate
        self.div      = div
        self.mx       = muzzle[0]
        self.my       = muzzle[1]
        self.vx       = vx
        self.wrap_x   = wrap_x
        self.radius   = radius
        self.z        = z
        self.floor_y  = floor_y        # this lane's own ground, not world y = 0
        # "stream"   one bead per step, so bead count == tick count (the still)
        # "solo"     exactly one ball in flight, re-fired on wrap (choppiness)
        # "interval" the 25 shots/s cannon — ladder mode, since one-per-step at
        #            240 Hz over an 11.5 s flight would want ~2800 balls
        self.mode     = mode
        self.timer    = 0.0
        self.active   = []             # [entity, x, y, vx, vy] — FIFO by age
        self.steps    = 0              # lane steps consumed so far
        self.wrapped  = False          # first recycle == window full == steady
        self.passes   = 0              # completed laps (solo phase counts these)
        self.enabled  = True           # False = parked slot, skipped entirely
        # Ceiling on steps consumed in one frame. A guard against a stall
        # dumping unbounded work into a single update, NOT a policy knob — the
        # doom demo raises it, because there the whole point is a recovery burst
        # in which the sim legitimately takes thousands of ticks in one frame,
        # and clamping it would hide the very warp being filmed.
        self.max_catchup = 240

    def _park(self, world, e):
        """Hide a ball AND move it out of frame (see PARK_Y), so a parked ball
        never sits anywhere the camera can see — otherwise the next reuse shows
        that stale spot for a moment before the muzzle reset lands."""
        tf = yope3d.reg_get(e, "Transform")
        tf.position = yope3d.Vec3(self.mx, PARK_Y, self.z)
        world.set_mesh_visible(e, False)

    def _emit(self, world):
        if not self.pool.ready:
            return
        e = self.pool.ready.pop()
        tf = yope3d.reg_get(e, "Transform")
        tf.position = yope3d.Vec3(self.mx, self.my, self.z)
        world.set_mesh_visible(e, True)
        self.active.append([e, self.mx, self.my, self.vx, 0.0])

    def restart(self, world, ticks):
        for ball in self.active:
            self._park(world, ball[0])
            self.pool.ready.append(ball[0])
        self.active  = []
        self.timer   = 0.0
        self.steps   = ticks // self.div
        self.wrapped = False
        self.passes  = 0
        if not self.enabled:
            return
        # Load the muzzle now rather than on the first tick, so the leading ball
        # moves on tick 1 instead of tick 2 — at 1 Hz that is a whole second of
        # dead air after every restart.
        self._emit(world)

    def step(self, world):
        """One fixed step of the whole lane — the article's entire physics."""
        h  = self.h
        gy = self.floor_y + self.radius   # resting height on THIS lane's floor
        for ball in self.active:
            ball[4] -= G * h           # vy
            ball[1] += ball[3] * h     # x
            ball[2] += ball[4] * h     # y
            if ball[2] < gy and ball[4] < 0.0:
                ball[2] = gy
                ball[4] *= -BOUNCE
        # Balls share one launch point and one vx, so age orders them strictly
        # along x: whatever has wrapped is always at the front of the list.
        while self.active and self.active[0][1] > self.wrap_x:
            ball = self.active.pop(0)
            self._park(world, ball[0])
            self.pool.ready.append(ball[0])
            self.wrapped = True        # window is full — lane has reached steady state
            self.passes += 1

    def update(self, world, ticks):
        """Consume however many of this lane's steps the engine ticks bought."""
        if not self.enabled:
            return
        target = ticks // self.div
        n = min(target - self.steps, self.max_catchup)
        self.steps = target
        if n <= 0:
            return
        if self.mode == "stream":
            for _ in range(n):
                self.step(world)
                self._emit(world)
        elif self.mode == "solo":
            # Exactly one ball alive: re-fire the instant the previous lap wraps,
            # so the lane shows motion rather than a trail.
            for _ in range(n):
                self.step(world)
                if not self.active:
                    self._emit(world)
        else:
            for _ in range(n):
                self.step(world)
            # Fire on the fixed interval of SIMULATED time, at most one shot per
            # frame. A ball's position is a pure function of how many steps it
            # has lived, so two balls born between the same pair of steps sit on
            # the exact same point forever: emitting on the wall clock just
            # stacks 1 / (EMIT_DT * hz) invisible duplicates per bead at low Hz,
            # and lets one oversized frame dt (Engine::update clamps it to
            # 50 ms) dump a burst that leads the rest of the stream by a visible
            # gap. Driving the timer off `n * h` keeps the spacing at 25 beads/s
            # wherever the rate can afford it and degrades to one bead per step
            # below that.
            self.timer += n * self.h
            if self.timer >= EMIT_DT:
                # Never bank more than a single shot of credit — at 1 Hz one
                # tick is worth 25 intervals.
                self.timer = min(self.timer - EMIT_DT, EMIT_DT)
                self._emit(world)
        # Write transforms once per frame, not once per step.
        for ball in self.active:
            tf = yope3d.reg_get(ball[0], "Transform")
            tf.position = yope3d.Vec3(ball[1], ball[2], self.z)


class Smoothness:
    PARAMS = {
        "n_balls":    {"type": "int",  "default": 900,   "label": "Cannon Pool"},
        "still_mode": {"type": "bool", "default": False,
                       "label": "Still Mode (all 6 rates at once)"},
    }

    def init(self, world, entity, params):
        yope3d.set_profile_scene("Article2 Smoothness")

        self.still     = bool(params.get("still_mode", False))
        self.fps_ema   = 0.0
        self.last_tick = world.tick_count
        self.tick_rate = 0.0
        if self.still:
            self._init_still(world, params)
        else:
            self._init_ladder(world, params)

    # ---------------------- ladder mode (one rate) ---------------------- #

    def _init_ladder(self, world, params):
        build_brick_strip(world, -ARENA, ARENA, 0.0, BRICK_Z)
        pool = BallPool(world, int(params.get("n_balls", 900)), BALL_R,
                        yope3d.Vec3(LAUNCH_POS[0], PARK_Y, SCENE_Z))
        self.lanes = [Cannon(pool, RATES[0], 1, LAUNCH_POS, LAUNCH_V[0],
                             ARENA - 0.6, BALL_R, SCENE_Z, "interval")]

        # Side view: the whole trajectory in frame, arcs reading left→right.
        yope3d.camera.set_position(yope3d.Vec3(0.0, 5.0, 20.0 + SCENE_Z))
        yope3d.camera.set_rotation(yope3d.Vec3(-0.12, 0.0, 0.0))

        self.hud = world.add_ui_text(FONT, "", yope3d.Vec2(0.07, 0.04),
                                     yope3d.Vec2(0.88, 0.12), 2)
        self.rate_idx = 0
        self._apply_rate(world)

    def _apply_rate(self, world):
        hz = RATES[self.rate_idx]
        world.physics_hz = hz
        # The physics thread only advances once its accumulator holds a full
        # step, and that accumulator is clamped to max_backlog (0.25 s by
        # default). Below ~4 Hz the step is LONGER than the ceiling, so the
        # condition can never be met and the sim silently freezes — which is
        # what made the 1 Hz rung look like "spheres take forever to drop".
        # Give the ceiling room for one step plus change at the current rate.
        world.max_backlog = max(0.25, 1.5 / hz)
        lane = self.lanes[0]
        lane.rate = hz
        lane.h    = 1.0 / hz
        lane.restart(world, world.tick_count)

    # ------------- still mode (all six rates, 2 rows x 3 columns) -------- #

    def _init_still(self, world, params):
        # Engine pinned at the top of the ladder; every lane subsamples it.
        world.physics_hz  = STILL_HZ
        world.max_backlog = max(0.25, 1.5 / STILL_HZ)
        self.hud = None

        rows = (len(RATES) + STILL_COLS - 1) // STILL_COLS
        self.span_total = (STILL_COLS - 1) * STILL_PITCH + STILL_SPAN
        self.x_first    = -0.5 * self.span_total

        # One bead per lane step means a lane holds rate * STILL_T balls in
        # flight; size the shared pool from the rates rather than a magic
        # number, so editing RATES or STILL_T cannot silently starve a lane.
        # The 240 Hz lane is most of it — showing all six costs barely more
        # than showing three.
        need = sum(int(r * STILL_T) + 2 for r in RATES)
        pool = BallPool(world, int(need * 1.1) + 8, STILL_R,
                        yope3d.Vec3(0.0, PARK_Y, SCENE_Z))

        ticks       = world.tick_count
        self.lanes  = []
        self.labels = []
        for i, rate in enumerate(RATES):
            row  = i // STILL_COLS
            col  = i % STILL_COLS
            # Row 0 on top, so the rates read left-to-right, top-to-bottom.
            base = (rows - 1 - row) * STILL_ROW_H
            x0   = self.x_first + col * STILL_PITCH
            lane = Cannon(pool, rate, int(round(STILL_HZ / rate)),
                          (x0, base + STILL_H), STILL_VX, x0 + STILL_WRAP,
                          STILL_R, SCENE_Z, "solo", base)
            lane.restart(world, ticks)
            self.lanes.append(lane)
            # Per-lane label only — no unified HUD in still mode.
            label = world.add_text_label_3d(
                FONT, "%d Hz" % int(rate),
                yope3d.Vec3(x0 + STILL_SPAN * 0.5, base + STILL_H + 0.9, SCENE_Z))
            tl = yope3d.reg_get(label, "TextLabel3D")
            tl.size_meters = STILL_LABEL_EM
            tl.billboard   = 0          # camera is dead-on; billboarding only jitters
            self.labels.append(label)

        # One shallow plinth per row, spanning that row's lanes.
        for row in range(rows):
            build_brick_strip(world, self.x_first - 0.4,
                              self.x_first + self.span_total + 0.4,
                              (rows - 1 - row) * STILL_ROW_H, BRICK_Z)

        self.phase  = "solo"
        self.hold_t = 0.0
        self._frame_still_camera(rows)

    def _set_mode(self, world, ticks, mode, phase):
        for lane in self.lanes:
            lane.mode = mode
            lane.restart(world, ticks)
        self.phase  = phase
        self.hold_t = 0.0

    def _advance_phase(self, world, ticks, dt):
        """solo -> stream -> hold -> solo.

        solo   one ball per lane, no trail: the only state where choppiness is
               visible, because a single ball is trackable.
        stream the trail accumulates into the long exposure.
        hold   frozen for STILL_HOLD s — nothing moves, because every lane is
               recycling one ball per step into the slot the last one left.
               This final frame is the still.
        """
        if self.phase == "solo":
            if all(l.passes >= STILL_SOLO_PASSES for l in self.lanes):
                print("[smoothness] solo done - trail on")
                self._set_mode(world, ticks, "stream", "stream")
        elif self.phase == "stream":
            if all(lane.wrapped for lane in self.lanes):
                self.phase  = "hold"
                self.hold_t = 0.0
                print("[smoothness] steady - hold %.0fs (screenshot window)" %
                      STILL_HOLD)
        else:
            self.hold_t += dt
            if self.hold_t >= STILL_HOLD:
                self._set_mode(world, ticks, "solo", "solo")

    def _frame_still_camera(self, rows):
        """Near-orthographic framing: long lens, slight downward tilt.

        Distance and tilt come from STILL_LENS_D / STILL_TILT_DEG, which exist
        to make both rows present the same face at the same angle — see the
        note there. The FOV is then whatever frames the grid at that distance,
        derived from the ACTUAL window aspect (yope3d.window), so changing
        width/height in yope3d.cfg reframes the shot with no code change.
        """
        bottom = -2.0 * BRICK_THICK                       # underside of the plinths
        top    = (rows - 1) * STILL_ROW_H + STILL_H + 0.9 + STILL_LABEL_EM
        cy     = 0.5 * (bottom + top)
        aspect = max(0.1, yope3d.window.get_width() /
                          max(1.0, float(yope3d.window.get_height())))

        tilt = math.radians(STILL_TILT_DEG)
        d    = STILL_LENS_D
        # Half-extents that must fit in the tilted view plane. A vertical world
        # segment projects to length * cos(tilt) there; d is measured along the
        # view axis, so it is the divisor for both.
        need_v = 0.5 * (top - bottom) * math.cos(tilt) * STILL_FIT_MARGIN
        need_h = 0.5 * self.span_total * STILL_FIT_MARGIN / aspect
        yope3d.camera.set_fov(2.0 * math.atan(max(need_v, need_h) / d))
        yope3d.camera.set_position(
            yope3d.Vec3(0.0, cy + d * math.sin(tilt),
                        SCENE_Z + d * math.cos(tilt)))
        yope3d.camera.look_at(yope3d.Vec3(0.0, cy, SCENE_Z))

        # Pull the near plane up to just short of the content. All of it sits in
        # a ~2-unit slab at distance d, so the default 0.1 near plane spends the
        # entire depth buffer on empty space in front of it — that is what makes
        # the overlapping beads in the dense lanes z-fight. Bracketing the slab
        # takes resolvable depth from ~0.0086 to ~4e-6 at d = 120.
        # Guarded: set_clip_planes lands on main, so this is a no-op until the
        # branch has merged it, and the framing above stands on its own.
        if hasattr(yope3d.camera, "set_clip_planes"):
            yope3d.camera.set_clip_planes(d * 0.5, d * 2.0)

    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        inp   = yope3d.input
        ticks = world.tick_count
        if not self.still:
            if inp.is_key_pressed(yope3d.KEY_RIGHT) and self.rate_idx < len(RATES) - 1:
                self.rate_idx += 1
                self._apply_rate(world)
            if inp.is_key_pressed(yope3d.KEY_LEFT) and self.rate_idx > 0:
                self.rate_idx -= 1
                self._apply_rate(world)
        if inp.is_key_pressed(yope3d.KEY_SPACE) or inp.is_key_pressed(yope3d.KEY_R):
            for lane in self.lanes:
                lane.restart(world, ticks)
            if self.still:
                # Re-arm the cycle too, or a restart during a hold would keep
                # counting down and swap groups over a half-filled picture.
                self.phase  = "fill"
                self.hold_t = 0.0

        # Drive every lane off the engine tick counter it subsamples.
        for lane in self.lanes:
            lane.update(world, ticks)

        if self.still:
            self._advance_phase(world, ticks, dt)

        # Still mode carries per-lane labels only — nothing else to update.
        if self.hud is None:
            return

        # Measured rates (EMA-smoothed so the HUD is readable on camera).
        if dt > 0.0:
            a = 0.06
            self.fps_ema = (1.0 - a) * self.fps_ema + a * (1.0 / dt)
            inst = (ticks - self.last_tick) / dt
            self.last_tick = ticks
            self.tick_rate = (1.0 - a) * self.tick_rate + a * inst

        # --------------------------- HUD text --------------------------- #
        yope3d.set_text(self.hud,
                        "Physics %d Hz | Render %3.0f fps | Real: %2.0f tps" %
                        (int(RATES[self.rate_idx]), self.fps_ema, self.tick_rate))