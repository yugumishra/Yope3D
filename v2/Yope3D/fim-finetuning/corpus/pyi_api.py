#!/usr/bin/env python3
"""Parse typings/yope3d/__init__.pyi into a structured API model.

The stub is valid Python, so this uses `ast` rather than regex. That is not
fussiness: the eval harness originally used a regex API extractor, missed seven
lowercase module singletons (`world`, `camera`, `input`, `audio`,
`scene_manager`, `window`, `settings`), and inflated the measured hallucination
rate 2-3x — 19.5% reported against 7.3% actual (PLAN.txt 5.6). Regex over
declarations is how that class of bug gets in. `ast` cannot miss a binding that
is syntactically present.

The stub is also the canonical reference for the entire yope3d Python surface
(per CLAUDE.md), which makes it the right synthesis source: every name here is
real by construction, so generated training text cannot hallucinate.
"""

from __future__ import annotations

import ast
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUB = ROOT / "typings" / "yope3d" / "__init__.pyi"


@dataclass
class Param:
    name: str
    annotation: str = ""
    has_default: bool = False


@dataclass
class Func:
    name: str
    params: list[Param] = field(default_factory=list)
    returns: str = ""
    doc: str = ""
    owner: str = ""       # class name, or "" for module level
    is_property: bool = False
    overloads: int = 1

    @property
    def qualname(self) -> str:
        return f"{self.owner}.{self.name}" if self.owner else self.name

    @property
    def required(self) -> list[Param]:
        return [p for p in self.params if not p.has_default]


@dataclass
class Attr:
    name: str
    annotation: str
    doc: str = ""
    owner: str = ""
    readonly: bool = False


@dataclass
class Class:
    name: str
    doc: str = ""
    attrs: list[Attr] = field(default_factory=list)
    methods: list[Func] = field(default_factory=list)

    def attr(self, name: str) -> Attr | None:
        return next((a for a in self.attrs if a.name == name), None)


@dataclass
class Api:
    classes: dict[str, Class] = field(default_factory=dict)
    funcs: dict[str, Func] = field(default_factory=dict)
    singletons: dict[str, str] = field(default_factory=dict)   # name -> type
    constants: dict[str, str] = field(default_factory=dict)    # name -> annotation
    literals: dict[str, list[str]] = field(default_factory=dict)

    @property
    def names(self) -> set[str]:
        """Every top-level `yope3d.<name>` that is real.

        This is the set the validation proxy and the eval harness check
        emitted calls against.
        """
        return (set(self.classes) | set(self.funcs)
                | set(self.singletons) | set(self.constants) | set(self.literals))

    @property
    def components(self) -> list[str]:
        return self.literals.get("ComponentName", [])


def _txt(node) -> str:
    """Render an annotation node back to source-ish text."""
    if node is None:
        return ""
    try:
        return ast.unparse(node)
    except Exception:
        return ""


def _doc(node) -> str:
    d = ast.get_docstring(node) or ""
    return d.strip().split("\n")[0] if d else ""


def _literal_values(node) -> list[str] | None:
    """Extract `Literal["a", "b", ...]` string members, else None."""
    if not isinstance(node, ast.Subscript):
        return None
    base = _txt(node.value)
    if not base.endswith("Literal"):
        return None
    sl = node.slice
    elts = sl.elts if isinstance(sl, ast.Tuple) else [sl]
    vals = [e.value for e in elts
            if isinstance(e, ast.Constant) and isinstance(e.value, str)]
    return vals or None


def _func(node, owner: str = "") -> Func:
    decos = {_txt(d) for d in node.decorator_list}
    params: list[Param] = []
    args = node.args
    n_def = len(args.defaults)
    pos = args.posonlyargs + args.args
    first_default = len(pos) - n_def

    for i, a in enumerate(pos):
        if i == 0 and owner and a.arg in ("self", "cls"):
            continue
        params.append(Param(a.arg, _txt(a.annotation), i >= first_default))
    for a, d in zip(args.kwonlyargs, args.kw_defaults):
        params.append(Param(a.arg, _txt(a.annotation), d is not None))

    return Func(
        name=node.name,
        params=params,
        returns=_txt(node.returns),
        doc=_doc(node),
        owner=owner,
        is_property="property" in decos,
    )


