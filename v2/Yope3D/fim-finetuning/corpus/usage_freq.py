#!/usr/bin/env python3
"""Empirical API-usage frequencies from real hand-written behaviors.

WHY THIS EXISTS
    synth_pyi.py sampled every API name UNIFORMLY. That was a deliberate
    design choice — "uniform coverage of the whole surface" — and it is the
    reason the first LoRA made things worse: at 86% of the corpus, uniform
    sampling into interchangeable method bodies makes P(name | context)
    essentially FLAT within each name family. Ten passes over that overwrote
    the base model's context-conditioned prior with a uniform one.

    The measured symptom was unambiguous. The model learned each family's
    vocabulary and lost the joint:
        real EASE_CUBIC_IN_OUT  ->  emitted EASE_IN_OUT_CUBIC   (tokens, wrong order)
        real EASE_LINEAR        ->  emitted EASING_LINEAR
        real BUS_MUSIC          ->  emitted AUDIO_BUS_MUSKC / BGM_BUS
    Tier-2 invention rate went 12.2% -> 36.8%, on every stratum including the
    synthetic one.

    So the generator needs the real skew: common names common, rare names rare
    but present.

TRAIN-SPLIT ONLY
    Frequencies come from scripts/behaviors MINUS splits.HELDOUT_BEHAVIORS.
    Weighting the generator by held-out usage would leak the evaluation
    distribution into the training corpus — the fine-tune would then look good
    on tier 1 for a reason that has nothing to do with learning the API.

SMOOTHING
    Pure empirical weighting would drop the ~120 names the 19 behavior files
    never touch, and those are exactly the ones the model has no chance on
    today. So the sampler interpolates:
        p = (1 - lam) * p_empirical + lam * p_uniform
    lam=0.25 keeps the whole surface reachable while letting real usage
    dominate. lam=1.0 reproduces the old uniform behaviour exactly, which
    makes this an A/B-able knob rather than a rewrite.
"""

from __future__ import annotations

import argparse
import ast
import collections
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _behavior_files(include_heldout: bool = False) -> list[Path]:
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import splits
    d = ROOT / "scripts" / "behaviors"
    held = set(splits.HELDOUT_BEHAVIORS)
    out = []
    for p in sorted(d.rglob("*.py")):
        if p.name.startswith("_"):
            continue
        if not include_heldout and p.name in held:
            continue
        out.append(p)
    return out


def counts(include_heldout: bool = False) -> dict[str, collections.Counter]:
    """Per-surface name counts.

    Buckets mirror the places synth_pyi.py makes a choice, so the weights can
    be applied at exactly those call sites:
        method     world.X(...) and singleton .X(...)
        toplevel   yope3d.X  (free functions, classes, constants)
        component  the "Name" string literal in reg_get/reg_add/has_component
    """
    out = {k: collections.Counter() for k in ("method", "toplevel", "component")}
    for p in _behavior_files(include_heldout):
        try:
            tree = ast.parse(p.read_text())
        except SyntaxError:
            continue
        for n in ast.walk(tree):
            # yope3d.X — free funcs, classes, constants
            if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name) \
                    and n.value.id == "yope3d":
                out["toplevel"][n.attr] += 1
            # any .X(...) call — world methods, singleton methods, Vec3 methods
            if isinstance(n, ast.Call):
                f = n.func
                if isinstance(f, ast.Attribute):
                    out["method"][f.attr] += 1
                # component name literals
                name = f.attr if isinstance(f, ast.Attribute) else getattr(f, "id", None)
                if name in ("reg_get", "reg_add", "has_component", "reg_remove"):
                    for arg in n.args:
                        if isinstance(arg, ast.Constant) and isinstance(arg.value, str):
                            out["component"][arg.value] += 1
    return out


def weights(names: list[str], bucket: collections.Counter, lam: float = 0.25) -> list[float]:
    """Interpolated sampling weights, aligned with `names`.

    lam=0 is purely empirical (drops unused names entirely), lam=1 is uniform
    (the old behaviour). Returns weights summing to 1.0.
    """
    if not names:
        return []
    n = len(names)
    tot = sum(bucket.get(x, 0) for x in names)
    unif = 1.0 / n
    if tot == 0:
        return [unif] * n
    return [(1.0 - lam) * (bucket.get(x, 0) / tot) + lam * unif for x in names]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--lam", type=float, default=0.25)
    a = ap.parse_args()

    files = _behavior_files()
    c = counts()
    print(f"train-split behavior files: {len(files)} "
          f"(held-out excluded to avoid leaking the eval distribution)\n")

    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from pyi_api import parse
    api = parse(str(ROOT / "typings" / "yope3d" / "__init__.pyi"))

    for bucket in ("method", "toplevel", "component"):
        b = c[bucket]
        print(f"=== {bucket} — {len(b)} distinct, {sum(b.values())} uses ===")
        for nm, ct in b.most_common(a.top):
            print(f"  {ct:>5}  {nm}")
        print()

    # How skewed is reality, and what does smoothing do to it?
    names = sorted(api.names)
    w = weights(names, c["toplevel"], a.lam)
    pairs = sorted(zip(names, w), key=lambda t: -t[1])
    head10 = sum(x for _, x in pairs[:10])
    unused = sum(1 for nm in names if c["toplevel"].get(nm, 0) == 0)
    print(f"=== skew of yope3d.X at lam={a.lam} ===")
    print(f"  top-10 names hold {100*head10:.1f}% of sampling mass "
          f"(uniform would be {1000/len(names):.1f}%)")
    print(f"  {unused}/{len(names)} names unused in real code — still reachable "
          f"at {100*a.lam/len(names):.3f}% each")


if __name__ == "__main__":
    main()
