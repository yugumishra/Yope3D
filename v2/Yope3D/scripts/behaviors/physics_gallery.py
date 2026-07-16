"""Physics gallery — the 39 validation scenes from the pre-scripting engine.

Ported from the scene switcher that lived in Engine::loadScene at commit b0b630a
(May 2026), before scripting existed. Each scene isolates one narrowphase pair or
one solver property (resting stability, deflection, stacking, spring rest length),
so a regression shows up as a body that jitters, sinks, spins wrong, or passes
through something it shouldn't.

    0-4    Sphere vs floor          — drop, high-speed drop, shallow approach, rest, roll
    5-8    Sphere vs floating panel — center hit, edge approach, edge miss, rest
    9-13   Sphere vs sphere         — head-on, glancing, one fixed, resting stack, high-speed
    14-18  Sphere vs AABB           — drop on top, hit side, floor only, start inside, high-speed
    19-22  Springs                  — two free, one anchored, compressed, stretched
    23-26  AABB vs floor            — drop, high-speed drop, rest, 45-degree ramp
    27-29  AABB vs floating panel   — center hit, edge miss, rest
    30-32  AABB vs AABB             — drop on static, two dynamic, stacking
    33     Sphere on the 45-degree ramp (compare against scene 26)
    34-38  OBB                      — drop, sphere vs OBB, drop on static AABB, head-on, rest

PORTING NOTE — the originals were built on physics::Barrier / BoundedBarrier (an
infinite plane and a finite one) plus the CCD collider, all deleted in M12 when
physics went ECS-native. Here the infinite floor is a 100x100x1 static AABB, the
bounded panel is a thin static AABB, and the angled barrier is a fixed rotated OBB
— same surfaces at the same heights, so every drop height and rest position below
is unchanged. Two consequences worth knowing when you read the results:

  * The "no tunneling" scenes (1, 13, 18, 24) were CCD tests. Nothing sweeps the
    swept volume now, so a fast enough body CAN tunnel — these are honest discrete-
    solver checks today, not guarantees.
  * The "edge miss" scenes (7, 28) dropped a body just outside a *point-tested*
    panel boundary so it fell straight through. A real AABB panel has corners, so
    the body now clips the corner and deflects instead of passing cleanly by.

Attach to any entity in a scene file (assets/scenes/physicsGallery.json does it).

Controls: LEFT/RIGHT switch scene, UP/DOWN pick spawn shape, LMB fire it,
WASD+mouse fly, P collider overlay, H hide HUD.
"""
import math

import yope3d

from behaviors._gallery import GalleryBase

FLOOR = (0.35, 0.30, 0.25)
PANEL = (0.5, 0.7, 0.4)
PROP = (0.7, 0.5, 0.3)
BOX_PURPLE = (0.7, 0.3, 0.7)

BALL = (0.2, 0.6, 1.0)
BALL_HOT = (1.0, 0.4, 0.2)
BALL_FAST = (1.0, 0.2, 0.2)
BALL_MISS = (1.0, 0.3, 0.3)
FIXED = (0.8, 0.8, 0.2)

CUBE = (0.3, 0.7, 0.4)
CUBE_FAST = (1.0, 0.3, 0.2)
CUBE_MISS = (1.0, 0.3, 0.3)
OBB_A = (0.8, 0.4, 0.1)
OBB_B = (0.2, 0.5, 0.9)

R = 0.5          # sphere radius used by every scene
H = (0.5, 0.5, 0.5)   # standard box half-extents


