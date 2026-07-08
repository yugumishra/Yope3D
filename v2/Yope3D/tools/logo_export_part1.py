"""
logo_export_part1.py — Blender exporter for the Yope3D loading-logo, PART 1
(the "draw-on" reveal that precedes the Part-2 tumble).

Unlike logo_export.py, Part 1 has NO simulation: the letters stand still at a
single pose and the reveal is a *synthetic* timeline the exporter generates
itself. It reads the standing pose ONCE, then bakes N output frames that:

  1. TRACE each letter's camera-facing (near) cap outline progressively by
     ARC LENGTH, letter by letter (order = TRACE_ORDER, D last).
  2. After the last letter, QUICK 3D FILL: trace the far cap + zip in the
     front->back connectors, landing on geometry IDENTICAL to logo_part2.json
     frame 0 (same pose, camera, projection, BVH occlusion) — so the eventual
     engine hand-off Part1(last) -> Part2(frame 0) is a seamless cut.
  3. HOLD the completed frame-0 pose briefly.

Output schema matches logo_export.py exactly:
    { "fps", "frame_start", "frame_count", "ref_aspect", "frames": [[x,y,x,y]...] }
so scripts/behaviors/logo_playback.py replays it unchanged (point it at
logo_part1.json via the YOPE_LOGO_JSON env var or a `logo_json` script param).

Run it the SAME way as Part 2 — chain the camera centering first so the pose is
framed identically to the Part-2 bake:

  blender file.blend --background \\
      --python tools/logo_camera_center.py \\
      --python tools/logo_export_part1.py

Does NOT touch logo_part2.json or logo_export.py. The occlusion helpers below
are duplicated from logo_export.py on purpose (that script executes its bake on
import, so it can't be imported as a module).
"""

import bpy
import json
import os
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from bpy_extras.object_utils import world_to_camera_view

# ----------------------------------------------------------------------------
# CONFIG — edit these
# ----------------------------------------------------------------------------
LETTERS           = "Yope3D"          # objects are "LineArt <L>+" / "LineArt <L>-"
SPHERE_OBJECTS    = ["Icosphere"]     # drawn (+ occluder) only on the FILL/HOLD frames
STANDING_FRAME    = None              # pose to reveal; None = scene.frame_start
OUTPUT_FPS        = 60.0
CONNECTOR_SPACING = 0.05              # world units between connectors (match Part 2)

# --- reveal schedule (all seconds) ------------------------------------------
TRACE_ORDER       = list(LETTERS)     # reveal order; the LAST one triggers the 3D fill
TRACE_PER_LETTER  = 0.35              # time to trace one letter's near-cap outline
TRACE_OVERLAP     = 0.12              # head-start the next letter gets before this one
                                      #   finishes (0 = strictly sequential; >0 overlaps)
TRACE_START_DELAY = 0.15              # dead time before the first stroke
FILL_3D_DUR       = 0.40              # time for the far cap + connectors to fill in
HOLD_END          = 0.50             # hold the finished (== Part2 frame 0) pose

TRACE_NEAR_CAP    = True              # trace the camera-FACING cap (auto near/far by
                                      #   centroid distance). At the standing pose this
                                      #   is the "-" copy, NOT "+". Flip to False if the
                                      #   reveal traces the hidden (back) outline instead.

# --- occlusion (BVH depth test, identical to logo_export.py's default) ------
OCCLUDER_OBJECTS  = None              # None = auto (letters + SPHERE_OBJECTS)
DEPTH_EPS         = 0.01              # eye-ray depth tolerance (< the +/-0.03 offset)
OCCLUSION_SAMPLES = 5                 # samples per segment for the visible-run clip

OUTPUT_PATH       = "/Users/me/Desktop/dev/Yope3D/v2/Yope3D/assets/logo/logo_part1.json"
# ----------------------------------------------------------------------------

scene = bpy.context.scene
cam   = scene.camera
if cam is None:
    raise RuntimeError("Scene has no active camera (scene.camera is None).")


# ---- readers (duplicated from logo_export.py) ------------------------------
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
        return []
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
    obj = bpy.data.objects.get(obj_name)
    if obj is None:
        return []
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


def all_points(loops):
    return [p for pts, _ in loops for p in pts]


def loop_start_index(pts):
    """Index of the point to START a trace from: the leftmost point at the
    loop's vertical middle, measured in CAMERA/screen space (that's what
    'left'/'vertical' mean here). Restrict to a horizontal band around mid-v,
    then take min-u (tie-break: closest to mid-v, so a straight left edge like
    'D's bar starts at its middle)."""
    uv = [world_to_camera_view(scene, cam, p) for p in pts]
    us = [c.x for c in uv]
    vs = [c.y for c in uv]
    midv = 0.5 * (min(vs) + max(vs))
    band = 0.15 * (max(vs) - min(vs))
    cand = [i for i in range(len(pts)) if abs(vs[i] - midv) <= band] or list(range(len(pts)))
    return min(cand, key=lambda i: (us[i], abs(vs[i] - midv)))


