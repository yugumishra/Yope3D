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
        detach_physics_body helpers, fix_entity, or anything that adds/removes
        a tag. (``wake`` is *not* on this list — sleep state is a plain
        ``Hull.asleep`` field, not a tag.)

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
        # UI pointer callbacks — entity must carry a UITransform + this ScriptComponent.
        def on_ui_press(self, world: World, entity: Entity) -> None: ...
        def on_ui_release(self, world: World, entity: Entity) -> None: ...
        def on_ui_enter(self, world: World, entity: Entity) -> None: ...
        def on_ui_leave(self, world: World, entity: Entity) -> None: ...
        def on_text_input(self, world: World, entity: Entity, codepoint: int) -> None: ...

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
- ``on_ui_press`` / ``on_ui_release`` fire on the mouse-button press/release edge
  while the cursor is over this entity's UITransform rect (topmost by depth
  wins). ``on_ui_enter`` / ``on_ui_leave`` fire when the cursor starts/stops
  hovering the rect. Dispatched once per frame from ``World``'s UI input
  router — same main-thread-only guarantee as the collision callbacks. For
  menu-driver scripts that don't want a ScriptComponent per widget, poll
  ``world.ui_hit_test(x, y)``, ``world.ui_hovered()``, and
  ``world.ui_consumed_click()`` instead.
- ``on_text_input`` fires once per typed UTF-32 codepoint while this entity
  holds UI focus (``world.get_ui_focus() is entity``) — focus moves to
  whatever UI entity was last pressed, or clears on a press that misses all
  UI. Requires a visible cursor (``window.set_cursor_locked(False)``) since
  GLFW only emits char events then.
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

Trigger volumes (overlap events, zero physical response)::

    def init(self, world, entity, params):
        # Retrofit the already-scripted `entity` into a trigger: attach a
        # static collider, then flip is_trigger. on_collision_enter/exit only
        # dispatches to entities holding a *live* script instance, and those
        # are only created for entities scripted at scene load -- so the
        # trigger you want events from must be (or replace) an existing
        # scripted entity, not a fresh world.add_trigger_box(...) call.
        if not yope3d.reg_has(entity, "Hull"):
            world.attach_aabb_collider(entity, 0.0, yope3d.Vec3(1, 1, 1), True)
        yope3d.reg_get(entity, "Hull").is_trigger = True
        world.attach_box_mesh(entity, yope3d.Vec3(1, 1, 1), 1.0, 0.85, 0.15)

        # A freestanding trigger built straight from the factory -- fully
        # functional (nothing overlapping it is pushed), but since it carries
        # no script of its own it won't independently log enter/exit.
        world.add_trigger_sphere(yope3d.Vec3(3, 0, 0), 1.0)

    def on_collision_enter(self, world, entity, other):
        world.set_mesh_color(entity, 0.3, 1.0, 0.3)   # flash while occupied
        print(f"entered trigger: {other}")

    def on_collision_exit(self, world, entity, other):
        world.set_mesh_color(entity, 1.0, 0.85, 0.15)  # back to idle tint
        print(f"exited trigger: {other}")

See ``scripts/behaviors/trigger_volume_demo.py`` for a runnable end-to-end
version (static floor + dropped spheres passing through both trigger shapes).

Spawning entities with their own independent behavior (enemy waves, pickups)::

    def update(self, world, entity, dt):
        self.spawned += 1
        e = world.add_sphere(1.0, 0.4, self._next_spawn_pos())
        world.attach_sphere_mesh(e, 0.4, 1.0, 0.3, 0.3)
        yope3d.attach_script(e, "behaviors.enemy", "Enemy",
                              {"hp": 30, "wave": self.spawned})

See ``scripts/behaviors/attach_script_demo.py`` for a runnable end-to-end
version (periodic spawns, each with an independently-counting instance).

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
    is_trigger: bool
    """If ``True`` this body still generates broadphase/narrowphase overlaps and
    ``on_collision_enter``/``on_collision_exit`` events, but the PGS solver never
    resolves its contacts (no physical push-back). Orthogonal to ``tangible`` —
    do not set both. Overlapping sleeping bodies are kept awake-checked so a
    trigger doesn't miss an exit event when the other body falls asleep inside it.
    """
    asleep: bool
    """``True`` once the physics thread has parked this body (see the Hull
    Warning above). A plain field, not a tag — reading/writing it is not a
    composition change. Prefer ``world.wake`` over setting it directly, since
    ``wake`` also zeros ``sleepFrames`` so the body doesn't immediately re-sleep.
    """
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

class CompoundCollider:
    """Static level collider: a baked multi-shape body + mid-phase BVH, authored
    via the editor's "Generate Static Collider" button (bakes every mesh in an
    entity's subtree into one static body so the player can't walk through
    walls). Read-only from scripts — there's no runtime API to rebuild it.
    """

    asset_path: str
    """Asset-relative path to the cooked ``.bcbvh`` file."""
    loaded: bool
    """Whether the baked collider data has resolved (World-cache lazy load)."""

