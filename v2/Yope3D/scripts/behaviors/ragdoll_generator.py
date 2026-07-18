"""
Procedural human ragdoll generator.

No external asset/rig is imported (this engine's glTF loader doesn't parse
skinning data — see CLAUDE.md's GJK/EPA + asset notes) — instead the rig is
built from standard anthropometric proportions (segment-length and segment-
mass fractions of total body height/mass, Winter's "Biomechanics and Motor
Control of Human Movement" table) and documented human range-of-motion (ROM)
values for each joint. Everything here is a plain function, not a Script
class, so it can be called from any scene-logic script or the console.

Rig (11 OBB bodies, all upright/arms-at-sides at rest so every box's
local +Y axis is the world "long axis" — no per-limb rotation math
needed): pelvis (root) -> torso -> head, torso -> {L,R} upper arm -> forearm,
pelvis -> {L,R} thigh -> shin. Each non-root body holds exactly one joint back
to its parent (never the reverse) — a hub like the torso can have any number
of children this way despite each entity only carrying one joint component
per type (see Components.h's PointJointConstraint/HingeJointConstraint docs).

Joint types: Hinge at elbows/knees (single bend axis); Cone-Twist at neck/
spine/shoulders/hips (swing + independent twist — the anatomically correct
type for ball-and-socket-like joints). ROM limits below are reasonable
textbook approximations, not measured in-engine against a specific hinge-axis
sign convention — if a limit visibly bends the wrong way for your rig's
orientation, negate the min/max fields until it looks right.
"""
import yope3d

_RAGDOLL_LAYER = 1 << 1

# Self-collision mode:
#   "full"     — every part collides with every other part. Most physically
#                correct; without a collider shrink it JAMS/stiffens at the
#                joints (adjacent limbs overlap at their shared anchor).
#   "adjacent" — only NON-jointed pairs collide (game-standard middle ground):
#                removes the joint jamming while still blocking a limb from
#                clipping through the body.
#   "off"      — no self-collision (fully ragdolly, limbs pass through).
_SELF_COLLISION = "full"

# Physics collider extents are scaled by this factor relative to the (full-size)
# visual mesh; 1.0 = collider matches mesh. Shrinking (<1) opens a resting gap
# between limbs so "full" self-collision stops jamming at the joints, while
# gross interpenetration is still blocked. Mass/inertia keep the FULL-limb
# values (computed at add_obb time) — only the collision proxy shrinks, which is
# the standard physics-asset trick. Thickness clearance (X/Z) keeps the upper
# arm off the torso; length clearance (Y) keeps segment end-caps from grinding
# at each joint.
_COLLIDER_SHRINK = 0.7

# Directly-jointed (adjacent) segment pairs — used by the "adjacent" mode.
_ADJACENCY = [
    ("pelvis", "torso"),         ("torso", "head"),
    ("torso", "left_uarm"),      ("left_uarm", "left_farm"),
    ("torso", "right_uarm"),     ("right_uarm", "right_farm"),
    ("pelvis", "left_thigh"),    ("left_thigh", "left_shin"),
    ("pelvis", "right_thigh"),   ("right_thigh", "right_shin"),
]


def _apply_self_collision_filter(parts):
    """Set each part's collision layer/mask according to _SELF_COLLISION."""
    if _SELF_COLLISION == "full":
        return   # leave default 0xFFFFFFFF layer/mask — everything collides

    if _SELF_COLLISION == "off":
        for e in parts.values():
            hull = yope3d.reg_get(e, "Hull")
            hull.collision_layer = _RAGDOLL_LAYER
            hull.collision_mask  = 0xFFFFFFFF & ~_RAGDOLL_LAYER
        return

    # "adjacent": each segment gets a unique layer bit and masks out its own bit
    # plus its jointed neighbours' — non-adjacent pairs still collide. World
    # collision is untouched (each part still advertises a world-visible bit).
    # Note: 11 parts fit in 32 bits for ONE ragdoll; bits are reused across
    # instances, so two ragdolls won't collide 100% correctly with each other —
    # a proper per-body ignore list is the real long-term fix.
    names = list(parts.keys())
    bit = {name: 1 << i for i, name in enumerate(names)}
    neighbours = {name: set() for name in names}
    for a, b in _ADJACENCY:
        neighbours[a].add(b)
        neighbours[b].add(a)
    for name, e in parts.items():
        exclude = bit[name]
        for nb in neighbours[name]:
            exclude |= bit[nb]
        hull = yope3d.reg_get(e, "Hull")
        hull.collision_layer = bit[name]
        hull.collision_mask  = 0xFFFFFFFF & ~exclude


