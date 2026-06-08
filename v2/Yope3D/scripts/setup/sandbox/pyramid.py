"""
Sandbox setup: pyramid of OBBs.
Run in the Scene Script panel, then Save Scene.
Usage: import and call build_pyramid(world, base_n)
"""
import yope, math

PYR_HALF    = 0.45
PYR_SPACING = 1.0

def build_pyramid(world, base_n):
    half_floor = (base_n + 15) * 1.0
    world.add_static_aabb(yope.Vec3(0, -0.5, 0), yope.Vec3(half_floor, 0.5, 15))

    for row in range(base_n):
        count = base_n - row
        y = PYR_HALF + row * (2 * PYR_HALF - 0.012)
        t = row / max(base_n - 1, 1)
        for j in range(count):
            x = -(count - 1) * PYR_SPACING * 0.5 + j * PYR_SPACING
            e = world.add_obb(yope.Vec3(PYR_HALF, PYR_HALF, PYR_HALF), 1.0, yope.Vec3(x, y, 0))
            r = 0.2 + t * 0.7
            g = 0.45 - t * 0.15
            b = 0.9  - t * 0.7
            world.attach_box_mesh(e, yope.Vec3(PYR_HALF, PYR_HALF, PYR_HALF), r, g, b)

    cam_z = base_n + 4.0
    cam_y = base_n * 0.7
    yope.camera.set_position(yope.Vec3(0, cam_y, cam_z))
    yope.camera.set_rotation(yope.Vec3(0, 0, 0))

# Run immediately when the script is executed in the Scene Script panel
build_pyramid(yope.world, 7)