class LightSource:
    """A light emitter component."""

    type: LightType
    """Light kind (see ``LightType``): 0=Point, 1=Directional, 2=Spot, 3=Flash."""
    intensity: float
    color: Vec3
    """RGB in ``[0, 1]``."""
    position: Vec3
    direction: Vec3
    constant: float
    """Constant attenuation term (point/spot)."""
    linear: float
    """Linear attenuation term (point/spot)."""
    quadratic: float
    """Quadratic attenuation term (point/spot)."""
    inner_cone_angle: float
    """Spot/flash inner cone half-angle in radians (full intensity inside)."""
    outer_cone_angle: float
    """Spot/flash outer cone half-angle in radians (zero intensity outside). The
    spot shadow frustum's FOV is derived from this."""
    casts_shadow: bool
    """Whether this light is the scene shadow caster. Prefer
    :meth:`World.set_shadow_caster` over setting this directly — it enforces the
    single-caster (radio) invariant the renderer relies on."""

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

class PointJointConstraint:
    """Serializable mirror of a ball-socket (point-to-point) joint. Created by
    :meth:`World.add_point_joint` with ``persist=True``. Anchors are body-local
    offsets from each body's COM."""

    target: Entity
    local_anchor_a: Vec3
    local_anchor_b: Vec3

class HingeJointConstraint:
    """Serializable mirror of a hinge (revolute) joint. Created by
    :meth:`World.add_hinge_joint` with ``persist=True``."""

    target: Entity
    local_anchor_a: Vec3
    local_anchor_b: Vec3
    local_axis_a: Vec3
    local_axis_b: Vec3
    limit_enabled: bool
    lower_angle: float
    upper_angle: float

class ConeTwistJointConstraint:
    """Serializable mirror of a cone-twist joint. Created by
    :meth:`World.add_cone_twist_joint` with ``persist=True``."""

    target: Entity
    local_anchor_a: Vec3
    local_anchor_b: Vec3
    local_twist_axis_a: Vec3
    local_twist_axis_b: Vec3
    swing_limit: float
    twist_limit: float

class Parent:
    """Transform-hierarchy link. The entity's ``Transform`` is LOCAL to ``parent``'s
    frame; use :func:`get_world_position` for the composed world position. Physics
    bodies must be hierarchy roots (no ``Parent``)."""

    parent: Entity

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
    anchor: int
    """0=Free (legacy: min/max used verbatim) 1=TopLeft 2=TopRight 3=BottomLeft
    4=BottomRight 5=Center 6=CenterTop 7=CenterBottom 8=CenterLeft
    9=CenterRight. Any non-Free value repositions the rect relative to that
    screen corner/edge/center — fixes a HUD element that should hug a corner
    (or edge midpoint) at constant size instead of drifting with min/max
    fractions.
    """
    size_mode: int
    """Only consulted when ``anchor != 0``. 0=Fraction (size = max-min, still
    aspect-stretchy) 1=Pixel (size = pixel_width/pixel_height in real screen
    pixels — the fix for aspect-ratio distortion, e.g. a square icon that
    should stay square regardless of window aspect).
    """
    pixel_width: float
    pixel_height: float
    """Used when ``size_mode == 1``."""
    offset_x_px: float
    offset_y_px: float
    """Pixel offset from the anchor point, pushing inward from the anchored
    edge(s). Used whenever ``anchor != 0``.
    """
    opacity: float
    """Own opacity multiplier in ``[0, 1]``. Composes down an ``ecs::Parent``
    chain (see ``World.set_ui_parent``) — fading a parent fades every
    descendant. Unlike ``visible``, ``opacity == 0`` does not block clicks;
    it's purely a render-time fade.
    """

class UIBackground:
    """Solid-color rectangle (pairs with ``UITransform``). RGBA in ``[0, 1]``."""

    r: float
    g: float
    b: float
    a: float

class UITexturedBackground:
    """Texture-modulated rectangle (pairs with ``UITransform``).

    Setting ``path`` clears ``has_texture`` until the engine resolves it from
    disk on a later frame (immediately for ``World.add_ui_textured_background``,
    within a frame otherwise) — check ``has_texture`` before relying on it
    having rendered.
    """

    path: str
    """Asset-relative image path. Reassigning triggers a reload."""
    has_texture: bool
    """True once ``path`` has resolved to a loaded GPU texture."""
    tint_r: float
    tint_g: float
    tint_b: float
    tint_a: float

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
    auto_size: bool
    """When ``True``, the renderer snaps this entity's ``UITransform`` to the
    natural (unwrapped) size of ``text`` at ``display_px`` — no more manually
    guessing a width/height that fits. anchor==Free grows/shrinks max_x/max_y
    from the current min_x/min_y; any other anchor sets size_mode=Pixel and
    pixel_width/pixel_height instead, keeping the authored anchor/offset.
    Only recomputed when ``text`` actually changes (internally cached), so a
    manual resize in the editor sticks until the text content itself changes
    — it won't fight you every frame.
    """

