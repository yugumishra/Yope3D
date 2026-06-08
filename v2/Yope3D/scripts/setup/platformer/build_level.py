"""
Platformer level setup script.
Run in the Scene Script panel -> Save Scene -> load 'scenes/platformer.json'.
"""
import yope, math

PLAT_HALF_XZ = 1.0
PLAT_HALF_Y  = 0.5
PLATFORM_COUNT = 51
INITIAL_RADIUS = 15.0
ANGLE_STEP     = 0.3
HEIGHT_STEP    = 2.0
RADIUS_GROW    = 0.5

def build_level(world):
    # Floor
    world.add_static_aabb(yope.Vec3(0, 0, 0), yope.Vec3(30, 0.1, 30))

    # Spiral platforms
    angle  = 0.0
    radius = INITIAL_RADIUS
    for i in range(PLATFORM_COUNT):
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        y = i * HEIGHT_STEP + PLAT_HALF_Y
        e = world.add_static_aabb(yope.Vec3(x, y, z),
                                   yope.Vec3(PLAT_HALF_XZ, PLAT_HALF_Y, PLAT_HALF_XZ))
        # Colour: warm gradient along the spiral
        t = i / (PLATFORM_COUNT - 1)
        world.attach_box_mesh(e, yope.Vec3(PLAT_HALF_XZ, PLAT_HALF_Y, PLAT_HALF_XZ),
                              0.3 + 0.6*t, 0.6 - 0.3*t, 0.9 - 0.7*t)
        angle  += ANGLE_STEP
        radius += RADIUS_GROW

    # Player spawn point (platform 0)
    spawn_x = INITIAL_RADIUS * math.cos(0.0)
    spawn_z = INITIAL_RADIUS * math.sin(0.0)
    spawn_y = PLAT_HALF_Y * 2 + 1.0  # just above first platform

    player = world.add_sphere(1.0, 0.5, yope.Vec3(spawn_x, spawn_y, spawn_z))
    world.attach_sphere_mesh(player, 0.5, 0.2, 0.6, 1.0)

    # Name the player so the behavior script can find it
    reg = world.get_registry()
    # (Name must be set via ECS — we rely on scene JSON "Name" component for now)

    yope.camera.set_position(yope.Vec3(spawn_x, spawn_y + 0.6, spawn_z))
    yope.camera.set_rotation(yope.Vec3(0, 0, 0))

build_level(yope.world)
