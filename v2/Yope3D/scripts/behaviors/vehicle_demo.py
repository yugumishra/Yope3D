"""
Raycast-wheel vehicle demo (see physics/Joint.h's SuspensionJoint/
WheelFrictionJoint). The chassis is one OBB rigid body; each of its 4 wheels
is a raycast — no separate wheel rigid bodies, no hinge/motor joints — with a
spring/damper suspension plus lateral (grip) and longitudinal (drive/brake)
friction rows solved in the same island-parallel PGS pass as everything else.

Controls: W/S throttle+reverse, A/D steer the front wheels. No mouse-look
(unlike mouse_drag_ragdoll.py) — the camera is a fixed-offset chase cam.

DRIVETRAIN — front-wheel drive. The two front wheels both steer AND drive; the
two rear wheels free-roll (lateral grip only). This is the realistic model the
user asked for: steering rotates the front wheels, and because those same
wheels carry the drive force, the propulsion vector turns with the steering.
It relies on WheelFrictionJoint.driven (WheelSpec.driven): a wheel with
driven=False skips its longitudinal row entirely and rolls free, instead of
targeting wheel_angular_vel*radius (which for an un-driven wheel is 0, i.e. it
would *brake*). Driving all four wheels was the earlier workaround for that
brake behavior before the driven flag existed — no longer needed.

STEERING IS NOT A DIRECT TORQUE. set_wheel_steer only rotates the front
WheelFrictionJoints' longitudinal direction by steer_angle about the wheel
up-axis (precomputeWheelFriction, ColliderDiscrete.cpp). Drive + lateral-grip
impulses are then applied at each wheel's ground contact point, offset from the
chassis COM, so `angularImpulse += rChassis x imp` yaws the car — real
tyre-force steering. Because those contacts sit well BELOW the COM (a long roll
lever arm) sharp cornering rolls the body hard and, unchecked, flips it. A car
tips when lateral accel exceeds (half_track / com_height) * g — a purely
GEOMETRIC threshold, independent of mass (both the cornering force and gravity
scale with mass, so it cancels; dropping mass does not help, and with these
springs would raise the car and make it worse). We stay under that threshold
instead of fighting it after the fact: a wide track (WHEEL_TRACK_X) and low
ride height (REST_LENGTH) push the tip point to ~1.9 g, and MU_LAT is held at
1.0 g so the tyres SLIDE (understeer) at the limit rather than gripping hard
enough to trip. Stiffer springs (SUSP_STIFFNESS) curb body roll, a modest top
speed (MAX_DRIVE_ANGVEL) limits v*yaw lateral load, and the chassis yaw rate is
clamped to MAX_YAW_RATE each frame as a final governor.

VISUALS: chassis OBB = lower body (box 1); a cabin (box 2) and a leaned
windshield panel at the front/+Z (box 3) are collider-less meshes riding the
chassis so heading is legible. The 4 wheels are collider-less CYLINDERS (axis
rotated to lateral); the front pair also carries the live steer angle so you
can see them turn. Nothing here is simulated — the wheels aren't animated with
real suspension travel, and body/wheels are cosmetic. Everything stays
collider-less (detach_physics_body strips the Hull + Form that add_obb/
add_cylinder create): the wheels sit on the suspension-raycast contact points,
so as real colliders they'd hijack the raycasts and collide with the chassis —
the original "drives off then won't steer" bug.

paramsBlob keys (all optional):
  spawn_y float 2.0 — chassis spawn height (m)
"""
import math
import yope3d

MAX_STEER = 0.5            # radians (~28 deg)
MAX_DRIVE_ANGVEL = 28.0    # rad/s (top speed ~= this * WHEEL_RADIUS m/s)
STEER_RATE = 8.0           # 1/s, how fast steer_angle chases the input
MAX_YAW_RATE = 1.3         # rad/s — chassis yaw is clamped to this each frame
SUSP_STIFFNESS = 70000.0   # N/m per wheel — also sets roll stiffness (see docstring)
SUSP_DAMPING = 7000.0      # N/(m/s) per wheel

