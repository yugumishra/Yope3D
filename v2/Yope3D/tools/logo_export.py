"""
logo_export.py — Blender (4.3+ / Grease Pencil v3) exporter for the Yope3D
loading-screen logo, PART 2 (the rigid-body tumble).

Run from inside Blender (Scripting → Run Script, or
`blender file.blend --background --python tools/logo_export.py`). Bakes the
Part 2 range into a JSON of per-frame 2D line segments, projected through the
scene camera, that the engine replays via `logo_playback.py`.

Sources, per letter of LETTERS ("Yope3D"):
  - "LineArt <L>+"  → FRONT contour (forward copy, +depth), a GP Line Art object
  - "LineArt <L>-"  → BACK  contour (backward copy, -depth)
  Emits front-loop + back-loop segments, plus CONNECTORS front→back placed by
  EVEN ARC LENGTH (not per-vertex — so Y's straights and D's curve get the same
  connector density). Front↔back contours are paired by nearest centroid and
  connected at a shared arc-length parameter (robust to differing point counts).

Plus SPHERE_OBJECTS: solid meshes rendered as their full edge wireframe (read
directly from the mesh — an icosphere's Line Art would only give the silhouette
circle, so we skip Line Art for it entirely).

Resampling: the sim may be authored at 24 fps; set OUTPUT_FPS to resample to
e.g. 60 via Blender subframe interpolation (motion-blur-style) — no re-bake.

Occlusion (hidden-line): OCCLUSION_METHOD="bvh" (default) depth-tests each
contour sample against a per-frame BVH of all solids via an eye ray from the
camera — one knob (DEPTH_EPS), no per-object heuristics. "raycast_v1" is the
legacy per-object raycast + bias path, kept only for A/B comparison.

Framing: run tools/logo_camera_center.py FIRST (physically centers the real
Blender camera on the letters at the framing frame) — occlusion is a function
of camera POSITION, so framing has to happen before this script raycasts, not
after. With SELF_FRAME_2D = False (default), this script trusts that upstream
centering and just passes through Blender's native cam-view [0,1] coords
against the render's own aspect. SELF_FRAME_2D = True re-enables the old
post-hoc 2D bbox crop (kept as a fallback/legacy path — note it scales u/v
independently, which can introduce a slight aspect stretch that the physical
camera-dolly approach avoids).
"""

import bpy
import json
import os
import math
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from bpy_extras.object_utils import world_to_camera_view

# ----------------------------------------------------------------------------
# CONFIG — edit these
# ----------------------------------------------------------------------------
LETTERS           = "Yope3D"          # objects are "LineArt <L>+" / "LineArt <L>-"
SPHERE_OBJECTS    = ["Icosphere"]     # solid meshes drawn as full edge wireframe
CONNECTOR_SPACING = 0.05              # world units between connectors along a contour
OUTPUT_FPS        = 60.0              # resample sim to this fps (subframe interp)
BAKE_PHYSICS      = True              # bake the rigid-body cache first — REQUIRED
                                      #   for OUTPUT_FPS != src fps (subframe interp
                                      #   stalls on an unbaked/live sim).

# Hidden-line removal. Needs the SOLID meshes present.
OCCLUSION_CULL    = True
OCCLUDER_OBJECTS  = None              # None = auto (the letter solids + SPHERE_OBJECTS)

