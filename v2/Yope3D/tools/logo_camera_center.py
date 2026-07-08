"""
logo_camera_center.py — physically recenters the REAL Blender camera on the
Part 2 logo, before tools/logo_export.py bakes/raycasts against it.

Why this has to be a separate, earlier step: hidden-line occlusion (the
raycast in logo_export.py) depends only on the camera's WORLD POSITION — the
ray to test is `point -> camera position`, and camera rotation never enters
that math. A 2D crop/rescale applied to already-projected coordinates (what
logo_export.py's old self-framing pass did) cannot fix an off-center vantage
point, because the occlusion decisions were already baked in at raycast time.
If the camera physically sits off to one side of "Yope3D", the letters on the
far side are correctly, physically more self-occluded than the near side —
that's a real photograph of an off-axis shot, and no amount of post-hoc
cropping un-shoots it. So: move the camera first, then raycast.

What it does:
  - Reads the LETTERS solids' world-space vertices at FRAMING_FRAME (the
    readable "first frame" pose — same convention as logo_export's
    FRAMING_FRAME_T=0.0).
  - Solves for a camera POSITION (translation only — rotation/tilt is left
    exactly as authored, so any intentional artistic angle survives) that:
      1. centers the letters' projected bounding box at (0.5, 0.5), and
      2. dollies along the view axis so the bbox fits within
         (1 - 2*MARGIN) of the frame, matching the tighter of width/height
         (a uniform zoom-to-fit — a real lens can't stretch axes
         independently the way the old 2D-crop trick did).
  - Solved numerically (finite-difference sensitivities + a few Newton
    iterations) rather than from closed-form FOV/sensor formulas, so it's
    robust to whatever lens/sensor-fit settings the camera uses.
  - Leaves the change live in the current Blender session (does NOT save the
    .blend by default — see SAVE_FILE) so it can be chained straight into
    logo_export.py in the same process:

      blender file.blend --background \\
          --python tools/logo_camera_center.py \\
          --python tools/logo_export.py

    (Blender runs --python scripts in order within one session, so the moved
    camera is exactly what logo_export.py then raycasts against.)

Run standalone (Scripting workspace → Run Script) to preview the camera move
interactively before committing to the full export.
"""

import bpy
import math
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

# ----------------------------------------------------------------------------
# CONFIG
# ----------------------------------------------------------------------------
LETTERS         = "Yope3D"    # solid mesh object names (bbox source; sphere excluded)
FRAMING_FRAME   = None        # None = scene.frame_start (the readable first frame)
# BAKE_PHYSICS is intentionally False by default: this script only reads ONE
# frame (FRAMING_FRAME, default frame_start), where the letters are still held
# by their keyframed Animated=True kinematic lock, before any simulation has
# run — that pose is correct with or without a baked cache. Baking here too
# is not just redundant with logo_export.py's own bake, it actively BREAKS
# it: bpy.ops.ptcache.bake_all() called a second time back-to-back in the
# same chained session can silently no-op (a known headless/scripted-context
# quirk), so logo_export.py's free_bake_all() wipes the good cache from here
# and the "re-bake" never actually happens — the sim then evaluates frame 0
# for every subframe request ("only the first frame" in the exported clip).
# Only flip this on if you point FRAMING_FRAME at a mid-simulation frame AND
# you are running this script by itself (not chained before logo_export.py).
MARGIN          = 0.06        # border left around the fitted content (tune this)
ITERATIONS      = 6           # Newton rounds; content is simple enough to converge fast
BAKE_PHYSICS    = False
SAVE_FILE       = False       # True = bpy.ops.wm.save_mainfile() after solving
# ----------------------------------------------------------------------------

scene = bpy.context.scene
cam   = scene.camera
if cam is None:
    raise RuntimeError("Scene has no active camera (scene.camera is None).")

f_frame = scene.frame_start if FRAMING_FRAME is None else FRAMING_FRAME

if BAKE_PHYSICS:
    rbw = scene.rigidbody_world
    if rbw is not None and rbw.point_cache is not None:
        rbw.point_cache.frame_start = scene.frame_start
        rbw.point_cache.frame_end   = scene.frame_end
    try:
        bpy.ops.ptcache.free_bake_all()
    except Exception:
        pass
    try:
        bpy.ops.ptcache.bake_all(bake=True)
    except Exception as e:
        print("logo_camera_center: WARNING — auto-bake failed (%s); proceeding "
              "with whatever pose is currently evaluable at frame %d." % (e, f_frame))

