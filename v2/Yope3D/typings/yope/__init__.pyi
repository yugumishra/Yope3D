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
  with the entity.

  **Dangling-reference hazard:** the ECS is archetype-based, so adding or
  removing *any* component on an entity memcpy-moves its existing components to
  a new archetype block — which silently invalidates every reference you already
  hold to that entity's components. Never cache a `view()`/`reg_get()` result
  across a call that can change entity composition: `reg_add`, `reg_remove`,
  `remove_entity`, the `attach_*_collider` / `detach_physics_body` helpers,
  `fix_entity`, `wake`, or anything that adds/removes a tag. Re-fetch after such
  calls, or use the re-resolving helpers `get_position` / `set_position` /
  `set_velocity`, which look the component up per call.
- Threading: scripts run on the main thread between physics ticks. Calling
  into `yope` from your own Python threads is not supported. Every structure-
  mutating `yope` call takes the engine's structure lock internally, so it is
  safe to call while the 240 Hz physics thread is running.

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
        # All optional:
        def on_unload(self, world: World, entity: Entity) -> None: ...
        def on_collision_enter(self, world: World, entity: Entity, other: Entity) -> None: ...
        def on_collision_exit(self, world: World, entity: Entity, other: Entity) -> None: ...

`init` runs once when play mode starts; `update` runs every frame with the
frame delta time in seconds. `params` holds the inspector-edited values
(falls back to PARAMS defaults via `params.get(...)`).

`on_collision_enter` / `on_collision_exit` fire when this entity *starts* or
*stops* touching another tangible body — `entity` is yours, `other` is the body
you hit. They're dispatched on the main thread once per frame from physics
events, so it's safe to spawn/destroy entities inside them. (The entity needs a
collider to receive these — a behavior on a mesh-only entity never collides.)

Support modules (pure Python, in scripts/behaviors/):
  - `_events`: a tiny emit/subscribe bus for inter-behavior messaging.
  - `_timers`: `after(s, fn)`, `every(s, fn)`, and `yield`-based coroutines;
    drive it by calling `_timers.tick(dt)` once per frame from one behavior.
  - `_debug`: `draw_aabb`, `draw_sphere`, `draw_cross` wireframes over yope.draw_line.
Use `yope.get_behavior(other)` to reach another entity's live behavior instance.
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
    @staticmethod
    def from_euler(pitch: float, yaw: float, roll: float = 0.0) -> Quat:
        """Tait-Bryan yaw(Y)·pitch(X)·roll(Z) in radians (matches FPS yaw/pitch)."""
    @staticmethod
    def slerp(a: Quat, b: Quat, t: float) -> Quat:
        """Spherical linear interpolation between two orientations (t in [0,1])."""
    def __mul__(self, other: Quat) -> Quat:
        """Hamilton product — compose rotations (self applied after other)."""
    def rotate(self, vec: Vec3) -> Vec3:
        """Rotate a vector by this quaternion."""

def look_at(forward: Vec3, up: Vec3 = ...) -> Quat:
    """Quaternion whose local +Z points along `forward`, with +Y near `up`.

    Engine camera forward is -Z, so negate `forward` if orienting toward the camera.
    """

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
    collision_layer: int
    """Bitmask of layers this body belongs to (see `yope.world.layers`)."""
    collision_mask: int
    """Bitmask of layers this body collides with. Both directions must match to collide."""

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

class CylinderForm:
    """Flat-capped cylinder aligned to local +Y. GJK-only (no SAT ground truth)."""

    radius: float
    half_height: float
    """Half the cylinder section length (total height = 2*half_height)."""

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

class UITransform:
    """Screen-space layout bounds for a UI element. Coords in [0,1], top-left origin."""

    min_x: float
    min_y: float
    max_x: float
    max_y: float
    depth: int
    visible: bool

class UIBackground:
    """Solid-color rectangle (pairs with UITransform). RGBA in [0,1]."""

    r: float
    g: float
    b: float
    a: float

class UIText:
    """Screen-space text label. Mutate `text` to update a HUD score/health readout."""

    text: str
    font: str
    """Asset-relative .ttf path; must be baked (e.g. "fonts/monaco.ttf")."""
    r: float
    g: float
    b: float
    a: float
    display_px: int
    """Glyph height in reference pixels (0 = native atlas size)."""
    alignment: int
    """0 = left, 1 = centered."""