# --- OCCLUSION METHOD -------------------------------------------------------
# "bvh"  (default): TRUE depth test. Per frame, build one BVH of ALL solids.
#   For each contour sample P, cast an EYE ray cam->P and get the distance t to
#   the nearest solid; P is visible iff t >= |cam-P| - DEPTH_EPS (the frontmost
#   solid at that pixel is AT or BEHIND P). This asks the correct occlusion
#   question ("what is frontmost at this pixel"), so it needs NONE of the
#   SELF_/CROSS_/GRAZE/test_self/near-far heuristics: self-rest, cross-occlusion,
#   coplanar-cap grazing, and through-hole far rims all fall out for free. One
#   knob, DEPTH_EPS, and it's forgiving because the +/-0.03 copy offset gives the
#   near copy a 0.03 margin in front of its surface and the far copy 0.03 behind.
# "raycast_v1": the legacy per-object raycast + bias heuristics below. Kept only
#   for A/B comparison against the BVH bake; all the SELF_/CROSS_/GRAZE knobs and
#   the near/far test_self logic apply ONLY to this path.
OCCLUSION_METHOD  = "bvh"
DEPTH_EPS         = 0.01              # (bvh) eye-ray depth tolerance. Must stay well
                                      #   under the +/-0.03 copy offset so the near copy
                                      #   (0.03 in front) isn't culled and the far copy
                                      #   (0.03 behind) still is.

# ============================================================================
# LEGACY (raycast_v1 only) — ignored when OCCLUSION_METHOD == "bvh".
# Kept for A/B comparison. See git history / the v1 discussion for why each knob
# existed; the BVH path makes all of them unnecessary.
# ----------------------------------------------------------------------------
# --- SELF occlusion (occluder == this contour's own letter solid) -----------
SELF_BIAS         = 0.05              # step origin off own surface toward cam before
                                      #   testing (the "+" copy sits inside the body)
SELF_MIN_HIT_DIST = 0.065             # ignore own-body hits closer than this as the
                                      #   contour resting in/on its own solid — keeps
                                      #   the camera-facing outline visible.
# --- CROSS occlusion (occluder is a DIFFERENT solid) ------------------------
CROSS_BIAS        = 0.002             # near-zero: don't shove the origin, so a neighbor
                                      #   right up against this point still occludes and
                                      #   the result doesn't flip as poses jitter.
CROSS_MIN_HIT_DIST = 0.002            # honor cross-occlusion at ANY real distance,
                                      #   including tightly-piled letters (the whole
                                      #   point of the tumble). Only rejects a
                                      #   coincident/zero-distance grazing touch.
# --- grazing/tangent-hit rejection (self AND cross) -------------------------
GRAZE_COS         = 0.11              # reject any hit whose surface normal is nearly
                                      #   PERPENDICULAR to the ray (|cos| below this) —
                                      #   a tangent skim, not a real occlusion. The
                                      #   letters all lie flat & roughly COPLANAR on the
                                      #   floor, so a shallow ray to the elevated camera
                                      #   grazes across a neighbour's (or its own) flat
                                      #   top cap and registers a spurious hit; a real
                                      #   occluder is entered head-on. Cap grazes have
                                      #   |cos|~0 (vertical cap normal vs a shallow ray);
                                      #   genuine side-wall occlusion has |cos| well
                                      #   above this (horizontal wall normal vs the ray's
                                      #   large horizontal component). Raise toward ~0.3
                                      #   if false occlusion persists; lower if real
                                      #   occlusion starts leaking through.
# ============================================================================
# END LEGACY. Below applies to BOTH methods.
# ----------------------------------------------------------------------------
OCCLUSION_SAMPLES = 5                 # points tested along each segment (incl. both
                                      #   ends); partially-occluded segments are CLIPPED
                                      #   into their visible sub-run(s) rather than
                                      #   kept/dropped as a whole. Cost is one-time
                                      #   (offline bake) — raise freely for smoother
                                      #   clip boundaries on thick/angled letters.

# Framing is now done PHYSICALLY, upstream, by tools/logo_camera_center.py
# (it moves the real camera before this script raycasts — see module docstring
# for why occlusion requires that ordering). Leave this False in the normal
# workflow. True re-enables the legacy post-hoc 2D crop as a fallback.
SELF_FRAME_2D     = False
FRAMING_FRAME_T   = 0.0               # (SELF_FRAME_2D=True only) seconds to frame on
MARGIN            = 0.06              # (SELF_FRAME_2D=True only) border around bbox
FRAME_START       = None              # None = scene.frame_start
FRAME_END         = None              # None = scene.frame_end
OUTPUT_PATH       = "/Users/me/Desktop/dev/Yope3D/v2/Yope3D/assets/logo/logo_part2.json"
# ----------------------------------------------------------------------------

