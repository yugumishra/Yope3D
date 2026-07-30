#!/usr/bin/env python3
"""Assemble the non-Yope3D control corpus — tier 3 (PLAN.txt section 10).

WHAT THIS IS FOR
    The catastrophic-forgetting detector. A LoRA trained on ~40K lines of one
    narrow API can trade away general Python ability, and every other metric in
    this project would happily report that as success: tier 1 (chars saved on
    Yope3D files) and tier 2 (API naming) BOTH go up when the model overfits to
    Yope3D. Nothing else here can see the cost.

    A tier-1/tier-2 win with an unmeasured tier-3 regression is not a win — you
    would have traded a general coding assistant for a Yope3D autocomplete that
    is worse at everything else you write.

    Run this BEFORE training to capture a baseline. A control measured only
    after the fact tells you nothing.

TWO STRATA, because they fail differently
    stdlib  Python standard library modules from the local interpreter.
            Real, diverse, human-written, and definitely not in the LoRA's
            training data.
            CAVEAT: it is almost certainly in Qwen2.5-Coder's PRETRAINING data,
            probably verbatim. That cuts both ways. As a forgetting detector it
            is exactly right — retained pretraining knowledge is the thing we
            are guarding. As a proxy for "novel code the user writes" it is
            weak, because recalling memorised text may survive damage that
            genuine generalisation would not.

    local   Non-engine Python from this repo (tools/articleAnalysis, logo_pack).
            Recent, project-specific, and plausibly NOT memorised — so it is
            the more sensitive detector of the two. Only ~760 lines, so treat
            it as directional. Files that import yope3d are excluded: they are
            in-domain and would mask the very effect being measured.

    Expect stdlib to be stable and local to move first. If stdlib drops, the
    damage is gross.

REPRODUCIBILITY
    Stdlib contents vary by interpreter version, so the manifest records the
    Python version and a SHA-256 of every selected file. Re-running on a
    different machine warns loudly rather than silently comparing against a
    different corpus — which would look like a model regression.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import random
import shutil
import sys
import sysconfig
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Repo-local non-engine Python. Anything importing yope3d is in-domain and is
# rejected below even if listed here.
LOCAL_CANDIDATES = [
    "tools/articleAnalysis/article2_diagrams_excalidraw.py",
    "tools/articleAnalysis/plot_ecs_bench.py",
    "tools/articleAnalysis/article2_diagrams.py",
    "tools/logo_pack.py",
]

MIN_LINES, MAX_LINES = 150, 900


def usable(text: str) -> bool:
    """Pure, parseable Python with real function bodies to complete inside."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return False
    return any(isinstance(n, ast.FunctionDef) for n in ast.walk(tree))


def pick_stdlib(rng: random.Random, budget: int) -> list[Path]:
    lib = Path(sysconfig.get_paths()["stdlib"])
    cands = []
    for f in sorted(lib.glob("*.py")):
        # Skip private modules and the test tree: dunder-heavy and
        # fixture-heavy code is not representative of ordinary Python.
        if f.name.startswith(("_", "test")):
            continue
        try:
            t = f.read_text(errors="ignore")
        except OSError:
            continue
        n = len(t.splitlines())
        if MIN_LINES <= n <= MAX_LINES and usable(t):
            cands.append((f, n))
    rng.shuffle(cands)
    out, total = [], 0
    for f, n in cands:
        if total >= budget:
            break
        out.append(f)
        total += n
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="fim-finetuning/corpus/control")
    ap.add_argument("--budget", type=int, default=9000,
                    help="approx stdlib lines to include")
    ap.add_argument("--seed", type=int, default=31337)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    out = ROOT / a.out
    for sub in ("stdlib", "local"):
        d = out / sub
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)

    manifest = {"python": sys.version.split()[0],
                "stdlib_path": sysconfig.get_paths()["stdlib"],
                "seed": a.seed, "files": []}

    n_lines = 0
    for f in pick_stdlib(rng, a.budget):
        t = f.read_text(errors="ignore")
        (out / "stdlib" / f.name).write_text(t)
        manifest["files"].append({
            "stratum": "stdlib", "name": f.name, "src": str(f),
            "lines": len(t.splitlines()),
            "sha256": hashlib.sha256(t.encode()).hexdigest()[:16]})
        n_lines += len(t.splitlines())

    n_local = 0
    for rel in LOCAL_CANDIDATES:
        p = ROOT / rel
        if not p.exists():
            print(f"  WARN missing {rel}", file=sys.stderr)
            continue
        t = p.read_text(errors="ignore")
        if "yope3d" in t:
            # In-domain. Including it would mask the effect being measured.
            print(f"  SKIP {rel} — imports yope3d, not a control")
            continue
        if not usable(t):
            print(f"  SKIP {rel} — no function bodies to complete inside")
            continue
        (out / "local" / p.name).write_text(t)
        manifest["files"].append({
            "stratum": "local", "name": p.name, "src": rel,
            "lines": len(t.splitlines()),
            "sha256": hashlib.sha256(t.encode()).hexdigest()[:16]})
        n_local += len(t.splitlines())

    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))

    n_std = sum(1 for f in manifest["files"] if f["stratum"] == "stdlib")
    n_loc = sum(1 for f in manifest["files"] if f["stratum"] == "local")
    print(f"stdlib   {n_std:>3} files  {n_lines:>6} lines")
    print(f"local    {n_loc:>3} files  {n_local:>6} lines")
    print(f"python   {manifest['python']}")
    print(f"-> {out}")
    print("\nBaseline BEFORE training, or the control is worthless:")
    print("  python3 fim-finetuning/harness/fim_eval3.py --port 8012 "
          f"--label ctl-base --dir {a.out}/stdlib --out "
          "fim-finetuning/data/ctl_stdlib_base.json")
    print("  python3 fim-finetuning/harness/fim_eval3.py --port 8012 "
          f"--label ctl-base --dir {a.out}/local --out "
          "fim-finetuning/data/ctl_local_base.json")


if __name__ == "__main__":
    main()
