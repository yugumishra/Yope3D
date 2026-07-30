#!/usr/bin/env python3
"""Synthesise correct-by-construction Yope3D Python from the .pyi.

Emits ordinary .py source files, NOT FIM examples. `make_dataset.py` does the
cutting, so synthetic and real files go through one identical code path and the
held-out logic works the same on both.

WHY SYNTHESIS COMES BEFORE HAND-WRITING (PLAN.txt 9.3)
    The 19 behavior files are ~4.4K lines that hammer a narrow slice of the API
    — add_sphere, get_position, a little UI. Meanwhile attach_*_collider,
    build_sphere_compound, add_spring_with_proxies and scene_payload are barely
    touched, so the model has no chance on them today. Synthesis gives UNIFORM
    coverage of all 187 names for the cost of running a script. Hand-written
    scripts are the scarcest resource and should not be spent on volume the
    generator produces free.

WHAT THIS DELIBERATELY DOES NOT DO
    It does not teach the CLAUDE.md invariants — physics-bodies-are-hierarchy-
    roots, the composite-behavior stacking pattern. Those are multi-line
    ordering and composition rules that no type signature encodes, so no
    generator driven by signatures can emit them. A pyright squiggle catches a
    wrong name; nothing catches a correctly-named call in a subtly wrong order.

    Consequence for the corpus: this output is broad but shallow. Weight it
    accordingly at training time rather than letting line count decide.

    But it MUST NOT teach a violation. A signature-driven generator has no way
    to know a binding is forbidden — see FORBIDDEN below, which is the one case
    where that mattered.
"""

from __future__ import annotations

import argparse
import ast
import random
from pathlib import Path

import usage_freq
from pyi_api import Api, Attr, Class, Func, parse

# ---------------------------------------------------------------- value model

VEC_CTORS = {"Vec2": 2, "Vec3": 3, "Vec4": 4}

# Name-keyed value hints. Type alone gives `1.0`; the field name is what makes
# `radius=0.5` and `mass=70.0` look like code a person wrote. Plausible
# magnitudes matter because the model learns joint distributions over
# (identifier, literal) — training on `radius = 12345.0` teaches noise.
NUMERIC_HINTS: dict[str, tuple[float, float]] = {
    "radius": (0.15, 2.0), "mass": (0.5, 90.0), "friction": (0.2, 0.9),
    "restitution": (0.0, 0.8), "damping": (0.0, 0.4), "stiffness": (20.0, 400.0),
    "half_height": (0.2, 1.2), "height": (0.5, 3.0), "speed": (1.0, 12.0),
    "gain": (0.2, 1.0), "pitch": (0.8, 1.3), "roughness": (0.05, 0.95),
    "metallic": (0.0, 1.0), "intensity": (0.5, 8.0), "range": (2.0, 30.0),
    "fov": (45.0, 90.0), "scale": (0.25, 2.0), "angle": (-1.4, 1.4),
    "limit": (0.3, 1.5), "force": (10.0, 500.0), "impulse": (1.0, 40.0),
    "dt": (0.008, 0.033), "duration": (0.2, 3.0), "t": (0.0, 1.0),
}

STRING_HINTS: dict[str, list[str]] = {
    "albedo_map": ["textures/crate_albedo.png", "textures/metal_basecolor.png"],
    "normal_map": ["textures/crate_normal.png", "textures/metal_normal.png"],
    "metal_rough_map": ["textures/crate_mr.png"],
    "occlusion_map": ["textures/crate_ao.png"],
    "emissive_map": ["textures/lamp_emissive.png"],
    "path": ["audios/impact.ogg", "models/prop.glb", "textures/tnail.png"],
    "name": ["player", "ground", "crate_01", "spawn_point"],
    "text": ["Score: 0", "Paused", "Press E to interact"],
    "font": ["fonts/inter_ascii.msdf"],
    "scene": ["scenes/level_01.yscene", "scenes/menu.yscene"],
    "mesh": ["models/crate.obj", "models/sphere.obj"],
}

ENTITY_VARS = ["e", "target", "other", "body", "prop", "anchor", "wheel"]