scene = bpy.context.scene
cam   = scene.camera
if cam is None:
    raise RuntimeError("Scene has no active camera (scene.camera is None).")


# ---- Grease Pencil (v3) Line Art readers -----------------------------------
def layer_drawing(layer, frame_num):
    chosen = None
    try:
        for fr in layer.frames:
            if fr.frame_number <= frame_num:
                chosen = fr
            else:
                break
        if chosen is None and len(layer.frames) > 0:
            chosen = layer.frames[0]
    except Exception:
        return None
    return chosen.drawing if chosen else None


def read_contours(obj_name, depsgraph):
    """GP Line Art object → list of (points:[Vector], cyclic:bool) in world space."""
    obj = bpy.data.objects.get(obj_name)
    if obj is None:
        return None
    ob_eval = obj.evaluated_get(depsgraph)
    data    = ob_eval.data
    mw      = ob_eval.matrix_world
    out = []
    if not hasattr(data, "layers"):
        return out
    for layer in data.layers:
        drw = layer_drawing(layer, scene.frame_current)
        if drw is None:
            continue
        for s in drw.strokes:
            pts = [mw @ Vector(p.position) for p in s.points]
            if len(pts) >= 2:
                out.append((pts, bool(getattr(s, "cyclic", False))))
    return out


def read_mesh_edges(obj_name, depsgraph):
    """Solid mesh object → list of (a:Vector, b:Vector) world-space edges."""
    obj = bpy.data.objects.get(obj_name)
    if obj is None:
        return None
    ob_eval = obj.evaluated_get(depsgraph)
    try:
        me = ob_eval.to_mesh()
    except Exception:
        return []
    mw = ob_eval.matrix_world
    verts = [mw @ v.co for v in me.vertices]
    edges = [(verts[e.vertices[0]], verts[e.vertices[1]]) for e in me.edges]
    ob_eval.to_mesh_clear()
    return edges


# ---- geometry helpers ------------------------------------------------------
def polyline_length(pts, cyclic):
    total = 0.0
    for i in range(len(pts) - 1):
        total += (pts[i + 1] - pts[i]).length
    if cyclic and len(pts) >= 2:
        total += (pts[0] - pts[-1]).length
    return total


def resample_fractions(pts, cyclic, fractions):
    ring = list(pts) + ([pts[0]] if cyclic else [])
    seglens, total = [], 0.0
    for i in range(len(ring) - 1):
        d = (ring[i + 1] - ring[i]).length
        seglens.append(d)
        total += d
    if total < 1e-9:
        return [ring[0] for _ in fractions]
    result = []
    for t in fractions:
        target = total * max(0.0, min(1.0, t))
        acc, idx = 0.0, 0
        while idx < len(seglens) and acc + seglens[idx] < target:
            acc += seglens[idx]
            idx += 1
        if idx >= len(seglens):
            result.append(ring[-1])
        else:
            f = (target - acc) / seglens[idx] if seglens[idx] > 1e-9 else 0.0
            result.append(ring[idx].lerp(ring[idx + 1], f))
    return result


def centroid(pts):
    c = Vector((0.0, 0.0, 0.0))
    for p in pts:
        c += p
    return c / len(pts)


# ---- hidden-line removal: BVH depth test (default method) ------------------
def occluder_names():
    if OCCLUDER_OBJECTS is not None:
        return list(OCCLUDER_OBJECTS)
    return list(LETTERS) + list(SPHERE_OBJECTS)


