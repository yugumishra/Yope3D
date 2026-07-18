"""
logo_occlusion_debug.py — visualize the hidden-line raycast decision directly
in the Blender viewport, so we can SEE what's being culled instead of
guessing from engine output.

Run from the Text Editor (Run Script) with the .blend open — no export, no
engine build. It samples every letter's LineArt contour + connector segments
at DEBUG_FRAME using the exact same multi-sample raycast as logo_export.py,
then builds two new mesh objects in the scene:

  Debug_Visible   — bright GREEN emissive edges: samples with clear line-of-sight
  Debug_Occluded  — bright RED   emissive edges: samples blocked by an occluder

Orbit/zoom the viewport, toggle the letter solids' visibility, isolate one
letter via DEBUG_LETTERS, etc. Re-running replaces the previous debug objects.

Also prints (and logs to LOG_PATH, so it's readable even without a terminal
attached — see the note in tools/logo_export.py's usage docs about macOS
Blender.app having no console by default) a per-letter visible/occluded
breakdown, plus full ray diagnostics (origin, direction-to-camera, which
occluder was hit, hit distance vs. total distance, local-space hit point) for
a handful of occluded samples per letter — enough to tell whether a ray is
hitting a DIFFERENT letter's solid, its own solid unexpectedly, or something
geometrically implausible (e.g. near-zero hit distance, which means the
sample point is effectively embedded inside its own occluder mesh).
"""

import bpy
import os
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

# ----------------------------------------------------------------------------
# CONFIG
# ----------------------------------------------------------------------------
LETTERS          = "Yope3D"
DEBUG_LETTERS    = None          # None = all of LETTERS; e.g. ["Y"] to isolate one
DEBUG_FRAME      = None          # None = whatever frame the timeline is on now
# Identity-aware occlusion — MUST mirror logo_export.py (see its long comment).
# SELF (occluder == this contour's own letter): big tolerance so the contour
# resting in/on its own body doesn't self-cull. CROSS (a different solid): ~none,
# so tightly-piled letters still occlude and the result is stable.
SELF_BIAS         = 0.05
SELF_MIN_HIT_DIST = 0.065
CROSS_BIAS        = 0.002
CROSS_MIN_HIT_DIST = 0.002
GRAZE_COS         = 0.15          # reject near-tangent hits (|cos(ray,normal)| below
                                 #   this) — a shallow ray skimming a coplanar neighbour's
                                 #   flat cap, not a real occlusion. Mirror logo_export.py.
SAMPLES_PER_SEG  = 9
MARKER_FRAC      = 0.12          # stub length per marker, as a fraction of the full
                                 #   segment — the original 0.02 was too short to
                                 #   spot in the viewport at normal zoom.
RAY_DIAG_COUNT   = 5             # occluded-ray diagnostics printed per letter
LOG_PATH         = "/Users/me/Desktop/dev/Yope3D/v2/Yope3D/tools/logo_occlusion_debug.log"
# ----------------------------------------------------------------------------

scene = bpy.context.scene
cam   = scene.camera
if cam is None:
    raise RuntimeError("Scene has no active camera.")

if DEBUG_FRAME is not None:
    scene.frame_set(DEBUG_FRAME)

dg = bpy.context.evaluated_depsgraph_get()
cam_pos = cam.matrix_world.translation.copy()

_log_lines = []
def log(msg):
    print(msg)
    _log_lines.append(msg)


# ---- readers (same logic as logo_export.py) --------------------------------
def layer_drawing(layer, frame_num):
    chosen = None
    for fr in layer.frames:
        if fr.frame_number <= frame_num:
            chosen = fr
        else:
            break
    if chosen is None and len(layer.frames) > 0:
        chosen = layer.frames[0]
    return chosen.drawing if chosen else None


def read_contours(obj_name):
    obj = bpy.data.objects.get(obj_name)
    if obj is None:
        return []
    ob_eval = obj.evaluated_get(dg)
    data = ob_eval.data
    mw = ob_eval.matrix_world
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


def build_occluders(names):
    occ = []
    for nm in names:
        o = bpy.data.objects.get(nm)
        if o is None:
            log("logo_occlusion_debug: WARNING — occluder '%s' not found" % nm)
            continue
        oe = o.evaluated_get(dg)
        if oe.type != 'MESH':
            continue
        mw = oe.matrix_world.copy()
        occ.append((oe.name, oe, mw, mw.inverted()))
    return occ


def raycast_test(P, owner, occluders, test_self, want_diag=False):
    """Returns (visible: bool, diag: dict|None). `owner` is the solid this
    contour belongs to — matched by name to switch SELF vs CROSS tolerance.
    `test_self=False` (near cap) skips the own-body occluder entirely."""
    to_cam = cam_pos - P
    dist = to_cam.length
    if dist < 1e-6:
        return True, None
    d = to_cam / dist

    for name, oe, mw, minv in occluders:
        self_hit = (name == owner)
        if self_hit and not test_self:
            continue
        bias    = SELF_BIAS         if self_hit else CROSS_BIAS
        min_hit = SELF_MIN_HIT_DIST if self_hit else CROSS_MIN_HIT_DIST
        O = P + d * bias
        maxd = dist - bias
        if maxd <= 0.0:
            continue
        o_l = minv @ O
        d_l3 = minv.to_3x3() @ d
        nl = d_l3.length
        if nl < 1e-12:
            continue
        d_l = d_l3 / nl
        hit, loc, n_l, _i = oe.ray_cast(o_l, d_l, distance=1e12)
        if not hit:
            continue
        if n_l.length > 1e-9 and abs(d_l.dot(n_l.normalized())) < GRAZE_COS:
            continue               # tangent skim across a flat cap, not real occlusion
        hit_dist = ((mw @ loc) - O).length
        if hit_dist < min_hit:     # self-rest (SELF) / grazing touch (CROSS)
            continue
        if hit_dist < maxd:
            diag = None
            if want_diag:
                diag = dict(point=P, cam_dir=d, occluder=name, is_self=self_hit,
                            hit_dist=hit_dist, total_dist=dist,
                            local_hit=loc)
            return False, diag
    return True, None


