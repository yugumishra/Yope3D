#!/usr/bin/env python3
"""Cut .py sources into FIM training examples (JSONL).

Real behavior files and synthetic files go through this one path, so the
held-out logic, the cut-point selection, and the format are identical for both.

OUTPUT
    train.jsonl / valid.jsonl, one {"text": "<full FIM example>"} per line —
    completions style, NOT chat. Qwen2.5-Coder-1.5B is a BASE model; wrapping
    examples in a chat template would train a format it does not use at
    inference.

THE SPLIT IS BY FILE, NEVER BY LINE
    A random-line split leaks catastrophically: train on line 100, evaluate on
    line 101 whose 120-line prefix *contains line 100*. The reported number
    then measures memorisation and looks fantastic. Splitting whole files is
    the only honest option here.

TYPED-PREFIX DISTRIBUTION
    Cut points are sampled at several typed fractions, not just at line starts.
    Accuracy is strongly conditioned on how much of the line is already typed
    (20.0% exact at nothing-typed vs 56.8% at three-quarters, PLAN.txt 5.1), so
    training only at fraction 0 would fit the rarest and hardest case. The
    weights below lean toward the early fractions because that is where the
    characters-saved payoff is largest.
"""

from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

from fim_format import Chunk, make_example

ROOT = Path(__file__).resolve().parents[2]

# (typed fraction, sampling weight)
FRACTIONS: list[tuple[float, int]] = [(0.0, 30), (0.25, 25), (0.5, 25), (0.75, 20)]

# Held-out files, spanning the measured difficulty tiers so validation loss is
# not dominated by one regime (PLAN.txt 9.4).
DEFAULT_HELDOUT = [
    "attach_script_demo.py",   # top tier    62.5% exact
    "vehicle_demo.py",         # mid tier    37.5%
    "physics_gallery.py",      # bottom tier 0%
    "sandbox_gallery.py",      # bottom tier 0%
]


def cuttable(lines: list[str], i: int) -> bool:
    """Is line `i` a sensible place to invoke completion?

    Indented, non-trivial, not a comment — i.e. a statement inside a function,
    which is where ghost text actually fires. Same predicate as fim_eval3.py so
    training and evaluation agree on what a cut point is.
    """
    ln = lines[i]
    s = ln.strip()
    return bool(s) and not s.startswith("#") and ln[:1].isspace() and len(s) > 8


def pick_fraction(rng: random.Random) -> float:
    fr, w = zip(*FRACTIONS)
    return rng.choices(fr, weights=w)[0]


def examples_for(path: Path, rel: str, rng: random.Random, cuts: int,
                 n_prefix: int, n_suffix: int,
                 neighbours: list[tuple[str, str]], repo_prob: float) -> list[dict]:
    lines = path.read_text().splitlines(keepends=True)
    idxs = [i for i in range(len(lines))
            if cuttable(lines, i) and 4 < i < len(lines) - 2]
    if not idxs:
        return []

    out = []
    for i in (idxs if len(idxs) <= cuts else rng.sample(idxs, cuts)):
        f = pick_fraction(rng)
        target = lines[i].rstrip("\n")
        col = int(len(target) * f)
        # A cut that leaves nothing to predict is a wasted example.
        if not target[col:].strip():
            continue

        extra: list[Chunk] = []
        use_repo = neighbours and rng.random() < repo_prob
        if use_repo:
            p, txt = rng.choice(neighbours)
            extra = [Chunk(p, txt)]

        ex = make_example(lines, i, n_prefix=n_prefix, n_suffix=n_suffix,
                          path=rel if use_repo else "", extra=extra,
                          col=col or None)
        if not ex.middle.strip():
            continue
        out.append({"text": ex.text()})
    return out