class UIButton:
    """Interactive button (pairs with ``UITransform``).

    Renders as a solid rect that swaps between four color states based on
    ``World``'s UI input router (hover/press) and ``enabled`` — the component
    itself carries no click logic. Attach a ``ScriptComponent`` to the same
    entity for ``on_ui_press``/``on_ui_release``/``on_ui_enter``/``on_ui_leave``.
    """

    normal_r: float
    normal_g: float
    normal_b: float
    normal_a: float
    hover_r: float
    hover_g: float
    hover_b: float
    hover_a: float
    pressed_r: float
    pressed_g: float
    pressed_b: float
    pressed_a: float
    disabled_r: float
    disabled_g: float
    disabled_b: float
    disabled_a: float
    enabled: bool
    """``False`` renders the disabled state and excludes the button from
    hit-testing — clicks pass through to whatever's behind it.
    """

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

class Material:
    """PBR metallic-roughness material (pairs with a MeshRenderer).

    Map paths are asset-relative (empty = engine default for that slot); setting
    any map path invalidates the cached GPU descriptor set so the renderer
    re-resolves it next frame. Scalar factors ride push constants and can be
    changed freely without triggering a re-resolve.

    **sRGB vs linear:** the engine loads each slot with the correct gamma:

    * ``albedo_map`` — **sRGB** (perceptual color; gamma-decoded by the sampler).
    * ``normal_map`` — **linear** (tangent-space vectors; must not be gamma-decoded).
    * ``metal_rough_map`` — **linear** (G = roughness, B = metallic; physical scalars).
    * ``occlusion_map`` — **linear** (R = ambient occlusion; physical scalar).
    * ``emissive_map`` — **sRGB** (perceptual color; gamma-decoded by the sampler).

    Mixing these up (e.g. loading a normal map as sRGB) produces subtly wrong
    lighting that is difficult to diagnose — use the correct slot for each map.
    """

    albedo_map: str
    """Asset-relative path to the base-color texture (**sRGB**). Empty = 1×1 white."""
    normal_map: str
    """Asset-relative path to the tangent-space normal map (**linear**). Empty = 1×1 flat (0.5, 0.5, 1)."""
    metal_rough_map: str
    """Asset-relative path to the metallic-roughness map (**linear**). glTF packing: G = roughness, B = metallic. Empty = 1×1 (r=1, g=1)."""
    occlusion_map: str
    """Asset-relative path to the ambient-occlusion map (**linear**). R channel only. Empty = 1×1 white (no occlusion)."""
    emissive_map: str
    """Asset-relative path to the emissive map (**sRGB**). Empty = 1×1 black (no emission)."""
    albedo: tuple[float, float, float, float]
    """Base-color factor (rgba). Multiplies the sampled albedo_map value."""
    metallic: float
    """Metallic factor [0, 1]. Multiplies the B channel of metal_rough_map."""
    roughness: float
    """Roughness factor [0, 1]. Multiplies the G channel of metal_rough_map."""
    normal_scale: float
    """Scales the XY components of the sampled normal before TBN transform. 1.0 = full strength."""
    emissive: tuple[float, float, float]
    """Emissive factor (rgb). Multiplies the sampled emissive_map value."""

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
    "CompoundCollider",
    "Material",
    "LightSource",
    "Name",
    "SpringConstraint",
    "PointJointConstraint",
    "HingeJointConstraint",
    "ConeTwistJointConstraint",
    "Parent",
    "ScriptComponent",
    "UITransform",
    "UIBackground",
    "UITexturedBackground",
    "UIText",
    "UIButton",
    "TextLabel3D",
    "AudioSource",
    "Fixed",
]
"""Component names accepted by ``view()`` / ``reg_get()`` / ``reg_has()``.

``"Fixed"`` is a zero-size tag: ``reg_has`` / ``is_fixed`` report presence, and
``reg_get`` returns ``True`` / ``None``. Prefer ``fix_entity`` over ``reg_add``-ing
it. Sleep state is a plain ``Hull.asleep`` field, not a tag — read it directly
or via ``is_sleeping`` / ``wake``.
"""

# ==============================================================================
# Engine singletons (bound by the engine before any script runs)
# ==============================================================================

class JointHandle:
    """Opaque handle to a live joint (returned by ``World.add_vehicle``).

    No methods or fields — hold the value and pass it back into
    ``World.set_wheel_drive`` / ``World.set_wheel_steer``, never inspect it.
    """

