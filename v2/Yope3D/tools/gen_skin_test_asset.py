#!/usr/bin/env python3
"""Generate assets/models/skinTest.gltf — a minimal skinned test model.

The engine had no skinned asset of any kind, which made the M16 compute skinning
pass impossible to execute end to end. This emits the smallest model that still
exercises every part of it: two bones, a blend band with fractional weights, and
a clip that bends the joint through 90 degrees and back.

Geometry is a 4-sided column, 2 segments tall, flat-shaded (each face owns its
vertices, so edges stay crisp) with caps — 32 vertices across 3 rings:

    ring 2 (y=2) ---- bone1 @ 1.00      <- rigid to the upper bone
    ring 1 (y=1) ---- bone0/1 @ 0.5     <- the blend band; this is what bends
    ring 0 (y=0) ---- bone0 @ 1.00      <- rigid to the root

Bone 1 sits at y=1 and rotates about +Z, so the top half swings onto -X while
the bottom half stays put. Ring 1's split weights make the seam deform smoothly
rather than shearing.

Output is a .gltf with a base64 data-URI buffer (not .glb): no binary chunk
packing to get wrong, and the loader takes both.

Usage:  python3 tools/gen_skin_test_asset.py
"""

import base64
import json
import math
import os
import struct

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "models", "skinTest.gltf")

RINGS = [0.0, 1.0, 2.0]     # y of each ring
HALF = 0.25                 # column half-width
CORNERS = [(-HALF, -HALF), (HALF, -HALF), (HALF, HALF), (-HALF, HALF)]

# Per-ring (joint0, joint1, weight0, weight1). Ring 1 is the blend band.
RING_WEIGHTS = [(0, 1, 1.0, 0.0),
                (0, 1, 0.5, 0.5),
                (0, 1, 0.0, 1.0)]


def build_geometry():
    """Flat-shaded column: every face owns its vertices.

    Sharing corner vertices between adjacent faces (the obvious 12-vertex build)
    forces a single averaged normal across a 90-degree edge, so the box shades
    like a smooth cylinder and the lighting reads as wrong. Splitting per face
    costs 20 extra vertices and makes the shading unambiguous — which matters
    here, because this asset exists to let someone eyeball whether SKINNING is
    deforming normals correctly. Any oddity should come from the skinning, not
    from the test mesh.
    """
    positions, normals, uvs, joints, weights = [], [], [], [], []
    indices = []

    def emit(x, y, z, n, u, v, ring):
        j0, j1, w0, w1 = RING_WEIGHTS[ring]
        positions.append((x, y, z))
        normals.append(n)
        uvs.append((u, v))
        joints.append((j0, j1, 0, 0))
        weights.append((w0, w1, 0.0, 0.0))
        return len(positions) - 1

    # ---- 4 side faces, each spanning all rings with its own flat normal ----
    for f in range(4):
        (x0, z0) = CORNERS[f]
        (x1, z1) = CORNERS[(f + 1) % 4]
        # Outward normal of the edge: perpendicular to it in the XZ plane. The
        # column is convex and centred, so (dz, 0, -dx) points away from the axis.
        dx, dz = x1 - x0, z1 - z0
        ln = math.hypot(dx, dz) or 1.0
        n = (dz / ln, 0.0, -dx / ln)

        col = []
        for r, y in enumerate(RINGS):
            v = y / RINGS[-1]
            col.append((emit(x0, y, z0, n, 0.0, v, r),
                        emit(x1, y, z1, n, 1.0, v, r)))
        for r in range(len(RINGS) - 1):
            a, b = col[r]
            d, e = col[r + 1]
            indices += [a, d, b,  b, d, e]

    # ---- Caps, so the column reads as a solid rather than an open tube ----
    bottom = [emit(x, RINGS[0], z, (0.0, -1.0, 0.0), 0.0, 0.0, 0) for (x, z) in CORNERS]
    indices += [bottom[0], bottom[1], bottom[2],  bottom[0], bottom[2], bottom[3]]

    top_ring = len(RINGS) - 1
    top = [emit(x, RINGS[-1], z, (0.0, 1.0, 0.0), 0.0, 1.0, top_ring) for (x, z) in CORNERS]
    indices += [top[0], top[2], top[1],  top[0], top[3], top[2]]

    return positions, normals, uvs, joints, weights, indices


