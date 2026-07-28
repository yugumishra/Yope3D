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

Two accumulator regimes, selected by the `regime` param at LOAD time (one
process, one regime — see "Recording" below), differing ONLY in what happens to
the debt the overload built up — which is the whole of iterations 3 and 4:

  discard   max_backlog = one iteration's worth. Debt past that is dropped, so
            the sim runs in clean bounded slow motion and, when the load lifts,
            simply resumes at 100%. The lost seconds are lost: "behind
            realtime" never comes back down.
  retain    max_backlog = RETAIN_BACKLOG seconds. Debt is kept and repaid at up
            to max_catchup_steps per thread iteration, so lifting the load
            produces a visible fast-forward — sim speed reads well ABOVE 100%
            and "behind realtime" walks back to zero.

Drop the load with the two side by side and the difference is the article's
point: capping the backlog is what makes the recovery bounded instead of a
spiral, and discarding it is what makes the loss permanent.

A horizontal HUD band across the top, no knob dump, so the eye stays on the sim
— and so the two regimes can be stacked vertically in an editor without the HUD
eating half the frame twice:
  load     — the artificial cost added to every physics step
  render   — frames per second. The control variable, and adjacent to sim on
             purpose: a flat render rate beside a collapsing sim rate IS the
             claim. Without it a viewer cannot tell the sim slowed rather than
             the whole app stalling.
  sim      — sim clock vs wall clock as a MULTIPLE (0.27x, 1.99x), so slowdown
             and catch-up read on one scale either side of 1.00x
and, on its own line below, the number the whole demo is about:
  behind   — how many seconds the sim has fallen behind wall time. It is the
             only readout that differs between the two regimes.
There is deliberately no regime label: which run is which is a caption the
editor adds once, not a line burned into both takes.

The take runs itself: SCHEDULE (below) drives the whole load timeline off the
wall clock, so both regimes execute an identical sequence and the two recordings
stack frame for frame. It arms on scene load and re-arms on [R], which resets
the clocks AND the ball stream (see _restart_take).

RECORDING — one process per take, and the regime is a param for exactly that
reason. Launch, press [R], record until the console says "schedule complete",
quit. Then launch the OTHER scene and do the same:

    ./build/mac-debug/yope3d --scene scenes/article2_doom.json          # discard
    ./build/mac-debug/yope3d --scene scenes/article2_doom_retain.json   # retain

Cut both on the [R] press. Do NOT try to shoot both takes in one run. Three
pieces of state carry across a regime change inside a live process, and each one
puts the second take somewhere the first never was:

  * the physics thread's accumulator (`accum`, Engine::startPhysicsThread) holds
    whatever undrained debt the previous run ended with. Nothing in the scripting
    surface can zero it, which is the hard blocker and the reason a key cannot be
    made correct here.
  * the ball stream is mid-arc, stretched out by the dilation the last run ended
    in, where a fresh take starts from an empty floor.
  * `retain` runs its sim clock to ~200% during recovery, so the two regimes
    reach the same wall-clock second having consumed very different tick counts.

None of that shows on screen at the moment you press the key. It shows up in the
edit, once both takes are already shot — which is how it was found.

[R] is still an honest take start, but only from a drained accumulator: press it
right after load (the schedule opens on 7 s at zero load, so there is no debt
yet), not partway through a run.

Render fps never moves in either take. That is the control variable, and it is
the half of the claim the HUD used to leave unevidenced.

Note what retain does NOT do: it recovers only the RETAIN_BACKLOG seconds its
cap held, so the rest of the deficit is gone for good. Bounded recovery, not
free recovery — which is exactly the article's iteration-4 point.

paramsBlob keys (all optional):
  n_balls  int   900       — cannon pool size
  regime   enum  discard   — "discard" or "retain"; load-time only, see above