class PhysicsGallery(GalleryBase):

    # ---- Terrain helpers -------------------------------------------------

    def plane_floor(self, world):
        """Top face at y=0 — a sphere of radius 0.5 rests at y=0.5."""
        self.static_box(world, (0.0, -0.5, 0.0), (50.0, 0.5, 50.0), FLOOR)

    def aabb_floor(self, world):
        """Top face at y=0.5 — a sphere of radius 0.5 rests at y=1.0."""
        self.static_box(world, (0.0, 0.0, 0.0), (50.0, 0.5, 50.0), FLOOR)

    def panel(self, world, y, s):
        """Floating slab, half-extent s, top face exactly at y."""
        self.static_box(world, (0.0, y - 0.05, 0.0), (s, 0.05, s), PANEL)

    def ramp(self, world):
        """45-degree slope through the origin (surface normal (1,1,0)/sqrt2).

        Offset half a thickness down the normal so the slab's TOP face — not its
        center — passes through the origin, matching the old infinite barrier plane.
        """
        rot = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 0, 1), math.radians(-45.0))
        self.obb(world, (30.0, 0.5, 30.0), 1.0, (-0.354, -0.354, 0.0), FLOOR,
                 rot=rot, fixed=True)

    # ---- Sphere vs floor -------------------------------------------------

    def s_drop(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (0, 8, 0), BALL)
        self.set_camera((0.0, 4.0, 16.0), pitch=-0.1)

    def s_drop_fast(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (0, 40, 0), BALL)
        self.set_camera((0.0, 12.0, 30.0), pitch=-0.2)

    def s_shallow(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (-12, 1.5, 0), BALL, vel=(6.0, -0.5, 0.0))
        self.set_camera((0.0, 4.0, 18.0), pitch=-0.1)

    def s_rest(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (0, 0.5, 0), BALL)
        self.set_camera((0.0, 2.5, 8.0), pitch=-0.15)

    def s_roll(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (-6, 0.5, 0), BALL, vel=(3.0, 0.0, 0.0))
        self.set_camera((0.0, 3.0, 14.0), pitch=-0.1)

    # ---- Sphere vs floating panel (y=4, half-extent 3) -------------------

    def p_center(self, world):
        self.plane_floor(world)
        self.panel(world, 4.0, 3.0)
        self.sphere(world, 1.0, R, (0, 10, 0), BALL)
        self.set_camera((0.0, 6.0, 16.0), pitch=-0.1)

    def p_edge(self, world):
        self.plane_floor(world)
        self.panel(world, 4.0, 3.0)
        self.sphere(world, 1.0, R, (2.0, 10, 0), BALL)
        self.set_camera((0.0, 6.0, 16.0), pitch=-0.1)

    def p_miss(self, world):
        self.plane_floor(world)
        self.panel(world, 4.0, 3.0)
        # Red = "meant to fall past the panel". Now clips the corner (see module docstring).
        self.sphere(world, 1.0, R, (3.5, 10, 0), BALL_MISS)
        self.set_camera((0.0, 6.0, 16.0), pitch=-0.1)

    def p_rest(self, world):
        self.plane_floor(world)
        self.panel(world, 4.0, 3.0)
        self.sphere(world, 1.0, R, (0, 4.5, 0), BALL)
        self.set_camera((0.0, 6.0, 14.0), pitch=-0.1)

    # ---- Sphere vs sphere ------------------------------------------------

    def ss_headon(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (-5, 3, 0), BALL, vel=(4.0, 0, 0))
        self.sphere(world, 1.0, R, (5, 3, 0), BALL_HOT, vel=(-4.0, 0, 0))
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def ss_glancing(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (-5, 3, 0.35), BALL, vel=(4.0, 0, 0))
        self.sphere(world, 1.0, R, (5, 3, 0), BALL_HOT, vel=(-4.0, 0, 0))
        self.set_camera((0.0, 8.0, 12.0), pitch=-0.5)

    def ss_one_fixed(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (0, 3, 0), FIXED, fixed=True)
        self.sphere(world, 1.0, R, (-2, 3, 0), BALL, vel=(5.0, 0, 0))
        self.set_camera((0.0, 4.0, 12.0), pitch=-0.1)

    def ss_stack(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (0, 0.5, 0), FIXED, fixed=True)
        self.sphere(world, 1.0, R, (0, 3.0, 0), BALL)
        self.set_camera((0.0, 3.0, 9.0), pitch=-0.15)

    def ss_fast(self, world):
        self.plane_floor(world)
        self.sphere(world, 1.0, R, (-15, 3, 0), BALL_FAST, vel=(25.0, 0, 0))
        self.sphere(world, 1.0, R, (0, 3, 0), BALL)
        self.set_camera((0.0, 5.0, 18.0), pitch=-0.1)

    # ---- Sphere vs AABB --------------------------------------------------

    def sa_top(self, world):
        self.aabb_floor(world)
        self.static_box(world, (0, 3.0, 0), (2.0, 0.5, 2.0), PROP)
        self.sphere(world, 1.0, R, (0, 10, 0), BALL)
        self.set_camera((0.0, 6.0, 15.0), pitch=-0.1)

    def sa_side(self, world):
        self.aabb_floor(world)
        self.static_box(world, (6, 2.5, 0), (0.5, 2.5, 3.0), PROP)
        self.sphere(world, 1.0, R, (-4, 1.0, 0), BALL, vel=(5.0, 0, 0))
        self.set_camera((0.0, 4.0, 16.0), pitch=-0.1)

    def sa_floor(self, world):
        self.aabb_floor(world)
        self.sphere(world, 1.0, R, (0, 8, 0), BALL)
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def sa_inside(self, world):
        # Sphere starts fully inside the box — must be pushed out smoothly, not
        # launched across the level.
        self.plane_floor(world)
        self.static_box(world, (0, 3, 0), (2.0, 2.0, 2.0), BOX_PURPLE)
        self.sphere(world, 1.0, R, (0, 3, 0), BALL)
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def sa_fast(self, world):
        self.plane_floor(world)
        self.static_box(world, (6, 3, 0), (0.2, 3.0, 3.0), PROP)
        self.sphere(world, 1.0, R, (-10, 3, 0), BALL_FAST, vel=(30.0, 0, 0))
        self.set_camera((0.0, 5.0, 18.0), pitch=-0.1)

    # ---- Springs ---------------------------------------------------------

    def sp_two_free(self, world):
        self.plane_floor(world)
        a = self.sphere(world, 1.0, R, (-3, 5, 0), BALL)
        b = self.sphere(world, 1.0, R, (3, 5, 0), BALL_HOT)
        self.spring(world, a, b, 5.0, 2.0)
        self.set_camera((0.0, 5.0, 14.0), pitch=-0.1)

    def sp_anchor(self, world):
        self.plane_floor(world)
        a = self.sphere(world, 1.0, R, (0, 8, 0), FIXED, fixed=True)
        b = self.sphere(world, 1.0, R, (0, 3, 0), BALL)
        self.spring(world, a, b, 5.0, 3.0)
        self.set_camera((0.0, 5.0, 12.0), pitch=-0.1)

    def sp_compressed(self, world):
        self.plane_floor(world)
        a = self.sphere(world, 1.0, R, (-0.5, 5, 0), BALL)
        b = self.sphere(world, 1.0, R, (0.5, 5, 0), BALL_HOT)
        self.spring(world, a, b, 5.0, 3.0)
        self.set_camera((0.0, 5.0, 12.0), pitch=-0.1)

    def sp_stretched(self, world):
        self.plane_floor(world)
        a = self.sphere(world, 1.0, R, (-4, 5, 0), BALL)
        b = self.sphere(world, 1.0, R, (4, 5, 0), BALL_HOT)
        self.spring(world, a, b, 5.0, 3.0)
        self.set_camera((0.0, 5.0, 14.0), pitch=-0.1)

    # ---- AABB vs floor ---------------------------------------------------

    def a_drop(self, world):
        self.plane_floor(world)
        self.box(world, H, 1.0, (0, 8, 0), CUBE)
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def a_drop_fast(self, world):
        self.plane_floor(world)
        self.box(world, H, 1.0, (0, 50, 0), CUBE_FAST)
        self.set_camera((0.0, 14.0, 32.0), pitch=-0.2)

    def a_rest(self, world):
        self.plane_floor(world)
        self.box(world, H, 1.0, (0, 0.5, 0), CUBE)
        self.set_camera((0.0, 2.5, 8.0), pitch=-0.15)

    def a_ramp(self, world):
        self.ramp(world)
        self.box(world, H, 1.0, (-4, 8, 0), CUBE)
        self.set_camera((0.0, 5.0, 16.0), pitch=-0.1)

    # ---- AABB vs floating panel -----------------------------------------

    def ap_center(self, world):
        self.plane_floor(world)
        self.panel(world, 4.0, 3.0)
        self.box(world, H, 1.0, (0, 10, 0), CUBE)
        self.set_camera((0.0, 6.0, 16.0), pitch=-0.1)

    def ap_miss(self, world):
        self.plane_floor(world)
        self.panel(world, 4.0, 3.0)
        self.box(world, H, 1.0, (4.0, 10, 0), CUBE_MISS)
        self.set_camera((0.0, 6.0, 16.0), pitch=-0.1)

    def ap_rest(self, world):
        self.plane_floor(world)
        self.panel(world, 4.0, 3.0)
        self.box(world, H, 1.0, (0, 4.5, 0), CUBE)
        self.set_camera((0.0, 6.0, 14.0), pitch=-0.1)

    # ---- AABB vs AABB ----------------------------------------------------

    def aa_static(self, world):
        self.aabb_floor(world)
        self.box(world, H, 1.0, (0, 8, 0), CUBE)
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def aa_dynamic(self, world):
        self.plane_floor(world)
        self.box(world, H, 1.0, (-5, 3, 0), CUBE, vel=(4.0, 0, 0))
        self.box(world, H, 1.0, (5, 3, 0), BALL_HOT, vel=(-4.0, 0, 0))
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def aa_stack(self, world):
        self.plane_floor(world)
        self.box(world, (1.0, 0.5, 1.0), 2.0, (0, 0.5, 0), FIXED)
        self.box(world, H, 1.0, (0, 8, 0), CUBE)
        self.set_camera((0.0, 4.0, 12.0), pitch=-0.1)

    # ---- Comparison ------------------------------------------------------

    def s_ramp(self, world):
        self.ramp(world)
        self.sphere(world, 1.0, R, (-4, 8, 0), BALL)
        self.set_camera((0.0, 5.0, 16.0), pitch=-0.1)

    # ---- OBB -------------------------------------------------------------

    def o_drop(self, world):
        self.plane_floor(world)
        rot = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 0, 1), math.radians(20.0))
        self.obb(world, H, 1.0, (0, 8, 0), OBB_A, rot=rot)
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def o_vs_sphere(self, world):
        self.plane_floor(world)
        rot = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 1, 0), math.radians(30.0))
        self.obb(world, (0.6, 0.6, 0.6), 2.0, (0, 3, 0), OBB_A, rot=rot)
        self.sphere(world, 1.0, R, (-6, 3, 0), BALL, vel=(6.0, 0, 0))
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def o_on_static(self, world):
        self.aabb_floor(world)
        rot = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 0, 1), math.radians(15.0))
        self.obb(world, H, 1.0, (0, 8, 0), OBB_A, rot=rot)
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def o_headon(self, world):
        self.plane_floor(world)
        ra = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 1, 0), math.radians(20.0))
        rb = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 1, 0), math.radians(-20.0))
        self.obb(world, H, 1.0, (-5, 3, 0), OBB_A, vel=(4.0, 0, 0), rot=ra)
        self.obb(world, H, 1.0, (5, 3, 0), OBB_B, vel=(-4.0, 0, 0), rot=rb)
        self.set_camera((0.0, 4.0, 14.0), pitch=-0.1)

    def o_rest(self, world):
        self.plane_floor(world)
        self.obb(world, H, 1.0, (0, 0.5, 0), OBB_A)
        self.set_camera((0.0, 2.5, 8.0), pitch=-0.15)


