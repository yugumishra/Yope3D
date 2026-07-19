"""
Article 2 demo — the loop of doom (fixed-timestep overload).

Same stage as the smoothness demo: brick floor and cannon streams of
script-driven terra-cotta balls tracing one fixed trajectory each, integrated
one step per elapsed physics tick. Because the balls advance only when the
physics thread actually ticks, they double as a visual tachometer for the
fixed-timestep loop itself.

Here the tick rate stays pinned at 240 Hz while an artificial per-step cost
(world.step_burden_us, a spin-wait in the physics thread) ramps past the
per-step budget. With the accumulator clamp ON the engine degrades into
bounded time dilation — the streams run in slow motion and the backlog stays
pinned. Turning the clamp OFF restores the classic spiral of death: time
spent stepping re-enters the accumulator in full, the backlog compounds, and
the balls freeze mid-arc while the HUD counter runs away. The render thread
keeps drawing the whole time — the spiral eats the physics thread, not the
framerate.

Auto phase timeline (loops):
  HEALTHY -> RISING LOAD -> OVER BUDGET (clamped: dilation)
          -> CLAMP OFF (spiral of death) -> RECOVERY

Controls: [A] auto timeline, [C] toggle clamp, [UP]/[DOWN] burden +-500 us,
[R] reset (burden 0, clamp on).
"""
import yope3d

from behaviors.article2_smoothness import ARENA, Cannon, build_brick_floor

FONT  = "fonts/monaco.ttf"
HZ    = 240.0
LANES = [-5.0, 0.0, 5.0]

# (label, duration s, burden us, clamp)
PHASES = [
    ("HEALTHY",                     6.0,     0, True),
    ("RISING LOAD",                 6.0,  2500, True),
    ("OVER BUDGET — TIME DILATION", 8.0,  6000, True),
    ("CLAMP OFF — SPIRAL OF DEATH", 10.0, 8000, False),
    ("RECOVERY",                    6.0,     0, True),
]


class LoopOfDoom:
    PARAMS = {
        "n_balls": {"type": "int", "default": 270, "label": "Total Pool (split across lanes)"},
        "auto":    {"type": "bool", "default": True, "label": "Auto Timeline"},
    }

    def init(self, world, entity, params):
        yope3d.set_profile_scene("Article2 LoopOfDoom")
        build_brick_floor(world, ARENA)

        per_lane = max(1, int(params.get("n_balls", 270)) // len(LANES))
        self.cannons = [Cannon(world, z, per_lane) for z in LANES]

        yope3d.camera.set_position(yope3d.Vec3(0.0, 5.5, 21.0))
        yope3d.camera.set_rotation(yope3d.Vec3(-0.14, 0.0, 0.0))

        # ------------------------------ HUD ------------------------------ #
        self.hud_phase = world.add_ui_text(FONT, "", yope3d.Vec2(0.18, 0.03),
                                           yope3d.Vec2(0.82, 0.10), 2)
        self.hud_a = world.add_ui_text(FONT, "", yope3d.Vec2(0.16, 0.11),
                                       yope3d.Vec2(0.84, 0.15), 2)
        self.hud_b = world.add_ui_text(FONT, "", yope3d.Vec2(0.16, 0.16),
                                       yope3d.Vec2(0.84, 0.20), 2)
        self.hud_c = world.add_ui_text(FONT, "", yope3d.Vec2(0.16, 0.21),
                                       yope3d.Vec2(0.84, 0.25), 2)

        world.physics_hz = HZ
        self.auto      = bool(params.get("auto", True))
        self.phase_idx = 0
        self.phase_t   = 0.0
        self.wall_t    = 0.0
        self.fps_ema   = 0.0
        self.tick_rate = 0.0
        self.sim_last  = world.tick_count
        self.last_tick = world.tick_count
        self.tick0     = world.tick_count
        self._enter_phase(world, 0)

    def _enter_phase(self, world, idx):
        self.phase_idx = idx
        self.phase_t = 0.0
        _, _, burden, clamp = PHASES[idx]
        world.step_burden_us = burden
        world.accumulator_clamp = clamp

    # ------------------------------------------------------------------ #

    def update(self, world, entity, dt):
        inp = yope3d.input
        if inp.is_key_pressed(yope3d.KEY_A):
            self.auto = True
            self._enter_phase(world, 0)
        if inp.is_key_pressed(yope3d.KEY_C):
            self.auto = False
            world.accumulator_clamp = not world.accumulator_clamp
        if inp.is_key_pressed(yope3d.KEY_UP):
            self.auto = False
            world.step_burden_us = world.step_burden_us + 500
        if inp.is_key_pressed(yope3d.KEY_DOWN):
            self.auto = False
            world.step_burden_us = max(0, world.step_burden_us - 500)
        if inp.is_key_pressed(yope3d.KEY_R):
            self.auto = False
            world.step_burden_us = 0
            world.accumulator_clamp = True

        self.phase_t += dt
        if self.auto and self.phase_t >= PHASES[self.phase_idx][1]:
            self._enter_phase(world, (self.phase_idx + 1) % len(PHASES))

        # Drive the streams by the physics ticks that actually elapsed —
        # during the spiral this is what freezes them mid-arc.
        ticks = world.tick_count
        nticks = ticks - self.sim_last
        self.sim_last = ticks
        h = 1.0 / HZ
        for c in self.cannons:
            c.update(world, dt, nticks, h)

        self.wall_t += dt
        if dt > 0.0:
            a = 0.06
            self.fps_ema = (1.0 - a) * self.fps_ema + a * (1.0 / dt)
            inst = (ticks - self.last_tick) / dt
            self.last_tick = ticks
            self.tick_rate = (1.0 - a) * self.tick_rate + a * inst

        budget_us = 1e6 / HZ
        burden    = world.step_burden_us
        backlog   = world.accumulator_backlog
        sim_t     = (world.tick_count - self.tick0) / HZ
        clamp     = world.accumulator_clamp

        # --------------------------- HUD text --------------------------- #
        label = PHASES[self.phase_idx][0] if self.auto else "MANUAL"
        yope3d.set_text(self.hud_phase, label)
        yope3d.set_text(self.hud_a,
                        "step budget %4.0f us   |   step cost +%d us   |   clamp %s" %
                        (budget_us, burden, "ON" if clamp else "OFF"))
        yope3d.set_text(self.hud_b,
                        "target %d Hz   |   measured %3.0f Hz   |   sim at %3.0f%% of real time" %
                        (int(HZ), self.tick_rate,
                         min(100.0, 100.0 * self.tick_rate / HZ)))
        yope3d.set_text(self.hud_c,
                        "backlog %6.2f s   |   sim %6.1f s   wall %6.1f s   |   render %3.0f fps" %
                        (backlog, sim_t, self.wall_t, self.fps_ema))
