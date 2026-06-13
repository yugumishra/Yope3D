import yope3d
from yope3d import audio
class Test:
    PARAMS={
        "frame_mod":    {"type": "int", "default": 480,     "label": "Frames between errors in update()"},
        "up_speed": {"type": "float", "default": 1.0, "label": "how fast it goes up (meters / second)"},
        "up": {"type": "bool", "default": True, "label": "whether to go up or right"},
        "message": {"type": "str", "default": "regular workflow", "label": "what to print in the update() loop"},
    }

    def init(self, world, entity, params):
        print("hello? only once though")
        tf = yope3d.reg_get(entity, "Transform")
        print("hello multiple times")
        pos = tf.position
        up = yope3d.Vec3(0,0.1,0)
        tf.position = pos + up

        self.frameCounter = 1
        self.frameMod = params.get("frame_mod", 480)
        self.upSpeed = params.get("up_speed", 1.0)
        self.modCount = 1

        self.up = params.get("up", True)

        self.message = params.get("message", "regular workflow")

        
        spheres = yope3d.view("Transform", "SphereForm")
        for i in range(1, len(spheres)):
            if spheres[i][1].position.x < spheres[i+1][1].position.x:
                world.add_spring_with_proxies(spheres[i][0], spheres[i+1][0], 100, 1.5, 1.0, 0.25)
    
    def update(self, world, entity, dt):
        inp = yope3d.input
        tf = yope3d.reg_get(entity, "Transform")
        if tf is None:
            
            return

        self.frameCounter = self.frameCounter + 1
        """
        if self.frameCounter % self.frameMod == 0:
            x = 0
            y = 1
            z = y / x
            print("hello, but error")

            
        else:
            print(self.message)"""
        if self.frameCounter % self.frameMod == 0:
            
            print(world.get_hull_count())
            e = yope3d.world.add_sphere(mass=1.0, radius=0.5, pos=yope3d.Vec3(0,5,0))
            yope3d.world.attach_sphere_mesh(e, 0.5, 0.85, 0.5, 0.2)
            hull = yope3d.reg_get(e, "Hull")
            hull.velocity += yope3d.Vec3(1,0,0)

            self.modCount = self.modCount + 1

            if self.modCount % 2 == 0:
                audio.pause_all()
            else:
                audio.resume_all()

            if self.modCount % 5 == 0:
                yope3d.scene_manager.load_scene("assets/scenes/startup.json")

        

        
        pos = tf.position
        up = yope3d.Vec3(0 if self.up else self.upSpeed * dt,(self.upSpeed * dt) if (self.up) else (0),0)
        tf.position = pos + up

    def on_unload(self, world, entity):
        print("yay")