def _shrink_colliders(parts, factor):
    """Scale every part's OBB collider extent by `factor`, leaving the visual
    mesh (and mass/inertia) at full size. See _COLLIDER_SHRINK."""
    if factor >= 0.999:
        return
    for e in parts.values():
        form = yope3d.reg_get(e, "OBBForm")
        ext = form.extent
        form.extent = yope3d.Vec3(ext.x * factor, ext.y * factor, ext.z * factor)

_ELBOW_LIMIT = True

# --- Range of motion constraints (radians) ---
ELBOW_LOWER, ELBOW_UPPER = 0.0, 2.4     # fold up toward shoulder
KNEE_LOWER,  KNEE_UPPER  = -2.4, 0.0    # fold back toward thigh
SHOULDER_SWING, SHOULDER_TWIST = 1.3, 0.4
HIP_SWING,      HIP_TWIST      = 0.9, 0.3
SPINE_SWING,    SPINE_TWIST    = 0.4, 0.2
NECK_SWING,     NECK_TWIST     = 0.6, 0.3


def spawn_ragdoll(world, total_height=1.8, total_mass=70.0, spawn_pos=yope3d.Vec3(0, 3, 0),
                  persist=False):
    """Spawns an 11-segment human ragdoll based on Winter's data using OBBs for collision response.

    Returns a dict mapping part names to entities.

    persist=True makes the result save-able as a scene: every joint also writes
    its serializable mirror component (add_*_joint(persist=True)), and every part
    is finalize_entity()'d (Name + EditorSelectable/Pickable) so the scene
    serializer actually saves it. Use it from the setup-script entry point; leave
    it False for the runtime behavior spawn (RagdollDemo), which is transient.
    """
    h = total_height
    m = total_mass

    # 1. Kinematic segments (lengths & masses derived as fractions)
    # Total H layout splits: feet-to-hip ~0.53H, hip-to-neck ~0.29H, head ~0.18H
    pelvis_len = 0.08 * h
    torso_len  = 0.21 * h
    head_len   = 0.13 * h  # remaining 0.05 is neck spacing
    thigh_len  = 0.25 * h
    shin_len   = 0.25 * h
    uarm_len   = 0.19 * h
    farm_len   = 0.19 * h

    # Masses (Winter fractions of total mass M)
    pelvis_m = 0.142 * m
    torso_m  = 0.436 * m
    head_m   = 0.081 * m
    thigh_m  = 0.100 * m
    shin_m   = 0.047 * m
    uarm_m   = 0.028 * m
    farm_m   = 0.022 * m

    # Radii used as X/Z half-extents (thickness)
    pelvis_r = 0.13
    torso_r  = 0.15
    head_r   = 0.10
    thigh_r  = 0.09
    shin_r   = 0.07
    uarm_r   = 0.06
    farm_r   = 0.05

    # Helper to calculate box half-extents Vec3. 
    # For an OBB, the half-extent along the long axis (Y) is simply segment_len / 2.0.
    # X and Z dimensions are determined by the thickness radius.
    def get_extents(length, radius):
        return yope3d.Vec3(radius, length / 2.0, radius)

    # 2. Compute absolute Y positions for centers (stacked along world Y)
    shin_y   = spawn_pos.y + shin_len / 2.0
    thigh_y  = spawn_pos.y + shin_len + thigh_len / 2.0
    pelvis_y = spawn_pos.y + shin_len + thigh_len + pelvis_len / 2.0

    torso_bottom_y = spawn_pos.y + shin_len + thigh_len + pelvis_len
    torso_y        = torso_bottom_y + torso_len / 2.0
    torso_top_y    = torso_bottom_y + torso_len

    head_y = torso_top_y + 0.05 + head_len / 2.0  # +5cm neck gap

    # Arms hang down from the top of the torso
    uarm_y = torso_top_y - uarm_len / 2.0
    farm_y = torso_top_y - uarm_len - farm_len / 2.0

    # Horizontal spacing offsets (X axis)
    hip_offset = 0.12
    arm_offset = torso_r + uarm_r + 0.01

    parts = {}

    # 3. Create bodies (Swapped from add_capsule to add_obb / attach_box_mesh)
    # --- Central Trunk ---
    p_pos = yope3d.Vec3(spawn_pos.x, pelvis_y, spawn_pos.z)
    p_ext = get_extents(pelvis_len, pelvis_r)
    pelvis_e = world.add_obb(p_ext, pelvis_m, p_pos)
    world.attach_box_mesh(pelvis_e, p_ext, 0.6, 0.6, 0.6)
    parts["pelvis"] = pelvis_e

    t_pos = yope3d.Vec3(spawn_pos.x, torso_y, spawn_pos.z)
    t_ext = get_extents(torso_len, torso_r)
    torso_e = world.add_obb(t_ext, torso_m, t_pos)
    world.attach_box_mesh(torso_e, t_ext, 0.3, 0.5, 0.8)
    parts["torso"] = torso_e

    h_pos = yope3d.Vec3(spawn_pos.x, head_y, spawn_pos.z)
    h_ext = get_extents(head_len, head_r)
    head_e = world.add_obb(h_ext, head_m, h_pos)
    world.attach_box_mesh(head_e, h_ext, 0.9, 0.7, 0.6)
    parts["head"] = head_e

    # --- Legs ---
    for side, sign in [("left", -1), ("right", 1)]:
        t_p = yope3d.Vec3(spawn_pos.x + sign * hip_offset, thigh_y, spawn_pos.z)
        t_ext = get_extents(thigh_len, thigh_r)
        thigh_e = world.add_obb(t_ext, thigh_m, t_p)
        world.attach_box_mesh(thigh_e, t_ext, 0.2, 0.2, 0.2)
        parts[f"{side}_thigh"] = thigh_e

        s_p = yope3d.Vec3(spawn_pos.x + sign * hip_offset, shin_y, spawn_pos.z)
        s_ext = get_extents(shin_len, shin_r)
        shin_e = world.add_obb(s_ext, shin_m, s_p)
        world.attach_box_mesh(shin_e, s_ext, 0.2, 0.2, 0.2)
        parts[f"{side}_shin"] = shin_e

    # --- Arms ---
    for side, sign in [("left", -1), ("right", 1)]:
        ua_p = yope3d.Vec3(spawn_pos.x + sign * arm_offset, uarm_y, spawn_pos.z)
        ua_ext = get_extents(uarm_len, uarm_r)
        uarm_e = world.add_obb(ua_ext, uarm_m, ua_p)
        world.attach_box_mesh(uarm_e, ua_ext, 0.9, 0.7, 0.6)
        parts[f"{side}_uarm"] = uarm_e

        fa_p = yope3d.Vec3(spawn_pos.x + sign * arm_offset, farm_y, spawn_pos.z)
        fa_ext = get_extents(farm_len, farm_r)
        farm_e = world.add_obb(fa_ext, farm_m, fa_p)
        world.attach_box_mesh(farm_e, fa_ext, 0.9, 0.7, 0.6)
        parts[f"{side}_farm"] = farm_e

    # 3b. Self-collision policy (_SELF_COLLISION) + collider shrink
    # (_COLLIDER_SHRINK). Shrink runs AFTER the filter so it applies regardless
    # of mode — with "full" self-collision the shrink is what keeps the joints
    # from jamming; see both constants up top.
    _apply_self_collision_filter(parts)
    _shrink_colliders(parts, _COLLIDER_SHRINK)

    # 4. Bind Joints
    # --- Left Arm Joints ---
    sh_l_anchor = yope3d.Vec3(spawn_pos.x - arm_offset, torso_top_y, spawn_pos.z)
    # Twist axis is (0,-1,0) — ALONG the arm bone (which hangs down the Y axis),
    # matching the hips. NOT (-1,0,0): that pointed the cone sideways, out of the
    # torso, so ordinary forward/back arm swing got misread as twist (limited to
    # a tight 0.4 rad) and the swing/twist decomposition ran in its unstable
    # misaligned region — the erratic-shoulder / elbow-disjoint bug.
    world.add_cone_twist_joint(parts["left_uarm"], torso_e, sh_l_anchor, yope3d.Vec3(0, -1, 0),
                               SHOULDER_SWING, SHOULDER_TWIST, persist=persist)
    el_l_anchor = yope3d.Vec3(spawn_pos.x - arm_offset, torso_top_y - uarm_len, spawn_pos.z)
    world.add_hinge_joint(parts["left_farm"], parts["left_uarm"], el_l_anchor, yope3d.Vec3(0, 0, 1),
                          _ELBOW_LIMIT, ELBOW_LOWER, ELBOW_UPPER, persist=persist)

    # --- Right Arm Joints ---
    sh_r_anchor = yope3d.Vec3(spawn_pos.x + arm_offset, torso_top_y, spawn_pos.z)
    # (0,-1,0) along the arm bone, same as the left shoulder / the hips.
    world.add_cone_twist_joint(parts["right_uarm"], torso_e, sh_r_anchor, yope3d.Vec3(0, -1, 0),
                               SHOULDER_SWING, SHOULDER_TWIST, persist=persist)
    el_r_anchor = yope3d.Vec3(spawn_pos.x + arm_offset, torso_top_y - uarm_len, spawn_pos.z)
    world.add_hinge_joint(parts["right_farm"], parts["right_uarm"], el_r_anchor, yope3d.Vec3(0, 0, 1),
                          _ELBOW_LIMIT, ELBOW_LOWER, ELBOW_UPPER, persist=persist)

    # --- Left Leg Joints ---
    hip_l_anchor = yope3d.Vec3(spawn_pos.x - hip_offset, pelvis_y + pelvis_len / 2.0, spawn_pos.z)
    world.add_cone_twist_joint(parts["left_thigh"], pelvis_e, hip_l_anchor, yope3d.Vec3(0, -1, 0),
                               HIP_SWING, HIP_TWIST, persist=persist)
    knee_l_anchor = yope3d.Vec3(spawn_pos.x - hip_offset, spawn_pos.y + shin_len, spawn_pos.z)
    world.add_hinge_joint(parts["left_shin"], parts["left_thigh"], knee_l_anchor, yope3d.Vec3(0, 0, 1),
                          True, KNEE_LOWER, KNEE_UPPER, persist=persist)

    # --- Right Leg Joints ---
    hip_r_anchor = yope3d.Vec3(spawn_pos.x + hip_offset, pelvis_y + pelvis_len / 2.0, spawn_pos.z)
    world.add_cone_twist_joint(parts["right_thigh"], pelvis_e, hip_r_anchor, yope3d.Vec3(0, -1, 0),
                               HIP_SWING, HIP_TWIST, persist=persist)
    knee_r_anchor = yope3d.Vec3(spawn_pos.x + hip_offset, spawn_pos.y + shin_len, spawn_pos.z)
    world.add_hinge_joint(parts["right_shin"], parts["right_thigh"], knee_r_anchor, yope3d.Vec3(0, 0, 1),
                          True, KNEE_LOWER, KNEE_UPPER, persist=persist)

    # Torso <-> pelvis (spine) and head <-> torso (neck)
    spine_anchor = yope3d.Vec3(p_pos.x, torso_bottom_y, p_pos.z)  # Fixed unassigned pelvis_pos bug here
    world.add_cone_twist_joint(torso_e, pelvis_e, spine_anchor, yope3d.Vec3(0, 1, 0),
                               SPINE_SWING, SPINE_TWIST, persist=persist)
    neck_anchor = yope3d.Vec3(p_pos.x, torso_top_y, p_pos.z)
    world.add_cone_twist_joint(head_e, torso_e, neck_anchor, yope3d.Vec3(0, 1, 0),
                               NECK_SWING, NECK_TWIST, persist=persist)

    # Rename the parts for a legible Hierarchy (add_obb already finalized them as
    # "OBB" — Name + EditorSelectable/Pickable — so they're saveable regardless;
    # finalize_entity is idempotent and just overwrites the Name here). Only for
    # the persist/setup path; the transient runtime spawn doesn't need names.
    if persist:
        for name, e in parts.items():
            world.finalize_entity(e, f"ragdoll_{name}")

    return parts


