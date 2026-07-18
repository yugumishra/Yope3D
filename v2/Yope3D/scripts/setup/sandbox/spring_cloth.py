"""
Sandbox setup: spring cloth grid.
variant:    0 = top-row fixed, 1 = 4-corners fixed, 2 = 2-top-corners fixed
shape_type: 0 = Sphere,        1 = AABB,            2 = OBB
"""
import yope3d

GRID_N    = 20
GRID_STEP = 1.0
NODE_HALF = 0.45
NODE_MASS = 1.0
SPRING_K  = 300.0

def build_spring_cloth(world, variant=0, shape_type=0):
    fh = (GRID_N + 1) * GRID_STEP * 2.0
    world.add_static_aabb(yope3d.Vec3(0, -5, 0), yope3d.Vec3(fh, 5, fh))

    horizontal = (variant == 1)
    half_w = (GRID_N - 1) * GRID_STEP * 0.5
    top_y  = 25.0 if horizontal else (GRID_N - 1) * GRID_STEP + 25.0

    grid = [[None]*GRID_N for _ in range(GRID_N)]

    for j in range(GRID_N):
        for i in range(GRID_N):
            if horizontal:
                cx, cy, cz = -half_w + i*GRID_STEP, top_y, -half_w + j*GRID_STEP
            else:
                cx, cy, cz = -half_w + i*GRID_STEP, top_y - j*GRID_STEP, 0.0

            fi = i / (GRID_N - 1)
            fj = j / (GRID_N - 1)

            if shape_type == 0:
                e = world.add_sphere(NODE_MASS, NODE_HALF, yope3d.Vec3(cx, cy, cz))
                world.attach_sphere_mesh(e, NODE_HALF,
                    0.9-0.4*fi, 0.3+0.4*fj, 0.15+0.3*fi)
            elif shape_type == 1:
                e = world.add_aabb(yope3d.Vec3(NODE_HALF,NODE_HALF,NODE_HALF), NODE_MASS,
                                   yope3d.Vec3(cx, cy, cz))
                world.attach_box_mesh(e, yope3d.Vec3(NODE_HALF,NODE_HALF,NODE_HALF),
                    0.1+0.2*fj, 0.5+0.3*fi, 0.8-0.3*fj)
            else:
                e = world.add_obb(yope3d.Vec3(NODE_HALF,NODE_HALF,NODE_HALF), NODE_MASS,
                                  yope3d.Vec3(cx, cy, cz))
                world.attach_box_mesh(e, yope3d.Vec3(NODE_HALF,NODE_HALF,NODE_HALF),
                    fi, fj, 0.4+0.3*(fi+fj)*0.5)

            grid[i][j] = e

            # Determine if this node should be fixed
            if variant == 0:
                fix = (j == 0)
            elif variant == 1:
                fix = (i == 0 or i == GRID_N-1) and (j == 0 or j == GRID_N-1)
            else:
                fix = (j == 0) and (i == 0 or i == GRID_N-1)

            if fix:
                world.fix_entity(e)

    # Horizontal springs
    for j in range(GRID_N):
        for i in range(GRID_N - 1):
            world.add_spring(grid[i][j], grid[i+1][j], SPRING_K, GRID_STEP)

    # Vertical springs
    for i in range(GRID_N):
        for j in range(GRID_N - 1):
            world.add_spring(grid[i][j], grid[i][j+1], SPRING_K, GRID_STEP)

    dist = (GRID_N - 1) * GRID_STEP
    if horizontal:
        yope3d.camera.set_position(yope3d.Vec3(0, top_y + dist, dist * 0.7))
        yope3d.camera.set_rotation(yope3d.Vec3(-0.8, 0, 0))
    else:
        yope3d.camera.set_position(yope3d.Vec3(0, top_y * 0.5, dist + 5))
        yope3d.camera.set_rotation(yope3d.Vec3(0, 0, 0))

# Run immediately: default is sphere cloth, top-row fixed
build_spring_cloth(yope3d.world, variant=0, shape_type=0)
