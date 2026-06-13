"""Yope3D embedded scripting API (the ``yope3d`` module).

``yope3d`` is a pybind11 *embedded* module: it exists only inside the running
engine process and cannot be ``import``-ed by a stand-alone Python interpreter.
This stub file is what gives editors (VS Code / Pylance) completions,
signatures, and these docs. It is hand-maintained against the C++ bindings in
``src/scripting/python/bindings_{math,ecs,world}.cpp`` — if you change a
binding, update this file to match.

Table of contents
------------------
The file is divided into ``====``-bannered sections; jump to a banner to find
a group of related symbols:

    Conventions ............. units, coordinate system, references
    Hazards ................. read this before writing a behavior
    Behavior scripts ........ the class contract the engine calls
    Examples ................ copy-paste recipes pulled from scripts/
    Support modules ......... pure-Python helpers in scripts/behaviors/
    --- API surface ---
    Math types .............. Vec2/Vec3/Vec4/Quat + free functions
    ECS core ................ Entity, Registry
    Components .............. live references returned by view()/reg_get()
    Engine singletons ....... world, camera, input, audio, window, ...
    ECS queries ............. view, reg_get/has/add/remove, find_entity
    Kinematic queries ....... raycast, capsule_overlap, capsule_cast
    Convenience helpers ..... play_sound, draw_line, get/set_position, ...
    Key/mouse constants ..... KEY_*, MOUSE_*

================================================================================
Conventions
================================================================================
- Units are meters / seconds / kilograms / radians unless noted.
- The coordinate system is right-handed, +Y up. Capsules and cylinders are
  axis-aligned to +Y in local space.
- ``extent`` fields are **half**-extents. ``half_height`` is half the cylinder
  section length (capsule total height = ``2*half_height + 2*radius``).
- Colors are RGB(A) floats in ``[0, 1]``.
- All component objects returned by ``view()`` / ``reg_get()`` are *references*
  into engine memory: mutations apply immediately, but the reference dies with
  the entity (see Hazards).

================================================================================
Hazards
================================================================================
These are the four ways scripts most commonly break. Each affected binding
also carries a ``Warning:`` block in its own docstring.

1. Dangling references (composition changes)
    The ECS is archetype-based, so adding or removing *any* component on an
    entity memcpy-moves its existing components to a new archetype block —
    silently invalidating every reference you already hold to that entity's
    components. Never cache a ``view()`` / ``reg_get()`` result across a call
    that can change entity composition:

        reg_add, reg_remove, remove_entity, the attach_*_collider /
        detach_physics_body helpers, fix_entity, wake, or anything that
        adds/removes a tag.

    Re-fetch after such calls, or use the re-resolving helpers ``get_position``
    / ``set_position`` / ``set_velocity``, which look the component up per call.

2. Threading
    Scripts run on the main thread between physics ticks. Calling into ``yope3d``
    from your own Python threads is not supported. Every ``yope3d`` call that
    touches the registry — reads (``view``, ``reg_get`` / ``reg_has``,
    ``find_entity``, ``get_position``, ``raycast``, ``capsule_*``, …) as well
    as mutators — takes the engine's structure lock internally, so each
    individual call is safe while the 240 Hz physics thread runs (it may block
    for up to ~one physics tick). The lock is per-call: a reference returned by
    ``reg_get`` / ``view`` is only guaranteed valid until your next
    composition-changing call (hazard #1).

3. Writing velocity on a sleeping body
    A raw ``hull.velocity = ...`` (or ``set_velocity``) on a parked body has no
    visible effect until it is woken. Use ``world.apply_impulse`` (which wakes
    the body) or call ``world.wake`` first.

4. Pooled audio voices
    The ``Source`` returned by ``play_sound`` is pooled: once the sound
    finishes, the engine may recycle it for a later call. Don't hold the handle
    past the end of the sound. For a long/looping sound you own, use
    ``audio.create_source(...)`` instead.

5. FPS mouselook vs. cursor position
    The runtime starts with the cursor locked (hidden, FPS mouselook). While
    locked, ``window.get_cursor_pos()`` returns *unbounded virtual*
    coordinates, so ``camera.screen_to_ray`` is meaningless. Unlock with
    ``window.set_cursor_locked(False)`` before cursor-based picking, or ray
    from the camera directly for a center-screen crosshair.

================================================================================
Behavior scripts
================================================================================
A behavior is a class in ``scripts/behaviors/*.py`` with a class-level
``PARAMS`` dict; the editor harvests it for the ScriptComponent inspector::

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

- ``init`` runs once when play mode starts. ``params`` holds the
  inspector-edited values (fall back to ``PARAMS`` defaults via
  ``params.get(...)``).
- ``update`` runs every frame; ``dt`` is the frame delta in seconds.
- ``on_collision_enter`` / ``on_collision_exit`` fire when this entity *starts*
  or *stops* touching another tangible body — ``entity`` is yours, ``other`` is
  the body you hit. They are dispatched on the main thread once per frame from
  physics events, so it is safe to spawn/destroy entities inside them. The
  entity needs a collider to receive these (a behavior on a mesh-only entity
  never collides). Note: an entity that gains a ScriptComponent while already
  in contact won't get an enter until separation + recontact.
- One ScriptComponent per entity. For a real game, split logic across entities
  (e.g. CharacterController on the player capsule, gameplay behaviors on the
  objects) and reach across with ``yope3d.get_behavior(other)``.

================================================================================
Examples
================================================================================
Recipes distilled from ``scripts/behaviors/``. Each is a fragment — drop it
into a behavior class.

Reading input + driving a dynamic rigid body (velocity-based movement)::

    SENS, SPEED, DAMP, JUMP = 0.002, 1.4, 0.85, 10.0

    def update(self, world, entity, dt):
        inp  = yope3d.input
        hull = yope3d.reg_get(entity, "Hull")
        tf   = yope3d.reg_get(entity, "Transform")
        if hull is None or tf is None:
            return

        dx, dy = inp.get_mouse_delta()
        self.yaw   -= dx * SENS
        self.pitch  = yope3d.clamp(self.pitch - dy * SENS, -1.4, 1.4)
        yope3d.camera.set_rotation(yope3d.Vec3(self.pitch, self.yaw, 0))

        cy, sy = math.cos(self.yaw), math.sin(self.yaw)
        fwd, right = yope3d.Vec3(-sy, 0, -cy), yope3d.Vec3(cy, 0, -sy)
        move = yope3d.Vec3(0, 0, 0)
        if inp.is_key_down(yope3d.KEY_W): move += fwd
        if inp.is_key_down(yope3d.KEY_S): move -= fwd
        if inp.is_key_down(yope3d.KEY_D): move += right
        if inp.is_key_down(yope3d.KEY_A): move -= right
        n = move.length()
        if n > 1e-4:
            move = move * (SPEED / n)
        hull.velocity.x = hull.velocity.x * DAMP + move.x
        hull.velocity.z = hull.velocity.z * DAMP + move.z
        if inp.is_key_pressed(yope3d.KEY_SPACE):
            hull.velocity.y = JUMP

Spawning a body at runtime (rate-limited)::

    def update(self, world, entity, dt):
        self.cooldown = max(0.0, self.cooldown - dt)
        if yope3d.input.is_key_pressed(yope3d.KEY_SPACE) and self.cooldown <= 0.0:
            pos = yope3d.camera.position + yope3d.camera.get_forward() * 3.0
            e = world.add_sphere(1.0, 0.5, pos)      # mass, radius, pos
            world.attach_sphere_mesh(e, 0.5, 0.9, 0.5, 0.2)  # radius, r, g, b
            self.cooldown = 0.15

Crosshair raycast (shooting / picking)::

    def update(self, world, entity, dt):
        o, d = yope3d.camera.position, yope3d.camera.get_forward()
        hit, e, point, normal, t = yope3d.raycast(o, d, 100.0, entity)
        if hit:
            yope3d.draw_line(point - yope3d.Vec3(0.2, 0, 0),
                           point + yope3d.Vec3(0.2, 0, 0), yope3d.Vec3(0.2, 0.9, 1))
        if yope3d.input.is_mouse_pressed(yope3d.MOUSE_LEFT) and hit:
            world.apply_impulse(e, d * 8.0)   # impulse also wakes the body

Collision events + cross-behavior messaging::

    def on_collision_enter(self, world, entity, other):
        world.set_mesh_color(entity, 0.2, 1.0, 0.2)
        hp = yope3d.get_behavior(other)         # other entity's live instance
        if hp is not None:
            hp.take_damage(10)

HUD readout + 3D world label::

    def init(self, world, entity, params):
        font = "fonts/monaco.ttf"             # must be a baked font
        self.score = 0
        self.hud = world.add_ui_text(font, "Score: 0",
                                     yope3d.Vec2(0.02, 0.02), yope3d.Vec2(0.4, 0.1))
        world.add_text_label_3d(font, "spawn!", yope3d.Vec3(0, 5, 0))

    def update(self, world, entity, dt):
        yope3d.set_text(self.hud, f"Score: {self.score}")

Kinematic capsule controller (no Hull — drive the Transform, resolve overlaps)::

    def _resolve(self, pos, exclude):
        for _ in range(3):                    # iterate to settle stacked contacts
            contacts = yope3d.capsule_overlap(pos, self.r, self.hh, exclude)
            if not contacts:
                break
            for n, depth in contacts:         # push out along each normal
                pos = pos + n * depth
        return pos

    def _grounded(self, pos, exclude):
        t, hit, normal = yope3d.capsule_cast(
            pos, self.r, self.hh, yope3d.Vec3(0, -1, 0), 0.1, exclude)
        return hit and normal.y > 0.7         # walkable if near-vertical normal

================================================================================
Support modules
================================================================================
Pure-Python helpers living in ``scripts/behaviors/`` (no C++ involved):

- ``_events`` — tiny emit/subscribe bus for inter-behavior messaging
  (``subscribe(name, fn)`` / ``emit(name, *args)`` / ``unsubscribe`` / ``clear``).
- ``_timers`` — ``after(s, fn)``, ``every(s, fn)``, ``cancel(handle)``, and
  ``yield``-based coroutines via ``start(gen)``. Drive it by calling
  ``_timers.tick(dt)`` once per frame from exactly one behavior.
- ``_debug`` — ``draw_aabb(center, half, color)``, ``draw_sphere(pos, radius)``,
  ``draw_cross(point, color)`` wireframes layered over ``yope3d.draw_line``.

Import them with ``from behaviors import _events, _timers, _debug`` and
unsubscribe / cancel in ``on_unload`` to avoid stale callbacks.
"""

