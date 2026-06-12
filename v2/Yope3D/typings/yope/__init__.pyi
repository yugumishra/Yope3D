"""Yope3D embedded scripting module.

`yope` is a pybind11 *embedded* module — it exists only inside the running
engine process and cannot be imported by a normal Python interpreter. This
stub file is what gives VS Code / Pylance completions, signatures, and these
docs; it is hand-maintained against the C++ bindings in
`src/scripting/python/bindings_{math,ecs,world}.cpp`. If you change a binding,
update this file to match.

Conventions
-----------
- Units are meters / seconds / kilograms / radians unless noted.
- Coordinate system is right-handed, +Y up. Capsules and cylinders are
  axis-aligned to +Y in local space.
- `extent` fields are **half**-extents; `half_height` is half the cylinder
  section length (capsule total height = 2*half_height + 2*radius).
- All component objects returned by `view()` / `reg_get()` are *references*
  into engine memory — mutations apply immediately, but the reference dies
  with the entity. Don't cache them across frames.
- Threading: scripts run on the main thread between physics ticks. Calling
  into `yope` from your own Python threads is not supported.

Behavior scripts
----------------
A behavior is a class in `scripts/behaviors/*.py` with a class-level `PARAMS`
dict; the editor harvests it for the ScriptComponent inspector:

    class MyBehavior:
        PARAMS = {
            "speed": {"type": "float", "default": 5.0, "label": "Speed"},
            # types: "float" | "int" | "bool" | "str" | "enum" (+ "options")
            #        | "strlist"
        }
        def init(self, world: World, entity: Entity, params: dict) -> None: ...
        def update(self, world: World, entity: Entity, dt: float) -> None: ...

`init` runs once when play mode starts; `update` runs every frame with the
frame delta time in seconds. `params` holds the inspector-edited values
(falls back to PARAMS defaults via `params.get(...)`).
"""

from typing import Any, Literal, overload

# ---------------------------------------------------------------------------
# Math types
# ---------------------------------------------------------------------------

class Vec2:
    """2D float vector. Supports +, -, * scalar, unary -."""

    x: float
    y: float
    def __init__(self, x: float = 0.0, y: float = 0.0) -> None: ...
    def __add__(self, other: Vec2) -> Vec2: ...
    def __sub__(self, other: Vec2) -> Vec2: ...
    def __mul__(self, s: float) -> Vec2: ...
    def __rmul__(self, s: float) -> Vec2: ...
    def __neg__(self) -> Vec2: ...
    def length(self) -> float:
        """Euclidean length."""
    def normalize(self) -> Vec2:
        """Return a unit-length copy (does not modify self)."""

class Vec3:
    """3D float vector. Supports +, -, * scalar, unary -, +=, -=."""

    x: float
    y: float
    z: float
    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0) -> None: ...
    def __add__(self, other: Vec3) -> Vec3: ...
    def __sub__(self, other: Vec3) -> Vec3: ...
    def __mul__(self, s: float) -> Vec3: ...
    def __rmul__(self, s: float) -> Vec3: ...
    def __neg__(self) -> Vec3: ...
    def __iadd__(self, other: Vec3) -> Vec3: ...
    def __isub__(self, other: Vec3) -> Vec3: ...
    def dot(self, other: Vec3) -> float:
        """Standard dot product."""
    def cross(self, other: Vec3) -> Vec3:
        """3D cross product (right-handed)."""
    def length(self) -> float:
        """Euclidean length."""
    def normalize(self) -> Vec3:
        """Return a unit-length copy (does not modify self)."""

class Vec4:
    """4D float vector (no arithmetic operators bound)."""

    x: float
    y: float
    z: float
    w: float
    def __init__(
        self, x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 0.0
    ) -> None: ...

class Quat:
    """Rotation quaternion (x, y, z, w). Default-constructs to identity."""

    x: float
    y: float
    z: float
    w: float
    def __init__(
        self, x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 1.0
    ) -> None: ...
    @staticmethod
    def from_axis_angle(axis: Vec3, radians: float) -> Quat:
        """Build a quaternion rotating `radians` around `axis` (need not be unit length)."""

PI: float

