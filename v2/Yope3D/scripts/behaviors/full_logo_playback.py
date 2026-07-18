"""
logo_playback.py — replays Part-1 followed by Part-2 sequentially (from 
tools/logo_export.py) in a continuous loop.
"""

import os
import json
import math
import yope3d

VFOV_DEG = 50.0   # engine vertical FOV used to frame the plane


class LogoPlayback:
    def init(self, world, entity, params):
        # Base directory for assets
        base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "assets", "logo"))
        
        # Load both parts
        self.part1_data = self._load_logo_json(os.path.join(base_dir, "logo_part1.json"))
        self.part2_data = self._load_logo_json(os.path.join(base_dir, "logo_part2.json"))

        # Calculate durations based on frame counts and FPS metadata
        self.part1_frames = self.part1_data.get("frames", [])
        self.part1_fps    = float(self.part1_data.get("fps", 60.0)) or 60.0
        self.part1_dur    = len(self.part1_frames) / self.part1_fps

        self.part2_frames = self.part2_data.get("frames", [])
        self.part2_fps    = float(self.part2_data.get("fps", 60.0)) or 60.0
        self.part2_dur    = len(self.part2_frames) / self.part2_fps

        # Combined total time for a single full sequence loop
        self.total_duration = self.part1_dur + self.part2_dur

        # Use part2's aspect ratio as baseline reference for the view plane
        self.ref_aspect = float(self.part2_data.get("ref_aspect", 16.0 / 9.0))
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
        print("LogoPlayback: Sequentially looping Part 1 (%.2fs) -> Part 2 (%.2fs)" 
              % (self.part1_dur, self.part2_dur))

    def _load_logo_json(self, path):
        """Helper to safely load a JSON asset file."""
        try:
            with open(path) as fp:
                return json.load(fp)
        except Exception as e:
            print("LogoPlayback: failed to load %s: %s" % (path, e))
            return {"frames": [], "fps": 60.0, "ref_aspect": 16.0 / 9.0}

    def _place_camera(self):
        w = yope3d.window.get_width()
        h = yope3d.window.get_height()
        screen_aspect = (w / h) if h > 0 else self.ref_aspect

        vfov = math.radians(VFOV_DEG)
        tvh  = math.tan(vfov * 0.5)
        cam_z_h = self.halfH / tvh
        thh = screen_aspect * tvh
        cam_z_w = self.halfW / thh
        cam_z = max(cam_z_h, cam_z_w) * 1.05   # 5% margin

        yope3d.camera.set_fov(vfov)
        yope3d.camera.set_position(yope3d.Vec3(0.0, 0.0, cam_z))
        yope3d.camera.look_at(yope3d.Vec3(0.0, 0.0, 0.0))

    def _draw_lines(self, frame_data, color):
        """Helper to draw line segments for a target frame list."""
        aw, ah = self.halfW, self.halfH
        for s in frame_data:
            ax = (s[0] - 0.5) * 2.0 * aw
            ay = (s[1] - 0.5) * 2.0 * ah
            bx = (s[2] - 0.5) * 2.0 * aw
            by = (s[3] - 0.5) * 2.0 * ah
            yope3d.draw_line(yope3d.Vec3(ax, ay, 0.0),
                             yope3d.Vec3(bx, by, 0.0), color)

    def update(self, world, entity, dt):
        if self.total_duration <= 0.0:
            return

        # Get total runtime elapsed, wrapped to the total combined timeline duration
        t = (yope3d.time() - self.t0) % self.total_duration
        white = yope3d.Vec3(1.0, 1.0, 1.0)
        
        # Decide which part to play based on time window
        if t < self.part1_dur:
            # We are in Part 1's window
            if len(self.part1_frames) > 0:
                idx = int(t * self.part1_fps) % len(self.part1_frames)
                self._draw_lines(self.part1_frames[idx], white)
        else:
            # We are in Part 2's window
            if len(self.part2_frames) > 0:
                # Subtract Part 1's time offset to start Part 2 tracking at 0.0s
                part2_local_t = t - self.part1_dur
                idx = int(part2_local_t * self.part2_fps) % len(self.part2_frames)
                self._draw_lines(self.part2_frames[idx], white)