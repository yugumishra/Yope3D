"""Tier-1 scripting-extensions smoketest.

Attach to any entity with a Transform, press Play, and watch the console + viewport:
  - spawns a capsule and a cylinder (mesh + collider attached from script)
  - adds a point light, a HUD score readout, and a 3D world-space label
  - plays a one-shot sound via yope.play_sound
  - update(): mutates the score text, reads the scroll wheel, reports LMB click edges

Exercises: add_capsule/add_cylinder, attach_*_mesh, attach_*_collider, add_point_light,
add_ui_text, add_text_label_3d, set_text, play_sound, is_mouse_pressed, get_scroll_y.
"""
import yope


class Tier1Smoketest:
    PARAMS = {
        "sound": {"type": "str", "default": "audios/test.ogg", "label": "One-shot sound"},
        "font":  {"type": "str", "default": "fonts/monaco.ttf", "label": "HUD font"},
    }

    def init(self, world, entity, params):
        self.score = 0
        self.t = 0.0
        font = params.get("font", "fonts/monaco.ttf")

        # GJK-only primitives: spawn body, then attach mesh + collider from script.
        cap = world.add_capsule(0.4, 0.6, 1.0, yope.Vec3(-2, 4, 0))
        world.attach_capsule_mesh(cap, 0.4, 0.6, 0.3, 0.7, 1.0)

        cyl = world.add_cylinder(0.5, 0.7, 1.0, yope.Vec3(2, 4, 0))
        world.attach_cylinder_mesh(cyl, 0.5, 0.7, 1.0, 0.6, 0.2)

        # A visual-only sphere that we make physical afterward (attach collider path).
        ball = world.add_sphere(1.0, 0.5, yope.Vec3(0, 6, 0))
        world.attach_sphere_mesh(ball, 0.5, 0.9, 0.9, 0.3)

        world.add_point_light(yope.Vec3(0, 6, 3), yope.Vec3(1, 1, 1), 2.0)

        # HUD score + a 3D label anchored in the world.
        self.score_ui = world.add_ui_text(font, "Score: 0",
                                           yope.Vec2(0.02, 0.02), yope.Vec2(0.4, 0.1))
        world.add_text_label_3d(font, "tier1!", yope.Vec3(0, 5, 0))

        snd = yope.play_sound(params.get("sound", "audios/test.ogg"))
        print(f"[tier1] init: capsule={cap.id} cylinder={cyl.id} sound={'ok' if snd else 'none'}")

    def update(self, world, entity, dt):
        self.t += dt
        if self.t >= 1.0:
            self.t = 0.0
            self.score += 1
            yope.set_text(self.score_ui, f"Score: {self.score}")

        if yope.input.is_mouse_pressed(yope.MOUSE_LEFT):
            print("[tier1] LMB click edge")

        sy = yope.input.get_scroll_y()
        if sy != 0.0:
            print(f"[tier1] scroll {sy}")