def to_radians(degrees: float) -> float: ...
def to_degrees(radians: float) -> float: ...
def clamp(v: float, lo: float, hi: float) -> float:
    """Clamp v into [lo, hi]."""

def lerp(a: float, b: float, t: float) -> float:
    """Linear interpolation a + (b-a)*t. t is not clamped."""

# ---------------------------------------------------------------------------
# ECS core
# ---------------------------------------------------------------------------

class Entity:
    """Opaque entity handle: (id, generation).

    The generation increments when an id is reused, so stale handles compare
    unequal to the new entity. Hashable — usable as dict keys / in sets.
    Check liveness with `yope.reg_valid(e)`.
    """

    @property
    def id(self) -> int: ...
    @property
    def generation(self) -> int: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class Registry:
    """Opaque handle to the ECS registry.

    No methods are bound; use the module-level helpers `yope.view()`,
    `yope.reg_get()`, `yope.reg_has()`, `yope.reg_valid()` instead.
    """

# ---------------------------------------------------------------------------
# Components (references into engine memory — edits apply immediately)
# ---------------------------------------------------------------------------

class Transform:
    """Position + rotation + scale. Every renderable/physical entity has one."""

    position: Vec3
    rotation: Quat
    scale: Vec3
    """Render scale. Note: cylinders encode (radius, halfHeight, radius) here;
    capsules use a baked mesh and keep scale at (1,1,1)."""

class Hull:
    """Rigid-body dynamic state (paired with one shape Form component).

    Owned by the 240 Hz physics thread during `World::advance()`; scripts
    mutate it between ticks (the normal `update()` path is safe).
    """

    velocity: Vec3
    """Linear velocity (m/s)."""
    omega: Vec3
    """Angular velocity (rad/s, world space)."""
    mass: float
    """Mass in kg. 0 = infinite mass (static); prefer `World.fix_entity`."""
    linear_damping: float
    angular_damping: float
    friction: float
    """Coulomb friction coefficient used by the PGS solver."""
    restitution: float
    """Bounciness in [0, 1]."""
    gravity: bool
    """Whether world gravity is applied to this body."""
    tangible: bool
    """If False the body is ignored by collision detection (still integrates)."""

class SphereForm:
    radius: float

class AABBForm:
    """Axis-aligned box. `extent` is half-extents; rotation always identity."""

    extent: Vec3

class OBBForm:
    """Oriented box. `extent` is half-extents; rotation lives in Transform."""

    extent: Vec3

class CapsuleForm:
    """Capsule aligned to local +Y. Total height = 2*half_height + 2*radius."""

    radius: float
    half_height: float
    """Half the cylindrical section length (excludes the hemispherical caps)."""

class LightSource:
    type: int
    """0=Point, 1=Directional, 2=Spot, 3=Flash."""
    intensity: float
    color: Vec3
    """RGB in [0,1]."""
    position: Vec3
    direction: Vec3

class Name:
    """Editor-visible entity name (fixed-size buffer; long names truncate)."""

    value: str

class SpringConstraint:
    target: Entity
    k: float
    """Hookean stiffness."""
    rest_length: float

class ScriptComponent:
    """Attaches a Python behavior class to an entity."""

    script_class: str
    """Class name of a behavior in scripts/behaviors/ (e.g. "CharacterController")."""
    params_blob: str
    """Serialized parameter values edited in the inspector."""

ComponentName = Literal[
    "Transform",
    "Hull",
    "SphereForm",
    "AABBForm",
    "OBBForm",
    "CapsuleForm",
    "CylinderForm",
    "LightSource",
    "Name",
    "SpringConstraint",
    "ScriptComponent",
]
"""Component names accepted by view() / reg_get() / reg_has()."""

# ---------------------------------------------------------------------------
# Engine singletons (bound by the engine before any script runs)
# ---------------------------------------------------------------------------