Controls: [R] restart the take (clocks, ball stream, schedule), LEFT/RIGHT step
the workload ladder, [UP]/[DOWN] nudge +-500 us. Any manual load key drops out
of the schedule until [R]. There is deliberately no regime key.
"""
import yope3d

from behaviors.article2_smoothness import (
    ARENA, BALL_R, BRICK_Z, LAUNCH_POS, LAUNCH_V, PARK_Y, SCENE_Z,
    BallPool, Cannon, build_brick_strip)

FONT  = "fonts/monaco.ttf"
HZ    = 240.0
LANES = [0.0]   # single centered lane

# HUD band: wide and thin, hugging the top edge. Pull the x pair in or out to
# match however much of the frame the stream actually fills; keep the height
# small, because stacking the two regimes doubles every pixel it costs.
HUD_MIN = yope3d.Vec2(0.035, 0.035)
HUD_MAX = yope3d.Vec2(0.965, 0.085)
# The debt gets its own line underneath. It is the only readout that differs
# between the two regimes — the whole reason there are two takes — so it should
# not have to compete for the eye with three steady numbers on the same row.
HUD2_MIN = yope3d.Vec2(0.035, 0.095)
HUD2_MAX = yope3d.Vec2(0.965, 0.145)

# name -> (max_catchup_steps, max_backlog). Both cap the per-iteration work the
# same way — the article's iteration 4 — and differ only in retained debt:
#   discard  debt dropped, loss permanent
#   retain   debt repaid, sim fast-forwards
#
# Chosen ONCE at init from the `regime` param, never live. See the module
# docstring: a regime swap inside a running process cannot reproduce a cold
# start, and the two takes have to be frame-identical to be worth stacking.
RETAIN_BACKLOG = 3.0     # seconds of debt kept in the "retain" regime
REGIMES = {
    "discard": (4, 4.0 / HZ),
    "retain":  (4, RETAIN_BACKLOG),
}
DEFAULT_REGIME = "discard"

# LEFT/RIGHT walk this workload ladder (us added to every physics step). The
# per-step budget at 240 Hz is ~4166 us, so the upper rungs sit well over it
# and the sim is forced into dilation.
#
# 2000 is the RECOVERY rung and the reason it exists: catch-up speed is
# (budget / actual step cost), so at 2000 us the sim replays at ~2x realtime and
# drains its backlog over seconds — visible. Dropping straight to 0 instead
# repays the same debt in roughly one frame (the physics thread iterates every
# 100 us, so with no per-step cost it can retire ~40,000 steps a second), and
# the recovery vanishes before a viewer sees it. 4000 sits just under budget and
# recovers at only ~1.04x, which is too slow to film.
WORKLOADS = [0, 2000, 4000, 8000, 12000, 16000]

# ---------------------------------------------------------------------------
# The take, as (seconds, us/step). Both regimes run this identical timeline so
# the two recordings can be stacked in an editor and compared frame for frame.
#
# It advances on WALL time, never on tick_count. That is the load-bearing
# detail: the retain regime deliberately runs its sim clock at ~200% during
# recovery, so a tick-driven schedule would reach every transition earlier in
# real time than the discard run and the two takes would drift apart exactly
# where the viewer is being asked to compare them.
#
# The ascent is worth reading as three acts, not five steps: 2000 and 4000 sit
# at or under the ~4166 us per-step budget, so the sim holds 100% and nothing
# appears to happen — that IS the point, it locates the cliff. 8000 and beyond
# are over budget and dilation begins.
# ---------------------------------------------------------------------------
SCHEDULE = [
    ( 7.0,     0),   # baseline: everything nominal
    ( 2.5,  2000),   # under budget  — no effect, so it only needs long enough
    ( 2.5,  4000),   # at budget     — still no effect, to register as a beat
    ( 5.0,  8000),   # over budget   — dilation starts, ~52%
    ( 5.0, 12000),   #                 ~35%
    ( 5.0, 16000),   #                 ~26%, debt piles up
    (10.0,  2000),   # RECOVERY: reduced, NOT zero (see WORKLOADS above)
    ( 3.0,     0),   # settle, so the take has an ending to cut on
]


class LoopOfDoom:
    PARAMS = {
        "n_balls": {"type": "int", "default": 900, "label": "Cannon Pool"},
        # The regime is a LOAD-TIME choice, not a control. One process, one
        # regime, one take — see the module docstring for why it cannot be a key.
        "regime":  {"type": "enum", "default": DEFAULT_REGIME,
                    "options": list(REGIMES), "label": "Accumulator Regime"},
    }

    def init(self, world, entity, params):
        yope3d.set_profile_scene("Article2 TimeDilation")
        build_brick_strip(world, -ARENA, ARENA, 0.0, BRICK_Z)

        # One shared pool across the lanes, same as the smoothness ladder. The
        # cannon runs in "interval" mode: the 25 shots/s wall-clock-paced stream,
        # NOT one-bead-per-step — here the stream is a tachometer for the sim
        # clock, so it has to keep firing at a rate the dilation can visibly slow.
        pool = BallPool(world, int(params.get("n_balls", 900)), BALL_R,
                        yope3d.Vec3(LAUNCH_POS[0], PARK_Y, SCENE_Z))
        self.cannons = [
            Cannon(pool, HZ, 1, LAUNCH_POS, LAUNCH_V[0], ARENA - 0.6,
                   BALL_R, SCENE_Z + z, "interval")
            for z in LANES
        ]
        for c in self.cannons:
            # In the retain regime a recovery burst legitimately delivers
            # thousands of ticks in one frame; the default 240 guard would clamp
            # the stream and hide the fast-forward this demo exists to show.
            # Sized off RETAIN_BACKLOG in BOTH regimes on purpose: it is a
            # ceiling, never reached under discard, and a knob that differed
            # between the takes would be a second variable in a comparison that
            # is supposed to have exactly one.
            c.max_catchup = int(RETAIN_BACKLOG * HZ) + 240

        yope3d.camera.set_position(yope3d.Vec3(0.0, 5.5, 21.0 + SCENE_Z))
        yope3d.camera.set_rotation(yope3d.Vec3(-0.14, 0.0, 0.0))

        # ------------------------------ HUD ------------------------------ #
        # ONE horizontal band, spanning the stream's width at the top of frame.
        # The two regimes get stacked vertically in an editor, so every row of
        # pixels the HUD costs is doubled — and a tall block would collide with
        # the launch arc. No regime label: which run is which is a caption the
        # editor adds once, not a line burned into both takes.
        self.hud        = world.add_ui_text(FONT, "", HUD_MIN,  HUD_MAX,  2)
        self.hud_behind = world.add_ui_text(FONT, "", HUD2_MIN, HUD2_MAX, 2)

        world.physics_hz = HZ

        self.load_idx    = 0
        self.wall_t      = 0.0
        self.sim_t       = 0.0
        self.tick_rate   = 0.0
        self.fps_ema     = 0.0
        self.sched_on    = False
        self.sched_idx   = 0
        self.sched_t     = 0.0
        self._apply_regime(world, params.get("regime", DEFAULT_REGIME))
        # Scene load is the take, so it goes through the same restart path as
        # [R] rather than seeding the stream and clocks itself — the desync this
        # used to produce was exactly the two paths disagreeing.
        self._restart_take(world)

    def _apply_load(self, world, idx):
        self.load_idx = idx
        world.step_burden_us = WORKLOADS[idx]

    def _set_burden(self, world, us):
        world.step_burden_us = us
        if us in WORKLOADS:          # keep LEFT/RIGHT continuous after a takeover
            self.load_idx = WORKLOADS.index(us)

    def _start_schedule(self, world):
        self.sched_on  = True
        self.sched_idx = 0
        self.sched_t   = 0.0
        self._set_burden(world, SCHEDULE[0][1])
        print("[doom] schedule start — %s, %.0fs total" %
              (self.regime, sum(d for d, _ in SCHEDULE)))

    def _advance_schedule(self, world, dt):
        """Walk the timeline on WALL time — see the note on SCHEDULE."""
        if not self.sched_on or self.sched_idx >= len(SCHEDULE):
            return
        self.sched_t += dt
        while (self.sched_idx < len(SCHEDULE)
               and self.sched_t >= SCHEDULE[self.sched_idx][0]):
            self.sched_t -= SCHEDULE[self.sched_idx][0]
            self.sched_idx += 1
            if self.sched_idx < len(SCHEDULE):
                self._set_burden(world, SCHEDULE[self.sched_idx][1])
                print("[doom] t=%5.1fs  load -> %d us/step" %
                      (self.wall_t, SCHEDULE[self.sched_idx][1]))
            else:
                self.sched_on = False
                print("[doom] schedule complete — stop recording")

    def _apply_regime(self, world, name):
        """Resolve the `regime` param once, at load. Never called again.

        Loudly rejects an unknown name instead of quietly picking one: the
        param is normally edited by hand in a scene's paramsBlob, and a typo
        that silently ran discard twice would not be visible until both takes
        were shot and neither showed a fast-forward.
        """
        name = str(name).strip().lower()
        if name not in REGIMES:
            print("[doom] unknown regime %r — expected one of %s; using %s"
                  % (name, "/".join(REGIMES), DEFAULT_REGIME))
            name = DEFAULT_REGIME
        self.regime = name
        world.max_catchup_steps, world.max_backlog = REGIMES[name]
        # The console line IS the take's receipt: it is the only record of which
        # regime a recording was made under, since the HUD deliberately carries
        # no regime label.
        print("[doom] regime: %s   (max_catchup_steps=%d, max_backlog=%.4fs)"
              % (name, world.max_catchup_steps, world.max_backlog))

    def _restart_take(self, world):
        """Put the whole demo back to its take-zero state and re-arm the timeline.

        Everything a take starts from lives here, so scene load and [R] cannot
        drift apart. The BALL STREAM is the piece that is easy to forget and the
        one that matters most: the balls are the entire visual, and a take begun
        with a previous run's arc still in flight — a stream stretched out by
        whatever dilation that run ended in — cannot be stacked frame-for-frame
        against a take that began from an empty floor. Nothing on screen flags
        it either; it only surfaces in the edit, once both takes are shot.

        This resets everything the SCRIPT owns. It cannot reset the physics
        thread's accumulator, which is why the regime is a load-time param and
        not a key — see the module docstring.

        ONE tick_count read seeds both the clocks and every lane. The physics
        thread advances tick_count concurrently, so reading it twice can straddle
        a tick and leave the stream a few steps ahead of sim_t for the rest of
        the take — small, but it is a per-take offset, which is the one kind of
        error that survives into the comparison.
        """
        ticks = world.tick_count
        self.wall_t    = 0.0
        self.sim_t     = 0.0
        self.tick_rate = 0.0
        self.sim_last  = ticks
        for c in self.cannons:
            c.restart(world, ticks)
        self._start_schedule(world)

    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        inp = yope3d.input
        # Any manual load key drops out of the scheduled take — rehearsing or
        # hunting for a tuning value must not fight the timeline, and a take
        # you touched is not the take you meant to record anyway.
        manual = False
        if inp.is_key_pressed(yope3d.KEY_RIGHT) and self.load_idx < len(WORKLOADS) - 1:
            self._apply_load(world, self.load_idx + 1); manual = True
        if inp.is_key_pressed(yope3d.KEY_LEFT) and self.load_idx > 0:
            self._apply_load(world, self.load_idx - 1); manual = True
        if inp.is_key_pressed(yope3d.KEY_UP):
            world.step_burden_us = world.step_burden_us + 500; manual = True
        if inp.is_key_pressed(yope3d.KEY_DOWN):
            world.step_burden_us = max(0, world.step_burden_us - 500); manual = True
        if manual and self.sched_on:
            self.sched_on = False
            print("[doom] manual override — schedule off, [R] to re-arm")
        # No regime key, by design. It used to be [T] and it was a trap: see the
        # module docstring — the physics thread's accumulator survives the swap,
        # so the second regime never started from the same state as the first.
        if inp.is_key_pressed(yope3d.KEY_R):
            self._restart_take(world)

        self._advance_schedule(world, dt)

        # Drive the ball stream by the physics ticks that actually elapsed:
        # under dilation fewer ticks land per render frame, so the balls slow.
        ticks = world.tick_count
        nticks = ticks - self.sim_last
        self.sim_last = ticks
        h = 1.0 / HZ
        for c in self.cannons:
            c.update(world, ticks)

        # Wall time advances by real dt; sim time advances only by the ticks
        # actually taken. Their gap IS the dilation debt: seconds behind reality.
        self.wall_t += dt
        self.sim_t  += nticks * h
        if dt > 0.0:
            a = 0.06
            self.tick_rate = (1.0 - a) * self.tick_rate + a * (nticks / dt)
            self.fps_ema   = (1.0 - a) * self.fps_ema + a * (1.0 / dt)

        burden = world.step_burden_us
        # A RATE MULTIPLE, not a percentage, and deliberately NOT clamped to 1.
        # A recovery burst genuinely runs the sim clock faster than the wall
        # clock; "1.99x realtime" says that plainly where "199%" reads like a
        # broken progress bar, and it puts slowdown and speedup on one scale
        # either side of 1.00x.
        rate   = self.tick_rate / HZ
        behind = max(0.0, self.wall_t - self.sim_t)

        # --------------------------- HUD text --------------------------- #
        # Every field is fixed-width so nothing reflows as the numbers move —
        # a value that shifts sideways mid-take reads as a glitch on video.
        # render and sim sit adjacent on purpose: a flat render rate beside a
        # collapsing sim rate IS the claim. Separated, a viewer cannot tell the
        # sim slowed rather than the whole application stalling.
        yope3d.set_text(self.hud,
                        "load %+6d us/step   |   render %3.0f fps   |   "
                        "sim %5.2fx realtime" % (burden, self.fps_ema, rate))
        yope3d.set_text(self.hud_behind, "behind realtime   %5.1f s" % behind)