def build_bvh(depsgraph):
    """One BVHTree over ALL solid occluders in world space (rebuilt per frame,
    since the letters tumble). Returns (bvh, tri_owner) — tri_owner[i] is the
    object name owning triangle i, so a hit can be classified self vs other.
    Returns (None, None) if no geometry is found.

    Feeds Blender's own loop-triangle triangulation (all_triangles=True) rather
    than raw polygons — a letter's cap is a CONCAVE ngon, and FromPolygons would
    fan-triangulate it incorrectly, poking spurious depth through the concavity.
    """
    verts, tris, tri_owner = [], [], []
    for nm in occluder_names():
        o = bpy.data.objects.get(nm)
        if o is None:
            continue
        oe = o.evaluated_get(depsgraph)   # rigid-body pose lives on the eval object
        if oe.type != 'MESH':
            continue
        try:
            me = oe.to_mesh()
        except Exception:
            continue
        me.calc_loop_triangles()
        mw = oe.matrix_world
        base = len(verts)
        verts.extend(mw @ v.co for v in me.vertices)
        for lt in me.loop_triangles:
            v0, v1, v2 = lt.vertices
            tris.append((base + v0, base + v1, base + v2))
            tri_owner.append(nm)          # parallel to tris; ray_cast returns this index
        oe.to_mesh_clear()
    if not verts or not tris:
        return None, None
    return BVHTree.FromPolygons(verts, tris, all_triangles=True), tri_owner


def is_visible_bvh(P, owner, cam_pos, bvh, tri_owner, ignore_self):
    """Depth test via eye ray cam->P against the combined solid BVH.

    Base rule (ignore_self=False, used for the far cap / wall connectors /
    sphere): P is visible iff the nearest solid along the ray is AT or BEHIND it.

    ignore_self=True (the NEAR cap): a hit on P's OWN body doesn't count — the
    camera-facing cap is the letter's topmost surface and is drawn regardless of
    grazing self-occlusion (its far rim tucking behind its own near wall at
    oblique angles). A nearer hit on ANOTHER object still occludes: since a real
    cross-occluder sits in FRONT of P it becomes the nearest hit, so we only need
    to check the nearest one.
    """
    if bvh is None:
        return True
    d = P - cam_pos
    dist = d.length
    if dist < 1e-6:
        return True
    loc, _n, idx, t = bvh.ray_cast(cam_pos, d / dist)
    if loc is None:
        return True                                    # nothing solid along the ray
    if ignore_self and tri_owner[idx] == owner:
        return True                                    # own body — near cap shows anyway
    return t >= dist - DEPTH_EPS                        # frontmost solid AT or BEHIND P


# ---- hidden-line removal: legacy per-object raycast (raycast_v1 only) --------
def build_occluders(depsgraph):
    names = OCCLUDER_OBJECTS
    if names is None:
        names = list(LETTERS) + list(SPHERE_OBJECTS)
    occ = []
    for nm in names:
        o = bpy.data.objects.get(nm)
        if o is None:
            continue
        oe = o.evaluated_get(depsgraph)   # rigid-body pose lives on the eval object
        if oe.type != 'MESH':
            continue
        mw = oe.matrix_world.copy()
        occ.append((nm, oe, mw, mw.inverted()))
    return occ