class WheelSpec:
    """One wheel's setup for ``World.add_vehicle`` (raycast wheel — see
    physics/Joint.h's SuspensionJoint/WheelFrictionJoint). All fields
    chassis-local; construct with ``WheelSpec()`` then set fields.
    """

    local_pos: Vec3
    """Wheel mount point, chassis-local."""
    local_up: Vec3
    """Suspension travel axis, chassis-local (default ``(0,1,0)``) — the wheel
    ray casts downward along ``-local_up`` (rotated to world)."""
    rest_length: float
    """Suspension rest length in meters."""
    max_travel: float
    """Extra ray distance past rest_length the wheel can droop before losing contact."""
    stiffness: float
    """Spring constant, N/m."""
    damping: float
    """Damper constant, N*s/m."""
    radius: float
    """Wheel radius in meters — converts angular velocity to a surface speed target."""
    mu_long: float
    """Longitudinal (drive/brake) friction coefficient."""
    mu_lat: float
    """Lateral (grip) friction coefficient."""
    driven: bool
    """When ``False`` the wheel free-rolls (lateral grip only) instead of
    driving/braking toward ``wheel_angular_vel * radius``. Default ``True``.
    Set the non-powered wheels ``False`` for a real FWD/RWD drivetrain."""
    def __init__(self) -> None: ...