def collect(dirs: list[Path]) -> list[Path]:
    files: list[Path] = []
    for d in dirs:
        if d.is_file() and d.suffix == ".py":
            files.append(d)
        elif d.is_dir():
            files += [p for p in sorted(d.rglob("*.py"))
                      if not p.name.startswith("_")]
    return files


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("sources", nargs="*",
                    default=["scripts/behaviors", "fim-finetuning/corpus/synth"],
                    help="files or directories of .py to cut")
    ap.add_argument("--out", default="fim-finetuning/data")
    ap.add_argument("--cuts", type=int, default=12, help="cut points per file")
    ap.add_argument("--n-prefix", type=int, default=120)
    ap.add_argument("--n-suffix", type=int, default=10)
    ap.add_argument("--repo-prob", type=float, default=0.3,
                    help="fraction of examples that carry repo-level context")
    ap.add_argument("--valid-cuts", type=int, default=40,
                    help="cut points per held-out file (denser: only 4 files)")
    ap.add_argument("--max-chars", type=int, default=10240,
                    help="drop examples longer than this (~2900 tokens)")
    ap.add_argument("--heldout", default=",".join(DEFAULT_HELDOUT))
    ap.add_argument("--seed", type=int, default=20260728)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    held = {h.strip() for h in a.heldout.split(",") if h.strip()}
    files = collect([Path(s) for s in a.sources])
    if not files:
        raise SystemExit("no .py sources found — run synth_pyi.py first?")

    # Neighbour pool for repo-level examples: short files only, so one chunk
    # does not blow past the 2048-token training sequence length.
    pool = [(str(p.relative_to(ROOT)) if p.is_absolute() else str(p),
             p.read_text()) for p in files]
    pool = [(p, t) for p, t in pool if len(t) < 4000]

    train, valid = [], []
    n_train_files = n_valid_files = 0
    for p in files:
        rel = str(p.relative_to(ROOT)) if p.is_absolute() else str(p)
        is_held = p.name in held
        others = [(q, t) for q, t in pool if q != rel]
        ex = examples_for(p, rel, rng, a.valid_cuts if is_held else a.cuts,
                          a.n_prefix, a.n_suffix, others, a.repo_prob)
        ex = [r for r in ex if len(r["text"]) <= a.max_chars]
        if is_held:
            valid += ex
            n_valid_files += 1
        else:
            train += ex
            n_train_files += 1

    rng.shuffle(train)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, rows in (("train", train), ("valid", valid)):
        with (out / f"{name}.jsonl").open("w") as fh:
            for r in rows:
                fh.write(json.dumps(r) + "\n")

    chars = sum(len(r["text"]) for r in train)
    src_chars = sum(len(p.read_text()) for p in files)
    est_tok = chars / 3.5           # ~3.5 chars/token measured on this corpus
    redundancy = chars / max(src_chars, 1)
    lens = sorted(len(r["text"]) for r in train)
    p95 = lens[int(len(lens) * 0.95)] if lens else 0

    print(f"sources     {len(files)} files "
          f"({n_train_files} train, {n_valid_files} held out)")
    print(f"train       {len(train)} examples  (~{est_tok/1e6:.2f}M tokens est.)")
    print(f"valid       {len(valid)} examples")
    print(f"held out    {', '.join(sorted(held)) or '(none)'}")
    print(f"len p95     {p95} chars (~{p95/3.5:.0f} tokens)")
    print(f"-> {out}/train.jsonl, {out}/valid.jsonl")

    # Every cut point re-embeds up to n_prefix lines of its file, so the same
    # source line is seen many times within a SINGLE epoch. This is inherent to
    # FIM training, but it means "epochs" does not mean what it usually means.
    print(f"\nREDUNDANCY  {redundancy:.1f}x — each source line appears in "
          f"~{redundancy:.0f} examples")
    print(f"            so ONE epoch already makes ~{redundancy:.0f} passes over "
          f"the unique text.")
    print(f"            Budget 1-2 epochs, NOT the 2-4 in PLAN.txt 9.6: at "
          f"{redundancy:.0f}x, 3 epochs is ~{3*redundancy:.0f} passes and "
          f"straight memorisation.")
    print(f"SEQ LEN     use 3072, not 2048. Trainers truncate from the END, "
          f"which is where MIDDLE lives —")
    print(f"            a truncated example has no target at all and teaches "
          f"nothing.")

    if not valid:
        print("\nWARNING: held-out set is empty — none of the named files were "
              "found in the sources. Every number you measure will be on data "
              "the model trained on.")
    print("\nREMINDER: verify_format.py must PASS before you train on this.")


if __name__ == "__main__":
    main()