def is_visible(P, owner, cam_pos, occluders, test_self):
    """True if world point P (on `owner`'s contour) has clear sight to the camera.

    Self (occluder name == owner) uses the big SELF_* tolerance so the contour
    resting in/on its own body doesn't self-cull the camera-facing outline;
    every OTHER solid uses the tight CROSS_* tolerance so a neighbor pressed
    right up against P still occludes (and the decision is stable in a pile).

    `test_self=False` skips the own-body occluder entirely — used for the NEAR
    cap outline, which is the letter's topmost/frontmost surface and thus can
    never be self-occluded. Raycasting it anyway produced false hits at grazing
    angles (a letter lying flat: the ray to the camera skims along its own top
    face and clips a raised feature across the letter), chopping the top rings.
    """
    to_cam = cam_pos - P
    dist = to_cam.length
    if dist < 1e-6:
        return True
    d = to_cam / dist

    for nm, oe, mw, minv in occluders:
        self_hit = (nm == owner)
        if self_hit and not test_self:
            continue
        bias    = SELF_BIAS         if self_hit else CROSS_BIAS
        min_hit = SELF_MIN_HIT_DIST if self_hit else CROSS_MIN_HIT_DIST
        O = P + d * bias           # step off the surface toward the camera
        maxd = dist - bias
        if maxd <= 0.0:
            continue
        o_l = minv @ O
        d_l = (minv.to_3x3() @ d)
        nl = d_l.length
        if nl < 1e-12:
            continue
        d_l = d_l / nl
        hit, loc, n_l, _i = oe.ray_cast(o_l, d_l, distance=1e12)
        if not hit:
            continue
        if n_l.length > 1e-9 and abs(d_l.dot(n_l.normalized())) < GRAZE_COS:
            continue               # tangent skim across a flat cap, not real occlusion
        hit_dist = ((mw @ loc) - O).length
        if hit_dist < min_hit:     # too close: self-rest (SELF) / grazing touch (CROSS)
            continue
        if hit_dist < maxd:
            return False
    return True


# ----------------------------------------------------------------------------
# Bake (resampled to OUTPUT_FPS via subframes)
# ----------------------------------------------------------------------------
src_fps = scene.render.fps / scene.render.fps_base
f0 = scene.frame_start if FRAME_START is None else FRAME_START
f1 = scene.frame_end   if FRAME_END   is None else FRAME_END
duration = (f1 - f0) / src_fps
n_out = max(1, int(round(duration * OUTPUT_FPS)) + 1)
framing_k = max(0, min(n_out - 1, int(round(FRAMING_FRAME_T * OUTPUT_FPS))))

# Subframe resampling reads an interpolated rigid-body cache; a live/unbaked sim
# stalls on fractional & repeated frames (the "frozen ball at 60 fps" bug).
if BAKE_PHYSICS:
    rbw = scene.rigidbody_world
    if rbw is not None and rbw.point_cache is not None:
        rbw.point_cache.frame_start = f0
        rbw.point_cache.frame_end   = f1
    try:
        bpy.ops.ptcache.free_bake_all()
    except Exception:
        pass
    try:
        bpy.ops.ptcache.bake_all(bake=True)
        print("logo_export: baked point caches over frames %d..%d" % (f0, f1))
    except Exception as e:
        print("logo_export: WARNING — auto-bake failed (%s). Bake the Rigid Body "
              "World cache manually (Scene Properties > Rigid Body World > Cache "
              "> Bake) or set OUTPUT_FPS to the scene fps." % e)

rx = scene.render.resolution_x * scene.render.pixel_aspect_x
ry = scene.render.resolution_y * scene.render.pixel_aspect_y
ref_aspect = rx / ry

frames_out = []
diag_printed = False
framing_letter_count = None   # # of letter/connector segs on the framing frame

