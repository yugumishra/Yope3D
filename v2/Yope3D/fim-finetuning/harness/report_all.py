#!/usr/bin/env python3
"""Report all three metric tiers side by side, base vs candidate.

Exists because the tiers are only meaningful together. Tier 1 (chars saved on
Yope3D) and tier 2 (API naming) both RISE when a LoRA overfits to Yope3D, and
tier 3 is the only thing that can see what that cost. Reading them in separate
terminal scrollbacks is how a regression gets missed, so this prints one table
and states a verdict.

USAGE
    # after capturing base + lora runs for each tier
    python3 fim-finetuning/harness/report_all.py \\
        --base-dir  fim-finetuning/data --base-tag  base \\
        --cand-dir  fim-finetuning/data --cand-tag  lora

It reads whatever files exist and says plainly which tiers are missing rather
than silently reporting a partial picture.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

# tier -> (label, filename stem, kind)
FILES = [
    ("1  yope3d held-out", "eval_heldout", "eval"),
    ("2  API probes", "probe", "probe"),
    ("3  control stdlib", "ctl_stdlib", "eval"),
    ("3  control local", "ctl_local", "eval"),
]


def load(d: Path, stem: str, tag: str) -> dict | None:
    p = d / f"{stem}_{tag}.json"
    if not p.exists():
        return None
    return json.loads(p.read_text())


def summarize(kind: str, d: dict) -> tuple[float, str]:
    """(primary number, one-line description)."""
    if kind == "eval":
        return d.get("mean_saved", 0.0), "mean chars saved"
    rows = d["rows"]
    n = len(rows)
    correct = sum(1 for r in rows if r["verdict"] == "correct")
    return (100 * correct / n if n else 0.0), "% correct"


def probe_invented(d: dict) -> float:
    rows = d["rows"]
    n = len(rows)
    return 100 * sum(1 for r in rows if r["verdict"] == "invented") / n if n else 0.0


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base-dir", default="fim-finetuning/data")
    ap.add_argument("--cand-dir", default="fim-finetuning/data")
    ap.add_argument("--base-tag", default="base")
    ap.add_argument("--cand-tag", default=None,
                    help="omit to just print the baseline")
    a = ap.parse_args()

    bd, cd = Path(a.base_dir), Path(a.cand_dir)
    print(f"{'tier':<22}{'metric':<18}{'base':>9}{'cand':>9}{'delta':>9}")
    print("-" * 67)

    missing, regressions = [], []
    for label, stem, kind in FILES:
        b = load(bd, stem, a.base_tag)
        if b is None:
            missing.append(f"{label}  (no {stem}_{a.base_tag}.json)")
            continue
        bv, desc = summarize(kind, b)
        if a.cand_tag:
            c = load(cd, stem, a.cand_tag)
            if c is None:
                missing.append(f"{label}  (no {stem}_{a.cand_tag}.json)")
                print(f"{label:<22}{desc:<18}{bv:>9.1f}{'--':>9}{'--':>9}")
                continue
            cv, _ = summarize(kind, c)
            delta = cv - bv
            print(f"{label:<22}{desc:<18}{bv:>9.1f}{cv:>9.1f}{delta:>+9.1f}")
            # Deliberately sensitive: flag early and let the reader judge.
            # SE on chars-saved is roughly +-1 at these sample sizes, so a
            # delta inside +-1 is NOT distinguishable from noise — it is a
            # prompt to re-run, not a verdict.
            if label.startswith("3") and delta < -0.5:
                regressions.append((label, delta))
        else:
            print(f"{label:<22}{desc:<18}{bv:>9.1f}{'--':>9}{'--':>9}")

        if kind == "probe":
            ib = probe_invented(b)
            if a.cand_tag and (c := load(cd, stem, a.cand_tag)):
                ic = probe_invented(c)
                print(f"{'':22}{'% invented':<18}{ib:>9.1f}{ic:>9.1f}{ic-ib:>+9.1f}")
            else:
                print(f"{'':22}{'% invented':<18}{ib:>9.1f}{'--':>9}{'--':>9}")

    if missing:
        print("\nMISSING — this is a partial picture, not a result:")
        for m in missing:
            print("  " + m)

    if a.cand_tag:
        print()
        if regressions:
            print("VERDICT: NOT A WIN. Tier 3 regressed:")
            for label, d in regressions:
                print(f"  {label}: {d:+.1f} chars saved")
            print("General Python ability was traded away. A tier-1/tier-2 gain "
                  "does not offset this\nunless you genuinely only ever edit "
                  "Yope3D scripts with this model.")
            if all(abs(d) < 1.0 for _, d in regressions):
                print("\nNOTE: every flagged delta is under 1.0, which is about "
                      "the standard error here.\nRe-run before acting on it — "
                      "this is a prompt to look, not a confirmed regression.")
        elif not missing:
            print("VERDICT: tier 3 held. Tier 1/2 gains are real gains.")
        else:
            print("VERDICT: withheld — tiers are missing above.")


if __name__ == "__main__":
    main()
