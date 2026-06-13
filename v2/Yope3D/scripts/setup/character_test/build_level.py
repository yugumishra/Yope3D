"""
Character controller test level.
Run from the editor Scene Script panel or call build(world) directly.

Layout:
  - Large flat ground
  - Low platform (step-climb test)
  - High platform (jump required)
  - Tilted ramp (slope test: ~20 deg walkable, ~50 deg slide)
  - A few dynamic spheres to bump into

Returns the player entity (Transform + CapsuleForm).
Wire the ScriptComponent via the editor:
  scriptClass  = "PythonScript"
  paramsBlob   = {"module": "behaviors.character_controller",
                  "class": "CharacterController", "cam_mode": "first"}
"""
import yope3d, math

def build(world):
    # Ground plane
    ground = world.add_static_aabb(yope3d.Vec3(0, -0.5, 0), yope3d.Vec3(30, 0.5, 30))
    world.attach_box_mesh(ground, yope3d.Vec3(30, 0.5, 30), 0.55, 0.55, 0.55)

    # Low platform — step climbing test (step_height ~0.35 clears this)
    p1 = world.add_static_aabb(yope3d.Vec3(5, 0.2, 0), yope3d.Vec3(3, 0.2, 3))
    world.attach_box_mesh(p1, yope3d.Vec3(3, 0.2, 3), 0.4, 0.6, 0.8)

    # High platform — jump required
    p2 = world.add_static_aabb(yope3d.Vec3(11, 1.0, 0), yope3d.Vec3(3, 0.5, 3))
    world.attach_box_mesh(p2, yope3d.Vec3(3, 0.5, 3), 0.4, 0.6, 0.8)

    # Gentle ramp (~20 deg) — should be walkable
    ramp_gentle = world.add_obb(yope3d.Vec3(3, 0.15, 4), 0.0, yope3d.Vec3(0, 0.6, 7))
    world.attach_box_mesh(ramp_gentle, yope3d.Vec3(3, 0.15, 4), 0.7, 0.55, 0.3)
    world.fix_entity(ramp_gentle)
    tf_rg = yope3d.reg_get(ramp_gentle, "Transform")
    if tf_rg:
        tf_rg.rotation = yope3d.Quat.from_axis_angle(yope3d.Vec3(1, 0, 0), math.radians(-20))

    # Steep ramp (~50 deg) — should slide the player off
    ramp_steep = world.add_obb(yope3d.Vec3(2, 0.15, 3), 0.0, yope3d.Vec3(-6, 1.0, 4))
    world.attach_box_mesh(ramp_steep, yope3d.Vec3(2, 0.15, 3), 0.8, 0.4, 0.3)
    world.fix_entity(ramp_steep)
    tf_rs = yope3d.reg_get(ramp_steep, "Transform")
    if tf_rs:
        tf_rs.rotation = yope3d.Quat.from_axis_angle(yope3d.Vec3(1, 0, 0), math.radians(-50))

    # Dynamic spheres
    for i in range(5):
        e = world.add_sphere(1.0, 0.4, yope3d.Vec3(i * 2.0 - 4.0, 3.0, -6.0))
        world.attach_sphere_mesh(e, 0.4, 0.85, 0.5, 0.2)

    # Dynamic AABBs — drop from height, stack on ground
    for i in range(3):
        e = world.add_aabb(yope3d.Vec3(0.5, 0.5, 0.5), 1.0, yope3d.Vec3(-8.0 + i * 1.5, 4.0 + i * 1.2, -3.0))
        world.attach_box_mesh(e, yope3d.Vec3(0.5, 0.5, 0.5), 0.9, 0.7, 0.3)

    # Dynamic OBBs — drop with initial rotation; should settle into stable orientation
    for i in range(3):
        e = world.add_obb(yope3d.Vec3(0.6, 0.3, 0.6), 1.0, yope3d.Vec3(-8.0 + i * 1.5, 6.0 + i * 1.0, -5.0))
        world.attach_box_mesh(e, yope3d.Vec3(0.6, 0.3, 0.6), 0.3, 0.7, 0.9)
        tf = yope3d.reg_get(e, "Transform")
        if tf:
            tf.rotation = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 1, 0), math.radians(30 + i * 25))

    # Fixed non-rotated OBB — identity rotation, tests OBB overlap path without slope
    fixed_obb = world.add_obb(yope3d.Vec3(2.0, 0.5, 2.0), 0.0, yope3d.Vec3(-10.0, 0.5, 0.0))
    world.attach_box_mesh(fixed_obb, yope3d.Vec3(2.0, 0.5, 2.0), 0.5, 0.9, 0.5)
    world.fix_entity(fixed_obb)

    # Player capsule (kinematic — no Hull, physics sim ignores it)
    player = world.add_kinematic_capsule(0.4, 0.9, yope3d.Vec3(0, 2, 0))
    world.attach_capsule_mesh(player, 0.4, 0.9, 0.3, 0.7, 1.0)

    yope3d.camera.set_position(yope3d.Vec3(0, 2, 0))
    yope3d.camera.set_rotation(yope3d.Vec3(0, 0, 0))

    return player

build(yope3d.world)