# Bindings that EXIST but must never appear in a behavior script. Filtered out
# of every sampler and re-checked in validate() as gate 4.
#
# WHY THIS IS NEEDED AT ALL. The generator reads signatures, and a signature
# cannot express "never call this here". `reset_physics` presents as an
# ordinary zero-arg World method with the benign docstring "Clear all
# velocities/contacts and wake everything"; CLAUDE.md's prohibition lives in
# prose, in _gallery.py and the engine docs. So the first run emitted it 56
# times across 53 of 400 files. With ~10x cut redundancy that is ~550 training
# examples carrying it in context, against ZERO counter-examples: the one place
# the rule is written in Python is _gallery.py, and make_dataset.py skips
# `_`-prefixed files, so it never entered the dataset at all.
#
# That is worse than an untaught invariant — it is a taught violation, and it
# is invisible to every metric here: tier 2 classifies `reset_physics` as
# `correct` because the name is real. Read the ban as "wrong caller", not
# "wrong name": it is legitimate from the editor and from C++, and this corpus
# is 100% behavior scripts.
FORBIDDEN: dict[str, str] = {
    "reset_physics": "never from a script — per-entity remove_entity instead "
                     "(CLAUDE.md; scripts/behaviors/_gallery.py)",
}


class Gen:
    """Type-and-name-directed literal generator."""

    def __init__(self, api: Api, rng: random.Random, freq=None, lam: float = 0.25):
        self.api, self.rng = api, rng
        # Empirical usage counts per surface, from train-split behaviors only.
        # None => uniform, i.e. the pre-2026-07-30 behaviour.
        self.freq = freq
        self.lam = lam

    def pick(self, seq, bucket: str, key=None):
        """Weighted choice over `seq`, favouring names real code actually uses.

        THE POINT OF THIS METHOD. Uniform sampling here is what broke the first
        LoRA. Measured: Vec3 is 36% of real `yope3d.X` uses and uniform gave it
        1/187 = 0.53% — a 68x under-weighting of the most common name in the
        API. Flattening the name distribution taught the model each family's
        vocabulary without its membership, so it emitted EASE_IN_OUT_CUBIC for
        EASE_CUBIC_IN_OUT and tripled the invention rate.
        """
        if not seq:
            return None
        if self.freq is None or self.lam >= 1.0:
            return self.rng.choice(seq)
        keyfn = key or (lambda x: getattr(x, "name", x))
        names = [keyfn(x) for x in seq]
        w = usage_freq.weights(names, self.freq[bucket], self.lam)
        return self.rng.choices(seq, weights=w, k=1)[0]

    def num(self, name: str, integral: bool = False) -> str:
        lo, hi = 0.0, 5.0
        for k, (a, b) in NUMERIC_HINTS.items():
            if k in name:
                lo, hi = a, b
                break
        if integral:
            return str(self.rng.randint(0, 4))
        v = self.rng.uniform(lo, hi)
        return f"{v:.2f}".rstrip("0").rstrip(".") or "0.0"

    def string(self, name: str, ctx: str = "") -> str:
        """Longest-key-first so `scene` beats `path` on `scene_path`.

        First-key-wins produced `load_scene("textures/tnail.png")` — a texture
        path passed to a scene loader. Syntactically fine, semantically wrong,
        and wrong in exactly the way training data must not be: it teaches a
        plausible-looking incorrect pairing.
        """
        hay = f"{ctx} {name}".lower()
        for k in sorted(STRING_HINTS, key=len, reverse=True):
            if k in hay:
                return f'"{self.rng.choice(STRING_HINTS[k])}"'
        return f'"{self.rng.choice(["player", "crate", "ground", "hud_root"])}"'

    def value(self, ann: str, name: str = "", scope: list[str] | None = None,
              ctx: str = "") -> str:
        """A syntactically valid, type-plausible literal for annotation `ann`."""
        ann = (ann or "").replace("None", "").replace("|", "").strip() or "float"
        scope = scope or []

        if ann in VEC_CTORS:
            n = VEC_CTORS[ann]
            args = ", ".join(self.num(name) for _ in range(n))
            return f"yope3d.{ann}({args})"
        if ann == "Quat":
            # from_axis_angle/from_euler are @staticmethods ON Quat, not free
            # functions. The first draft wrote `yope3d.quat_from_axis_angle`,
            # which does not exist — the C++-flavoured name the model itself
            # guesses. validate() caught it; that is the whole point of it.
            return self.rng.choice([
                "yope3d.Quat()",
                f"yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 1, 0), {self.num('angle')})",
                f"yope3d.Quat.from_euler(0.0, {self.num('angle')})",
            ])
        if ann == "Entity":
            return self.rng.choice(scope) if scope else "entity"
        if ann.startswith("bool"):
            return self.rng.choice(["True", "False"])
        if ann.startswith("int"):
            return self.num(name, integral=True)
        if ann.startswith("float"):
            return self.num(name)
        if ann.startswith("str"):
            return self.string(name, ctx)
        if ann.startswith("tuple"):
            n = max(2, ann.count("float") + ann.count("int")) or 3
            return "(" + ", ".join(self.num(name) for _ in range(n)) + ")"
        if ann.startswith("list"):
            return "[]"
        if ann == "ComponentName":
            return f'"{self.pick(self.api.components, "component", key=lambda x: x)}"'
        if ann in self.api.classes:
            return self.rng.choice(scope) if scope else "None"
        return self.num(name)

    def call_args(self, f: Func, scope: list[str], all_params: bool = False) -> str:
        ps = f.params if all_params else f.required
        # The callee name is context: `load_scene` should get a .yscene, not
        # whatever the parameter happens to be called.
        return ", ".join(
            self.value(p.annotation, p.name, scope, ctx=f.name) for p in ps)


