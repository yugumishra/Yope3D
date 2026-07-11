"""attach_script demo — give runtime-spawned entities their own live behavior.

Attach AttachScriptDemo to any scripted entity and press Play. Every second it
spawns a falling sphere and immediately calls yope3d.attach_script(...) to give
*that specific entity* a live SpawnedCounter instance — the use case this
unlocks is "spawn N things at runtime, each needs independent behavior state"
(enemy waves, pickups, procedural placement), which previously required
hand-authoring every instance in a scene file.

Watch the console:
  - [attach] spawned <entity> with SpawnedCounter
  - [counter] <entity> tag=<N> update #<k>   (each instance counts independently)
  - [counter] <entity> tag=<N> landed after <k> updates   (on_collision_enter fires
    normally from the very next frame -- no different from a scene-authored script)

Exercises: yope3d.attach_script (module, class_name, params -> init() dict),
independent per-instance state, get_behavior reaching a runtime-attached instance.
"""
import yope3d


class SpawnedCounter:
    """The behavior given to each dynamically spawned entity."""

    def init(self, world, entity, params):
        self.tag = params.get("tag", -1)
        self.count = 0
        print(f"[counter] {entity} tag={self.tag} init")

    def update(self, world, entity, dt):
        self.count += 1

    def on_collision_enter(self, world, entity, other):
        print(f"[counter] {entity} tag={self.tag} landed after {self.count} updates")


class AttachScriptDemo:
    PARAMS = {
        "spawn_interval": {"type": "float", "default": 1.0, "label": "Seconds between spawns"},
    }

    def init(self, world, entity, params):
        self.t = 0.0
        self.interval = params.get("spawn_interval", 1.0)
        self.spawned = 0
        self.pos = yope3d.get_position(entity) or yope3d.Vec3(0, 5, 0)

        floor = world.add_static_aabb(
            yope3d.Vec3(self.pos.x, -0.5, self.pos.z), yope3d.Vec3(4, 0.5, 4))
        world.attach_box_mesh(floor, yope3d.Vec3(4, 0.5, 4), 0.5, 0.5, 0.55)

    def update(self, world, entity, dt):
        self.t += dt
        if self.t < self.interval:
            return
        self.t = 0.0
        self.spawned += 1

        x = self.pos.x + (self.spawned % 3 - 1) * 1.5
        proj = world.add_sphere(1.0, 0.3, yope3d.Vec3(x, self.pos.y, self.pos.z))
        world.attach_sphere_mesh(proj, 0.3, 0.3, 0.6, 1.0)

        ok = yope3d.attach_script(
            proj, "behaviors.attach_script_demo", "SpawnedCounter",
            {"tag": self.spawned})
        print(f"[attach] spawned {proj} with SpawnedCounter -> {ok}")
