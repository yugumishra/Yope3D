"""
Platformer behavior script.
Attach as a PythonScript on the player entity.
paramsBlob: {"module": "behaviors.platformer_logic", "class": "PlatformerLogic"}

Controls: WASD move, SPACE jump, mouse look
"""
import yope3d, math

SENSITIVITY = 0.002
BASE_SPEED  = 1.4
HORIZ_DAMP  = 0.85
JUMP_VEL    = 10.0
CAMERA_LIFT = 0.6

class PlatformerLogic:
    PARAMS = {}
    def init(self, world, entity, params):
        self.entity    = entity
        self.yaw       = 0.0
        self.pitch     = 0.0
        self.grounded  = False
        self.prev_space = False

    def update(self, world, entity, dt):
        inp = yope3d.input
        reg = world.get_registry()

        hull = yope3d.reg_get(entity, "Hull")
        tf   = yope3d.reg_get(entity, "Transform")
        if hull is None or tf is None:
            return

        # Mouse look
        dx, dy = inp.get_mouse_delta()
        self.yaw   -= dx * SENSITIVITY
        self.pitch -= dy * SENSITIVITY
        self.pitch = max(-1.4, min(1.4, self.pitch))

        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0))

        # Forward/right from yaw only (no pitch in movement)
        cos_y = math.cos(self.yaw)
        sin_y = math.sin(self.yaw)
        fwd   = yope3d.Vec3(-sin_y, 0, -cos_y)
        right = yope3d.Vec3(cos_y,  0, -sin_y)

        move = yope3d.Vec3(0, 0, 0)
        if inp.is_key_down(yope3d.KEY_W): move += fwd
        if inp.is_key_down(yope3d.KEY_S): move -= fwd
        if inp.is_key_down(yope3d.KEY_D): move += right
        if inp.is_key_down(yope3d.KEY_A): move -= right

        length = move.length()
        if length > 1e-4:
            move = move * (BASE_SPEED / length)
        hull.velocity.x = hull.velocity.x * HORIZ_DAMP + move.x
        hull.velocity.z = hull.velocity.z * HORIZ_DAMP + move.z

        # Jump
        space = inp.is_key_down(yope3d.KEY_SPACE)
        if space and not self.prev_space:
            hull.velocity.y = JUMP_VEL
        self.prev_space = space

        # Camera follows player
        pos = tf.position
        yope3d.camera.set_position(yope3d.Vec3(pos.x, pos.y + CAMERA_LIFT, pos.z))