# ------------------------------------------------------------------ snippets
# Each returns a list of source lines (no trailing newlines).

def snip_component_configure(api: Api, g: Gen, cls: Class) -> list[str]:
    """reg_add -> configure fields. The single most common real pattern."""
    var = g.rng.choice(["c", "m", "hull", "cmp", "t"])
    writable = [a for a in cls.attrs if not a.readonly][:6]
    if not writable:
        return []
    lines = [f'{var} = yope3d.reg_add(entity, "{cls.name}")']
    for a in g.rng.sample(writable, min(len(writable), g.rng.randint(2, 5))):
        lines.append(f"{var}.{a.name} = {g.value(a.annotation, a.name)}")
    return lines


def snip_component_read(api: Api, g: Gen, cls: Class) -> list[str]:
    """reg_get guarded by the None check the type checker actually wants."""
    var = g.rng.choice(["c", "m", "hull", "form"])
    readable = [a for a in cls.attrs if not a.readonly]
    if not readable:
        return []
    a = g.rng.choice(readable)
    return [
        f'{var} = yope3d.reg_get(entity, "{cls.name}")',
        f"if {var} is not None:",
        f"    {var}.{a.name} = {g.value(a.annotation, a.name)}",
    ]


def snip_world_call(api: Api, g: Gen, f: Func, scope: list[str]) -> list[str]:
    """A World method call, with its result bound when it returns something.

    `scope` is the list of entity-typed names actually bound at this point in
    the method. Passing a fixed ["entity", "e", "other"] emitted
    `world.attach_capsule_collider(e, ...)` one line *above* `e = ...` — a
    guaranteed NameError. Dataflow that cannot run is not code to imitate.
    """
    args = g.call_args(f, scope, all_params=g.rng.random() < 0.35)
    call = f"world.{f.name}({args})"
    if f.returns and f.returns not in ("None", ""):
        if f.returns == "Entity":
            var = g.rng.choice([v for v in ENTITY_VARS if v not in scope] or ["e2"])
            scope.append(var)
        else:
            var = g.rng.choice(["r", "res", "h"])
        return [f"{var} = {call}"]
    return [f"{call}"]


def snip_singleton_call(api: Api, g: Gen, obj: str, cls: Class,
                        scope: list[str]) -> list[str]:
    usable = [m for m in cls.methods if m.name not in FORBIDDEN]
    if not usable:
        return []
    f = g.pick(usable, "method")
    args = g.call_args(f, scope)
    call = f"yope3d.{obj}.{f.name}({args})"
    if f.returns and f.returns not in ("None", ""):
        return [f"v = {call}"]
    return [f"{call}"]


