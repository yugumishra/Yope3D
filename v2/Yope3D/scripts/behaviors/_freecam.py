"""
Standard free-fly camera for test / smoketest scenes.

Extracted from the controller that mouse_drag_ragdoll.py and stress_test.py each
carried a private copy of, so a scene that just needs "let me fly around and look
at the thing" can add one host entity instead of duplicating it a third time.

Controls: mouselook (cursor is captured at startup — see Window::pause/unpause),
WASD to move, Space / Left-Ctrl for up / down, Shift to sprint.

paramsBlob keys (all optional):
  fly_speed     float 8.0    — movement speed (m/s)
  mouse_sens    float 0.002  — look sensitivity
  sprint_mult   float 3.0    — Shift multiplier
  start_pos     [x, y, z]    — initial camera position
  start_yaw     float 0.0    — initial yaw (radians)
  start_pitch   float 0.0    — initial pitch (radians)

Usage in a .yscene:

    {
      "fileId": 0, "runtimeId": 0,
      "Name": { "value": "FreeCam" },
      "ScriptComponent": {
        "scriptClass": "PythonScript",
        "paramsBlob": "{\\"module\\": \\"behaviors._freecam\\", \\"class\\": \\"FreeCam\\"}"
      }
    }
"""
import math
import yope3d


class FreeCam:
    PARAMS = {
        "fly_speed":   {"type": "float", "default": 8.0,   "label": "Fly Speed (m/s)"},
        "mouse_sens":  {"type": "float", "default": 0.002, "label": "Mouse Sensitivity"},
        "sprint_mult": {"type": "float", "default": 3.0,   "label": "Sprint Multiplier"},
    }

    def init(self, world, entity, params):
        # PARAMS defaults are editor metadata only and are NOT injected into
        # `params`, so the fallbacks here are the effective values unless the
        # scene's paramsBlob sets them. Keep the two in sync.
        self.speed  = params.get("fly_speed", 8.0)
        self.sens   = params.get("mouse_sens", 0.002)
        self.sprint = params.get("sprint_mult", 3.0)

        self.yaw   = params.get("start_yaw", 0.0)
        self.pitch = params.get("start_pitch", 0.0)

        sp = params.get("start_pos")
        if isinstance(sp, (list, tuple)) and len(sp) == 3:
            yope3d.camera.set_position(yope3d.Vec3(float(sp[0]), float(sp[1]), float(sp[2])))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

    def update(self, world, entity, dt):
        inp = yope3d.input

        dx, dy = inp.get_mouse_delta()
        self.yaw  -= dx * self.sens
        # Clamp just short of straight up/down; at exactly +-pi/2 the forward
        # vector degenerates and the view snaps.
        self.pitch = max(-1.5, min(1.5, self.pitch - dy * self.sens))
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0.0))

        fwd   = yope3d.camera.get_forward()
        # Right is derived from yaw alone, so strafing stays horizontal while
        # looking up or down.
        right = yope3d.Vec3(math.cos(self.yaw), 0.0, -math.sin(self.yaw))

        move = yope3d.Vec3(0.0, 0.0, 0.0)
        if inp.is_key_down(yope3d.KEY_W): move = move + fwd
        if inp.is_key_down(yope3d.KEY_S): move = move - fwd
        if inp.is_key_down(yope3d.KEY_D): move = move + right
        if inp.is_key_down(yope3d.KEY_A): move = move - right
        if inp.is_key_down(yope3d.KEY_SPACE):        move = move + yope3d.Vec3(0, 1, 0)
        if inp.is_key_down(yope3d.KEY_LEFT_CONTROL): move = move - yope3d.Vec3(0, 1, 0)

        speed = self.speed
        if inp.is_key_down(yope3d.KEY_LEFT_SHIFT):
            speed *= self.sprint

        p = yope3d.camera.position
        yope3d.camera.set_position(yope3d.Vec3(p.x + move.x * speed * dt,
                                               p.y + move.y * speed * dt,
                                               p.z + move.z * speed * dt))