for k in range(n_out):
    t  = k / OUTPUT_FPS
    bf = f0 + t * src_fps                 # fractional Blender frame
    fi = int(math.floor(bf))
    scene.frame_set(fi, subframe=bf - fi)  # subframe → rigid-body interpolation
    dg = bpy.context.evaluated_depsgraph_get()
    cam_pos = cam.matrix_world.translation.copy()
    use_bvh = OCCLUSION_CULL and OCCLUSION_METHOD == "bvh"
    bvh, tri_owner = build_bvh(dg) if use_bvh else (None, None)
    occluders = build_occluders(dg) if (OCCLUSION_CULL and not use_bvh) else []
    segs = []

    def project(a_world, b_world):
        ca = world_to_camera_view(scene, cam, a_world)
        cb = world_to_camera_view(scene, cam, b_world)
        if ca.z <= 0.0 or cb.z <= 0.0:     # behind the camera near-plane
            return
        # Blender cam-view: u=0 left, v=0 bottom. Engine (correct FOV) is +X
        # right, +Y up, so this maps straight through. Normalized after bake.
        segs.append([ca.x, ca.y, cb.x, cb.y])

    def emit(a_world, b_world, owner, test_self):
        if not OCCLUSION_CULL:
            project(a_world, b_world)
            return
        # Sample visibility along the segment and keep only the visible
        # sub-run(s) — a single midpoint test kills a whole segment even when
        # only its far portion is occluded, producing a hard "entire far side
        # gone" cliff instead of the correct silhouette-hugging clip. `owner`/
        # `test_self` are used only by the legacy raycast_v1 path; the BVH depth
        # test needs neither (it just compares eye-ray depth per sample).
        n = OCCLUSION_SAMPLES
        fracs = [i / (n - 1) for i in range(n)]
        if use_bvh:
            ignore_self = not test_self       # near cap (test_self=False) shows anyway
            vis = [is_visible_bvh(a_world.lerp(b_world, f), owner, cam_pos, bvh,
                                  tri_owner, ignore_self) for f in fracs]
        else:
            vis = [is_visible(a_world.lerp(b_world, f), owner, cam_pos, occluders, test_self)
                   for f in fracs]
        run_start = None
        for i, v in enumerate(vis):
            if v and run_start is None:
                run_start = fracs[i]
            elif not v and run_start is not None:
                project(a_world.lerp(b_world, run_start), a_world.lerp(b_world, fracs[i - 1]))
                run_start = None
        if run_start is not None:
            project(a_world.lerp(b_world, run_start), a_world.lerp(b_world, fracs[-1]))

    for L in LETTERS:
        front = read_contours("LineArt %s+" % L, dg) or []
        back  = read_contours("LineArt %s-" % L, dg) or []

        if not diag_printed:
            print("  [%s] front=%d strokes, back=%d strokes"
                  % (L, len(front), len(back)))

        # Which copy ("+"/"-") faces the camera? The NEAR cap is drawn regardless
        # of self-occlusion (test_self=False) — it's the letter's topmost surface,
        # and at grazing angles its far rim tucks behind its own near wall, which
        # true depth would (correctly but unwantedly) cull, blanking the outer
        # letters of an angled shot. The FAR cap keeps the full self test. BOTH
        # methods use this now; decided per frame by centroid distance so it
        # tracks the tumble (e.g. D rocking). It never disables CROSS-occlusion.
        def loop_centroid(loops):
            pts_all = [p for pts, _ in loops for p in pts]
            return centroid(pts_all) if pts_all else None
        fc, bc = loop_centroid(front), loop_centroid(back)
        if fc is not None and bc is not None:
            front_is_near = (cam_pos - fc).length <= (cam_pos - bc).length
        else:
            front_is_near = fc is not None    # whichever copy exists is visible
        front_test_self = not front_is_near   # near cap → shown regardless of self
        back_test_self  = front_is_near

        for pts, cyc in front:
            for i in range(len(pts) - 1):
                emit(pts[i], pts[i + 1], L, front_test_self)
            if cyc:
                emit(pts[-1], pts[0], L, front_test_self)
        for pts, cyc in back:
            for i in range(len(pts) - 1):
                emit(pts[i], pts[i + 1], L, back_test_self)
            if cyc:
                emit(pts[-1], pts[0], L, back_test_self)

        if front and back:
            back_cents = [centroid(p) for p, _ in back]
            for fpts, fcyc in front:
                fc = centroid(fpts)
                j = min(range(len(back)), key=lambda kk: (back_cents[kk] - fc).length)
                bpts, bcyc = back[j]
                length = polyline_length(fpts, fcyc)
                n = max(2, int(round(length / CONNECTOR_SPACING)) + 1)
                fracs = [c / (n - 1) for c in range(n)]
                fsamp = resample_fractions(fpts, fcyc, fracs)
                bsamp = resample_fractions(bpts, bcyc, fracs)
                for a, b in zip(fsamp, bsamp):
                    emit(a, b, L, True)   # wall connectors: self-tested (silhouette)

    # Framing uses ONLY the letters (+ connectors), which are emitted above;
    # the sphere rolls in from elsewhere and must not drive the framing bbox.
    if k == framing_k:
        framing_letter_count = len(segs)

    for name in SPHERE_OBJECTS:
        edges = read_mesh_edges(name, dg)
        if edges is None:
            if not diag_printed:
                print("  [sphere] WARNING — object '%s' not found" % name)
            continue
        if not diag_printed:
            print("  [sphere] %s: %d edges" % (name, len(edges)))
        for a, b in edges:
            emit(a, b, name, True)   # sphere is convex: self raycast hides far edges

    diag_printed = True
    frames_out.append(segs)
    if k % 50 == 0:
        print("  ...baked frame %d/%d (%d segs)" % (k, n_out, len(segs)))