def snip_free_func(api: Api, g: Gen, f: Func, scope: list[str]) -> list[str]:
    args = g.call_args(f, scope)
    call = f"yope3d.{f.name}({args})"
    if f.returns and f.returns not in ("None", ""):
        if f.returns == "Entity":
            var = g.rng.choice([v for v in ENTITY_VARS if v not in scope] or ["e2"])
            scope.append(var)
            return [f"{var} = {call}"]
        return [f"v = {call}"]
    return [f"{call}"]


def snip_constants(api: Api, g: Gen) -> list[str]:
    """Input / easing / bus constants in their idiomatic call sites.

    These are ~90 of the 187 names and never appear otherwise, since they are
    only ever *arguments*. Without this the generator's coverage number is
    misleadingly low and, worse, the model sees no KEY_* usage at all.
    """
    keys = [k for k in api.constants if k.startswith("KEY_")]
    mouse = [k for k in api.constants if k.startswith("MOUSE_")]
    eases = [k for k in api.constants if k.startswith("EASE_")]
    buses = [k for k in api.constants if k.startswith("BUS_")]
    opts: list[list[str]] = []
    if keys:
        k = g.pick(keys, "toplevel", key=lambda x: x)
        opts.append([f"if yope3d.input.is_key_down(yope3d.{k}):",
                     f"    world.apply_impulse(entity, yope3d.Vec3(0, "
                     f"{g.num('impulse')}, 0))"])
        opts.append([f"if yope3d.input.is_key_pressed(yope3d.{g.pick(keys, "toplevel", key=lambda x: x)}):",
                     '    yope3d.play_sound("audios/impact.ogg")'])
    if mouse:
        # raycast returns a 5-tuple; unpacking it is the idiomatic form and
        # the shape the model most needs to learn.
        opts.append([f"if yope3d.input.is_mouse_down(yope3d.{g.pick(mouse, "toplevel", key=lambda x: x)}):",
                     "    hit, ent, point, normal, dist = yope3d.raycast(",
                     "        yope3d.get_position(entity), "
                     f"yope3d.Vec3(0, 0, -1), {g.num('range')})"])
    if buses:
        opts.append([f"yope3d.audio.set_bus_gain(yope3d.{g.pick(buses, "toplevel", key=lambda x: x)}, "
                     f"{g.num('gain')})"])
    if eases:
        opts.append(['src = yope3d.play_sound("audios/impact.ogg", None, '
                     f"{g.num('gain')})",
                     "if src is not None:",
                     f"    yope3d.audio.fade_gain(src, {g.num('gain')}, "
                     f"{g.num('duration')}, yope3d.{g.pick(eases, "toplevel", key=lambda x: x)})"])
    return g.rng.choice(opts) if opts else []


def snip_view_loop(api: Api, g: Gen) -> list[str]:
    """A view() loop — the idiomatic ECS iteration."""
    comps = [c for c in api.components if c in api.classes
             and any(not a.readonly for a in api.classes[c].attrs)]
    if not comps:
        return []
    picked = g.rng.sample(comps, min(len(comps), g.rng.randint(1, 2)))
    names = ", ".join(f'"{c}"' for c in picked)
    binds = ", ".join(["ent"] + [c.lower()[:6] for c in picked])
    lines = [f"for {binds} in yope3d.view({names}):"]
    c0 = api.classes[picked[0]]
    a = g.rng.choice([x for x in c0.attrs if not x.readonly])
    lines.append(f"    {picked[0].lower()[:6]}.{a.name} = "
                 f"{g.value(a.annotation, a.name)}")
    return lines


