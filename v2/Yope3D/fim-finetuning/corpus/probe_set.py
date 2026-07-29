#!/usr/bin/env python3
"""Build the API-usage probe set — the tier-2 metric (PLAN.txt section 10).

THE PROBLEM THIS SOLVES
    The invention rate is the metric that SHOULD move most under fine-tuning,
    and right now it cannot be seen. The tier-1 harness produced ~41 API calls
    and ~3 inventions across 152 cases. You cannot detect a change in a
    3-event signal; a fine-tune could halve the invention rate and the number
    would look identical.

    This concentrates the signal. Every probe is a cut point chosen BECAUSE
    the ground truth is a `yope3d.` call, so ~100% of probes produce a
    scoreable API event instead of ~27%.

THE PROBE SHAPE
    The prefix ends exactly at `yope3d.`, mid-token:

        prefix = "...    hull = yope3d."
        target = "reg_get"

    That is the sharpest available probe. There is exactly one right answer,
    the model cannot dodge by emitting something else, and the score is not
    diluted by argument text or surrounding formatting. It also happens to be
    a real invocation point — this is precisely where a developer pauses.

THREE STRATA, REPORTED SEPARATELY
    They differ in how much you should believe them, so averaging them into
    one number would hide the thing that matters.

    real   held-out behavior files. Authentic contexts, zero leakage, but only
           ~60 probes and the name distribution is head-heavy (Vec3 alone is
           37% of all calls in the behavior corpus).
    stub   usage lines lifted from the .pyi docstrings, minus any line that
           also appears in a training file. Human-written, and the stub is not
           a training source.
    synth  probe-only generated files: different seed, different structural
           parameters, separate directory that make_dataset never reads.
           This is where the ~138 API names the behavior corpus never touches
           get covered at all.

    CAVEAT ON `synth`, stated plainly: it shares a GENERATOR with the training
    data even though it shares no files. A win there means "the model learned
    the API surface as this generator presents it" — weaker than "the model
    generalises to real code". `real` and `stub` are the honest generalisation
    signal; `synth` is the high-n precision signal. Read them together.

HEAD VS TAIL
    Probes are also tagged by how often the name appears in the behavior
    corpus. `tail` names (rare or never used) are what the LoRA is supposed to
    fix and where movement is expected; `head` names the base model may already
    know from context. Reporting only the pooled number would let a big tail
    gain hide behind an unchanged head.
"""

from __future__ import annotations

import argparse
import ast
import collections
import json
import random
import re
import sys
from pathlib import Path

from pyi_api import parse
from splits import HELDOUT_BEHAVIORS, PROBE_SYNTH_DIR

ROOT = Path(__file__).resolve().parents[2]
BEHAVIORS = ROOT / "scripts" / "behaviors"
STUB = ROOT / "typings" / "yope3d" / "__init__.pyi"

CALL = re.compile(r"yope3d\.([A-Za-z_]\w*)")
# `yope3d.` NOT preceded by an identifier char, so we never cut inside a word.
ANCHOR = re.compile(r"(?<![A-Za-z0-9_.])yope3d\.([A-Za-z_]\w*)")

HEAD_CUTOFF = 8   # >= this many uses in behaviors == "head"


def behavior_freq() -> collections.Counter:
    """How often each name is used across ALL behavior files."""
    c: collections.Counter = collections.Counter()
    for p in sorted(BEHAVIORS.glob("*.py")):
        if p.name.startswith("_"):
            continue
        c.update(CALL.findall(p.read_text()))
    return c


def probes_from_text(text: str, origin: str, stratum: str,
                     n_prefix: int, n_suffix: int) -> list[dict]:
    """One probe per `yope3d.<name>` occurrence in `text`."""
    lines = text.splitlines(keepends=True)
    out: list[dict] = []
    for i, line in enumerate(lines):
        for m in ANCHOR.finditer(line):
            name = m.group(1)
            # Cut immediately after the dot; the model must produce `name`.
            col = m.end() - len(name)
            prefix = "".join(lines[max(0, i - n_prefix):i]) + line[:col]
            suffix = "".join(lines[i + 1:i + 1 + n_suffix])
            # A probe with no left context is not a realistic invocation.
            if i < 2:
                continue
            out.append({
                # Unique, stable key. (origin, line) is NOT unique — a line can
                # hold two calls, e.g.
                #   yope3d.camera.set_rotation(yope3d.Vec3(p, y, 0))
                # 19 of 296 probes collided that way, and a paired comparison
                # keyed on (origin, line) silently drops one side of each and
                # can mispair the rest. Include the column.
                "id": f"{origin}:{i + 1}:{m.start()}:{name}",
                "stratum": stratum,
                "origin": origin,
                "line": i + 1,
                "col": m.start(),
                "prefix": prefix,
                "suffix": suffix,
                "target": name,
                # Rest of the line after the name — used for the secondary
                # "did it get the whole call right" score.
                "rest": line[m.end():].rstrip("\n"),
            })
    return out