class World:
    """Entity/physics factory and queries. Access via the `yope.world` singleton."""

    gravity: Vec3
    """World gravity in m/s² (default (0, -9.8…, 0))."""

    def add_sphere(self, mass: float, radius: float, pos: Vec3 = ...) -> Entity:
        """Spawn a dynamic sphere body (Transform + Hull + SphereForm).

        No render mesh is attached — call `attach_sphere_mesh` after.
        """
    def add_obb(self, extent: Vec3, mass: float, pos: Vec3 = ...) -> Entity:
        """Spawn a dynamic oriented box. `extent` is half-extents.

        No render mesh is attached — call `attach_box_mesh` after.
        """
    def add_aabb(self, extent: Vec3, mass: float, pos: Vec3 = ...) -> Entity:
        """Spawn a dynamic axis-aligned box (never rotates). `extent` is half-extents."""
    def add_static_aabb(self, pos: Vec3, extent: Vec3) -> Entity:
        """Spawn an immovable axis-aligned box (Fixed tag, infinite mass)."""
    def add_kinematic_capsule(
        self, radius: float, half_height: float, pos: Vec3 = ...
    ) -> Entity:
        """Spawn a kinematic capsule: Transform + CapsuleForm, **no Hull**.

        The physics sim ignores it; drive its Transform directly (this is the
        character-controller body). Query the world with `capsule_overlap` /
        `capsule_cast`.
        """
    def remove_entity(self, e: Entity) -> None:
        """Destroy an entity: purges its springs, contact-cache entries, and mesh."""
    def reset_physics(self) -> None:
        """Clear all velocities/contacts and wake everything."""
    def get_hull_count(self) -> int: ...
    def get_island_count(self) -> int:
        """Number of contact islands in the last physics step."""
    def get_registry(self) -> Registry: ...
    def add_spring(self, a: Entity, b: Entity, k: float, rest: float) -> Any:
        """Connect two bodies with a Hookean spring (stiffness k, rest length in meters).

        Warning: the returned C++ Spring handle has no Python binding yet, so
        calling this currently raises a pybind11 cast error on return.
        """
    def add_spring_with_proxies(
        self,
        a: Entity,
        b: Entity,
        k: float,
        rest: float,
        proxy_count: int,
        proxy_radius: float,
    ) -> Any:
        """Spring plus procedural helix proxy spheres for visualization.

        Same caveat as `add_spring`: the return value is not bound yet.
        """
    def attach_sphere_mesh(
        self,
        entity: Entity,
        radius: float,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity an icosphere render mesh with flat color (RGB in [0,1])."""
    def attach_box_mesh(
        self,
        entity: Entity,
        half: Vec3,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity a box render mesh. `half` is half-extents."""
    def attach_capsule_mesh(
        self,
        entity: Entity,
        radius: float,
        half_height: float,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity a capsule render mesh baked at (radius, half_height)."""
    def set_mesh_color(self, entity: Entity, r: float, g: float, b: float) -> None:
        """Recolor an already-attached render mesh (RGB in [0,1])."""
    def fix_entity(self, entity: Entity) -> None:
        """Pin a body in place: zero mass/velocity, disable gravity, add Fixed tag.

        Useful for spring cloth anchors and static obstacles created from script.
        """

class Camera:
    position: Vec3
    rotation: Vec3
    """Euler angles in radians (pitch/yaw/roll as Vec3)."""

    def set_position(self, p: Vec3) -> None: ...
    def set_rotation(self, r: Vec3) -> None:
        """Set Euler rotation in radians."""
    def set_fov(self, fov: float) -> None:
        """Vertical field of view in degrees."""
    def get_forward(self) -> Vec3:
        """Camera forward direction in world space (unit length)."""

class Input:
    """Polled keyboard/mouse state. Key codes are the `yope.KEY_*` constants."""

    def is_key_down(self, key: int) -> bool:
        """True every frame the key is held."""
    def is_key_pressed(self, key: int) -> bool:
        """True only on the frame the key went down (one-shot)."""
    def is_key_released(self, key: int) -> bool:
        """True only on the frame the key went up (one-shot)."""
    def is_lmb_down(self) -> bool: ...
    def is_rmb_down(self) -> bool: ...
    def get_mouse_delta(self) -> tuple[float, float]:
        """Mouse movement (dx, dy) in pixels since last frame."""

class AudioSystem:
    def pause_all(self) -> None: ...
    def resume_all(self) -> None: ...
    def stop_all(self) -> None: ...

class SceneManager:
    def load_scene(self, path: str) -> None:
        """Queue a scene load (applied safely between frames).

        `path` is relative to the assets dir, e.g. "scenes/sandbox.json".
        """

world: World
"""The engine World. None only before the engine binds the context — always
set by the time behavior init()/update() run."""
camera: Camera
input: Input
audio: AudioSystem
scene_manager: SceneManager

# ---------------------------------------------------------------------------
# Module-level ECS queries
# ---------------------------------------------------------------------------

def view(*components: ComponentName) -> list[tuple[Any, ...]]:
    """All entities having every named component.

    Returns a list of tuples `(entity, comp1, comp2, ...)` in argument order,
    e.g. `for e, tf, hull in yope.view("Transform", "Hull"): ...`.
    Component objects are live references into engine memory.
    Must be called from the main thread (normal script update path).
    """

@overload
def reg_get(e: Entity, name: Literal["Transform"]) -> Transform | None: ...
@overload
def reg_get(e: Entity, name: Literal["Hull"]) -> Hull | None: ...
@overload
def reg_get(e: Entity, name: Literal["SphereForm"]) -> SphereForm | None: ...
@overload
def reg_get(e: Entity, name: Literal["AABBForm"]) -> AABBForm | None: ...
@overload
def reg_get(e: Entity, name: Literal["OBBForm"]) -> OBBForm | None: ...
@overload
def reg_get(e: Entity, name: Literal["CapsuleForm"]) -> CapsuleForm | None: ...
@overload
def reg_get(e: Entity, name: Literal["LightSource"]) -> LightSource | None: ...
@overload
def reg_get(e: Entity, name: Literal["Name"]) -> Name | None: ...
@overload
def reg_get(e: Entity, name: Literal["SpringConstraint"]) -> SpringConstraint | None: ...
@overload
def reg_get(e: Entity, name: Literal["ScriptComponent"]) -> ScriptComponent | None: ...
@overload
def reg_get(e: Entity, name: str) -> Any: ...
def reg_get(e: Entity, name: str) -> Any:
    """Get a component by name, or None if the entity doesn't have it.

    The result is a live reference into engine memory — edits apply
    immediately; don't cache it across frames.
    """

def reg_has(e: Entity, name: ComponentName) -> bool:
    """True if the entity has the named component."""

def reg_valid(e: Entity) -> bool:
    """True if the entity handle is still alive (generation matches)."""

# ---------------------------------------------------------------------------
# Kinematic queries (character controller support)
# ---------------------------------------------------------------------------

def capsule_overlap(
    pos: Vec3,
    radius: float,
    half_height: float,
    exclude: Entity | None = None,
) -> list[tuple[Vec3, float]]:
    """Test a capsule against all tangible world geometry.

    `pos` is the capsule center. Returns one `(normal, depth)` tuple per
    overlapping entity: push the capsule along `normal` by `depth` to resolve.
    Pass the controller's own entity as `exclude` to skip self-collision.
    """

def capsule_cast(
    pos: Vec3,
    radius: float,
    half_height: float,
    dir: Vec3,
    max_dist: float,
    exclude: Entity | None = None,
) -> tuple[float, bool, Vec3]:
    """Sweep a capsule from `pos` along `dir` (must be normalized).

    Returns `(t, hit, normal)`: `t` is the contact distance from the capsule's
    endpoint sphere center (== max_dist when nothing is hit). Used for
    grounding checks and step-up probes.
    """

def load_scene(path: str) -> None:
    """Shorthand for `yope.scene_manager.load_scene(path)`."""

# ---------------------------------------------------------------------------
# Key constants (GLFW key codes)
# ---------------------------------------------------------------------------

KEY_W: int
KEY_A: int
KEY_S: int
KEY_D: int
KEY_Q: int
KEY_E: int
KEY_SPACE: int
KEY_LEFT: int
KEY_RIGHT: int
KEY_UP: int
KEY_DOWN: int
KEY_R: int
KEY_F: int
KEY_H: int
KEY_P: int
KEY_ESCAPE: int
KEY_ENTER: int
KEY_LEFT_SHIFT: int
KEY_LEFT_CONTROL: int
KEY_V: int