def snip_math(api: Api, g: Gen, has_dt: bool = False) -> list[str]:
    """Self-contained math. Every operand is bound in the snippet itself.

    An earlier version emitted `yope3d.length(p - q)` with p and q undefined —
    valid syntax, guaranteed NameError. Training on code that cannot run
    teaches the shape of a bug.
    """
    a, b = g.num("pos"), g.num("pos")
    opts = [
        ["p = yope3d.get_position(entity)",
         f"d = (p - yope3d.Vec3({a}, 0, {b})).length()"],
        [f"n = yope3d.Vec3({a}, 0, {b}).normalize()"],
        [f"r = yope3d.to_radians({g.rng.randint(5, 180)}.0)"],
        ["origin = yope3d.get_position(entity)",
         f"q = yope3d.look_at((yope3d.Vec3({a}, 0, {b}) - origin).normalize())"],
        [f"spin = yope3d.Quat.from_axis_angle(yope3d.Vec3(0, 1, 0), "
         f"{g.num('angle')})"],
        [f"rot = yope3d.Quat.from_euler({g.num('angle')}, {g.num('angle')})"],
    ]
    if has_dt:
        # `dt` exists only in update(). Emitting it in init() or
        # on_collision_enter() is a NameError — caught by _undefined_names.
        opts.append([f"t = yope3d.clamp(dt * {g.num('speed')}, 0.0, 1.0)",
                     f"v = yope3d.lerp({a}, {b}, t)"])
    return g.rng.choice(opts)


# ------------------------------------------------------------------- assembly

METHOD_HEADS = [
    ("def init(self, world, entity, params):", ["entity"]),
    ("def update(self, world, entity, dt):", ["entity"]),
    ("def on_collision_enter(self, world, entity, other, contact):", ["entity", "other"]),
    ("def on_ui_press(self, world, entity):", ["entity"]),
]


def make_file(api: Api, g: Gen, index: int, methods: int, body_min: int,
              body_max: int) -> str:
    """One synthetic behavior module."""
    cls_name = f"Synth{index:03d}Behavior"
    out = [
        '"""Generated API-coverage sample. Not a real behavior — see synth_pyi.py."""',
        "import yope3d",
        "",
        "",
        f"class {cls_name}:",
        "    PARAMS = {",
        f'        "speed": {{"type": "float", "default": {g.num("speed")}}},',
        f'        "enabled": {{"type": "bool", "default": True}},',
        "    }",
        "",
    ]

    comp_classes = [api.classes[c] for c in api.components if c in api.classes]
    world = api.classes.get("World")
    # FORBIDDEN is filtered at every callable source, not just World, so adding
    # a name to the dict is sufficient wherever the binding happens to live.
    world_methods = [m for m in (world.methods if world else [])
                     if m.name not in FORBIDDEN]
    singletons = [(n, api.classes[t]) for n, t in api.singletons.items()
                  if t in api.classes and n != "world"]
    free = [f for f in api.funcs.values()
            if not f.name.startswith("_") and f.name not in FORBIDDEN]

    for head, sig_scope in g.rng.sample(METHOD_HEADS, min(methods, len(METHOD_HEADS))):
        out.append(f"    {head}")
        # Entity-typed names bound so far. Starts as whatever the signature
        # provides; factory calls append to it as they bind new ones.
        scope = list(sig_scope)
        body: list[str] = []
        for _ in range(g.rng.randint(body_min, body_max)):
            kind = g.rng.choices(
                ["cfg", "read", "world", "single", "free", "view", "math", "const"],
                weights=[20, 11, 23, 8, 9, 11, 7, 11],
            )[0]
            if kind == "cfg" and comp_classes:
                body += snip_component_configure(api, g, g.pick(comp_classes, "component"))
            elif kind == "read" and comp_classes:
                body += snip_component_read(api, g, g.pick(comp_classes, "component"))
            elif kind == "world" and world_methods:
                body += snip_world_call(api, g, g.pick(world_methods, "method"), scope)
            elif kind == "single" and singletons:
                n, c = g.pick(singletons, "toplevel", key=lambda t: t[0])
                body += snip_singleton_call(api, g, n, c, scope)
            elif kind == "free" and free:
                body += snip_free_func(api, g, g.pick(free, "toplevel"), scope)
            elif kind == "view":
                body += snip_view_loop(api, g)
            elif kind == "const":
                body += snip_constants(api, g)
            else:
                body += snip_math(api, g, has_dt="dt" in head)
        # Snippets are emitted indent-relative (0 = statement level); assembly
        # owns the depth. Method bodies live at 8 spaces inside a class.
        out += [("        " + ln if ln else "") for ln in (body or ["pass"])] + [""]

    return "\n".join(out) + "\n"