# ---- gather occluders + letters ---------------------------------------------
occluders = build_occluders(list(LETTERS))
letters_to_check = DEBUG_LETTERS if DEBUG_LETTERS is not None else list(LETTERS)

vis_edges = []
occ_edges = []
per_letter_stats = {}

for L in letters_to_check:
    front = read_contours("LineArt %s+" % L)
    back  = read_contours("LineArt %s-" % L)
    n_vis, n_occ = 0, 0
    diag_shown = 0
    diag_log = []

    # Near cap (copy closer to the camera) is never self-occluded — mirror
    # logo_export.py and skip its self raycast (test_self=False).
    def loop_centroid(loops):
        pts_all = [p for pts, _ in loops for p in pts]
        if not pts_all:
            return None
        c = Vector((0.0, 0.0, 0.0))
        for p in pts_all:
            c += p
        return c / len(pts_all)
    fc, bc = loop_centroid(front), loop_centroid(back)
    if fc is not None and bc is not None:
        front_is_near = (cam_pos - fc).length <= (cam_pos - bc).length
    else:
        front_is_near = fc is not None
    front_test_self, back_test_self = (not front_is_near), front_is_near

    all_segs = []   # (a, b, test_self)
    for loops, ts in ((front, front_test_self), (back, back_test_self)):
        for pts, cyc in loops:
            for i in range(len(pts) - 1):
                all_segs.append((pts[i], pts[i + 1], ts))
            if cyc:
                all_segs.append((pts[-1], pts[0], ts))

    for a, b, ts in all_segs:
        for i in range(SAMPLES_PER_SEG):
            f = i / (SAMPLES_PER_SEG - 1)
            P = a.lerp(b, f)
            want_diag = diag_shown < RAY_DIAG_COUNT
            visible, diag = raycast_test(P, L, occluders, ts, want_diag=want_diag)
            # stub edge at P so we get a visible marker, oriented toward the
            # next sample
            Pn = a.lerp(b, min(1.0, f + MARKER_FRAC))
            if visible:
                n_vis += 1
                vis_edges.append((P, Pn))
            else:
                n_occ += 1
                occ_edges.append((P, Pn))
                if diag is not None:
                    diag_shown += 1
                    diag_log.append(diag)

    per_letter_stats[L] = (n_vis, n_occ)
    log("[%s] visible=%d occluded=%d (%.0f%% occluded)"
        % (L, n_vis, n_occ, 100.0 * n_occ / max(1, n_vis + n_occ)))
    for d in diag_log:
        log("    [%s] ray from %s -> occluder '%s' hit at local %s, "
            "hit_dist=%.4f / total_dist=%.4f (dir-to-cam=%s)"
            % ("SELF " if d["is_self"] else "CROSS",
               tuple(round(c, 3) for c in d["point"]), d["occluder"],
               tuple(round(c, 3) for c in d["local_hit"]),
               d["hit_dist"], d["total_dist"],
               tuple(round(c, 3) for c in d["cam_dir"])))


# ---- build debug viz objects -------------------------------------------------
def make_debug_object(name, edges, color):
    old = bpy.data.objects.get(name)
    if old is not None:
        old_mesh = old.data
        bpy.data.objects.remove(old, do_unlink=True)
        if old_mesh and old_mesh.users == 0:
            bpy.data.meshes.remove(old_mesh)

    verts, e_idx = [], []
    for a, b in edges:
        i0 = len(verts)
        verts.append(a); verts.append(b)
        e_idx.append((i0, i0 + 1))

    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, e_idx, [])
    me.update()

    mat = bpy.data.materials.get(name + "_mat") or bpy.data.materials.new(name + "_mat")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (*color, 1.0)
    emit.inputs["Strength"].default_value = 2.0
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    me.materials.append(mat)

    obj = bpy.data.objects.new(name, me)
    scene.collection.objects.link(obj)
    return obj


make_debug_object("Debug_Visible", vis_edges, (0.0, 1.0, 0.15))
make_debug_object("Debug_Occluded", occ_edges, (1.0, 0.05, 0.0))

log("logo_occlusion_debug: created 'Debug_Visible' (green) and 'Debug_Occluded' "
    "(red) mesh objects at frame %d. Camera at %s."
    % (scene.frame_current, tuple(round(c, 3) for c in cam_pos)))
log("logo_occlusion_debug: if you don't see them — (1) viewport shading must be "
    "Material Preview or Rendered (the emission material is invisible in plain "
    "Solid mode; press Z for the shading pie menu), (2) select Debug_Visible / "
    "Debug_Occluded in the Outliner and press '.' (View Selected) to frame them — "
    "the markers are short stubs near each letter's own surface.")

try:
    with open(LOG_PATH, "w") as fp:
        fp.write("\n".join(_log_lines) + "\n")
    print("logo_occlusion_debug: wrote log -> %s" % LOG_PATH)
except Exception as e:
    print("logo_occlusion_debug: could not write log file (%s)" % e)