scene.frame_set(f_frame)
dg = bpy.context.evaluated_depsgraph_get()

# ---- gather the letters' world-space vertices at the framing frame ---------
points = []
for L in LETTERS:
    o = bpy.data.objects.get(L)
    if o is None:
        print("logo_camera_center: WARNING — letter solid '%s' not found, skipping" % L)
        continue
    oe = o.evaluated_get(dg)
    if oe.type != 'MESH':
        print("logo_camera_center: WARNING — '%s' is not a MESH, skipping" % L)
        continue
    try:
        me = oe.to_mesh()
    except Exception:
        continue
    mw = oe.matrix_world
    points.extend(mw @ v.co for v in me.vertices)
    oe.to_mesh_clear()

if not points:
    raise RuntimeError("logo_camera_center: no letter geometry found — check LETTERS "
                        "names against the actual solid mesh object names.")


def measure():
    """Project all points -> (u_center, v_center, u_extent, v_extent)."""
    us, vs = [], []
    for p in points:
        cv = world_to_camera_view(scene, cam, p)
        us.append(cv.x)
        vs.append(cv.y)
    return (min(us) + max(us)) * 0.5, (min(vs) + max(vs)) * 0.5, \
           max(us) - min(us), max(vs) - min(vs)


def local_axes():
    m3 = cam.matrix_world.to_3x3()
    right   = (m3 @ Vector((1.0, 0.0, 0.0))).normalized()
    up      = (m3 @ Vector((0.0, 1.0, 0.0))).normalized()
    forward = (m3 @ Vector((0.0, 0.0, -1.0))).normalized()   # Blender cams look -Z locally
    return right, up, forward


target_extent = 1.0 - 2.0 * MARGIN

for it in range(ITERATIONS):
    right, up, forward = local_axes()
    dist = (cam.location - sum(points, Vector()) / len(points)).length
    eps  = max(1e-4, 0.01 * dist)

    # --- centering pass: solve dx (local right), dy (local up) -------------
    uc0, vc0, ue0, ve0 = measure()

    cam.location += right * eps
    ucx, vcx, _, _ = measure()
    cam.location -= right * eps

    cam.location += up * eps
    ucy, vcy, _, _ = measure()
    cam.location -= up * eps

    dudx = (ucx - uc0) / eps
    dvdy = (vcy - vc0) / eps
    errU = 0.5 - uc0
    errV = 0.5 - vc0
    dx = errU / dudx if abs(dudx) > 1e-9 else 0.0
    dy = errV / dvdy if abs(dvdy) > 1e-9 else 0.0
    cam.location += right * dx + up * dy

    # --- dolly pass: solve dz (local forward) for margin fit ---------------
    uc0, vc0, ue0, ve0 = measure()
    cur_extent = max(ue0, ve0)

    cam.location += forward * eps
    _, _, uex, vex = measure()
    cam.location -= forward * eps

    dext_dz = (max(uex, vex) - cur_extent) / eps
    errExt  = target_extent - cur_extent
    dz = errExt / dext_dz if abs(dext_dz) > 1e-9 else 0.0
    cam.location += forward * dz

    print("logo_camera_center: iter %d  center=(%.4f,%.4f) extent=%.4f target=%.4f"
          % (it, uc0, vc0, cur_extent, target_extent))

uc, vc, ue, ve = measure()
print("logo_camera_center: DONE  center=(%.4f,%.4f)  extent(u,v)=(%.4f,%.4f)  "
      "target=%.4f  camera at %s" % (uc, vc, ue, ve, target_extent, tuple(cam.location)))

if SAVE_FILE:
    bpy.ops.wm.save_mainfile()
    print("logo_camera_center: saved .blend")
else:
    print("logo_camera_center: camera moved in this session only (SAVE_FILE=False). "
          "Chain straight into logo_export.py via multiple --python args, or set "
          "SAVE_FILE=True / save manually to persist the new camera position.")