def validate(src: str, api: Api, path: str) -> list[str]:
    """Enforce the two claims this generator makes about its own output.

    "Correct-by-construction" is worth nothing as an assertion — the first
    draft of this file emitted method bodies at the wrong indent level and was
    not valid Python at all. So it is checked, every file, every run:

      1. it parses (invalid Python is worse than no data)
      2. every `yope3d.<name>` resolves against the .pyi surface
      3. no name is read before it is bound (see _undefined_names)
      4. no FORBIDDEN binding is called

    Check 2 is the important one. The corpus exists to stop the model
    inventing bindings; a generator that invents them itself would train the
    exact failure it is meant to fix.

    Check 4 exists because filtering the samplers is not enough on its own.
    The filter lives at the call sites that happen to exist today; a new
    snippet function added later would bypass it and nothing downstream would
    complain, because a forbidden call is real, parses, type-checks, and scores
    as `correct` on the tier-2 probe metric. Only this gate fails loudly.
    """
    errs: list[str] = []
    try:
        tree = ast.parse(src)
    except SyntaxError as e:
        return [f"{path}:{e.lineno}: syntax error: {e.msg}"]

    names = api.names
    for n in ast.walk(tree):
        if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name) \
                and n.value.id == "yope3d" and n.attr not in names:
            errs.append(f"{path}:{n.lineno}: invented binding yope3d.{n.attr}")
        # Matched on the attribute/function name alone, deliberately: the
        # receiver may be `world`, `yope3d.world`, or a local alias, and a
        # forbidden call is forbidden however it is spelled.
        called = None
        if isinstance(n, ast.Call):
            called = n.func.attr if isinstance(n.func, ast.Attribute) \
                else getattr(n.func, "id", None)
        if called in FORBIDDEN:
            errs.append(f"{path}:{n.lineno}: forbidden call {called}() — "
                        f"{FORBIDDEN[called]}")

    errs += _undefined_names(tree, path)
    return errs