from typing import Any, Final, Literal, overload

# ==============================================================================
# Semantic type aliases (documentary — each resolves to its base type, but the
# Literals also let a type checker flag out-of-range enum assignments).
# ==============================================================================

LightType = Literal[0, 1, 2, 3]
"""Light kind: ``0`` Point, ``1`` Directional, ``2`` Spot, ``3`` Flash."""

TextAlign = Literal[0, 1]
"""Text alignment: ``0`` left, ``1`` centered."""

# ==============================================================================
# Math types
# ==============================================================================

class Vec2:
    """2D float vector.

    Supports ``+``, ``-``, ``*`` (scalar, either side), and unary ``-``.
    """

    x: float
    y: float
    def __init__(self, x: float = 0.0, y: float = 0.0) -> None: ...
    def __add__(self, other: Vec2) -> Vec2: ...
    def __sub__(self, other: Vec2) -> Vec2: ...
    def __mul__(self, s: float) -> Vec2: ...
    def __rmul__(self, s: float) -> Vec2: ...
    def __neg__(self) -> Vec2: ...
    def length(self) -> float:
        """Return the Euclidean length."""
    def normalize(self) -> Vec2:
        """Return a unit-length copy (does not modify ``self``)."""

class Vec3:
    """3D float vector.

    Supports ``+``, ``-``, ``*`` (scalar, either side), unary ``-``, and the
    in-place ``+=`` / ``-=`` operators.
    """

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
        """Return the dot product with ``other``."""
    def cross(self, other: Vec3) -> Vec3:
        """Return the right-handed cross product ``self × other``."""
    def length(self) -> float:
        """Return the Euclidean length."""
    def normalize(self) -> Vec3:
        """Return a unit-length copy (does not modify ``self``)."""

class Vec4:
    """4D float vector (no arithmetic operators are bound)."""

    x: float
    y: float
    z: float
    w: float
    def __init__(
        self, x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 0.0
    ) -> None: ...

