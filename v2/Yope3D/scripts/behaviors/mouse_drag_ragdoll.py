"""
Mouse-drag grab tool for physics bodies (ragdolls in particular).

Always-on FPS-style fly camera (WASD + mouselook, cursor captured — same
controller as stress_test.py) plus a crosshair grab: hold LEFT mouse to pull
whatever's dead-center under the crosshair toward the camera at a fixed
distance, release to let go. Look around while holding LEFT to swing the
grabbed body around.

The grab is a real PointToPointJoint (see physics/Joint.h), not a kinematic
teleport of the grabbed body: an invisible, massless anchor entity ("handle")
is moved to the crosshair's world-space target each frame, and a joint pulls
the grabbed body's clicked point toward it through the normal island-parallel
PGS solve. That means dragging one ragdoll limb pulls the whole connected
chain along (joint limits, gravity, and collisions on everything else still
apply) rather than just relocating one body in isolation.

Deliberately avoids cursor-position-based picking (screen_to_ray + an
unlocked cursor): this engine starts with the cursor captured (see
Window::pause/unpause), and captured-cursor position is an unbounded virtual
value not meaningful for screen_to_ray. Using the crosshair (camera position +
forward) instead sidesteps that entirely and matches the proven pattern
already used elsewhere (see the "Crosshair raycast" example in
typings/yope3d/__init__.pyi).

A crosshair (two small HUD bars forming a "+", dead center of the screen)
shows aim state at a glance: gray/idle when pointed at nothing grabbable,
yellow when hovering something grabbable, green while actively holding it.

The drag target's own movement is slew-rate-limited (see `max_handle_speed`)
rather than teleported straight to the crosshair every frame — physics ticks
at a fixed 240Hz on its own thread while this script only runs once per
render frame, so an unbounded jump would hand the joint solver a large gap to
close in only a few substeps, visibly "exploding" the ragdoll apart on fast
swings before it catches back up a few substeps later. Capping the target's
speed keeps that gap small enough for the existing per-substep iteration
budget to track smoothly instead of lagging behind.

paramsBlob keys (all optional):
  fly_speed        float 8.0    — camera speed (m/s)
  mouse_sens       float 0.002  — mouse look sensitivity
  grab_dist        float 5.0    — how far in front of the camera to hold a grabbed body
  max_handle_speed float 20.0   — cap on how fast the drag target may move (m/s)
"""
import math
import yope3d

_CROSSHAIR_IDLE    = yope3d.Vec4(0.85, 0.85, 0.85, 0.85)
_CROSSHAIR_HOVER   = yope3d.Vec4(1.0,  0.85, 0.2,  0.95)
_CROSSHAIR_GRABBED = yope3d.Vec4(0.25, 1.0,  0.35, 0.95)