def _undefined_names(tree: ast.AST, path: str) -> list[str]:
    """Flag reads of names never bound earlier in the same function.

    Catches use-before-assignment, which the fixed-scope version of
    snip_world_call produced constantly: `world.attach_capsule_collider(e, ..)`
    emitted above the `e = world.add_*()` that binds it. Such a file compiles
    and passes an API-name check while being guaranteed to raise NameError.

    Deliberately approximate — it walks statements in source order and does not
    model branches, so it under-reports rather than crying wolf. Generated code
    is straight-line, which is exactly where this is reliable.
    """
    errs: list[str] = []
    module_level = {"yope3d"} | {
        n.name for n in ast.walk(tree) if isinstance(n, ast.ClassDef)}
    builtins = {"True", "False", "None", "self", "range", "len", "print",
                "int", "float", "str", "bool", "list", "dict", "tuple", "min",
                "max", "abs", "enumerate", "zip", "sorted", "sum", "round"}

    def bind(t, into: set[str]) -> None:
        for x in ast.walk(t):
            if isinstance(x, ast.Name):
                into.add(x.id)

    def loads(node, bound: set[str]) -> None:
        """Report Load-context names not yet bound, ignoring nested bodies."""
        for sub in ast.walk(node):
            if isinstance(sub, ast.Name) and isinstance(sub.ctx, ast.Load) \
                    and sub.id not in bound:
                errs.append(f"{path}:{sub.lineno}: undefined name {sub.id}")
                bound.add(sub.id)   # report each name once per function

    def run(body: list, bound: set[str]) -> None:
        for stmt in body:
            if isinstance(stmt, ast.For):
                # Bind the loop target BEFORE descending, or every
                # `for ent, hull in view(): hull.x = ...` false-positives.
                loads(stmt.iter, bound)
                bind(stmt.target, bound)
                run(stmt.body, bound)
            elif isinstance(stmt, (ast.If, ast.While)):
                loads(stmt.test, bound)
                run(stmt.body, bound)
                run(stmt.orelse, bound)
            elif isinstance(stmt, ast.Assign):
                loads(stmt.value, bound)
                for t in stmt.targets:
                    bind(t, bound)
            elif isinstance(stmt, (ast.AnnAssign, ast.AugAssign)):
                if stmt.value is not None:
                    loads(stmt.value, bound)
                bind(stmt.target, bound)
            else:
                loads(stmt, bound)

    for n in ast.walk(tree):
        if isinstance(n, ast.FunctionDef):
            bound = set(builtins) | module_level
            bound |= {a.arg for a in
                      n.args.posonlyargs + n.args.args + n.args.kwonlyargs}
            run(n.body, bound)
    return errs


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="fim-finetuning/corpus/synth",
                    help="output directory for generated .py files")
    # 400 files (39.6K lines) made synth 86% of the corpus. Combined with
    # uniform name sampling that is what broke the first LoRA. The share is now
    # deliberately smaller AND the real behaviors are NOT duplicated to
    # compensate — a smaller, less redundant corpus is the intended trade.
    ap.add_argument("--files", type=int, default=60)
    ap.add_argument("--methods", type=int, default=4)
    ap.add_argument("--body-min", type=int, default=6)
    ap.add_argument("--body-max", type=int, default=14)
    ap.add_argument("--seed", type=int, default=20260728)
    ap.add_argument("--stub", default=None)
    # 0.0 = purely empirical (drops the ~140 names real code never uses)
    # 0.25 = default; real usage dominates, whole surface still reachable
    # 1.0 = uniform, reproducing the behaviour that tripled the invention rate
    ap.add_argument("--lam", type=float, default=0.25,
                    help="uniform-mixing weight for name sampling; 1.0 = old behaviour")
    a = ap.parse_args()

    api = parse(a.stub) if a.stub else parse()
    freq = None if a.lam >= 1.0 else usage_freq.counts()
    g = Gen(api, random.Random(a.seed), freq=freq, lam=a.lam)
    # Resolve against the REPO ROOT, not the caller's cwd. The default is a
    # repo-relative string, so a bare Path(a.out) silently wrote to
    # <cwd>/fim-finetuning/corpus/synth — which, when run from inside
    # fim-finetuning/, produced a 400-file phantom corpus at
    # fim-finetuning/fim-finetuning/corpus/synth. Nothing failed: the generator
    # validated the tree it had just written, so the gates passed while the
    # real corpus went untouched. Every other script here already does this.
    out = Path(a.out)
    if not out.is_absolute():
        out = Path(__file__).resolve().parents[2] / out
    out.mkdir(parents=True, exist_ok=True)

    for old in out.glob("synth_*.py"):
        old.unlink()

    total_lines = 0
    errors: list[str] = []
    covered: set[str] = set()
    for i in range(a.files):
        name = f"synth_{i:04d}.py"
        src = make_file(api, g, i, a.methods, a.body_min, a.body_max)
        errors += validate(src, api, name)
        for n in ast.walk(ast.parse(src)):
            if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name) \
                    and n.value.id == "yope3d":
                covered.add(n.attr)
            # Component classes are reached as string arguments
            # (reg_add(e, "Hull")), never as yope3d.Hull. Counting only
            # attribute access understates coverage by the whole 27-entry
            # component set.
            elif isinstance(n, ast.Constant) and isinstance(n.value, str) \
                    and n.value in api.classes:
                covered.add(n.value)
        (out / name).write_text(src)
        total_lines += src.count("\n")

    print(f"wrote {a.files} files, {total_lines} lines -> {out}")
    print(f"API surface: {len(api.names)} names, {len(api.components)} components")
    print(f"coverage:    {len(covered)}/{len(api.names)} top-level names "
          f"({100*len(covered)/len(api.names):.0f}%) appear in the output")

    if errors:
        print(f"\nFAILED — {len(errors)} validation errors:")
        for e in errors[:20]:
            print("  " + e)
        raise SystemExit(1)
    print("validation: OK (parse / no invented bindings / no undefined names / "
          f"no forbidden calls [{len(FORBIDDEN)}])")

    missing = sorted(api.names - covered)
    if missing:
        print(f"\nuncovered ({len(missing)}) — the generator never emits these; "
              f"hand-written examples are the only path for them:")
        print("  " + ", ".join(missing[:30]) + (" ..." if len(missing) > 30 else ""))


if __name__ == "__main__":
    main()