class World:
    """Entity/physics factory and queries. Access via the ``yope3d.world`` singleton."""

    gravity: Vec3
    """World gravity in m/s² (default ``(0, -9.8…, 0)``)."""

    debug_physics: bool
    """Toggle the physics debug overlay (collider wireframes)."""
    paused: bool
    """Whether the physics simulation is paused (same as ``set_paused``)."""

    # ------------------------------------------------------------------ #
    # World Settings: rendering / shadow tuning (mirror the editor panel)
    # ------------------------------------------------------------------ #

    exposure: float
    """Global scene exposure applied pre-tonemap in the PBR shader (default ``1.0``)."""
    shadow_bias: float
    """NDC-space depth-compare bias; last-resort acne margin (default ``0.0006``)."""
    shadow_normal_bias: float
    """World-space offset along the surface normal before the light transform — the
    primary acne fix for grazing-angle surfaces (default ``0.035``). Too large
    detaches shadows (peter-panning)."""
    shadow_pcf_radius: float
    """PCF softening kernel spread, in shadow-texel multiples (default ``1.0``)."""
    shadow_ortho_half_extent: float
    """Directional caster's camera-centered ortho box half-size; smaller = sharper
    but a smaller shadowed radius (default ``20.0``)."""
    shadow_ortho_far: float
    """Directional caster's ortho far plane (default ``40.0``)."""
    shadow_spot_near: float
    """Spot caster's perspective near plane (default ``1.0``). Keep as large as the
    scene allows — perspective depth precision is dominated by the near/far ratio,
    and too small crushes occluder depths toward 1.0 into detached blob shadows."""
    shadow_spot_far: float
    """Spot caster's perspective far plane (default ``30.0``). Keep no larger than
    the light's actual reach."""

    def set_shadow_caster(self, entity: Entity) -> None:
        """Mark ``entity``'s light as the single scene shadow caster (radio behavior:
        clears ``casts_shadow`` on every other light). Pass a spot or directional
        light; point lights aren't a supported caster type."""
    def clear_shadow_caster(self) -> None:
        """Disable shadow casting on all lights (no scene caster)."""
    def get_shadow_caster(self) -> Entity | None:
        """The current shadow-caster entity, or ``None`` if unset."""

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
    def add_trigger_box(self, pos: Vec3, extent: Vec3) -> Entity:
        """Spawn a static trigger volume shaped as an axis-aligned box.

        Equivalent to ``add_static_aabb`` but with ``Hull.is_trigger = True``:
        it stays in broadphase/narrowphase and fires
        ``on_collision_enter``/``on_collision_exit``, but the solver never
        resolves its contacts — nothing overlapping it is pushed. Use for
        checkpoints, pickups, damage zones, and door sensors.

        Args:
            pos: World position.
            extent: Box half-extents.

        Returns:
            The new entity.
        """
    def add_trigger_sphere(self, pos: Vec3, radius: float) -> Entity:
        """Spawn a static trigger volume shaped as a sphere.

        See ``add_trigger_box`` — same semantics, spherical bounds.

        Args:
            pos: World position.
            radius: Sphere radius.

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
        """Remove all physics components (Hull + shape + Fixed tag).

        Warning:
            Composition change — see Hazard #1.
        """
    def build_sphere_compound(
        self,
        spheres: list[tuple[Vec3, float]],
        density: float = 1.0,
        is_static: bool = False,
        pos: Vec3 = ...,
    ) -> Entity:
        """Synthesize a compound collider from (center, radius) sphere sub-shapes.

        Unlike the mesh-baked compound workflow (editor "Generate Collider"), this
        builds the ``CompiledCollider`` procedurally — for scripted/programmatic
        shapes (e.g. assembling primitives into one rigid body) rather than baking
        from real mesh geometry. Mass, center-of-mass recentering, and inertia are
        derived from ``density`` using the same math the mesh baker uses. Creates
        a new entity at ``pos`` (Transform + Hull + CompoundCollider — same
        factory-method convention as ``add_sphere``/``add_obb``) and returns it.
        ``is_static=False`` (default) makes it a real dynamic body driven by its
        true off-center inertia; ``is_static=True`` behaves like the baked
        compound collider — Fixed, mass 0.

        Args:
            spheres: List of (center, radius) pairs in the new entity's local frame.
            density: kg/m^3 driving each sphere's mass (default 1.0).
            is_static: If ``True``, create an immovable (Fixed) body instead of
                a dynamic one.
            pos: Initial world position (defaults to the origin).

        Returns:
            The new entity, or a null ``Entity`` if ``spheres`` was empty.
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
    # Joints — bilateral constraints (can push AND pull), solved inside the
    # same island-parallel PGS loop as contacts. Unlike springs (a soft,
    # one-shot force applied globally after the solve), a joint enforces a
    # hard equality — the right tool for ragdolls/mechanisms that must stay
    # rigidly connected even under tension or mid-air (no contact to lean on).
    # ------------------------------------------------------------------ #

    def add_point_joint(self, a: Entity, b: Entity, anchor: Vec3, persist: bool = False) -> None:
        """Connect two bodies at a shared world-space anchor point (ball socket).

        Args:
            a: First body (must have a Hull).
            b: Second body (must have a Hull).
            anchor: World-space point both bodies are pinned to; converted to
                each body's local-space offset at creation time (not re-derived
                later — moving the bodies afterward does not change the anchor).
            persist: If True, also add a serializable ``PointJointConstraint``
                mirror component (on ``a``) so the joint survives Save Scene /
                reload. Leave False for transient joints (e.g. a mouse-drag grab)
                that are created and removed within a session.

        Note:
            Remove later with ``remove_joint_between(a, b)``.
        """
    def add_hinge_joint(
        self,
        a: Entity,
        b: Entity,
        anchor: Vec3,
        axis: Vec3,
        limit_enabled: bool = False,
        lower_angle: float = 0.0,
        upper_angle: float = 0.0,
        persist: bool = False,
    ) -> None:
        """Connect two bodies with a hinge (revolute) — free rotation about one shared axis.

        Args:
            a: First body (must have a Hull).
            b: Second body (must have a Hull).
            anchor: World-space pivot point.
            axis: World-space hinge axis (direction, not a point).
            limit_enabled: Whether to clamp rotation to ``[lower_angle, upper_angle]``.
            lower_angle: Lower angle limit in radians (only if ``limit_enabled``).
            upper_angle: Upper angle limit in radians (only if ``limit_enabled``).
            persist: If True, also add a serializable ``HingeJointConstraint``
                mirror component (on ``a``) so the joint survives Save Scene / reload.

        Note:
            The limit is enforced at the position level only (split-impulse),
            not also velocity-clamped — a very hard, fast-spinning hit can
            overshoot by a substep before being pushed back. Remove later with
            ``remove_joint_between(a, b)``.
        """
    def add_cone_twist_joint(
        self,
        a: Entity,
        b: Entity,
        anchor: Vec3,
        twist_axis: Vec3,
        swing_limit: float = 0.785398,
        twist_limit: float = 0.785398,
        persist: bool = False,
    ) -> None:
        """Connect two bodies with a cone-twist (swing-twist) — the anatomically
        correct joint for shoulders/hips: a cone limit on how far ``b``'s bone
        axis can swing away from ``a``'s, plus an independent limit on how far
        ``b`` can twist about its own bone axis.

        Args:
            a: First body (must have a Hull).
            b: Second body (must have a Hull).
            anchor: World-space pivot point.
            twist_axis: World-space "bone" direction for both bodies.
            swing_limit: Half-angle cone limit in radians.
            twist_limit: Twist limit in radians (symmetric, +/-).
            persist: If True, also add a serializable ``ConeTwistJointConstraint``
                mirror component (on ``a``) so the joint survives Save Scene / reload.

        Note:
            Same position-only limit caveat as ``add_hinge_joint``. Remove
            later with ``remove_joint_between(a, b)``.
        """
    def remove_joint_between(self, a: Entity, b: Entity) -> None:
        """Remove the first joint (of any type) whose endpoints match ``{a, b}`` (either order)."""
    def finalize_entity(self, entity: Entity, name: str) -> None:
        """Give ``entity`` a ``Name`` plus (in editor builds) ``EditorSelectable`` +
        ``EditorPickable`` — the components the scene serializer's save loop and
        editor picking need. Idempotent: the ``add_*`` factory helpers already call
        this, so on a factory-spawned entity it simply **renames** in place. Use it
        to give spawned entities readable names, or to finalize an entity that
        somehow reached the registry without it."""

    # ------------------------------------------------------------------ #
    # Vehicles — raycast wheels (SuspensionJoint + WheelFrictionJoint per
    # wheel; see physics/Joint.h). Code-only API, no editor "Add Joint"
    # dropdown entry — a vehicle is a whole rig, not a single two-body joint.
    # ------------------------------------------------------------------ #

    def add_vehicle(self, chassis: Entity, wheels: list[WheelSpec]) -> list[JointHandle]:
        """Build one Suspension+WheelFriction joint pair per wheel spec on `chassis`.

        Args:
            chassis: The vehicle body (must already have a Hull — e.g. from ``add_obb``).
            wheels: One ``WheelSpec`` per wheel.

        Returns:
            One opaque handle per wheel, same order as `wheels`, for later
            ``set_wheel_drive`` / ``set_wheel_steer`` calls.
        """
    def set_wheel_drive(self, wheel: JointHandle, angular_vel: float) -> None:
        """Set a wheel's target angular velocity (rad/s) — throttle (positive),
        brake (toward zero), or reverse (negative). No-op if `wheel` isn't a
        wheel handle from ``add_vehicle``.
        """
    def set_wheel_steer(self, wheel: JointHandle, steer_angle: float) -> None:
        """Set a wheel's steer angle in radians, about its suspension's up axis."""

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
    def add_ui_textured_background(
        self, min: Vec2, max: Vec2, tint: Vec4, path: str, depth: int = 0
    ) -> Entity:
        """Add a texture-modulated HUD rectangle.

        Args:
            min: Top-left corner in ``[0, 1]`` screen percent.
            max: Bottom-right corner in ``[0, 1]`` screen percent.
            tint: RGBA multiplier in ``[0, 1]`` (``(1,1,1,1)`` = untinted).
            path: Asset-relative image path.
            depth: Sort order; higher draws on top.

        Returns:
            The new UI entity. The texture resolves synchronously before this
            call returns (``reg_get(e, "UITexturedBackground").has_texture``
            is already ``True`` on a valid path).
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
    def add_ui_button(
        self, min: Vec2, max: Vec2, normal_color: Vec4, depth: int = 0
    ) -> Entity:
        """Add an interactive button (``UITransform`` + ``UIButton``).

        Args:
            min: Top-left corner in ``[0, 1]`` screen percent.
            max: Bottom-right corner in ``[0, 1]`` screen percent.
            normal_color: RGBA in ``[0, 1]`` for the resting state; hover and
                pressed are derived by brightening/darkening it, disabled
                halves its alpha. Fine-tune individual states afterward via
                ``reg_get(e, "UIButton")``.
            depth: Sort order; higher draws on top.

        Returns:
            The new UI entity. Attach a ``ScriptComponent`` to receive
            ``on_ui_press``/``on_ui_release``/``on_ui_enter``/``on_ui_leave``.
        """
    def add_model(self, path: str) -> list[Entity]:
        """Load a model and spawn it into the scene.

        ``.obj`` yields one entity; ``.gltf`` / ``.glb`` yield one entity per
        primitive. Each entity gets a Transform + MeshRenderer, plus a
        :class:`Material` when the source defines one (glTF metallic-roughness or
        OBJ/MTL). ``path`` is relative to the assets directory.

        Returns:
            The created entities (one per primitive).
        """
    def set_skybox(self, faces: list[str]) -> None:
        """Set a cubemap skybox from six asset-relative face images.

        Args:
            faces: Exactly six paths in order ``+X, -X, +Y, -Y, +Z, -Z``.
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
    def ui_hit_test(self, x: float, y: float) -> Entity | None:
        """Topmost visible UI entity under (x, y) in ``[0, 1]`` screen percent, or None.

        Re-runs the same point-in-rect + depth-priority test the per-frame
        input router uses — safe to call with an arbitrary point (e.g. a
        gamepad cursor position), not just the live mouse cursor.
        """
    def ui_hovered(self) -> Entity | None:
        """UI entity the mouse cursor is currently hovering, or None.

        Reflects the router's state as of the start of this frame — the same
        entity that would receive ``on_ui_enter``/``on_ui_leave`` callbacks.
        """
    def ui_consumed_click(self) -> bool:
        """True if this frame's mouse-button press landed on a UI entity.

        Check this before running gameplay click logic (e.g. shoot-on-click)
        so clicking a menu button doesn't also fire world interactions.
        """
    def set_ui_focus(self, entity: Entity) -> None:
        """Give ``entity`` UI focus programmatically.

        Focus also moves automatically on press: onto whichever UI entity was
        pressed, or away from any entity on a press that misses all UI.
        """
    def get_ui_focus(self) -> Entity | None:
        """The UI entity currently holding focus, or None."""
    def set_ui_parent(self, child: Entity, parent: Entity) -> None:
        """Group ``child`` under ``parent`` for move/hide/fade-as-a-unit.

        Reuses the same ``ecs::Parent`` link 3D transform hierarchy uses.
        ``child``'s own ``UITransform`` becomes a ``[0, 1]`` rect local to
        ``parent``'s resolved rect (not the screen): move, hide (``visible``),
        or fade (``opacity``) the parent and the whole subtree follows.
        No-ops on a cycle (``parent`` already a descendant of ``child``) or an
        invalid entity. Pass ``NullEntity`` to un-parent.

        Note:
            Non-``Free`` anchors on a parented child anchor to the screen, not
            the parent — anchoring is for root-level HUD pinning; nested
            children should stay in the default Free/fraction mode.
        """
    def set_ui_group_visible(self, root: Entity, visible: bool) -> None:
        """Set ``visible`` on ``root`` and every UI descendant (one-shot bulk write).

        Visibility also composes live through the parent chain, so this is a
        convenience for "toggle a whole menu" rather than required for
        correctness — hiding just the root already hides its children.
        """
    def set_ui_group_opacity(self, root: Entity, opacity: float) -> None:
        """Set ``opacity`` on ``root`` and every UI descendant (one-shot bulk write).

        Opacity composes live too — for a simple fade, prefer animating just
        ``root``'s own ``opacity`` (e.g. via a tween) unless you specifically
        want to flatten every descendant to the same value.
        """
    def tween_ui_opacity(
        self, root: Entity, target: float, duration: float, ease: int = EASE_LINEAR
    ) -> None:
        """Animate ``root``'s own opacity to ``target`` over ``duration`` seconds.

        Composes down to every UI descendant of ``root`` (see
        ``set_ui_parent``) — one call fades a whole panel. Re-targeting an
        entity with an in-flight tween replaces it, starting from the current
        opacity (safe to call every frame with a changing target, e.g. driven
        by a hover state). ``ease`` is one of the ``yope3d.EASE_*`` constants.
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
        """Clear the Hull's ``asleep`` flag so direct velocity writes take effect
        again, and zero ``sleepFrames`` so the body doesn't immediately re-sleep.

        Not a composition change — ``asleep`` is a plain ``Hull`` field, so
        references held across this call stay valid.
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
        """Set the vertical field of view in **radians** (feeds ``tan(fov/2)``
        directly — pass ``yope3d.to_radians(60)``, not ``60``)."""
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
    def get_cursor_pos(self) -> tuple[float, float]:
        """Return the cursor position ``(x, y)`` in pixels (top-left origin).

        Same value ``Window.get_cursor_pos`` reads via GLFW directly, exposed
        here too since it's what UI hit-testing (``World.ui_hit_test``) uses
        internally each frame.
        """
    def get_typed_chars(self) -> list[int]:
        """Return UTF-32 codepoints typed since last frame, in order.

        Requires the cursor to be unlocked (``window.set_cursor_locked(False)``)
        — GLFW only emits character events then. Prefer the
        ``on_text_input`` Script callback for a focus-aware text field; use
        this directly for manual/global text capture.
        """

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
def reg_get(e: Entity, name: Literal["CompoundCollider"]) -> CompoundCollider | None: ...
@overload
def reg_get(e: Entity, name: Literal["Material"]) -> Material | None: ...
@overload
def reg_get(e: Entity, name: Literal["LightSource"]) -> LightSource | None: ...
@overload
def reg_get(e: Entity, name: Literal["Name"]) -> Name | None: ...
@overload
def reg_get(e: Entity, name: Literal["SpringConstraint"]) -> SpringConstraint | None: ...
@overload
def reg_get(e: Entity, name: Literal["PointJointConstraint"]) -> PointJointConstraint | None: ...
@overload
def reg_get(e: Entity, name: Literal["HingeJointConstraint"]) -> HingeJointConstraint | None: ...
@overload
def reg_get(e: Entity, name: Literal["ConeTwistJointConstraint"]) -> ConeTwistJointConstraint | None: ...
@overload
def reg_get(e: Entity, name: Literal["Parent"]) -> Parent | None: ...
@overload
def reg_get(e: Entity, name: Literal["ScriptComponent"]) -> ScriptComponent | None: ...
@overload
def reg_get(e: Entity, name: Literal["UITransform"]) -> UITransform | None: ...
@overload
def reg_get(e: Entity, name: Literal["UIBackground"]) -> UIBackground | None: ...
@overload
def reg_get(e: Entity, name: Literal["UITexturedBackground"]) -> UITexturedBackground | None: ...
@overload
def reg_get(e: Entity, name: Literal["UIText"]) -> UIText | None: ...
@overload
def reg_get(e: Entity, name: Literal["UIButton"]) -> UIButton | None: ...
@overload
def reg_get(e: Entity, name: Literal["TextLabel3D"]) -> TextLabel3D | None: ...
@overload
def reg_get(e: Entity, name: Literal["AudioSource"]) -> AudioSource | None: ...
@overload
def reg_get(e: Entity, name: Literal["Fixed"]) -> bool | None: ...
@overload
def reg_get(e: Entity, name: str) -> Any: ...
def reg_get(e: Entity, name: str) -> Any:
    """Get a component by name, or ``None`` if the entity doesn't have it.

    Args:
        e: Target entity.
        name: A ``ComponentName``.

    Returns:
        A **live reference** into engine memory (edits apply immediately), or
        ``None``. The ``"Fixed"`` tag component returns ``True`` / ``None``.

    Warning:
        Don't cache the result across a composition change (Hazard #1).
    """

