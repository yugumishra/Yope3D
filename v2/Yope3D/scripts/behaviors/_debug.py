"""Wireframe debug-draw helpers (pure Python over yope3d.draw_line).

Keeps behaviors from hand-rolling the same line loops. Every shape is per-frame
(yope3d clears debug lines each frame), so call these from update().

    from behaviors import _debug
    _debug.draw_aabb(center, half, (1, 1, 0))
    _debug.draw_sphere(pos, radius)
    _debug.draw_cross(hit_point, (0, 1, 0))
"""
import math
import yope3d

_YELLOW = None  # resolved lazily so importing this module never touches the engine


def _col(color):
    return color if color is not None else yope3d.Vec3(1, 1, 0)


def draw_cross(point, color=None, size=0.3):
    """A 3-axis cross at `point` — handy for marking raycast hits / waypoints."""
    c = _col(color)
    yope3d.draw_line(point - yope3d.Vec3(size, 0, 0), point + yope3d.Vec3(size, 0, 0), c)
    yope3d.draw_line(point - yope3d.Vec3(0, size, 0), point + yope3d.Vec3(0, size, 0), c)
    yope3d.draw_line(point - yope3d.Vec3(0, 0, size), point + yope3d.Vec3(0, 0, size), c)


def draw_aabb(center, half, color=None):
    """Wireframe axis-aligned box. `half` is the half-extents Vec3."""
    c = _col(color)
    cx, cy, cz = center.x, center.y, center.z
    hx, hy, hz = half.x, half.y, half.z
    # 8 corners
    corners = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                corners.append(yope3d.Vec3(cx + sx * hx, cy + sy * hy, cz + sz * hz))
    # index pattern: bit0=x, bit1=y, bit2=z
    edges = [(0, 1), (0, 2), (0, 4), (1, 3), (1, 5), (2, 3),
             (2, 6), (3, 7), (4, 5), (4, 6), (5, 7), (6, 7)]
    for a, b in edges:
        yope3d.draw_line(corners[a], corners[b], c)


def draw_sphere(center, radius, color=None, segments=16):
    """Three great-circle rings (XY, XZ, YZ) approximating a sphere."""
    c = _col(color)
    step = 2.0 * math.pi / segments
    for ring in range(3):
        prev = None
        for i in range(segments + 1):
            a = i * step
            u, v = math.cos(a) * radius, math.sin(a) * radius
            if ring == 0:
                p = yope3d.Vec3(center.x + u, center.y + v, center.z)
            elif ring == 1:
                p = yope3d.Vec3(center.x + u, center.y, center.z + v)
            else:
                p = yope3d.Vec3(center.x, center.y + u, center.z + v)
            if prev is not None:
                yope3d.draw_line(prev, p, c)
            prev = p


def draw_ray_hit(origin, direction, length=50.0, color=None):
    """Draw an aim ray AND a cross at where it currently hits (if anything)."""
    c = _col(color)
    hit, e, point, normal, t = yope3d.raycast(origin, direction, length)
    if hit:
        draw_cross(point, c)
    else:
        yope3d.draw_line(origin, origin + direction.normalize() * length, c)