class Quat:
    """Rotation quaternion ``(x, y, z, w)``. Default-constructs to identity."""

    x: float
    y: float
    z: float
    w: float
    def __init__(
        self, x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 1.0
    ) -> None: ...
    @staticmethod
    def from_axis_angle(axis: Vec3, radians: float) -> Quat:
        """Build a quaternion rotating ``radians`` around ``axis``.

        Args:
            axis: Rotation axis (need not be unit length).
            radians: Rotation angle in radians.

        Returns:
            The rotation quaternion.
        """
    @staticmethod
    def from_euler(pitch: float, yaw: float, roll: float = 0.0) -> Quat:
        """Build a quaternion from Tait-Bryan angles.

        The composition is ``yaw(Y) · pitch(X) · roll(Z)``, matching the engine's
        FPS yaw/pitch convention.

        Args:
            pitch: Rotation about local X, in radians.
            yaw: Rotation about local Y, in radians.
            roll: Rotation about local Z, in radians.

        Returns:
            The rotation quaternion.
        """
    @staticmethod
    def slerp(a: Quat, b: Quat, t: float) -> Quat:
        """Spherically interpolate between two orientations.

        Args:
            a: Start orientation.
            b: End orientation.
            t: Interpolation factor in ``[0, 1]``.

        Returns:
            The interpolated orientation.
        """
    def __mul__(self, other: Quat) -> Quat:
        """Compose rotations via the Hamilton product (``self`` applied after ``other``)."""
    def rotate(self, vec: Vec3) -> Vec3:
        """Rotate ``vec`` by this quaternion and return the result."""

def look_at(forward: Vec3, up: Vec3 = ...) -> Quat:
    """Build a quaternion whose local +Z points along ``forward``.

    Args:
        forward: Desired forward direction.
        up: Approximate up direction (defaults to world +Y).

    Returns:
        The orienting quaternion.

    Note:
        The engine camera's forward is -Z, so negate ``forward`` when orienting
        something *toward* the camera.
    """

PI: Final[float]
"""The constant pi."""

def to_radians(degrees: float) -> float:
    """Convert ``degrees`` to radians."""

def to_degrees(radians: float) -> float:
    """Convert ``radians`` to degrees."""

def clamp(v: float, lo: float, hi: float) -> float:
    """Clamp ``v`` into the closed interval ``[lo, hi]``."""

def lerp(a: float, b: float, t: float) -> float:
    """Linearly interpolate: ``a + (b - a) * t``. ``t`` is not clamped."""

# ==============================================================================
# ECS core
# ==============================================================================