class TextLabel3D:
    """World-space MSDF text anchored to a Transform."""

    text: str
    font: str
    r: float
    g: float
    b: float
    a: float
    size_meters: float
    """World height of one em."""
    billboard: int
    """Non-zero = always face the camera."""

class AudioSource:
    """Spatial audio emitter component (pairs with a Transform anchor)."""

    path: str
    """Asset-relative sound path (e.g. "audios/hum.ogg")."""
    gain: float
    pitch: float
    loop: bool
    autoplay: bool
    @property
    def source(self) -> Source | None:
        """The live OpenAL voice once bound/playing, else None. Read-only."""

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
    "UITransform",
    "UIBackground",
    "UIText",
    "TextLabel3D",
    "AudioSource",
    "Sleeping",
    "Fixed",
]
"""Component names accepted by view() / reg_get() / reg_has().

"Sleeping" and "Fixed" are zero-size tags: reg_has / is_sleeping / is_fixed report
presence, reg_get returns True/None. Prefer fix_entity / wake over reg_add'ing them.
"""

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
    def add_spring(self, a: Entity, b: Entity, k: float, rest: float) -> None:
        """Connect two bodies with a Hookean spring (stiffness k, rest length in meters).

        Returns nothing — remove later with `remove_spring_between(a, b)`.
        """
    def add_spring_with_proxies(
        self,
        a: Entity,
        b: Entity,
        k: float,
        rest: float,
        proxy_count: int,
        proxy_radius: float,
    ) -> None:
        """Spring plus procedural helix proxy spheres for visualization.

        Positional arguments only (no keyword names bound).
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
    def add_capsule(
        self, radius: float, half_height: float, mass: float, pos: Vec3 = ...
    ) -> Entity:
        """Spawn a dynamic capsule (GJK-only). No mesh — call `attach_capsule_mesh`."""
    def add_cylinder(
        self, radius: float, half_height: float, mass: float, pos: Vec3 = ...
    ) -> Entity:
        """Spawn a dynamic cylinder (GJK-only). No mesh — call `attach_cylinder_mesh`."""
    def attach_cylinder_mesh(
        self,
        entity: Entity,
        radius: float,
        half_height: float,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity a cylinder render mesh baked at (radius, half_height)."""
    def attach_sphere_collider(
        self, entity: Entity, mass: float, radius: float, static_: bool = False
    ) -> None:
        """Add a sphere physics body to an existing (e.g. visual-only) entity.

        No-ops if it already has a collider. Composition change — see the
        dangling-reference hazard at the top of this module.
        """
    def attach_aabb_collider(
        self, entity: Entity, mass: float, extent: Vec3, static_: bool = False
    ) -> None:
        """Add an axis-aligned-box body. `extent` is half-extents."""
    def attach_obb_collider(
        self, entity: Entity, mass: float, extent: Vec3, static_: bool = False
    ) -> None:
        """Add an oriented-box body. `extent` is half-extents."""
    def attach_capsule_collider(
        self,
        entity: Entity,
        mass: float,
        radius: float,
        half_height: float,
        static_: bool = False,
    ) -> None:
        """Add a capsule body (GJK)."""
    def attach_cylinder_collider(
        self,
        entity: Entity,
        mass: float,
        radius: float,
        half_height: float,
        static_: bool = False,
    ) -> None:
        """Add a cylinder body (GJK)."""
    def detach_physics_body(self, entity: Entity) -> None:
        """Remove all physics components (Hull + shape + Fixed/Sleeping tags)."""
    def set_mesh_visible(self, entity: Entity, visible: bool) -> None:
        """Show/hide an entity's render mesh without destroying it (blinking pickups)."""
    def remove_light(self, entity: Entity) -> None:
        """Destroy a light entity (the handle returned by `add_point_light`)."""
    def add_audio_source_entity(self, pos: Vec3 = ...) -> Entity:
        """Spawn an entity with an empty AudioSource (set its `path` via reg_get)."""
    def add_point_light(
        self, pos: Vec3, color: Vec3 = ..., intensity: float = 1.0
    ) -> Entity:
        """Spawn a point light (default attenuation). `color` RGB in [0,1]."""
    def add_ui_background(
        self, min: Vec2, max: Vec2, color: Vec4, depth: int = 0
    ) -> Entity:
        """Solid-color HUD rectangle. Coords in [0,1] screen percent, top-left origin."""
    def add_ui_curved_background(
        self, min: Vec2, max: Vec2, color: Vec4, curvature: float = 0.5, depth: int = 0
    ) -> Entity:
        """Rounded-corner HUD rectangle. `curvature` in [0,1]."""
    def add_ui_text(
        self, font: str, text: str, min: Vec2, max: Vec2, depth: int = 0
    ) -> Entity:
        """HUD text box. `font` is an asset-relative baked .ttf (e.g. "fonts/monaco.ttf").

        Mutate later with `yope.set_text(e, "...")` or `reg_get(e, "UIText").text`.
        """
    def add_text_label_3d(self, font: str, text: str, pos: Vec3) -> Entity:
        """World-space text label anchored at `pos` (Transform + TextLabel3D)."""
    def remove_spring_between(self, a: Entity, b: Entity) -> None:
        """Remove the first spring whose endpoints match {a, b} (either order)."""
    def set_paused(self, paused: bool) -> None:
        """Pause/resume the physics simulation (advance() becomes a no-op while paused)."""
    def apply_impulse(self, entity: Entity, impulse: Vec3) -> None:
        """Apply a linear impulse (kg·m/s) and wake the body. No-op on static bodies.

        Prefer this over writing `hull.velocity` directly: it also wakes a sleeping
        body, which a raw velocity write does not.
        """
    def apply_impulse_at(self, entity: Entity, impulse: Vec3, point: Vec3) -> None:
        """Apply an impulse at a world-space point — produces linear *and* angular change."""
    def wake(self, entity: Entity) -> None:
        """Remove the Sleeping tag so direct velocity writes take effect again."""

    debug_physics: bool
    """Toggle the physics debug overlay (collider wireframes)."""
    paused: bool
    """Whether the physics simulation is paused (same as set_paused)."""
    @property
    def tick_count(self) -> int:
        """Monotonic physics-tick counter (one per 240 Hz advance()). Read-only."""
    @property
    def layers(self) -> CollisionLayers:
        """The named collision-layer registry (read-only handle; call .add() on it)."""

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
    def look_at(self, target: Vec3) -> None:
        """Aim the camera at a world point (sets pitch/yaw; roll stays 0).

        Closes the loop on `yope.look_at` (which returns a Quat): this drives the
        Euler-angle camera directly, for orbit/cutscene scripts.
        """
    def screen_to_ray(self, px: float, py: float) -> tuple[Vec3, Vec3]:
        """Unproject a pixel (top-left origin) to a world ray `(origin, direction)`.

        For a **visible cursor** (menus / RTS-style picking), pair with
        `yope.window.get_cursor_pos()` + `yope.raycast`:

            px, py = yope.window.get_cursor_pos()
            o, d = yope.camera.screen_to_ray(px, py)
            hit, e, point, normal, t = yope.raycast(o, d, 100.0)

        NOTE: the default runtime is FPS mouselook (cursor locked/hidden), where
        `get_cursor_pos()` returns *unbounded virtual* coordinates — screen_to_ray
        is meaningless there. For a center-screen crosshair, ray from the camera
        directly instead:

            o, d = yope.camera.position, yope.camera.get_forward()
            hit, e, point, normal, t = yope.raycast(o, d, 100.0)
        """