# Anti-rollover tuning — the tip threshold is (half_track / com_height) * g and
# is INDEPENDENT of mass. We keep MU_LAT below that threshold so the car slides
# (understeers) at the limit instead of tripping over its tyres. See docstring.
WHEEL_TRACK_X = 1.15       # half-track (m) — wider = harder to tip
REST_LENGTH = 0.30         # suspension rest length (m) — shorter = lower COM
MU_LAT = 1.0               # lateral grip: 1.0 g, under the ~1.9 g tip threshold
MU_LONG = 1.3              # longitudinal grip (drive/brake) can stay high

WHEEL_RADIUS = 0.35
WHEEL_HALF_WIDTH = 0.11
WHEEL_VISUAL_Y = -0.27     # cylinder centre height (chassis-local) so it sits on the ground


class VehicleDemo:
    PARAMS = {
        "spawn_y": {"type": "float", "default": 2.0, "label": "Spawn Height (m)"},
    }

    def init(self, world, entity, params):
        spawn_y = params.get("spawn_y", 2.0)
        chassis_half = yope3d.Vec3(1.0, 0.4, 2.0)
        spawn_pos = yope3d.Vec3(0.0, spawn_y, 0.0)

        self.chassis = world.add_obb(chassis_half, 800.0, spawn_pos)
        world.attach_box_mesh(self.chassis, chassis_half, 0.75, 0.13, 0.13)  # lower body

        # Wheel mounts, chassis-local — front is +Z, rear is -Z. Indices 0,1 are
        # the front (steered + driven) pair; 2,3 the rear (free-rolling) pair.
        self._wheel_positions = [
            yope3d.Vec3(-WHEEL_TRACK_X, -0.35,  1.7),   # 0: front-left
            yope3d.Vec3( WHEEL_TRACK_X, -0.35,  1.7),   # 1: front-right
            yope3d.Vec3(-WHEEL_TRACK_X, -0.35, -1.7),   # 2: rear-left
            yope3d.Vec3( WHEEL_TRACK_X, -0.35, -1.7),   # 3: rear-right
        ]
        wheels = []
        for i, pos in enumerate(self._wheel_positions):
            spec = yope3d.WheelSpec()
            spec.local_pos   = pos
            spec.rest_length = REST_LENGTH
            spec.max_travel  = 0.3
            spec.stiffness   = SUSP_STIFFNESS
            spec.damping     = SUSP_DAMPING
            spec.radius      = WHEEL_RADIUS
            spec.mu_long     = MU_LONG
            spec.mu_lat      = MU_LAT
            spec.driven      = (i < 2)   # FWD: only the front wheels drive
            wheels.append(spec)

        self.wheels = world.add_vehicle(self.chassis, wheels)
        self.steer_angle = 0.0

        # Collider-less cylinder wheels. add_cylinder's mesh axis is +Y, so this
        # quat rotates it to the wheel's lateral spin axis; the front pair also
        # gets the live steer angle composed on top in update().
        self._wheel_align = yope3d.Quat.from_axis_angle(yope3d.Vec3(0.0, 0.0, 1.0), math.pi * 0.5)
        self._wheels_visual = []   # (entity, chassis-local offset, is_front)
        for i, pos in enumerate(self._wheel_positions):
            voff = yope3d.Vec3(pos.x, WHEEL_VISUAL_Y, pos.z)   # lifted to sit on the ground
            e = world.add_cylinder(WHEEL_RADIUS, WHEEL_HALF_WIDTH, 1.0, spawn_pos + voff)
            world.attach_cylinder_mesh(e, WHEEL_RADIUS, WHEEL_HALF_WIDTH, 0.09, 0.09, 0.10)
            world.detach_physics_body(e)   # collider off — decorative only
            self._wheels_visual.append((e, voff, i < 2))

        # Decorative body — collider-less boxes riding the chassis (boxes 2 & 3).
        # Each: (entity, chassis-local offset, chassis-local rotation | None).
        self._decor = []
        self._add_decor_box(world, spawn_pos,
                            yope3d.Vec3(0.7, 0.32, 0.8),      # cabin greenhouse
                            yope3d.Vec3(0.0, 0.72, -0.25),
                            0.85, 0.86, 0.90, None)
        self._add_decor_box(world, spawn_pos,
                            yope3d.Vec3(0.62, 0.30, 0.05),    # windshield panel (front, +Z)
                            yope3d.Vec3(0.0, 0.68, 0.55),
                            0.22, 0.42, 0.62,
                            yope3d.Quat.from_axis_angle(yope3d.Vec3(1.0, 0.0, 0.0), 0.6))

        self._cam_offset = yope3d.Vec3(0.0, 5.0, -9.0)
        yope3d.camera.set_position(spawn_pos + self._cam_offset)
        yope3d.camera.look_at(spawn_pos)

    def _add_decor_box(self, world, spawn_pos, half, offset, r, g, b, local_rot):
        e = world.add_obb(half, 1.0, spawn_pos + offset)
        world.attach_box_mesh(e, half, r, g, b)
        world.detach_physics_body(e)   # collider off — decorative only
        self._decor.append((e, offset, local_rot))
        return e

    def update(self, world, entity, dt):
        inp = yope3d.input

        throttle = 0.0
        if inp.is_key_down(yope3d.KEY_W): throttle += 1.0
        if inp.is_key_down(yope3d.KEY_S): throttle -= 1.0

        # Inverted steer: A turns right, D turns left.
        steer_input = 0.0
        if inp.is_key_down(yope3d.KEY_A): steer_input += 1.0
        if inp.is_key_down(yope3d.KEY_D): steer_input -= 1.0

        target_steer = steer_input * MAX_STEER
        self.steer_angle += (target_steer - self.steer_angle) * min(1.0, STEER_RATE * dt)

        drive_angvel = throttle * MAX_DRIVE_ANGVEL

        # Front wheels (0,1): steer + drive. Rear wheels (2,3): free-rolling
        # (driven=False), so nothing to drive — they just grip. Calling steer/
        # drive on the fronts every frame also keeps the chassis awake.
        world.set_wheel_steer(self.wheels[0], self.steer_angle)
        world.set_wheel_steer(self.wheels[1], self.steer_angle)
        world.set_wheel_drive(self.wheels[0], drive_angvel)
        world.set_wheel_drive(self.wheels[1], drive_angvel)

        tf = yope3d.reg_get(self.chassis, "Transform")
        rot = tf.rotation

        # Governor: clamp yaw rate so a sharp turn can't spin the car up faster
        # than it can stay upright (world-up ~= chassis-up while roughly level).
        hull = yope3d.reg_get(self.chassis, "Hull")
        omega = hull.omega
        if omega.y > MAX_YAW_RATE:
            omega.y = MAX_YAW_RATE
            hull.omega = omega
        elif omega.y < -MAX_YAW_RATE:
            omega.y = -MAX_YAW_RATE
            hull.omega = omega

        yope3d.camera.set_position(tf.position + self._cam_offset)
        yope3d.camera.look_at(tf.position)

        # Ride the chassis: rotate each part's body-frame offset by the chassis
        # orientation. Front wheels additionally carry the live steer angle.
        for e, offset, is_front in self._wheels_visual:
            wt = yope3d.reg_get(e, "Transform")
            wt.position = tf.position + rot.rotate(offset)
            if is_front:
                steer_q = yope3d.Quat.from_axis_angle(yope3d.Vec3(0.0, 1.0, 0.0), self.steer_angle)
                wt.rotation = rot * (steer_q * self._wheel_align)
            else:
                wt.rotation = rot * self._wheel_align

        for e, offset, local_rot in self._decor:
            dtf = yope3d.reg_get(e, "Transform")
            dtf.position = tf.position + rot.rotate(offset)
            dtf.rotation = (rot * local_rot) if local_rot is not None else rot