def reorder_loop(pts, cyclic):
    """Rotate a CLOSED loop so it begins at its leftmost-middle point. A closed
    loop's edge set is unchanged by this, so the completed frame is untouched —
    only the trace's starting point moves. Open loops are left as-is."""
    if not cyclic or len(pts) < 3:
        return pts
    i = loop_start_index(pts)
    return pts[i:] + pts[:i]


# ---- occlusion: BVH depth test (duplicated from logo_export.py) -------------
def occluder_names():
    if OCCLUDER_OBJECTS is not None:
        return list(OCCLUDER_OBJECTS)
    return list(LETTERS) + list(SPHERE_OBJECTS)


def build_bvh(depsgraph):
    """(bvh, tri_owner) over all solids at the standing pose; (None, None) if empty."""
    verts, tris, tri_owner = [], [], []
    for nm in occluder_names():
        o = bpy.data.objects.get(nm)
        if o is None:
            continue
        oe = o.evaluated_get(depsgraph)
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
            tri_owner.append(nm)
        oe.to_mesh_clear()
    if not verts or not tris:
        return None, None
    return BVHTree.FromPolygons(verts, tris, all_triangles=True), tri_owner


def is_visible_bvh(P, owner, cam_pos, bvh, tri_owner, ignore_self):
    """Visible iff the nearest solid along the eye ray cam->P is at/behind P.
    ignore_self=True (near cap): a hit on P's own body doesn't count, but a
    nearer hit on ANOTHER object still occludes."""
    if bvh is None:
        return True
    d = P - cam_pos
    dist = d.length
    if dist < 1e-6:
        return True
    loc, _n, idx, t = bvh.ray_cast(cam_pos, d / dist)
    if loc is None:
        return True
    if ignore_self and tri_owner[idx] == owner:
        return True
    return t >= dist - DEPTH_EPS


# ----------------------------------------------------------------------------
# Read the standing pose ONCE.
# ----------------------------------------------------------------------------
f_stand = scene.frame_start if STANDING_FRAME is None else STANDING_FRAME
scene.frame_set(f_stand)
dg      = bpy.context.evaluated_depsgraph_get()
cam_pos = cam.matrix_world.translation.copy()
bvh, tri_owner = build_bvh(dg)

rx = scene.render.resolution_x * scene.render.pixel_aspect_x
ry = scene.render.resolution_y * scene.render.pixel_aspect_y
ref_aspect = rx / ry

# Per letter: near-cap loops (traced during reveal), far-cap loops + connectors
# (filled during the 3D pop). Near/far by centroid distance to the camera.
letters = []   # list of dicts: name, near_loops, far_loops, connectors
for L in LETTERS:
    front = read_contours("LineArt %s+" % L, dg)
    back  = read_contours("LineArt %s-" % L, dg)
    fpts, bpts = all_points(front), all_points(back)
    if fpts and bpts:
        front_is_near = (cam_pos - centroid(fpts)).length <= (cam_pos - centroid(bpts)).length
    else:
        front_is_near = bool(fpts)
    if not TRACE_NEAR_CAP:
        front_is_near = not front_is_near
    near_loops, far_loops = (front, back) if front_is_near else (back, front)

    # Trace each near-cap loop (incl. holes) from its leftmost-middle point.
    # Only the traced loops are reordered; far cap + connectors keep their
    # original ordering so the completed frame still matches Part 2 frame 0.
    near_loops = [(reorder_loop(pts, cyc), cyc) for pts, cyc in near_loops]

    # Connectors: front->nearest-back, sampled by shared arc fraction (== Part 2).
    # `frac` (0..1 along the front loop) lets the fill zip them in order.
    connectors = []
    if front and back:
        back_cents = [centroid(p) for p, _ in back]
        for f_pts, f_cyc in front:
            fc = centroid(f_pts)
            j = min(range(len(back)), key=lambda kk: (back_cents[kk] - fc).length)
            b_pts, b_cyc = back[j]
            length = polyline_length(f_pts, f_cyc)
            n = max(2, int(round(length / CONNECTOR_SPACING)) + 1)
            fracs = [c / (n - 1) for c in range(n)]
            fsamp = resample_fractions(f_pts, f_cyc, fracs)
            bsamp = resample_fractions(b_pts, b_cyc, fracs)
            for a, b, fr in zip(fsamp, bsamp, fracs):
                connectors.append((a, b, fr))

    letters.append(dict(name=L, near=near_loops, far=far_loops, connectors=connectors))

sphere_edges = []
for name in SPHERE_OBJECTS:
    for a, b in read_mesh_edges(name, dg):
        sphere_edges.append((name, a, b))

for d in letters:
    print("  [%s] near=%d loops, far=%d loops, %d connectors"
          % (d["name"], len(d["near"]), len(d["far"]), len(d["connectors"])))
print("  [sphere] %d edges" % len(sphere_edges))