PhysicsGallery.SCENES = [
    ("Sphere-Floor 1/5 - Drop onto floor", lambda s, w: s.s_drop(w)),
    ("Sphere-Floor 2/5 - High-speed drop", lambda s, w: s.s_drop_fast(w)),
    ("Sphere-Floor 3/5 - Shallow angle approach", lambda s, w: s.s_shallow(w)),
    ("Sphere-Floor 4/5 - Resting stability", lambda s, w: s.s_rest(w)),
    ("Sphere-Floor 5/5 - Rolling", lambda s, w: s.s_roll(w)),
    ("Sphere-Panel 1/4 - Center hit", lambda s, w: s.p_center(w)),
    ("Sphere-Panel 2/4 - Edge approach", lambda s, w: s.p_edge(w)),
    ("Sphere-Panel 3/4 - Edge miss", lambda s, w: s.p_miss(w)),
    ("Sphere-Panel 4/4 - Resting stability", lambda s, w: s.p_rest(w)),
    ("Sphere-Sphere 1/5 - Head-on collision", lambda s, w: s.ss_headon(w)),
    ("Sphere-Sphere 2/5 - Glancing collision", lambda s, w: s.ss_glancing(w)),
    ("Sphere-Sphere 3/5 - One fixed", lambda s, w: s.ss_one_fixed(w)),
    ("Sphere-Sphere 4/5 - Resting stack", lambda s, w: s.ss_stack(w)),
    ("Sphere-Sphere 5/5 - High-speed", lambda s, w: s.ss_fast(w)),
    ("Sphere-AABB 1/5 - Drop onto AABB top", lambda s, w: s.sa_top(w)),
    ("Sphere-AABB 2/5 - Hit AABB side", lambda s, w: s.sa_side(w)),
    ("Sphere-AABB 3/5 - Static floor only", lambda s, w: s.sa_floor(w)),
    ("Sphere-AABB 4/5 - Sphere inside AABB", lambda s, w: s.sa_inside(w)),
    ("Sphere-AABB 5/5 - High-speed", lambda s, w: s.sa_fast(w)),
    ("Spring 1/4 - Two free spheres", lambda s, w: s.sp_two_free(w)),
    ("Spring 2/4 - One fixed anchor", lambda s, w: s.sp_anchor(w)),
    ("Spring 3/4 - Compressed", lambda s, w: s.sp_compressed(w)),
    ("Spring 4/4 - Stretched", lambda s, w: s.sp_stretched(w)),
    ("AABB-Floor 1/4 - Drop onto floor", lambda s, w: s.a_drop(w)),
    ("AABB-Floor 2/4 - High-speed drop", lambda s, w: s.a_drop_fast(w)),
    ("AABB-Floor 3/4 - Resting stability", lambda s, w: s.a_rest(w)),
    ("AABB-Floor 4/4 - 45-degree ramp", lambda s, w: s.a_ramp(w)),
    ("AABB-Panel 1/3 - Center hit", lambda s, w: s.ap_center(w)),
    ("AABB-Panel 2/3 - Edge miss", lambda s, w: s.ap_miss(w)),
    ("AABB-Panel 3/3 - Resting stability", lambda s, w: s.ap_rest(w)),
    ("AABB-AABB 1/3 - Drop onto static AABB", lambda s, w: s.aa_static(w)),
    ("AABB-AABB 2/3 - Two dynamic collide", lambda s, w: s.aa_dynamic(w)),
    ("AABB-AABB 3/3 - Stacking", lambda s, w: s.aa_stack(w)),
    ("Sphere on 45-degree ramp (compare vs 27)", lambda s, w: s.s_ramp(w)),
    ("OBB 1/5 - Drop onto floor", lambda s, w: s.o_drop(w)),
    ("OBB 2/5 - Sphere vs OBB", lambda s, w: s.o_vs_sphere(w)),
    ("OBB 3/5 - Drop onto static AABB floor", lambda s, w: s.o_on_static(w)),
    ("OBB 4/5 - OBB vs OBB head-on", lambda s, w: s.o_headon(w)),
    ("OBB 5/5 - Resting stability", lambda s, w: s.o_rest(w)),
]