# ----------------------------------------------------------------------------
# Framing: SELF_FRAME_2D=False (default) trusts logo_camera_center.py already
# physically centered the real camera — coords stay in Blender's native
# cam-view [0,1] space, and ref_aspect is just the render's own aspect.
# SELF_FRAME_2D=True re-enables the legacy post-hoc 2D bbox crop (proportion-
# correct "square" units: X = u*ref_aspect, Y = v) as a fallback; note it
# scales u/v independently and can introduce a slight aspect stretch that the
# physical camera-dolly approach avoids.
# ----------------------------------------------------------------------------
content_aspect = ref_aspect

if SELF_FRAME_2D:
    frame_ref = frames_out[framing_k][:framing_letter_count]  # letters only
    minX = minY = float("inf")
    maxX = maxY = float("-inf")
    for s in frame_ref:
        for (u, v) in ((s[0], s[1]), (s[2], s[3])):
            X = u * ref_aspect
            minX = min(minX, X); maxX = max(maxX, X)
            minY = min(minY, v); maxY = max(maxY, v)

    cw = maxX - minX
    ch = maxY - minY
    if cw < 1e-9 or ch < 1e-9:
        print("logo_export: WARNING — degenerate framing bbox (frame %d empty?)" % framing_k)
    else:
        content_aspect = cw / ch
        inv_cw, inv_ch = 1.0 / cw, 1.0 / ch
        scl = 1.0 - 2.0 * MARGIN
        for fr in frames_out:
            for s in fr:
                s[0] = round(MARGIN + (s[0] * ref_aspect - minX) * inv_cw * scl, 5)
                s[1] = round(MARGIN + (s[1] - minY) * inv_ch * scl, 5)
                s[2] = round(MARGIN + (s[2] * ref_aspect - minX) * inv_cw * scl, 5)
                s[3] = round(MARGIN + (s[3] - minY) * inv_ch * scl, 5)

data = {
    "fps": OUTPUT_FPS,
    "frame_start": 0,
    "frame_count": len(frames_out),
    "ref_aspect": content_aspect,   # plane aspect for the playback
    "frames": frames_out,
}

os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w") as fp:
    json.dump(data, fp)

total_segs = sum(len(fr) for fr in frames_out)
peak = max((len(fr) for fr in frames_out), default=0)
print("logo_export: %d frames @ %.3g fps (src %.3g, %d..%d), %d segs total"
      % (len(frames_out), OUTPUT_FPS, src_fps, f0, f1, total_segs))
print("logo_export: framing frame k=%d, content_aspect=%.4f, peak %d segs/frame"
      % (framing_k, content_aspect, peak))
if peak > 65000:
    print("logo_export: WARNING — peak segs/frame exceeds the engine's 65k line cap")
print("logo_export: wrote %s" % OUTPUT_PATH)