def main():
    positions, normals, uvs, joints, weights, indices = build_geometry()
    n_verts = len(positions)

    blob = b""
    views = []          # (byteOffset, byteLength)

    def add_view(data, align=4):
        nonlocal blob
        while len(blob) % align:
            blob += b"\x00"
        off = len(blob)
        blob += data
        views.append((off, len(data)))
        return len(views) - 1

    v_pos = add_view(b"".join(struct.pack("<3f", *p) for p in positions))
    v_nrm = add_view(b"".join(struct.pack("<3f", *n) for n in normals))
    v_uv  = add_view(b"".join(struct.pack("<2f", *t) for t in uvs))
    v_jnt = add_view(b"".join(struct.pack("<4B", *j) for j in joints))
    v_wgt = add_view(b"".join(struct.pack("<4f", *w) for w in weights))
    v_idx = add_view(b"".join(struct.pack("<H", i) for i in indices))

    # Inverse bind matrices, column-major. Bone 0 is at the origin, bone 1 at
    # y=1, so the inverse binds are translate(0,0,0) and translate(0,-1,0).
    ibm = []
    for ty in (0.0, -1.0):
        ibm += [1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, ty, 0, 1]
    v_ibm = add_view(b"".join(struct.pack("<f", f) for f in ibm))

    # Animation: bend bone1 0 -> 90 -> 0 degrees about +Z over 2 seconds.
    times = [0.0, 1.0, 2.0]
    quats = []
    for deg in (0.0, 90.0, 0.0):
        h = math.radians(deg) / 2.0
        quats.append((0.0, 0.0, math.sin(h), math.cos(h)))
    v_time = add_view(b"".join(struct.pack("<f", t) for t in times))
    v_rot = add_view(b"".join(struct.pack("<4f", *q) for q in quats))

    uri = "data:application/octet-stream;base64," + base64.b64encode(blob).decode()

    def acc(view, ctype, count, atype, **extra):
        a = {"bufferView": view, "componentType": ctype, "count": count, "type": atype}
        a.update(extra)
        return a

    gltf = {
        "asset": {"version": "2.0", "generator": "yope3d gen_skin_test_asset.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "SkinTestMesh", "mesh": 0, "skin": 0},
            {"name": "bone0", "translation": [0, 0, 0], "children": [2]},
            {"name": "bone1", "translation": [0, 1, 0]},
        ],
        "skins": [{
            "name": "TestRig",
            "skeleton": 1,
            "joints": [1, 2],
            "inverseBindMatrices": 6,
        }],
        "meshes": [{
            "name": "Column",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2,
                               "JOINTS_0": 3, "WEIGHTS_0": 4},
                "indices": 5,
                "material": 0,
            }],
        }],
        "materials": [{
            "name": "SkinTestMat",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.85, 0.35, 0.25, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.6,
            },
        }],
        "animations": [{
            "name": "bend",
            "samplers": [{"input": 7, "output": 8, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}],
        }],
        "accessors": [
            # POSITION needs min/max per spec.
            acc(v_pos, 5126, n_verts, "VEC3",
                min=[-HALF, RINGS[0], -HALF], max=[HALF, RINGS[-1], HALF]),
            acc(v_nrm, 5126, n_verts, "VEC3"),
            acc(v_uv,  5126, n_verts, "VEC2"),
            acc(v_jnt, 5121, n_verts, "VEC4"),
            acc(v_wgt, 5126, n_verts, "VEC4"),
            acc(v_idx, 5123, len(indices), "SCALAR"),
            acc(v_ibm, 5126, 2, "MAT4"),
            acc(v_time, 5126, len(times), "SCALAR", min=[times[0]], max=[times[-1]]),
            acc(v_rot, 5126, len(quats), "VEC4"),
        ],
        "bufferViews": [{"buffer": 0, "byteOffset": o, "byteLength": l} for (o, l) in views],
        "buffers": [{"byteLength": len(blob), "uri": uri}],
    }

    out = os.path.normpath(OUT)
    with open(out, "w") as f:
        json.dump(gltf, f, indent=2)
    print(f"wrote {out}  ({n_verts} verts, {len(indices)//3} tris, {len(blob)} buffer bytes)")


if __name__ == "__main__":
    main()
