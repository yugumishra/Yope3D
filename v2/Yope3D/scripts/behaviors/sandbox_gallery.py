"""
Sandbox gallery behavior script.
Attach via ScriptComponent (scriptClass=PythonScript) on any entity.
paramsBlob example:
    {"module": "behaviors.sandbox_gallery", "class": "SandboxGallery",
     "scenes": ["scenes/sandbox/pyramid_small.json",
                "scenes/sandbox/pyramid_medium.json",
                "scenes/sandbox/spring_sphere_top.json"]}

Controls:
  LEFT / RIGHT  — switch scene
  SPACE         — spawn a sphere at camera position
"""
import yope

class SandboxGallery:
    def init(self, world, entity, params):
        self.scenes   = params.get("scenes", [])
        self.current  = 0
        self.right_was_down = False
        self.left_was_down  = False
        self.space_was_down = False
        self.spawn_cooldown = 0.0

    def update(self, world, entity, dt):
        inp = yope.input

        right = inp.is_key_down(yope.KEY_RIGHT)
        left  = inp.is_key_down(yope.KEY_LEFT)
        space = inp.is_key_down(yope.KEY_SPACE)

        if right and not self.right_was_down and self.scenes:
            self.current = (self.current + 1) % len(self.scenes)
            yope.load_scene(self.scenes[self.current])

        if left and not self.left_was_down and self.scenes:
            self.current = (self.current - 1) % len(self.scenes)
            yope.load_scene(self.scenes[self.current])

        # Spawn a sphere on space press (rate-limited)
        self.spawn_cooldown = max(0.0, self.spawn_cooldown - dt)
        if space and not self.space_was_down and self.spawn_cooldown <= 0.0:
            pos = yope.camera.get_forward() * 3.0 + yope.camera.position
            e = world.add_sphere(1.0, 0.5, pos)
            world.attach_sphere_mesh(e, 0.5, 0.9, 0.5, 0.2)
            self.spawn_cooldown = 0.15

        self.right_was_down = right
        self.left_was_down  = left
        self.space_was_down = space