class RagdollDemo:
    """Attach to a scene-logic host entity to spawn one ragdoll on scene load."""

    PARAMS = {
        "height": {"type": "float", "default": 1.8,  "label": "Height (m)"},
        "mass":   {"type": "float", "default": 70.0, "label": "Mass (kg)"},
        "spawn_y": {"type": "float", "default": 3.0,  "label": "Spawn Height (m)"},
    }

    # --- Debug joint driver -------------------------------------------------
    # Select a hinge segment with the number row, then hold an arrow key to spin
    # it about its hinge axis: RIGHT = positive (extend outward / drive past the
    # limit), LEFT = negative (fold back toward the parent segment / other limit).
    # Lets you exercise a single elbow/knee limit in isolation without fighting
    # the whole chain through the mouse drag. Flip _DRIVE_SIGN if RIGHT doesn't
    # extend the way you expect for your hinge-axis convention.
    _SEG_KEYS = {49: "left_farm", 50: "right_farm", 51: "left_shin", 52: "right_shin"}  # keys 1..4
    _DRIVE_SPEED = 3.0   # rad/s about the hinge axis while an arrow is held
    _DRIVE_SIGN  = 1.0   # global flip if RIGHT/LEFT feel reversed

    def init(self, world, entity, params):
        h = params.get("height", 1.8)
        m = params.get("mass", 70.0)
        y = params.get("spawn_y", 3.0)
        self._parts = spawn_ragdoll(world, h, m, yope3d.Vec3(0.0, y, 5.0))
        self._sel = "left_farm"   # default selection (key 1)

    def update(self, world, entity, dt):
        inp = yope3d.input
        # Change selection (one-shot) with keys 1..4.
        for key, name in self._SEG_KEYS.items():
            if inp.is_key_pressed(key):
                self._sel = name
                print(f"[ragdoll driver] selected {name}")

        # Drive the selected segment about its hinge axis while an arrow is held.
        sign = 0.0
        if inp.is_key_down(yope3d.KEY_RIGHT): sign += 1.0
        if inp.is_key_down(yope3d.KEY_LEFT):  sign -= 1.0
        if sign != 0.0:
            seg = self._parts[self._sel]
            tf = yope3d.reg_get(seg, "Transform")
            # Hinge local axis is (0,0,1) on the child segment (see the
            # add_hinge_joint calls); rotate it into world space by the segment's
            # current orientation so the drive stays about the true hinge axis.
            axis_world = tf.rotation.rotate(yope3d.Vec3(0.0, 0.0, 1.0))
            omega = axis_world * (sign * self._DRIVE_SIGN * self._DRIVE_SPEED)
            yope3d.reg_get(seg, "Hull").omega = omega
            world.wake(seg)


# ---------------------------------------------------------------------------
# Scene-setup entry point
# ---------------------------------------------------------------------------
# Load this file in the editor's Scene Script window (Load File -> Run) to spawn
# a ragdoll straight into the live edit-mode world — real, selectable, tunable,
# SAVE-ABLE entities — instead of a Play-only behavior. Same yope3d.world.*
# pattern as the panel's built-in example. The `__name__` guard keeps this from
# firing when the module is merely IMPORTED (e.g. for the RagdollDemo behavior
# class above): execString runs in __main__, imports run under the module name.
#
# persist=True is what makes it round-trip Save Scene: every joint writes its
# serializable mirror component and every part is finalize_entity()'d so the
# serializer saves it. Select a part and tune its joint in the Inspector, then
# Save Scene — the reloaded scene rebuilds the live joints from the mirrors.
if __name__ == "__main__":
    _setup_parts = spawn_ragdoll(yope3d.world, 1.8, 70.0, yope3d.Vec3(0.0, 3.0, 5.0), persist=True)
    print(f"[ragdoll setup] spawned {len(_setup_parts)} saveable parts — tune in the Hierarchy/Inspector, then Save Scene")