def _body(nodes, owner: str, cls: Class | None, api: Api) -> None:
    """Walk a class or module body, filling `cls` (if given) and `api`."""
    pending_doc: list[tuple[object, str]] = []

    for i, n in enumerate(nodes):
        # A bare string expression following an assignment is its docstring.
        nxt = nodes[i + 1] if i + 1 < len(nodes) else None
        doc = ""
        if isinstance(nxt, ast.Expr) and isinstance(nxt.value, ast.Constant) \
                and isinstance(nxt.value.value, str):
            doc = nxt.value.value.strip().split("\n")[0]

        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)):
            f = _func(n, owner)
            if cls is not None:
                # @overload emits repeated defs; collapse and count them.
                prev = next((m for m in cls.methods if m.name == f.name), None)
                if prev:
                    prev.overloads += 1
                    continue
                if f.is_property:
                    cls.attrs.append(Attr(f.name, f.returns, f.doc, owner, True))
                else:
                    cls.methods.append(f)
            else:
                prev = api.funcs.get(f.name)
                if prev:
                    prev.overloads += 1
                    if not prev.doc:
                        prev.doc = f.doc
                else:
                    api.funcs[f.name] = f

        elif isinstance(n, ast.AnnAssign) and isinstance(n.target, ast.Name):
            name, ann = n.target.id, _txt(n.annotation)
            if cls is not None:
                cls.attrs.append(Attr(name, ann, doc, owner))
            elif ann in api.classes or ann in ("World", "Camera", "Input",
                                               "AudioSystem", "SceneManager",
                                               "Window", "Settings"):
                api.singletons[name] = ann
            else:
                api.constants[name] = ann

        elif isinstance(n, ast.Assign) and len(n.targets) == 1 \
                and isinstance(n.targets[0], ast.Name):
            name = n.targets[0].id
            vals = _literal_values(n.value)
            if vals is not None:
                api.literals[name] = vals
            elif cls is None:
                api.constants[name] = _txt(n.value)

    _ = pending_doc


def parse(path: Path | str = STUB) -> Api:
    src = Path(path).read_text()
    tree = ast.parse(src)
    api = Api()

    # Classes first, so singleton detection can resolve their types.
    for n in tree.body:
        if isinstance(n, ast.ClassDef):
            api.classes[n.name] = Class(n.name, _doc(n))

    for n in tree.body:
        if isinstance(n, ast.ClassDef):
            _body(n.body, n.name, api.classes[n.name], api)

    _body(tree.body, "", None, api)
    return api


def main() -> None:
    import argparse
    import json

    ap = argparse.ArgumentParser(description="Inspect the parsed yope3d API model")
    ap.add_argument("--stub", default=str(STUB))
    ap.add_argument("--json", action="store_true", help="dump the name set as JSON")
    a = ap.parse_args()

    api = parse(a.stub)
    if a.json:
        print(json.dumps(sorted(api.names), indent=1))
        return

    n_attrs = sum(len(c.attrs) for c in api.classes.values())
    n_meth = sum(len(c.methods) for c in api.classes.values())
    print(f"classes     {len(api.classes):>4}   ({n_attrs} attrs, {n_meth} methods)")
    print(f"functions   {len(api.funcs):>4}")
    print(f"singletons  {len(api.singletons):>4}   {', '.join(sorted(api.singletons))}")
    print(f"constants   {len(api.constants):>4}")
    print(f"literals    {len(api.literals):>4}   "
          f"{', '.join(f'{k}[{len(v)}]' for k, v in api.literals.items())}")
    print(f"TOTAL top-level names {len(api.names)}")
    print(f"components  {len(api.components)}")

    big = sorted(api.classes.values(), key=lambda c: -(len(c.attrs) + len(c.methods)))
    print("\nlargest classes:")
    for c in big[:8]:
        print(f"  {c.name:<22} {len(c.attrs):>3} attrs  {len(c.methods):>3} methods")


if __name__ == "__main__":
    main()
