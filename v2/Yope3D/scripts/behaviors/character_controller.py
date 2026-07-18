"""
Kinematic capsule character controller.
Attach as a ScriptComponent on any entity that has Transform + CapsuleForm.

paramsBlob keys (all optional):
  move_speed    float  5.0   — base movement speed (m/s)
  sprint_mult   float  2.0   — speed multiplier while Left Shift held
  jump_vel      float  7.0   — initial vertical velocity on jump
  mouse_sens    float  0.002 — mouse look sensitivity
  step_height   float  0.35  — max step-up height for automatic climbing
  max_slope_deg float  45.0  — steeper slopes slide the player off
  slide_mult    float  3.0   — multiplier on the gravity component along a steep slope;
                               raise for faster sliding (1.0 = pure gravity projection)
  eye_height    float  0.8   — FPS camera offset above capsule top
  cam_distance  float  4.0   — TPS orbit distance
  cam_mode      str   "first"— "first" or "third"; V key toggles at runtime

Controls: WASD move, Left Shift sprint, Space jump, V toggle camera mode.
"""
import yope3d, math

GRAVITY = -20.0

class CharacterController:
    PARAMS = {
        "move_speed":    {"type": "float", "default": 5.0,     "label": "Move Speed (m/s)"},
        "sprint_mult":   {"type": "float", "default": 2.0,     "label": "Sprint Multiplier"},
        "jump_vel":      {"type": "float", "default": 7.0,     "label": "Jump Velocity"},
        "mouse_sens":    {"type": "float", "default": 0.002,   "label": "Mouse Sensitivity"},
        "step_height":   {"type": "float", "default": 0.35,    "label": "Step Height"},
        "max_slope_deg": {"type": "float", "default": 45.0,    "label": "Max Slope (deg)"},
        "slide_mult":    {"type": "float", "default": 3.0,     "label": "Slide Multiplier"},
        "eye_height":    {"type": "float", "default": 0.8,     "label": "Eye Height"},
        "cam_distance":  {"type": "float", "default": 4.0,     "label": "Camera Distance"},
        "cam_mode":      {"type": "enum",  "default": "first",
                          "options": ["first", "third"],        "label": "Camera Mode"},
    }
    def init(self, world, entity, params):
        self.yaw        = 0.0
        self.pitch      = 0.0
        self.y_vel      = 0.0
        self.grounded   = False
        self.prev_space = False

        self.move_speed  = params.get("move_speed",    5.0)
        self.sprint_mult = params.get("sprint_mult",   2.0)
        self.jump_vel    = params.get("jump_vel",      7.0)
        self.sens        = params.get("mouse_sens",    0.002)
        self.step_height = params.get("step_height",   0.35)
        self.max_slope   = math.radians(params.get("max_slope_deg", 45.0))
        self.slide_mult  = params.get("slide_mult",    3.0)
        self.eye_height  = params.get("eye_height",    0.8)
        self.cam_dist    = params.get("cam_distance",  4.0)
        self.cam_mode    = params.get("cam_mode",      "first")

        cf = yope3d.reg_get(entity, "CapsuleForm")
        self.r  = cf.radius      if cf else 0.4
        self.hh = cf.half_height if cf else 0.9

    def update(self, world, entity, dt):
        inp = yope3d.input
        tf  = yope3d.reg_get(entity, "Transform")
        if tf is None:
            return

        # Mouse look
        dx, dy = inp.get_mouse_delta()
        self.yaw   -= dx * self.sens
        self.pitch  = max(-1.4, min(1.4, self.pitch - dy * self.sens))

        # Yaw-relative movement vectors (no pitch in horizontal movement)
        cy, sy = math.cos(self.yaw), math.sin(self.yaw)
        fwd   = yope3d.Vec3(-sy, 0.0, -cy)
        right = yope3d.Vec3( cy, 0.0, -sy)

        speed = self.move_speed * (self.sprint_mult if inp.is_key_down(yope3d.KEY_LEFT_SHIFT) else 1.0)
        move  = yope3d.Vec3(0.0, 0.0, 0.0)
        if inp.is_key_down(yope3d.KEY_W): move = move + fwd
        if inp.is_key_down(yope3d.KEY_S): move = move - fwd
        if inp.is_key_down(yope3d.KEY_D): move = move + right
        if inp.is_key_down(yope3d.KEY_A): move = move - right
        ml = move.length()
        if ml > 1e-4:
            move = move * (speed / ml)

        # Jump
        space = inp.is_key_down(yope3d.KEY_SPACE)
        if space and not self.prev_space and self.grounded:
            self.y_vel    = self.jump_vel
            self.grounded = False
        self.prev_space = space

        if not self.grounded:
            self.y_vel += GRAVITY * dt

        pos = tf.position

        # --- Horizontal move + overlap resolution ---
        nx = yope3d.Vec3(pos.x + move.x * dt, pos.y, pos.z + move.z * dt)
        nx = self._resolve(nx, entity)

        # Step climb: if a horizontal contact is blocking, try stepping up
        if self._h_blocked(nx, entity):
            elev = yope3d.Vec3(nx.x, nx.y + self.step_height, nx.z)
            elev = self._resolve(elev, entity)
            t, hit, _ = yope3d.capsule_cast(
                elev, self.r, self.hh, yope3d.Vec3(0.0, -1.0, 0.0),
                self.step_height + 0.1, entity)
            if hit:
                nx = yope3d.Vec3(elev.x, elev.y - t, elev.z)
            else:
                nx = yope3d.Vec3(pos.x, pos.y, pos.z)  # blocked, no valid step

        # --- Vertical move + overlap resolution ---
        nx = yope3d.Vec3(nx.x, nx.y + self.y_vel * dt, nx.z)
        nx = self._resolve(nx, entity)

        # --- Ground probe (small downward cast from bottom sphere center) ---
        probe = self.r * 0.2 + 0.02
        t, hit, gn = yope3d.capsule_cast(
            nx, self.r, self.hh, yope3d.Vec3(0.0, -1.0, 0.0), probe, entity)

        if hit and self.y_vel <= 0.01:
            if gn.y >= math.cos(self.max_slope):
                # Walkable surface: snap down and zero vertical velocity
                nx = yope3d.Vec3(nx.x, nx.y - t, nx.z)
                self.y_vel    = 0.0
                self.grounded = True
            else:
                # Steep slope: project gravity onto the slope surface and apply
                # it as a lateral force so the player slides downhill.
                self.grounded = False
                g_dot_n = GRAVITY * gn.y  # (0,G,0) · normal
                slide_x = (-g_dot_n * gn.x) * self.slide_mult * dt
                slide_z = (-g_dot_n * gn.z) * self.slide_mult * dt
                nx = yope3d.Vec3(nx.x + slide_x, nx.y, nx.z + slide_z)
        else:
            self.grounded = False

        tf.position = nx

        # Camera mode toggle
        if inp.is_key_pressed(yope3d.KEY_V):
            self.cam_mode = "third" if self.cam_mode == "first" else "first"

        self._update_camera(nx)

    def _resolve(self, pos, exclude):
        for _ in range(3):
            contacts = yope3d.capsule_overlap(pos, self.r, self.hh, exclude)
            if not contacts:
                break
            for (n, d) in contacts:
                pos = yope3d.Vec3(pos.x + n.x * d, pos.y + n.y * d, pos.z + n.z * d)
        return pos

    def _h_blocked(self, pos, exclude):
        for (n, _) in yope3d.capsule_overlap(pos, self.r, self.hh, exclude):
            if abs(n.y) < 0.7:
                return True
        return False

    def _update_camera(self, pos):
        eye_y = pos.y + self.hh + self.eye_height
        if self.cam_mode == "first":
            yope3d.camera.set_position(yope3d.Vec3(pos.x, eye_y, pos.z))
            yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))
        else:
            cy, sy = math.cos(self.yaw), math.sin(self.yaw)
            cp     = math.cos(self.pitch)
            sp     = math.sin(self.pitch)
            fwd    = yope3d.Vec3(-sy * cp, sp, -cy * cp)
            d      = self.cam_dist
            yope3d.camera.set_position(yope3d.Vec3(
                pos.x - fwd.x * d,
                eye_y - fwd.y * d,
                pos.z - fwd.z * d))
            yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))
