"""
logo_playback.py — replays the baked Part-2 logo lines (from tools/logo_export.py)
in the engine, for a first end-to-end look at the exporter output.

It loads assets/logo/logo_part2.json (per-frame 2D cam-space segments), parks a
fixed camera in front of a z=0 plane sized to the baked aspect, and each frame
maps that frame's segments onto the plane via yope3d.draw_line. A plane
perpendicular to the view projects LINEARLY under perspective, so no ortho
override is needed — the logo renders undistorted, just uniformly scaled.

Attach via assets/scenes/logo_test.json (ScriptComponent → PythonScript →
{"module": "behaviors.logo_playback", "class": "LogoPlayback"}).
"""

import os
import json
import math
import yope3d

VFOV_DEG = 50.0   # engine vertical FOV used to frame the plane


class LogoPlayback:
    def init(self, world, entity, params):
        # Which baked clip to replay. Precedence: script param `logo_json` >
        # env YOPE_LOGO_JSON > default part2. Lets you preview logo_part1.json
        # (or any experiment) without editing a scene asset:
        #   YOPE_LOGO_JSON=logo_part1.json ./build/mac-debug/yope3d --scene scenes/logo_test.json
        name = "logo_part2.json"
        try:
            if isinstance(params, dict) and params.get("logo_json"):
                name = params["logo_json"]
        except Exception:
            pass
        name = os.environ.get("YOPE_LOGO_JSON", name)
        path = (name if os.path.isabs(name) else os.path.abspath(os.path.join(
            os.path.dirname(__file__), "..", "..", "assets", "logo", name)))
        try:
            with open(path) as fp:
                self.data = json.load(fp)
        except Exception as e:
            print("LogoPlayback: failed to load %s: %s" % (path, e))
            self.data = {"frames": [], "fps": 60.0, "ref_aspect": 16.0 / 9.0}

        self.frames     = self.data.get("frames", [])
        self.fps        = float(self.data.get("fps", 60.0)) or 60.0
        self.ref_aspect = float(self.data.get("ref_aspect", 16.0 / 9.0))
        self.t0         = yope3d.time()

        # Plane: height 1.0, width = ref_aspect, centered on origin at z=0.
        self.halfH = 0.5
        self.halfW = 0.5 * self.ref_aspect

        # Freeze physics — we are only replaying baked lines this pass.
        try:
            world.set_paused(True)
        except Exception:
            pass

        self._place_camera()
        print("LogoPlayback: %d frames @ %.3g fps, ref_aspect=%.3f"
              % (len(self.frames), self.fps, self.ref_aspect))

    def _place_camera(self):
        w = yope3d.window.get_width()
        h = yope3d.window.get_height()
        screen_aspect = (w / h) if h > 0 else self.ref_aspect

        vfov = math.radians(VFOV_DEG)
        tvh  = math.tan(vfov * 0.5)
        # distance so the plane fits vertically...
        cam_z_h = self.halfH / tvh
        # ...and horizontally (hfov derives from vfov * screen aspect)
        thh = screen_aspect * tvh
        cam_z_w = self.halfW / thh
        cam_z = max(cam_z_h, cam_z_w) * 1.05   # 5% margin

        # NOTE: camera.set_fov binds to Camera::setFOV, which takes RADIANS
        # (the .pyi's "degrees" doc is wrong). Passing degrees here sets a ~50
        # rad FOV, whose negative tan mirrors X *and* Y and zooms wildly.
        yope3d.camera.set_fov(vfov)
        yope3d.camera.set_position(yope3d.Vec3(0.0, 0.0, cam_z))
        yope3d.camera.look_at(yope3d.Vec3(0.0, 0.0, 0.0))

    def update(self, world, entity, dt):
        n = len(self.frames)
        if n == 0:
            return
        t   = yope3d.time() - self.t0
        idx = int(t * self.fps) % n          # loop the whole clip
        white = yope3d.Vec3(1.0, 1.0, 1.0)

        aw, ah = self.halfW, self.halfH
        for s in self.frames[idx]:
            ax = (s[0] - 0.5) * 2.0 * aw
            ay = (s[1] - 0.5) * 2.0 * ah
            bx = (s[2] - 0.5) * 2.0 * aw
            by = (s[3] - 0.5) * 2.0 * ah
            yope3d.draw_line(yope3d.Vec3(ax, ay, 0.0),
                             yope3d.Vec3(bx, by, 0.0), white)
