"""
Article 2 demo — time dilation under a fixed-timestep accumulator.

Same stage as the smoothness demo: a brick floor and a cannon stream of
script-driven terra-cotta balls, each ball integrated one step per elapsed
physics tick. Because the balls advance only when the physics thread actually
ticks, they are a visual tachometer for the SIM CLOCK: when the sim falls
behind wall time, the balls visibly slow down.

The tick rate stays pinned at 240 Hz while an artificial per-step cost
(world.step_burden_us, a spin-wait in the physics thread) ramps past the
per-step budget (~4166 us at 240 Hz). Physics runs on its OWN thread, so the
overload never stalls the render loop or freezes the balls — the render thread
keeps drawing at full rate. The only thing that gives is the sim clock: it can
no longer keep pace with the wall clock, so the whole simulation runs in
bounded slow motion. That is TIME DILATION, and it is the one thing this demo
is built to show on screen.

The accumulator is pinned to DISCARD (debt past one iteration is dropped), so
the slowdown is a clean, steady dilation with no recovery burst to distract
from it. What happens to *retained* debt on recovery — a fast-forward warp vs.
a smooth bounded catch-up — is a subtle, hard-to-film distinction, and is left
to the writeup rather than faked here under an artificial workload.

Three numbers on screen, nothing else, so the eye stays on the sim:
  workload          — the artificial cost added to every physics step
  sim speed         — how fast the sim clock runs vs the wall clock (% realtime)
  behind realtime   — how many seconds the sim has fallen behind wall time

Controls: LEFT/RIGHT step the workload ladder, [UP]/[DOWN] nudge +-500 us,
[R] reset (zero load + zero the clocks, for a clean take).
"""
import yope3d

from behaviors.article2_smoothness import ARENA, Cannon, build_brick_floor

FONT  = "fonts/monaco.ttf"
HZ    = 240.0
LANES = [0.0]   # single centered lane

# DISCARD: drop debt past one iteration's worth -> pure steady time dilation,
# no retained backlog, no recovery warp. (max_catchup_steps, max_backlog).
DISCARD = (4, 4.0 / HZ)

# LEFT/RIGHT walk this workload ladder (us added to every physics step). The
# per-step budget at 240 Hz is ~4166 us, so the upper rungs sit well over it
# and the sim is forced into dilation.
WORKLOADS = [0, 4000, 8000, 12000, 16000]


class LoopOfDoom:
    PARAMS = {
        "n_balls": {"type": "int", "default": 900, "label": "Cannon Pool"},
    }

    def init(self, world, entity, params):
        yope3d.set_profile_scene("Article2 TimeDilation")
        build_brick_floor(world, ARENA)

        per_lane = max(1, int(params.get("n_balls", 900)) // len(LANES))
        self.cannons = [Cannon(world, z, per_lane) for z in LANES]

        yope3d.camera.set_position(yope3d.Vec3(0.0, 5.5, 21.0))
        yope3d.camera.set_rotation(yope3d.Vec3(-0.14, 0.0, 0.0))

        # ------------------------------ HUD ------------------------------ #
        # Exactly three lines. No mode label, no knob dump — those pull the
        # eye off the sim. Monospace + aligned values, sized for video.
        self.hud_work   = world.add_ui_text(FONT, "", yope3d.Vec2(0.20, 0.05),
                                             yope3d.Vec2(0.80, 0.11), 2)
        self.hud_speed  = world.add_ui_text(FONT, "", yope3d.Vec2(0.20, 0.12),
                                             yope3d.Vec2(0.80, 0.18), 2)
        self.hud_behind = world.add_ui_text(FONT, "", yope3d.Vec2(0.20, 0.19),
                                             yope3d.Vec2(0.80, 0.25), 2)

        world.physics_hz = HZ
        world.max_catchup_steps, world.max_backlog = DISCARD

        self.load_idx  = 0
        self.wall_t    = 0.0
        self.sim_t     = 0.0
        self.tick_rate = 0.0
        self.sim_last  = world.tick_count
        self._apply_load(world, 0)

    def _apply_load(self, world, idx):
        self.load_idx = idx
        world.step_burden_us = WORKLOADS[idx]

    def _reset_clocks(self, world):
        self.wall_t    = 0.0
        self.sim_t     = 0.0
        self.tick_rate = 0.0
        self.sim_last  = world.tick_count

    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        inp = yope3d.input
        if inp.is_key_pressed(yope3d.KEY_RIGHT) and self.load_idx < len(WORKLOADS) - 1:
            self._apply_load(world, self.load_idx + 1)
        if inp.is_key_pressed(yope3d.KEY_LEFT) and self.load_idx > 0:
            self._apply_load(world, self.load_idx - 1)
        if inp.is_key_pressed(yope3d.KEY_UP):
            world.step_burden_us = world.step_burden_us + 500
        if inp.is_key_pressed(yope3d.KEY_DOWN):
            world.step_burden_us = max(0, world.step_burden_us - 500)
        if inp.is_key_pressed(yope3d.KEY_R):
            self._apply_load(world, 0)
            self._reset_clocks(world)

        # Drive the ball stream by the physics ticks that actually elapsed:
        # under dilation fewer ticks land per render frame, so the balls slow.
        ticks = world.tick_count
        nticks = ticks - self.sim_last
        self.sim_last = ticks
        h = 1.0 / HZ
        for c in self.cannons:
            c.update(world, dt, nticks, h)

        # Wall time advances by real dt; sim time advances only by the ticks
        # actually taken. Their gap IS the dilation debt: seconds behind reality.
        self.wall_t += dt
        self.sim_t  += nticks * h
        if dt > 0.0:
            a = 0.06
            self.tick_rate = (1.0 - a) * self.tick_rate + a * (nticks / dt)

        burden = world.step_burden_us
        pct    = min(100.0, 100.0 * self.tick_rate / HZ)
        behind = max(0.0, self.wall_t - self.sim_t)

        # --------------------------- HUD text --------------------------- #
        yope3d.set_text(self.hud_work,   "workload          %+d us/step" % burden)
        yope3d.set_text(self.hud_speed,  "sim speed         %3.0f%% of realtime" % pct)
        yope3d.set_text(self.hud_behind, "behind realtime   %4.1f s" % behind)