class MouseDragRagdoll:
    PARAMS = {
        "fly_speed":        {"type": "float", "default": 8.0,   "label": "Fly Speed (m/s)"},
        "mouse_sens":       {"type": "float", "default": 0.002, "label": "Mouse Sensitivity"},
        "max_handle_speed": {"type": "float", "default": 20.0, "label": "Max Drag Speed (m/s)"},
    }

    def init(self, world, entity, params):
        self.speed = params.get("fly_speed", 8.0)
        self.sens  = params.get("mouse_sens", 0.002)
        # NOTE: the effective cap is this fallback unless the scene's paramsBlob
        # actually carries "max_handle_speed" — the PARAMS[...]["default"] above
        # is only editor metadata and is NOT injected into `params`. Keep the two
        # in sync so tweaking one isn't silently a no-op (the old 2.0/20.0 split
        # was exactly that trap).
        self._max_handle_speed = params.get("max_handle_speed", 20.0)
        self.pitch, self.yaw = 0.0, 0.0

        # Invisible anchor the grab joint pulls toward. A tiny fixed sphere is
        # the cheapest way to get a Hull+Transform pair through the existing
        # bindings; no mesh is attached so nothing is drawn, and 1cm radius
        # makes any stray narrowphase contact against it inconsequential.
        self._handle = world.add_sphere(1.0, 0.01, yope3d.Vec3(0.0, -9999.0, 0.0))
        world.fix_entity(self._handle)

        self._grabbed = None    # entity currently being dragged, or None
        self._grab_dist = 5.0   # distance from camera kept constant while dragging

        self._crosshair_bars = self._make_crosshair(world)

    def update(self, world, entity, dt):
        inp = yope3d.input
        self._fly_camera(inp, dt)
        self._drag(world, inp, dt)

    # ------------------------------------------------------------------ #
    # Crosshair HUD
    # ------------------------------------------------------------------ #

    def _make_crosshair(self, world):
        # min/max are placeholders — anchor=Center + size_mode=Pixel below
        # overrides them with a fixed on-screen size regardless of window
        # aspect (see UITransform's docs on why Fraction sizing distorts).
        bars = []
        for pw, ph in ((14.0, 2.0), (2.0, 14.0)):   # horizontal bar, vertical bar
            e = world.add_ui_background(yope3d.Vec2(0, 0), yope3d.Vec2(0, 0),
                                        _CROSSHAIR_IDLE, depth=100)
            tf = yope3d.reg_get(e, "UITransform")
            tf.anchor = 5          # Center
            tf.size_mode = 1       # Pixel
            tf.pixel_width = pw
            tf.pixel_height = ph
            bars.append(e)
        return bars

    def _set_crosshair_color(self, world, color):
        for e in self._crosshair_bars:
            bg = yope3d.reg_get(e, "UIBackground")
            bg.r, bg.g, bg.b, bg.a = color.x, color.y, color.z, color.w

    # ------------------------------------------------------------------ #
    # Always-on free-fly camera (identical controller to stress_test.py)
    # ------------------------------------------------------------------ #

    def _fly_camera(self, inp, dt):
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

        p = yope3d.camera.position
        yope3d.camera.set_position(yope3d.Vec3(p.x + move.x * self.speed * dt,
                                               p.y + move.y * self.speed * dt,
                                               p.z + move.z * self.speed * dt))

    # ------------------------------------------------------------------ #
    # Crosshair grab / drag
    # ------------------------------------------------------------------ #

    def _drag(self, world, inp, dt):
        origin    = yope3d.camera.position
        direction = yope3d.camera.get_forward()

        if self._grabbed is not None:
            if not inp.is_lmb_down():
                world.remove_joint_between(self._grabbed, self._handle)
                # Stop kinematically driving the handle — otherwise its last
                # commanded velocity lingers on the Hull and would keep being
                # read by any future grab's velocity row before the first
                # update overwrites it.
                yope3d.reg_get(self._handle, "Hull").velocity = yope3d.Vec3(0, 0, 0)
                self._grabbed = None
            else:
                target = origin + direction * self._grab_dist
                # Slew-rate-limit the handle instead of teleporting it to
                # `target` every frame: scripts run once per render frame but
                # physics ticks at a fixed 240Hz on its own thread, so a fast
                # mouse swing would otherwise hand the joint solver a huge gap
                # to close in only a handful of substeps. Capping how far the
                # target moves per frame keeps that gap bounded.
                htf = yope3d.reg_get(self._handle, "Transform")
                delta = target - htf.position
                dist = delta.length()
                max_step = self._max_handle_speed * dt
                if dist > max_step and dist > 1e-6:
                    delta = delta * (max_step / dist)
                htf.position = htf.position + delta

                # Drive the handle by VELOCITY, not just position. The handle is
                # a Fixed body, so on its own the grab P2P joint's velocity row
                # sees v_handle == 0 and spends all 24 velocity iterations
                # *zeroing* the grabbed body's velocity — the body then only
                # tracks the handle through the 5-iteration split-impulse
                # position pass, which propagates ~5 joints/substep and carries
                # no momentum. That's what tears the ragdoll apart on fast
                # swings: the grabbed cluster gets position-projected forward
                # while distal segments lag out of their cone/twist limits.
                #
                # applyImpulses() never writes back to a Fixed body, but the
                # solver still READS hull.velocity in the relative-velocity term
                # (ColliderDiscrete.cpp solveVelocityPointToPoint) — the classic
                # kinematic-body trick. Setting it to the commanded drag velocity
                # makes the velocity row impart real, momentum-carrying motion
                # that propagates down the whole chain, so it stays coherent and
                # the limits hold. Split impulse then only mops up residual.
                hull = yope3d.reg_get(self._handle, "Hull")
                hull.velocity = delta * (1.0 / dt) if dt > 1e-6 else yope3d.Vec3(0, 0, 0)
                self._set_crosshair_color(world, _CROSSHAIR_GRABBED)
                return

        # Not currently holding anything — raycast every frame (not just on
        # click) purely to drive the hover indicator, so the crosshair tells
        # you what you're about to grab before you commit to clicking.
        hit, e, point, normal, t = yope3d.raycast(origin, direction, 100.0, self._handle)
        grabbable = hit and yope3d.reg_has(e, "Hull")
        self._set_crosshair_color(world, _CROSSHAIR_HOVER if grabbable else _CROSSHAIR_IDLE)

        if grabbable and inp.is_mouse_pressed(yope3d.MOUSE_LEFT):
            world.wake(e)
            self._grabbed = e
            self._grab_dist = t
            yope3d.reg_get(self._handle, "Transform").position = point
            # Anchor at the exact point hit (not the body's center) — grabbing
            # a limb off-center pulls and torques it realistically.
            world.add_point_joint(e, self._handle, point)