class Entity:
    """Opaque entity handle: ``(id, generation)``.

    The generation increments when an id is reused, so stale handles compare
    unequal to the new entity. Hashable — usable as dict keys / in sets. Check
    liveness with ``yope3d.reg_valid(e)``.
    """

    @property
    def id(self) -> int:
        """Dense integer id (reused across entity lifetimes)."""
    @property
    def generation(self) -> int:
        """Reuse counter; distinguishes a recycled id from the original."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class Registry:
    """Opaque handle to the ECS registry.

    No methods are bound. Use the module-level helpers ``yope3d.view()``,
    ``yope3d.reg_get()``, ``yope3d.reg_has()``, ``yope3d.reg_valid()`` instead.
    """

# ==============================================================================
# Components (live references into engine memory — edits apply immediately)
# ==============================================================================

class Transform:
    """Position + rotation + scale. Every renderable/physical entity has one."""

    position: Vec3
    rotation: Quat
    scale: Vec3
    """Render scale.

    Note:
        Cylinders encode ``(radius, halfHeight, radius)`` here; capsules use a
        baked mesh and keep scale at ``(1, 1, 1)``.
    """

class Hull:
    """Rigid-body dynamic state (paired with one shape Form component).

    Owned by the 240 Hz physics thread during ``World::advance()``; scripts
    mutate it between ticks (the normal ``update()`` path is safe).

    Warning:
        Writing ``velocity`` on a *sleeping* body has no visible effect until it
        is woken. Prefer ``world.apply_impulse`` (which wakes the body) or call
        ``world.wake`` first (Hazard #3).
    """

    velocity: Vec3
    """Linear velocity (m/s)."""
    omega: Vec3
    """Angular velocity (rad/s, world space)."""
    mass: float
    """Mass in kg. ``0`` = infinite mass (static); prefer ``World.fix_entity``."""
    linear_damping: float
    """Per-second linear velocity decay factor."""
    angular_damping: float
    """Per-second angular velocity decay factor."""
    friction: float
    """Coulomb friction coefficient used by the PGS solver."""
    restitution: float
    """Bounciness in ``[0, 1]``."""
    gravity: bool
    """Whether world gravity is applied to this body."""
    tangible: bool
    """If ``False`` the body is ignored by collision detection (still integrates)."""
    collision_layer: int
    """Bitmask of layers this body belongs to (see ``yope3d.world.layers``)."""
    collision_mask: int
    """Bitmask of layers this body collides with. Both directions must match to collide."""

class SphereForm:
    """Sphere collision shape."""

    radius: float

class AABBForm:
    """Axis-aligned box. ``extent`` is half-extents; rotation is always identity."""

    extent: Vec3

class OBBForm:
    """Oriented box. ``extent`` is half-extents; rotation lives in the Transform."""

    extent: Vec3

class CapsuleForm:
    """Capsule aligned to local +Y. Total height = ``2*half_height + 2*radius``."""

    radius: float
    half_height: float
    """Half the cylindrical section length (excludes the hemispherical caps)."""

class CylinderForm:
    """Flat-capped cylinder aligned to local +Y. GJK-only (no SAT ground truth)."""

    radius: float
    half_height: float
    """Half the cylinder section length (total height = ``2*half_height``)."""

class LightSource:
    """A light emitter component."""

    type: LightType
    """Light kind (see ``LightType``): 0=Point, 1=Directional, 2=Spot, 3=Flash."""
    intensity: float
    color: Vec3
    """RGB in ``[0, 1]``."""
    position: Vec3
    direction: Vec3

class Name:
    """Editor-visible entity name (fixed-size buffer; long names truncate)."""

    value: str

class SpringConstraint:
    """A Hookean spring endpoint record."""

    target: Entity
    k: float
    """Hookean stiffness."""
    rest_length: float
    """Natural length in meters."""

class ScriptComponent:
    """Attaches a Python behavior class to an entity."""

    script_class: str
    """Class name of a behavior in ``scripts/behaviors/`` (e.g. ``"CharacterController"``)."""
    params_blob: str
    """Serialized parameter values edited in the inspector."""

class UITransform:
    """Screen-space layout bounds for a UI element.

    Coords are in ``[0, 1]`` with a top-left origin.
    """

    min_x: float
    min_y: float
    max_x: float
    max_y: float
    depth: int
    """Sort order; higher draws on top."""
    visible: bool

class UIBackground:
    """Solid-color rectangle (pairs with ``UITransform``). RGBA in ``[0, 1]``."""

    r: float
    g: float
    b: float
    a: float

class UIText:
    """Screen-space text label.

    Mutate ``text`` to update a HUD score/health readout (or call
    ``yope3d.set_text``).
    """

    text: str
    font: str
    """Asset-relative ``.ttf`` path; must be baked (e.g. ``"fonts/monaco.ttf"``)."""
    r: float
    g: float
    b: float
    a: float
    display_px: int
    """Glyph height in reference pixels (``0`` = native atlas size)."""
    alignment: TextAlign
    """Horizontal alignment (see ``TextAlign``): 0=left, 1=centered."""

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
    """Asset-relative sound path (e.g. ``"audios/hum.ogg"``)."""
    gain: float
    pitch: float
    loop: bool
    autoplay: bool
    """Fires only in play mode (not on scene load)."""
    @property
    def source(self) -> Source | None:
        """The live OpenAL voice once bound/playing, else ``None``. Read-only."""

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
"""Component names accepted by ``view()`` / ``reg_get()`` / ``reg_has()``.

``"Sleeping"`` and ``"Fixed"`` are zero-size tags: ``reg_has`` / ``is_sleeping``
/ ``is_fixed`` report presence, and ``reg_get`` returns ``True`` / ``None``.
Prefer ``fix_entity`` / ``wake`` over ``reg_add``-ing them.
"""

# ==============================================================================
# Engine singletons (bound by the engine before any script runs)
# ==============================================================================

class World:
    """Entity/physics factory and queries. Access via the ``yope3d.world`` singleton."""

    gravity: Vec3
    """World gravity in m/s² (default ``(0, -9.8…, 0)``)."""

    debug_physics: bool
    """Toggle the physics debug overlay (collider wireframes)."""
    paused: bool
    """Whether the physics simulation is paused (same as ``set_paused``)."""

    @property
    def tick_count(self) -> int:
        """Monotonic physics-tick counter (one per 240 Hz ``advance()``). Read-only."""
    @property
    def layers(self) -> CollisionLayers:
        """The named collision-layer registry (read-only handle; call ``.add()`` on it)."""

    # ------------------------------------------------------------------ #
    # Spawning rigid bodies (no mesh attached — call an attach_* after)
    # ------------------------------------------------------------------ #

    def add_sphere(self, mass: float, radius: float, pos: Vec3 = ...) -> Entity:
        """Spawn a dynamic sphere body (Transform + Hull + SphereForm).

        Args:
            mass: Body mass in kg.
            radius: Sphere radius in meters.
            pos: Initial world position (defaults to the origin).

        Returns:
            The new entity. No render mesh is attached — call
            ``attach_sphere_mesh`` after.
        """
    def add_obb(self, extent: Vec3, mass: float, pos: Vec3 = ...) -> Entity:
        """Spawn a dynamic oriented box (Transform + Hull + OBBForm).

        Args:
            extent: Box half-extents.
            mass: Body mass in kg.
            pos: Initial world position (defaults to the origin).

        Returns:
            The new entity. No render mesh is attached — call ``attach_box_mesh``
            after.
        """
    def add_aabb(self, extent: Vec3, mass: float, pos: Vec3 = ...) -> Entity:
        """Spawn a dynamic axis-aligned box (never rotates).

        Args:
            extent: Box half-extents.
            mass: Body mass in kg.
            pos: Initial world position (defaults to the origin).

        Returns:
            The new entity.
        """
    def add_static_aabb(self, pos: Vec3, extent: Vec3) -> Entity:
        """Spawn an immovable axis-aligned box (Fixed tag, infinite mass).

        Args:
            pos: World position.
            extent: Box half-extents.

        Returns:
            The new entity.
        """
    def add_capsule(
        self, radius: float, half_height: float, mass: float, pos: Vec3 = ...
    ) -> Entity:
        """Spawn a dynamic capsule (GJK-only).

        Args:
            radius: Capsule radius.
            half_height: Half the cylindrical section length.
            mass: Body mass in kg.
            pos: Initial world position (defaults to the origin).

        Returns:
            The new entity. No mesh — call ``attach_capsule_mesh``.
        """
    def add_cylinder(
        self, radius: float, half_height: float, mass: float, pos: Vec3 = ...
    ) -> Entity:
        """Spawn a dynamic cylinder (GJK-only).

        Args:
            radius: Cylinder radius.
            half_height: Half the cylinder section length.
            mass: Body mass in kg.
            pos: Initial world position (defaults to the origin).

        Returns:
            The new entity. No mesh — call ``attach_cylinder_mesh``.
        """
    def add_kinematic_capsule(
        self, radius: float, half_height: float, pos: Vec3 = ...
    ) -> Entity:
        """Spawn a kinematic capsule: Transform + CapsuleForm, **no Hull**.

        The physics sim ignores it; drive its Transform directly (this is the
        character-controller body). Query the world with ``capsule_overlap`` /
        ``capsule_cast``.

        Args:
            radius: Capsule radius.
            half_height: Half the cylindrical section length.
            pos: Initial world position (defaults to the origin).

        Returns:
            The new entity.
        """

    # ------------------------------------------------------------------ #
    # Attaching render meshes
    # ------------------------------------------------------------------ #

    def attach_sphere_mesh(
        self,
        entity: Entity,
        radius: float,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity an icosphere render mesh with a flat color.

        Args:
            entity: Target entity.
            radius: Sphere radius in meters.
            r: Red in ``[0, 1]``.
            g: Green in ``[0, 1]``.
            b: Blue in ``[0, 1]``.
        """
    def attach_box_mesh(
        self,
        entity: Entity,
        half: Vec3,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity a box render mesh.

        Args:
            entity: Target entity.
            half: Box half-extents.
            r: Red in ``[0, 1]``.
            g: Green in ``[0, 1]``.
            b: Blue in ``[0, 1]``.
        """
    def attach_capsule_mesh(
        self,
        entity: Entity,
        radius: float,
        half_height: float,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity a capsule render mesh baked at ``(radius, half_height)``.

        Args:
            entity: Target entity.
            radius: Capsule radius.
            half_height: Half the cylindrical section length.
            r: Red in ``[0, 1]``.
            g: Green in ``[0, 1]``.
            b: Blue in ``[0, 1]``.
        """
    def attach_cylinder_mesh(
        self,
        entity: Entity,
        radius: float,
        half_height: float,
        r: float = 1.0,
        g: float = 1.0,
        b: float = 1.0,
    ) -> None:
        """Give an entity a cylinder render mesh baked at ``(radius, half_height)``.

        Args:
            entity: Target entity.
            radius: Cylinder radius.
            half_height: Half the cylinder section length.
            r: Red in ``[0, 1]``.
            g: Green in ``[0, 1]``.
            b: Blue in ``[0, 1]``.
        """
    def set_mesh_color(self, entity: Entity, r: float, g: float, b: float) -> None:
        """Recolor an already-attached render mesh (RGB in ``[0, 1]``)."""
    def set_mesh_visible(self, entity: Entity, visible: bool) -> None:
        """Show/hide an entity's render mesh without destroying it (e.g. blinking pickups)."""

    # ------------------------------------------------------------------ #
    # Attaching / removing colliders on existing entities
    #
    # Warning: every call here is a composition change — it invalidates
    # references already held to this entity's components (Hazard #1).
    # ------------------------------------------------------------------ #

    def attach_sphere_collider(
        self, entity: Entity, mass: float, radius: float, static_: bool = False
    ) -> None:
        """Add a sphere physics body to an existing (e.g. visual-only) entity.

        No-ops if it already has a collider.

        Args:
            entity: Target entity.
            mass: Body mass in kg (ignored when ``static_``).
            radius: Sphere radius.
            static_: If ``True``, create an immovable (Fixed) body.

        Warning:
            Composition change — see Hazard #1.
        """
    def attach_aabb_collider(
        self, entity: Entity, mass: float, extent: Vec3, static_: bool = False
    ) -> None:
        """Add an axis-aligned-box body.

        Args:
            entity: Target entity.
            mass: Body mass in kg (ignored when ``static_``).
            extent: Box half-extents.
            static_: If ``True``, create an immovable (Fixed) body.

        Warning:
            Composition change — see Hazard #1.
        """
    def attach_obb_collider(
        self, entity: Entity, mass: float, extent: Vec3, static_: bool = False
    ) -> None:
        """Add an oriented-box body.

        Args:
            entity: Target entity.
            mass: Body mass in kg (ignored when ``static_``).
            extent: Box half-extents.
            static_: If ``True``, create an immovable (Fixed) body.

        Warning:
            Composition change — see Hazard #1.
        """
    def attach_capsule_collider(
        self,
        entity: Entity,
        mass: float,
        radius: float,
        half_height: float,
        static_: bool = False,
    ) -> None:
        """Add a capsule body (GJK).

        Args:
            entity: Target entity.
            mass: Body mass in kg (ignored when ``static_``).
            radius: Capsule radius.
            half_height: Half the cylindrical section length.
            static_: If ``True``, create an immovable (Fixed) body.

        Warning:
            Composition change — see Hazard #1.
        """
    def attach_cylinder_collider(
        self,
        entity: Entity,
        mass: float,
        radius: float,
        half_height: float,
        static_: bool = False,
    ) -> None:
        """Add a cylinder body (GJK).

        Args:
            entity: Target entity.
            mass: Body mass in kg (ignored when ``static_``).
            radius: Cylinder radius.
            half_height: Half the cylinder section length.
            static_: If ``True``, create an immovable (Fixed) body.

        Warning:
            Composition change — see Hazard #1.
        """
    def detach_physics_body(self, entity: Entity) -> None:
        """Remove all physics components (Hull + shape + Fixed/Sleeping tags).

        Warning:
            Composition change — see Hazard #1.
        """
    def fix_entity(self, entity: Entity) -> None:
        """Pin a body in place: zero mass/velocity, disable gravity, add the Fixed tag.

        Useful for spring-cloth anchors and static obstacles created from script.

        Warning:
            Composition change (adds a tag) — see Hazard #1.
        """

    # ------------------------------------------------------------------ #
    # Springs
    # ------------------------------------------------------------------ #

    def add_spring(self, a: Entity, b: Entity, k: float, rest: float) -> None:
        """Connect two bodies with a Hookean spring.

        Args:
            a: First endpoint.
            b: Second endpoint.
            k: Stiffness.
            rest: Rest length in meters.

        Note:
            Remove later with ``remove_spring_between(a, b)``.
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
        """Add a spring plus procedural helix proxy spheres for visualization.

        Args:
            a: First endpoint.
            b: Second endpoint.
            k: Stiffness.
            rest: Rest length in meters.
            proxy_count: Number of helix proxy spheres.
            proxy_radius: Radius of each proxy sphere.

        Note:
            Positional arguments only (no keyword names are bound).
        """
    def remove_spring_between(self, a: Entity, b: Entity) -> None:
        """Remove the first spring whose endpoints match ``{a, b}`` (either order)."""

    # ------------------------------------------------------------------ #
    # Lights
    # ------------------------------------------------------------------ #

    def add_point_light(
        self, pos: Vec3, color: Vec3 = ..., intensity: float = 1.0
    ) -> Entity:
        """Spawn a point light (default attenuation).

        Args:
            pos: World position.
            color: RGB in ``[0, 1]`` (defaults to white).
            intensity: Brightness multiplier.

        Returns:
            The new light entity (pass to ``remove_light`` to destroy).
        """
    def remove_light(self, entity: Entity) -> None:
        """Destroy a light entity (the handle returned by ``add_point_light``)."""

    # ------------------------------------------------------------------ #
    # Audio
    # ------------------------------------------------------------------ #

    def add_audio_source_entity(self, pos: Vec3 = ...) -> Entity:
        """Spawn an entity with an empty ``AudioSource``.

        Args:
            pos: World position (defaults to the origin).

        Returns:
            The new entity. Set its ``path`` via ``reg_get(e, "AudioSource")``.
        """

    # ------------------------------------------------------------------ #
    # UI / HUD (coords in [0,1] screen percent, top-left origin)
    # ------------------------------------------------------------------ #

    def add_ui_background(
        self, min: Vec2, max: Vec2, color: Vec4, depth: int = 0
    ) -> Entity:
        """Add a solid-color HUD rectangle.

        Args:
            min: Top-left corner in ``[0, 1]`` screen percent.
            max: Bottom-right corner in ``[0, 1]`` screen percent.
            color: RGBA in ``[0, 1]``.
            depth: Sort order; higher draws on top.

        Returns:
            The new UI entity.
        """
    def add_ui_curved_background(
        self, min: Vec2, max: Vec2, color: Vec4, curvature: float = 0.5, depth: int = 0
    ) -> Entity:
        """Add a rounded-corner HUD rectangle.

        Args:
            min: Top-left corner in ``[0, 1]`` screen percent.
            max: Bottom-right corner in ``[0, 1]`` screen percent.
            color: RGBA in ``[0, 1]``.
            curvature: Corner rounding in ``[0, 1]``.
            depth: Sort order; higher draws on top.

        Returns:
            The new UI entity.
        """
    def add_ui_text(
        self, font: str, text: str, min: Vec2, max: Vec2, depth: int = 0
    ) -> Entity:
        """Add a HUD text box.

        Args:
            font: Asset-relative baked ``.ttf`` (e.g. ``"fonts/monaco.ttf"``).
            text: Initial string.
            min: Top-left corner in ``[0, 1]`` screen percent.
            max: Bottom-right corner in ``[0, 1]`` screen percent.
            depth: Sort order; higher draws on top.

        Returns:
            The new UI entity. Mutate later with ``yope3d.set_text(e, "...")`` or
            ``reg_get(e, "UIText").text``.
        """
    def add_text_label_3d(self, font: str, text: str, pos: Vec3) -> Entity:
        """Add a world-space text label anchored at ``pos`` (Transform + TextLabel3D).

        Args:
            font: Asset-relative baked ``.ttf`` (e.g. ``"fonts/monaco.ttf"``).
            text: Initial string.
            pos: World position of the anchor.

        Returns:
            The new entity.
        """

    # ------------------------------------------------------------------ #
    # Simulation control + forces
    # ------------------------------------------------------------------ #

    def apply_impulse(self, entity: Entity, impulse: Vec3) -> None:
        """Apply a linear impulse (kg·m/s) and wake the body. No-op on static bodies.

        Args:
            entity: Target body.
            impulse: World-space linear impulse.

        Note:
            Prefer this over writing ``hull.velocity`` directly: it also wakes a
            sleeping body, which a raw velocity write does not (Hazard #3).
        """
    def apply_impulse_at(self, entity: Entity, impulse: Vec3, point: Vec3) -> None:
        """Apply an impulse at a world-space point — produces linear *and* angular change.

        Args:
            entity: Target body.
            impulse: World-space linear impulse (kg·m/s).
            point: World-space application point.
        """
    def wake(self, entity: Entity) -> None:
        """Remove the Sleeping tag so direct velocity writes take effect again.

        Warning:
            Composition change (removes a tag) — see Hazard #1.
        """
    def set_paused(self, paused: bool) -> None:
        """Pause/resume the physics simulation (``advance()`` becomes a no-op while paused)."""
    def reset_physics(self) -> None:
        """Clear all velocities/contacts and wake everything."""

    # ------------------------------------------------------------------ #
    # Lifecycle + bookkeeping
    # ------------------------------------------------------------------ #

    def remove_entity(self, e: Entity) -> None:
        """Destroy an entity, purging its springs, contact-cache entries, and mesh.

        Warning:
            Invalidates any handle/reference to this entity — see Hazard #1.
        """
    def get_hull_count(self) -> int:
        """Return the number of rigid bodies (Hull components) in the world."""
    def get_island_count(self) -> int:
        """Return the number of contact islands in the last physics step."""
    def get_registry(self) -> Registry:
        """Return the ECS registry handle (for use with the module-level helpers)."""

class Camera:
    """The render camera. Access via the ``yope3d.camera`` singleton."""

    position: Vec3
    rotation: Vec3
    """Euler angles in radians (pitch/yaw/roll as a Vec3)."""

    def set_position(self, p: Vec3) -> None:
        """Set the camera world position."""
    def set_rotation(self, r: Vec3) -> None:
        """Set the Euler rotation (pitch/yaw/roll) in radians."""
    def set_fov(self, fov: float) -> None:
        """Set the vertical field of view in degrees."""
    def get_forward(self) -> Vec3:
        """Return the camera forward direction in world space (unit length)."""
    def look_at(self, target: Vec3) -> None:
        """Aim the camera at a world point (sets pitch/yaw; roll stays 0).

        Args:
            target: World point to look at.

        Note:
            Complements ``yope3d.look_at`` (which returns a Quat); this drives the
            Euler-angle camera directly, for orbit/cutscene scripts.
        """
    def screen_to_ray(self, px: float, py: float) -> tuple[Vec3, Vec3]:
        """Unproject a pixel to a world ray.

        Args:
            px: Pixel X (top-left origin).
            py: Pixel Y (top-left origin).

        Returns:
            ``(origin, direction)`` of the world ray.

        Warning:
            Meaningless under FPS mouselook, where ``window.get_cursor_pos()``
            returns unbounded virtual coordinates (Hazard #5). For a visible
            cursor, unlock the mouse first::

                px, py = yope3d.window.get_cursor_pos()
                o, d = yope3d.camera.screen_to_ray(px, py)
                hit, e, point, normal, t = yope3d.raycast(o, d, 100.0)

            For a center-screen crosshair, ray from the camera directly::

                o, d = yope3d.camera.position, yope3d.camera.get_forward()
                hit, e, point, normal, t = yope3d.raycast(o, d, 100.0)
        """

class Window:
    """The application window — pixel size and cursor position. Singleton ``yope3d.window``."""

    def get_width(self) -> int:
        """Return the framebuffer width in pixels."""
    def get_height(self) -> int:
        """Return the framebuffer height in pixels."""
    def get_cursor_pos(self) -> tuple[float, float]:
        """Return the cursor position ``(x, y)`` in pixels (top-left origin).

        Warning:
            Only meaningful while the cursor is unlocked. Under FPS mouselock
            GLFW reports an unbounded virtual position (Hazard #5).
        """
    def set_cursor_locked(self, locked: bool) -> None:
        """Lock or unlock the mouse cursor.

        Args:
            locked: ``True`` hides + captures the cursor (FPS mouselook);
                ``False`` shows it. The runtime starts locked.

        Note:
            Unlock before showing a pause menu or using cursor-based
            ``screen_to_ray`` picking; re-lock to return to mouselook.
        """
    def is_cursor_locked(self) -> bool:
        """Return ``True`` while the cursor is captured for mouselook."""

class Input:
    """Polled keyboard/mouse state. Singleton ``yope3d.input``.

    Key codes are the ``yope3d.KEY_*`` constants; mouse buttons the ``yope3d.MOUSE_*``
    constants.
    """

    def is_key_down(self, key: int) -> bool:
        """Return ``True`` every frame ``key`` is held."""
    def is_key_pressed(self, key: int) -> bool:
        """Return ``True`` only on the frame ``key`` went down (one-shot)."""
    def is_key_released(self, key: int) -> bool:
        """Return ``True`` only on the frame ``key`` went up (one-shot)."""
    def is_lmb_down(self) -> bool:
        """Return ``True`` while the left mouse button is held."""
    def is_rmb_down(self) -> bool:
        """Return ``True`` while the right mouse button is held."""
    def is_mmb_down(self) -> bool:
        """Return ``True`` while the middle mouse button is held."""
    def is_forward_mb_down(self) -> bool:
        """Return ``True`` while the 5th (forward) mouse button is held."""
    def is_backward_mb_down(self) -> bool:
        """Return ``True`` while the 4th (backward) mouse button is held."""
    def is_mouse_pressed(self, button: int) -> bool:
        """Return ``True`` only on the frame ``button`` went down (use ``yope3d.MOUSE_*``)."""
    def is_mouse_released(self, button: int) -> bool:
        """Return ``True`` only on the frame ``button`` went up (use ``yope3d.MOUSE_*``)."""
    def get_scroll_x(self) -> float:
        """Return horizontal scroll accumulated since last frame."""
    def get_scroll_y(self) -> float:
        """Return vertical scroll (wheel) accumulated since last frame."""
    def get_mouse_delta(self) -> tuple[float, float]:
        """Return mouse movement ``(dx, dy)`` in pixels since last frame."""

class SoundBuffer:
    """Opaque decoded-audio buffer handle returned by ``AudioSystem.load_sound``."""

class Source:
    """One OpenAL voice. Owned by the engine; this is a non-owning view.

    Reuse the handle returned by ``yope3d.play_sound`` to stop/reposition the sound.

    Warning:
        ``play_sound`` voices are pooled and may be recycled after the sound
        finishes (Hazard #4). For a sound you own, use ``audio.create_source``.
    """

    def play(self) -> None:
        """Start (or restart) playback."""
    def pause(self) -> None:
        """Pause playback, keeping the playhead."""
    def stop(self) -> None:
        """Stop playback and reset the playhead."""
    def rewind(self) -> None:
        """Reset the playhead to the start."""
    def set_gain(self, gain: float) -> None:
        """Set the linear gain (volume)."""
    def set_pitch(self, pitch: float) -> None:
        """Set the playback pitch multiplier."""
    def set_position(self, pos: Vec3) -> None:
        """Set the world position for 3D spatialization (sources must be mono)."""
    def set_velocity(self, vel: Vec3) -> None:
        """Set the velocity used for the Doppler effect."""
    def set_reference_distance(self, dist: float) -> None:
        """Set the distance at which attenuation begins."""
    def enable_looping(self, loop: bool) -> None:
        """Enable or disable looping."""
    def is_playing(self) -> bool:
        """Return ``True`` while the voice is actively playing."""

class AudioSystem:
    """Global audio control + asset loading. Singleton ``yope3d.audio``."""

    def pause_all(self) -> None:
        """Pause every active voice."""
    def resume_all(self) -> None:
        """Resume every paused voice."""
    def stop_all(self) -> None:
        """Stop every active voice."""
    def load_sound(self, path: str) -> SoundBuffer | None:
        """Decode + cache an OGG by asset-relative path (deduped across calls).

        Args:
            path: Asset-relative sound path (e.g. ``"audios/hum.ogg"``).

        Returns:
            The buffer, or ``None`` if loading failed.
        """
    def create_source(self, buffer: SoundBuffer) -> Source:
        """Create a playable voice bound to ``buffer``.

        Args:
            buffer: A buffer from ``load_sound``.

        Returns:
            A ``Source`` you own (not pooled). Prefer ``yope3d.play_sound`` for
            one-shots.
        """

class CollisionLayers:
    """Named 32-bit collision-layer registry. Access via ``yope3d.world.layers``."""

    ALL: int
    """Bitmask with every layer set."""
    NONE: int
    """Empty bitmask."""
    def add(self, name: str) -> int:
        """Register a new layer.

        Args:
            name: Unique layer name.

        Returns:
            The layer bitmask (``1 << slot``).

        Raises:
            Exception: If the registry is full or ``name`` is a duplicate.
        """
    def has(self, name: str) -> bool:
        """Return ``True`` if a layer named ``name`` is registered."""
    def count(self) -> int:
        """Return the number of registered layers."""
    def __getitem__(self, name: str) -> int:
        """Return the bitmask for a registered layer.

        Raises:
            Exception: If ``name`` is unknown.
        """

class SceneManager:
    """Deferred scene loading. Singleton ``yope3d.scene_manager``."""

    def load_scene(self, path: str) -> None:
        """Queue a scene load (applied safely between frames).

        Args:
            path: Asset-relative scene path (e.g. ``"scenes/sandbox.json"``).
        """

world: World
"""The engine World (always set by the time behavior ``init()`` / ``update()`` run)."""
camera: Camera
"""The render camera."""
input: Input
"""Polled keyboard/mouse state."""
audio: AudioSystem
"""Global audio control."""
scene_manager: SceneManager
"""Deferred scene loader."""
window: Window
"""The application window."""

# ==============================================================================
# ECS queries
# ==============================================================================

def view(*components: ComponentName) -> list[tuple[Any, ...]]:
    """Return all entities having every named component.

    Args:
        *components: One or more component names.

    Returns:
        A list of tuples ``(entity, comp1, comp2, ...)`` in argument order, e.g.
        ``for e, tf, hull in yope3d.view("Transform", "Hull"): ...``. The first
        tuple element is the ``Entity``; the rest are **live references** into
        engine memory.

    Note:
        Must be called from the main thread (the normal script ``update`` path).
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
    """Get a component by name, or ``None`` if the entity doesn't have it.

    Args:
        e: Target entity.
        name: A ``ComponentName``.

    Returns:
        A **live reference** into engine memory (edits apply immediately), or
        ``None``. Tag components (``"Sleeping"`` / ``"Fixed"``) return ``True``
        / ``None``.

    Warning:
        Don't cache the result across a composition change (Hazard #1).
    """

def reg_has(e: Entity, name: ComponentName) -> bool:
    """Return ``True`` if the entity has the named component."""

def reg_valid(e: Entity) -> bool:
    """Return ``True`` if the entity handle is still alive (generation matches)."""

def is_sleeping(e: Entity) -> bool:
    """Return ``True`` if the body is asleep (physics has parked it).

    ``wake(e)`` clears it. Also reachable via ``reg_has(e, "Sleeping")``.
    """

def is_fixed(e: Entity) -> bool:
    """Return ``True`` if the body carries the Fixed (static) tag.

    See ``fix_entity``. Also reachable via ``reg_has(e, "Fixed")``.
    """

def reg_add(e: Entity, name: ComponentName) -> None:
    """Add a default-constructed component to an entity.

    Args:
        e: Target entity.
        name: A ``ComponentName``.

    Raises:
        Exception: If ``name`` is unknown.

    Warning:
        Composition change — invalidates references held to this entity's
        components (Hazard #1). Mutate the new component via ``reg_get(e, name)``
        afterward.
    """

def reg_remove(e: Entity, name: ComponentName) -> None:
    """Remove a component from an entity (no-op if absent).

    Args:
        e: Target entity.
        name: A ``ComponentName``.

    Raises:
        Exception: If ``name`` is unknown.

    Warning:
        Composition change — see Hazard #1.
    """

def find_entity(name: str) -> Entity | None:
    """Return the first entity whose Name matches ``name``, or ``None``.

    Note:
        Linear scan over all entities.
    """

def get_behavior(entity: Entity) -> Any | None:
    """Return the live Python behavior instance attached to ``entity``, or ``None``.

    Args:
        entity: The entity whose behavior you want.

    Returns:
        The behavior instance, so one behavior can read/call another's state
        directly, e.g. in a collision callback::

            hp = yope3d.get_behavior(other)
            if hp is not None:
                hp.take_damage(10)

        Returns ``None`` if the entity has no script, or its script isn't a
        Python behavior.
    """

# ==============================================================================
# Kinematic queries (character-controller support)
# ==============================================================================

def capsule_overlap(
    pos: Vec3,
    radius: float,
    half_height: float,
    exclude: Entity | None = None,
) -> list[tuple[Vec3, float]]:
    """Test a capsule against all tangible world geometry.

    Args:
        pos: Capsule center.
        radius: Capsule radius.
        half_height: Half the cylindrical section length.
        exclude: An entity to skip (pass the controller's own entity to avoid
            self-collision).

    Returns:
        One ``(normal, depth)`` tuple per overlapping entity. Push the capsule
        along ``normal`` by ``depth`` to resolve.
    """

def capsule_cast(
    pos: Vec3,
    radius: float,
    half_height: float,
    dir: Vec3,
    max_dist: float,
    exclude: Entity | None = None,
) -> tuple[float, bool, Vec3]:
    """Sweep a capsule from ``pos`` along ``dir``.

    Args:
        pos: Capsule center at the start of the sweep.
        radius: Capsule radius.
        half_height: Half the cylindrical section length.
        dir: Sweep direction (must be normalized).
        max_dist: Maximum sweep distance.
        exclude: An entity to skip (e.g. the controller itself).

    Returns:
        ``(t, hit, normal)`` — ``t`` is the contact distance from the capsule's
        endpoint sphere center (equals ``max_dist`` when nothing is hit). Used
        for grounding checks and step-up probes.
    """

def raycast(
    origin: Vec3, dir: Vec3, max_dist: float, exclude: Entity | None = None
) -> tuple[bool, Entity | None, Vec3, Vec3, float]:
    """Cast a thin ray and return the nearest tangible hit within ``max_dist``.

    Args:
        origin: Ray origin.
        dir: Ray direction (need not be normalized).
        max_dist: Maximum hit distance in world meters.
        exclude: An entity to skip (e.g. the shooter).

    Returns:
        ``(hit, entity, point, normal, t)`` — ``entity`` is ``None`` on a miss;
        ``t`` is the hit distance in world meters. This is the spine of
        shooting, mouse-picking (with ``camera.screen_to_ray``), and AI
        line-of-sight.

    Note:
        Coverage is sphere / AABB / OBB bodies (capsule/cylinder not yet).
    """

def load_scene(path: str) -> None:
    """Shorthand for ``yope3d.scene_manager.load_scene(path)``."""

# ==============================================================================
# Convenience helpers
# ==============================================================================

def play_sound(
    path: str, pos: Vec3 | None = None, gain: float = 1.0, loop: bool = False
) -> Source | None:
    """Load + create + play a sound in one call (footsteps, pickups, jumps).

    Args:
        path: Asset-relative sound path (e.g. ``"audios/jump.ogg"``).
        pos: World position for a 3D spatialized one-shot; omit for a
            non-positional sound.
        gain: Linear gain (volume).
        loop: Whether to loop.

    Returns:
        The ``Source`` (keep it to stop/reposition), or ``None`` if audio isn't
        available.

    Warning:
        These voices are pooled: once the sound finishes, the engine may recycle
        the ``Source`` for a later call (Hazard #4). For a long/looping sound you
        manage yourself, use ``yope3d.audio.create_source(...)`` instead.
    """

def set_text(entity: Entity, text: str) -> None:
    """Set the string of whichever text component the entity has (UIText or TextLabel3D)."""

def time() -> float:
    """Return wall-clock seconds since engine startup (GLFW timer).

    Note:
        For physics ticks use ``yope3d.world.tick_count`` instead.
    """

def draw_line(a: Vec3, b: Vec3, color: Vec3 | None = None) -> None:
    """Draw a world-space debug segment for this frame.

    Args:
        a: Start point.
        b: End point.
        color: RGB in ``[0, 1]`` (defaults to yellow).

    Note:
        Always-on-top. Debug lines are cleared automatically each frame before
        scripts run, so call this from ``update()``. Great for velocity vectors,
        AI paths, and visualizing ``raycast`` hits.
    """

def draw_ray(
    origin: Vec3, dir: Vec3, length: float = 1.0, color: Vec3 | None = None
) -> None:
    """Draw a debug ray for this frame.

    Args:
        origin: Ray origin.
        dir: Ray direction.
        length: Length in meters.
        color: RGB in ``[0, 1]`` (defaults to yellow).
    """

def get_position(entity: Entity) -> Vec3 | None:
    """Re-resolving read of the entity's ``Transform.position`` (``None`` if no Transform).

    Note:
        Safe to call after composition changes — it looks the component up per
        call (Hazard #1).
    """

def set_position(entity: Entity, pos: Vec3) -> None:
    """Re-resolving write of the entity's ``Transform.position`` (no-op if no Transform).

    Note:
        Safe to call after composition changes — it looks the component up per
        call (Hazard #1).
    """

def set_velocity(entity: Entity, velocity: Vec3) -> None:
    """Re-resolving write of the entity's ``Hull.velocity`` (no-op if no Hull).

    Warning:
        Writing velocity on a *sleeping* body has no visible effect until it is
        woken — use ``yope3d.world.wake(e)`` or ``yope3d.world.apply_impulse(e, j)``
        instead (Hazard #3).
    """

# ==============================================================================
# Key constants (GLFW key codes, for Input.is_key_*)
# ==============================================================================

# --- Letters ---
KEY_W: Final[int]
KEY_A: Final[int]
KEY_S: Final[int]
KEY_D: Final[int]
KEY_Q: Final[int]
KEY_E: Final[int]
KEY_R: Final[int]
KEY_F: Final[int]
KEY_H: Final[int]
KEY_P: Final[int]
KEY_V: Final[int]

# --- Arrows ---
KEY_LEFT: Final[int]
KEY_RIGHT: Final[int]
KEY_UP: Final[int]
KEY_DOWN: Final[int]

# --- Action / modifier / special ---
KEY_SPACE: Final[int]
KEY_ESCAPE: Final[int]
KEY_ENTER: Final[int]
KEY_LEFT_SHIFT: Final[int]
KEY_LEFT_CONTROL: Final[int]

# ==============================================================================
# Mouse button constants (for Input.is_mouse_pressed / is_mouse_released)
# ==============================================================================

MOUSE_LEFT: Final[int]
MOUSE_RIGHT: Final[int]
MOUSE_MIDDLE: Final[int]