def stub_probes(n_prefix: int, n_suffix: int, training_lines: set[str]) -> list[dict]:
    """Usage lines from the stub's docstrings, deduped against training text.

    The stub's own docs say its examples are "copy-paste recipes pulled from
    scripts/", so a naive lift would re-probe lines the model trained on. Any
    line whose stripped form appears in a training file is dropped.
    """
    text = STUB.read_text()
    kept: list[str] = []
    for line in text.splitlines():
        s = line.strip()
        if "yope3d." not in s or s.startswith(("#", '"', "*", "-")):
            continue
        if s in training_lines:
            continue
        kept.append(line)
    if not kept:
        return []
    # Rebuild a synthetic file so probes get plausible surrounding context.
    body = "import yope3d\n\n\ndef usage(world, entity, dt):\n" + "".join(
        ("    " + l.strip() + "\n") for l in kept)
    try:
        ast.parse(body)
    except SyntaxError:
        # Docstring snippets are fragments; keep only lines that stand alone.
        good = []
        for l in kept:
            frag = "    " + l.strip() + "\n"
            try:
                ast.parse("def f(world, entity, dt, inp, self, move):\n" + frag)
                good.append(l)
            except SyntaxError:
                pass
        body = ("import yope3d\n\n\n"
                "def usage(world, entity, dt, inp, self, move):\n"
                + "".join("    " + l.strip() + "\n" for l in good))
    return probes_from_text(body, "typings/yope3d/__init__.pyi", "stub",
                            n_prefix, n_suffix)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="fim-finetuning/data/probes.json")
    ap.add_argument("--n-prefix", type=int, default=120)
    ap.add_argument("--n-suffix", type=int, default=10)
    ap.add_argument("--synth-cap", type=int, default=200,
                    help="max probes drawn from the probe-only synth batch")
    ap.add_argument("--per-name", type=int, default=4,
                    help="max synth probes per API name (spreads coverage)")
    ap.add_argument("--seed", type=int, default=99)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    api = parse()
    freq = behavior_freq()

    # Lines the model trains on — used to keep stub probes clean.
    training_lines: set[str] = set()
    for p in sorted(BEHAVIORS.glob("*.py")):
        if p.name.startswith("_") or p.name in HELDOUT_BEHAVIORS:
            continue
        training_lines.update(l.strip() for l in p.read_text().splitlines())

    probes: list[dict] = []

    # --- stratum: real -----------------------------------------------------
    for name in HELDOUT_BEHAVIORS:
        p = BEHAVIORS / name
        if not p.exists():
            print(f"  WARN held-out file missing: {name}", file=sys.stderr)
            continue
        probes += probes_from_text(p.read_text(), f"scripts/behaviors/{name}",
                                   "real", a.n_prefix, a.n_suffix)

    # --- stratum: stub -----------------------------------------------------
    probes += stub_probes(a.n_prefix, a.n_suffix, training_lines)

    # --- stratum: synth ----------------------------------------------------
    synth_dir = ROOT / PROBE_SYNTH_DIR
    if synth_dir.is_dir():
        pool: list[dict] = []
        for p in sorted(synth_dir.glob("*.py")):
            pool += probes_from_text(p.read_text(),
                                     f"{PROBE_SYNTH_DIR}/{p.name}", "synth",
                                     a.n_prefix, a.n_suffix)
        # Bias toward tail names — they are what the LoRA must fix, and
        # uniform sampling would drown them in Vec3/reg_get. But cap each
        # name: without a cap, tail-first sorting front-loads every
        # zero-frequency name and one of them (play_sound) took 51 of 200
        # slots, which measures one binding rather than the API surface.
        rng.shuffle(pool)
        pool.sort(key=lambda d: freq.get(d["target"], 0))
        seen: collections.Counter = collections.Counter()
        picked = []
        for d in pool:
            if seen[d["target"]] >= a.per_name:
                continue
            seen[d["target"]] += 1
            picked.append(d)
            if len(picked) >= a.synth_cap:
                break
        probes += picked
    else:
        print(f"  WARN {PROBE_SYNTH_DIR} missing — run:\n"
              f"    python3 fim-finetuning/corpus/synth_pyi.py --files 120 "
              f"--seed 777 --body-min 4 --body-max 8 --out {PROBE_SYNTH_DIR}",
              file=sys.stderr)

    # Tag head/tail and drop anything whose target is not a real binding
    # (a probe whose ground truth is invented would be unscoreable).
    clean = []
    for d in probes:
        if d["target"] not in api.names:
            continue
        d["freq"] = freq.get(d["target"], 0)
        d["tier"] = "head" if d["freq"] >= HEAD_CUTOFF else "tail"
        clean.append(d)
    probes = clean
    rng.shuffle(probes)

    out = ROOT / a.out if not Path(a.out).is_absolute() else Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({
        "n_prefix": a.n_prefix, "n_suffix": a.n_suffix,
        "heldout": HELDOUT_BEHAVIORS, "probes": probes}, indent=1))

    by_s = collections.Counter(d["stratum"] for d in probes)
    by_t = collections.Counter(d["tier"] for d in probes)
    names = collections.Counter(d["target"] for d in probes)
    print(f"probes      {len(probes)}")
    for s in ("real", "stub", "synth"):
        n = by_s.get(s, 0)
        tail = sum(1 for d in probes if d["stratum"] == s and d["tier"] == "tail")
        print(f"  {s:<9} {n:>4}   ({tail} tail)")
    print(f"tiers       head {by_t.get('head', 0)}  tail {by_t.get('tail', 0)}")
    print(f"distinct targets {len(names)} / {len(api.names)} API names")
    print(f"most common {names.most_common(5)}")
    print(f"-> {out}")
    if by_s.get("real", 0) < 30:
        print("\nWARNING: the `real` stratum is small. It is the only stratum "
              "that is both authentic and leak-free — treat its number as "
              "directional, not precise.")


if __name__ == "__main__":
    main()