def reg_has(e: Entity, name: ComponentName) -> bool:
    """Return ``True`` if the entity has the named component."""

def reg_valid(e: Entity) -> bool:
    """Return ``True`` if the entity handle is still alive (generation matches)."""

def is_sleeping(entity: Entity) -> bool:
    """Return ``True`` if the body is asleep (physics has parked it).

    ``wake(e)`` clears it. Equivalent to ``reg_get(e, "Hull").asleep``.
    """

def is_fixed(entity: Entity) -> bool:
    """Return ``True`` if the body carries the Fixed (static) tag.

    See ``fix_entity``. Also reachable via ``reg_has(e, "Fixed")``.
    """

def reg_add(entity: Entity, name: ComponentName) -> None:
    """Add a default-constructed component to an entity.

    Args:
        entity: Target entity.
        name: A ``ComponentName``.

    Raises:
        Exception: If ``name`` is unknown.

    Warning:
        Composition change — invalidates references held to this entity's
        components (Hazard #1). Mutate the new component via ``reg_get(e, name)``
        afterward.
    """

def reg_remove(entity: Entity, name: ComponentName) -> None:
    """Remove a component from an entity (no-op if absent).

    Args:
        entity: Target entity.
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

def attach_script(
    entity: Entity, module: str, class_name: str, params: dict[str, Any] = ...
) -> bool:
    """Create and immediately instantiate a Python behavior on ``entity`` at runtime.

    Scene-authored ``ScriptComponent``s are instantiated once, at scene load or
    editor Play, by ``SceneManager`` -- an entity spawned mid-game
    (``world.add_sphere``, ``world.add_trigger_box``, ...) never gets a live
    instance on its own, so its ``on_collision_enter``/``on_collision_exit``/
    ``update`` can't dispatch. ``attach_script`` closes that gap: give a
    dynamically spawned entity its own script the moment it's created --
    ``init()`` runs before this call returns; ``update()``/collision callbacks
    dispatch normally starting the next frame. The classic use case is spawning
    N things at runtime that each need independent behavior state (enemy waves,
    pickups, procedural placement) without hand-authoring every instance in a
    scene file::

        def update(self, world, entity, dt):
            e = world.add_sphere(1.0, 0.4, spawn_pos)
            world.attach_sphere_mesh(e, 0.4, 1.0, 0.3, 0.3)
            yope3d.attach_script(e, "behaviors.enemy", "Enemy", {"hp": 30})

    Args:
        entity: The (already-created) entity to attach the behavior to.
        module: Python module path under ``scripts/behaviors/``, e.g.
            ``"behaviors.enemy"`` -- same string a scene file's paramsBlob
            ``"module"`` key would hold.
        class_name: Class name within that module, e.g. ``"Enemy"``.
        params: Merged into the dict the new instance's ``init()`` receives
            (mirrors a scene file's paramsBlob keys beyond ``module``/``class``).
            Serialized to JSON internally -- must fit in the 2048-byte
            paramsBlob budget together with ``module``/``class``.

    Returns:
        ``True`` on success. ``False`` (no-op) if ``entity`` is invalid or
        already carries a live script instance -- this does not replace or
        reset an existing one.
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
        Coverage is sphere / AABB / OBB / capsule bodies (cylinder not yet).
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

def set_profile_scene(name: str) -> None:
    """Stamp the profiler CSV's ``scene`` column for all subsequent records.

    Call once from ``init()`` so profile rows are attributable to the scene
    (e.g. the stress-test scene stamps ``"Stress Test"`` for
    ``tools/analyze_profile.py`` runs).

    Args:
        name: Scene label written into every following CSV row.

    Note:
        Profiling is debug-build-only; in release builds this is a no-op.
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
    """Re-resolving read of the entity's LOCAL ``Transform.position`` (``None`` if no
    Transform). For a parented entity this is relative to its parent — use
    :func:`get_world_position` for the composed world position.

    Note:
        Safe to call after composition changes — it looks the component up per
        call (Hazard #1).
    """

def get_world_position(entity: Entity) -> Vec3 | None:
    """World-space position, composing the entity's ``Parent`` chain (``None`` if the
    entity is invalid). Equals :func:`get_position` for unparented (root) entities.
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
KEY_BACKSPACE: Final[int]
KEY_LEFT_SHIFT: Final[int]
KEY_LEFT_CONTROL: Final[int]

# ==============================================================================
# Mouse button constants (for Input.is_mouse_pressed / is_mouse_released)
# ==============================================================================

MOUSE_LEFT: Final[int]
MOUSE_RIGHT: Final[int]
MOUSE_MIDDLE: Final[int]

# ==============================================================================
# Easing constants (for World.tween_ui_opacity's `ease` argument)
# ==============================================================================

EASE_LINEAR: Final[int]
EASE_QUAD_IN: Final[int]
EASE_QUAD_OUT: Final[int]
EASE_QUAD_IN_OUT: Final[int]
EASE_CUBIC_IN: Final[int]
EASE_CUBIC_OUT: Final[int]
EASE_CUBIC_IN_OUT: Final[int]