# ----------------------------------------------------------------------------
# Reveal schedule.
# ----------------------------------------------------------------------------
step = max(0.0, TRACE_PER_LETTER - TRACE_OVERLAP)
starts = {L: TRACE_START_DELAY + i * step for i, L in enumerate(TRACE_ORDER)}
trace_end = max(starts.values()) + TRACE_PER_LETTER if starts else TRACE_START_DELAY
fill_end  = trace_end + FILL_3D_DUR
total_dur = fill_end + HOLD_END
n_out     = max(1, int(round(total_dur * OUTPUT_FPS)) + 1)


def letter_frac(L, t):
    s = starts.get(L, trace_end)
    return max(0.0, min(1.0, (t - s) / TRACE_PER_LETTER)) if TRACE_PER_LETTER > 1e-6 else 1.0


def fill_frac(t):
    return max(0.0, min(1.0, (t - trace_end) / FILL_3D_DUR)) if FILL_3D_DUR > 1e-6 else 1.0


# ----------------------------------------------------------------------------
# Bake.
# ----------------------------------------------------------------------------
frames_out = []
for k in range(n_out):
    t = k / OUTPUT_FPS
    segs = []

    def project(a_world, b_world):
        ca = world_to_camera_view(scene, cam, a_world)
        cb = world_to_camera_view(scene, cam, b_world)
        if ca.z <= 0.0 or cb.z <= 0.0:
            return
        segs.append([ca.x, ca.y, cb.x, cb.y])

    def emit_seg(a_world, b_world, owner, ignore_self):
        """Occlusion-clip a single segment into its visible sub-run(s)."""
        n = OCCLUSION_SAMPLES
        fr = [i / (n - 1) for i in range(n)]
        vis = [is_visible_bvh(a_world.lerp(b_world, f), owner, cam_pos, bvh, tri_owner, ignore_self)
               for f in fr]
        run = None
        for i, v in enumerate(vis):
            if v and run is None:
                run = fr[i]
            elif not v and run is not None:
                project(a_world.lerp(b_world, run), a_world.lerp(b_world, fr[i - 1]))
                run = None
        if run is not None:
            project(a_world.lerp(b_world, run), a_world.lerp(b_world, fr[-1]))

    def emit_prefix(pts, cyclic, frac, owner, ignore_self):
        """Trace a polyline up to arc-length fraction `frac` (0..1)."""
        if frac <= 0.0 or len(pts) < 2:
            return
        ring = list(pts) + ([pts[0]] if cyclic else [])
        seglens = [(ring[i + 1] - ring[i]).length for i in range(len(ring) - 1)]
        total = sum(seglens)
        if total < 1e-9:
            return
        target = total * min(1.0, frac)
        acc = 0.0
        for i, sl in enumerate(seglens):
            if acc + sl <= target:
                emit_seg(ring[i], ring[i + 1], owner, ignore_self)
                acc += sl
            else:
                f = (target - acc) / sl if sl > 1e-9 else 0.0
                emit_seg(ring[i], ring[i].lerp(ring[i + 1], f), owner, ignore_self)
                break

    ff = fill_frac(t)
    for d in letters:
        L = d["name"]
        # 1) near cap: trace by this letter's own reveal fraction (always drawn).
        lf = letter_frac(L, t)
        for pts, cyc in d["near"]:
            emit_prefix(pts, cyc, lf, L, ignore_self=True)
        # 2) far cap + connectors: fill in after all letters are traced.
        if ff > 0.0:
            for pts, cyc in d["far"]:
                emit_prefix(pts, cyc, ff, L, ignore_self=False)
            for a, b, fr in d["connectors"]:
                if fr <= ff:                       # zip connectors in arc order
                    emit_seg(a, b, L, ignore_self=False)

    # 3) sphere appears with the 3D fill so the last frame matches Part 2 frame 0.
    if ff > 0.0:
        for name, a, b in sphere_edges:
            emit_seg(a, b, name, ignore_self=False)

    frames_out.append(segs)
    if k % 30 == 0:
        print("  ...baked frame %d/%d  (t=%.2fs, %d segs)" % (k, n_out, t, len(segs)))

data = {
    "fps": OUTPUT_FPS,
    "frame_start": 0,
    "frame_count": len(frames_out),
    "ref_aspect": ref_aspect,
    "frames": frames_out,
}

os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w") as fp:
    json.dump(data, fp)

peak = max((len(fr) for fr in frames_out), default=0)
print("logo_export_part1: %d frames @ %.3g fps (%.2fs: trace->%.2f fill->%.2f hold->%.2f)"
      % (len(frames_out), OUTPUT_FPS, total_dur, trace_end, fill_end, total_dur))
print("logo_export_part1: last frame %d segs (should ~= logo_part2.json frame 0), peak %d"
      % (len(frames_out[-1]) if frames_out else 0, peak))
print("logo_export_part1: wrote %s" % OUTPUT_PATH)