class Window:
    """The application window — pixel size and cursor position."""

    def get_width(self) -> int: ...
    def get_height(self) -> int: ...
    def get_cursor_pos(self) -> tuple[float, float]:
        """Current cursor position in pixels (top-left origin).

        Only meaningful while the cursor is unlocked — under FPS mouselock GLFW
        reports an unbounded virtual position (see `set_cursor_locked`).
        """
    def set_cursor_locked(self, locked: bool) -> None:
        """Lock (hide + capture, FPS mouselook) or unlock (visible cursor) the mouse.

        Unlock before showing a pause menu or using cursor-based `screen_to_ray`
        picking; re-lock to return to mouselook. The runtime starts locked.
        """
    def is_cursor_locked(self) -> bool:
        """True while the cursor is captured for mouselook."""

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
    def is_mmb_down(self) -> bool:
        """Middle mouse button held."""
    def is_forward_mb_down(self) -> bool:
        """5th (forward) mouse button held."""
    def is_backward_mb_down(self) -> bool:
        """4th (backward) mouse button held."""
    def is_mouse_pressed(self, button: int) -> bool:
        """True only on the frame `button` went down. Use the `yope.MOUSE_*` constants."""
    def is_mouse_released(self, button: int) -> bool:
        """True only on the frame `button` went up."""
    def get_scroll_x(self) -> float:
        """Horizontal scroll accumulated since last frame."""
    def get_scroll_y(self) -> float:
        """Vertical scroll (wheel) accumulated since last frame."""
    def get_mouse_delta(self) -> tuple[float, float]:
        """Mouse movement (dx, dy) in pixels since last frame."""

