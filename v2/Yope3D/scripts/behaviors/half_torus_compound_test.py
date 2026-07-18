"""
Dynamic compound collider validation: assembles a ring of spheres into a
half-torus (half-donut) and drops it as one dynamic rigid body. The shape is
intentionally asymmetric (half a ring, not a full one) so an incorrect
center-of-mass / inertia computation is visually obvious — unrealistic spin,
or an unstable/wrong-looking rest pose — rather than silently masked by
symmetry the way a sphere or full torus would be.

Attach as a ScriptComponent on any entity in the scene (the host entity is
otherwise unused — all the interesting state lives on the compound body this
script creates in init()). Relies on the existing debugPhysics overlay to
visualize the sphere sub-shapes; no cosmetic mesh is spawned.

paramsBlob keys (all optional):
  spawn_x, spawn_y, spawn_z  float  0, 8, 0   — world spawn position
  ring_radius                float  2.0       — half-torus ring radius (R)
  tube_radius                float  0.35      — sphere radius per ring segment (r)
  segments                   int    16        — spheres along the [0, pi] arc
  density                    float  1.0       — kg/m^3 fed into mass/inertia
"""
import math, yope3d


class HalfTorusCompoundTest:
    PARAMS = {
        "spawn_x":     {"type": "float", "default": 0.0,  "label": "Spawn X"},
        "spawn_y":     {"type": "float", "default": 8.0,  "label": "Spawn Y"},
        "spawn_z":     {"type": "float", "default": 0.0,  "label": "Spawn Z"},
        "ring_radius": {"type": "float", "default": 2.0,  "label": "Ring Radius"},
        "tube_radius": {"type": "float", "default": 0.35, "label": "Tube Radius"},
        "segments":    {"type": "int",   "default": 16,   "label": "Segments"},
        "density":     {"type": "float", "default": 1.0,  "label": "Density"},
    }

    def init(self, world, entity, params):
        spawn = yope3d.Vec3(params.get("spawn_x", 0.0),
                             params.get("spawn_y", 8.0),
                             params.get("spawn_z", 0.0))
        ring_radius = params.get("ring_radius", 2.0)
        tube_radius = params.get("tube_radius", 0.35)
        segments    = max(2, int(params.get("segments", 16)))
        density     = params.get("density", 1.0)

        spheres = []
        for i in range(segments):
            theta = math.pi * i / (segments - 1)
            center = yope3d.Vec3(ring_radius * math.cos(theta), 0.0, ring_radius * math.sin(theta))
            spheres.append((center, tube_radius))

        self.body = world.build_sphere_compound(spheres, density, False, spawn)