class SoundBuffer:
    """Opaque decoded-audio buffer handle returned by `AudioSystem.load_sound`."""

class Source:
    """One OpenAL voice. Owned by the engine; this is a non-owning view.

    Reuse the handle returned by `yope.play_sound` to stop/reposition the sound.
    """

    def play(self) -> None: ...
    def pause(self) -> None: ...
    def stop(self) -> None: ...
    def rewind(self) -> None: ...
    def set_gain(self, gain: float) -> None: ...
    def set_pitch(self, pitch: float) -> None: ...
    def set_position(self, pos: Vec3) -> None:
        """World position for 3D spatialization (sources must be mono)."""
    def set_velocity(self, vel: Vec3) -> None:
        """Velocity for Doppler."""
    def set_reference_distance(self, dist: float) -> None: ...
    def enable_looping(self, loop: bool) -> None: ...
    def is_playing(self) -> bool: ...

class AudioSystem:
    def pause_all(self) -> None: ...
    def resume_all(self) -> None: ...
    def stop_all(self) -> None: ...
    def load_sound(self, path: str) -> SoundBuffer | None:
        """Decode + cache an OGG by asset-relative path. Deduped across calls."""
    def create_source(self, buffer: SoundBuffer) -> Source:
        """Create a playable voice bound to `buffer`. Prefer `yope.play_sound` for one-shots."""

class CollisionLayers:
    """Named 32-bit collision-layer registry (access via `yope.world.layers`)."""

    ALL: int
    NONE: int
    def add(self, name: str) -> int:
        """Register a new layer, returning its bitmask (1 << slot). Raises if full/dup."""
    def has(self, name: str) -> bool: ...
    def count(self) -> int: ...
    def __getitem__(self, name: str) -> int:
        """Bitmask for a registered layer (raises if unknown)."""

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
window: Window

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
def reg_get(e: Entity, name: Literal["CylinderForm"]) -> CylinderForm | None: ...
@overload
def reg_get(e: Entity, name: Literal["LightSource"]) -> LightSource | None: ...
@overload
def reg_get(e: Entity, name: Literal["Name"]) -> Name | None: ...
@overload
def reg_get(e: Entity, name: Literal["SpringConstraint"]) -> SpringConstraint | None: ...
@overload
def reg_get(e: Entity, name: Literal["ScriptComponent"]) -> ScriptComponent | None: ...
@overload
def reg_get(e: Entity, name: Literal["UITransform"]) -> UITransform | None: ...
@overload
def reg_get(e: Entity, name: Literal["UIBackground"]) -> UIBackground | None: ...
@overload
def reg_get(e: Entity, name: Literal["UIText"]) -> UIText | None: ...
@overload
def reg_get(e: Entity, name: Literal["TextLabel3D"]) -> TextLabel3D | None: ...
@overload
def reg_get(e: Entity, name: Literal["AudioSource"]) -> AudioSource | None: ...
@overload
def reg_get(e: Entity, name: Literal["Sleeping", "Fixed"]) -> bool | None: ...
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

def is_sleeping(e: Entity) -> bool:
    """True if the body is asleep (physics has parked it). `wake(e)` clears it.

    Also reachable via `reg_has(e, "Sleeping")`.
    """

def is_fixed(e: Entity) -> bool:
    """True if the body carries the Fixed (static) tag. See `fix_entity`.

    Also reachable via `reg_has(e, "Fixed")`.
    """

def reg_add(e: Entity, name: ComponentName) -> None:
    """Add a default-constructed component to an entity (raises if `name` unknown).

    Composition change — invalidates references held to this entity's components
    (see the dangling-reference hazard at the top of this module). Mutate the new
    component via `reg_get(e, name)` afterward.
    """

def reg_remove(e: Entity, name: ComponentName) -> None:
    """Remove a component from an entity (no-op if absent; raises if `name` unknown).

    Composition change — see the dangling-reference hazard.
    """

def find_entity(name: str) -> Entity | None:
    """First entity whose Name matches `name`, or None. Linear scan over all entities."""

def get_behavior(entity: Entity) -> Any | None:
    """The live Python behavior instance attached to `entity`, or None.

    Lets one behavior read/call another's state directly, e.g. in a collision
    callback: `hp = yope.get_behavior(other); hp.take_damage(10)`. Returns None
    if the entity has no script, or its script isn't a Python behavior.
    """

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

def raycast(
    origin: Vec3, dir: Vec3, max_dist: float, exclude: Entity | None = None
) -> tuple[bool, Entity | None, Vec3, Vec3, float]:
    """Cast a thin ray and return the nearest tangible hit within `max_dist`.

    `dir` need not be normalized; `t` is reported in world meters. Returns
    `(hit, entity, point, normal, t)` — `entity` is None on a miss. This is the
    spine of shooting, mouse-picking (with `camera.screen_to_ray`), and AI
    line-of-sight. Coverage: sphere / AABB / OBB bodies (capsule/cylinder not yet).
    """

def load_scene(path: str) -> None:
    """Shorthand for `yope.scene_manager.load_scene(path)`."""

# ---------------------------------------------------------------------------
# Convenience helpers
# ---------------------------------------------------------------------------

def play_sound(
    path: str, pos: Vec3 | None = None, gain: float = 1.0, loop: bool = False
) -> Source | None:
    """Load + create + play a sound in one call (footsteps, pickups, jumps).

    `path` is asset-relative (e.g. "audios/jump.ogg"). Pass `pos` for a 3D
    spatialized one-shot, or omit it for a non-positional sound. Returns the
    `Source` (keep it to stop/reposition) or None if audio isn't available.

    These voices are pooled: once the sound finishes, the engine may recycle the
    `Source` for a later play_sound call — so don't hold the handle past the end
    of the sound. For a long/looping sound you manage yourself, use
    `yope.audio.create_source(...)` instead.
    """

def set_text(entity: Entity, text: str) -> None:
    """Set the string of whichever text component the entity has (UIText or TextLabel3D)."""

def time() -> float:
    """Wall-clock seconds since engine startup (GLFW timer). For physics ticks use
    `yope.world.tick_count`."""

def draw_line(a: Vec3, b: Vec3, color: Vec3 | None = None) -> None:
    """Draw a world-space debug segment for this frame (always-on-top, default yellow).

    Cleared automatically each frame before scripts run, so call it from `update()`.
    Great for velocity vectors, AI paths, and visualizing `raycast` hits.
    """

def draw_ray(
    origin: Vec3, dir: Vec3, length: float = 1.0, color: Vec3 | None = None
) -> None:
    """Draw a debug ray from `origin` along `dir` for `length` meters (per-frame)."""

def get_position(entity: Entity) -> Vec3 | None:
    """Re-resolving read of the entity's Transform.position (None if no Transform).

    Safe to call after composition changes — it looks the component up per call.
    """

def set_position(entity: Entity, pos: Vec3) -> None:
    """Re-resolving write of the entity's Transform.position (no-op if no Transform)."""

def set_velocity(entity: Entity, velocity: Vec3) -> None:
    """Re-resolving write of the entity's Hull.velocity (no-op if no Hull).

    Note: writing velocity on a *sleeping* body has no visible effect until it is
    woken — use `yope.world.wake(e)` or `yope.world.apply_impulse(e, j)` instead.
    """

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

# Mouse button constants (for Input.is_mouse_pressed / is_mouse_released)
MOUSE_LEFT: int
MOUSE_RIGHT: int
MOUSE_MIDDLE: int
