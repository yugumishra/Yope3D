#!/usr/bin/env python3
"""Run the API-usage probe set — tier-2 metric (PLAN.txt section 10).

Each probe cuts the prefix at `yope3d.` and asks the model to name the binding.
Three numbers come out, and they answer different questions:

    correct    emitted the RIGHT name           <- did it learn the API
    invented   emitted a name that DOESN'T EXIST <- the failure the LoRA targets
    wrong      real name, but not this one       <- confusion, not invention

Separating `invented` from `wrong` is the point. Pooling them as "not correct"
hides the distinction that matters: a model that says `add_sphere` where
`add_obb` belongs is usable and caught by review; a model that says
`yope3d.spawn_entity` is confidently wrong about a binding that does not exist,
which is what the proxy has to truncate and what a fine-tune should eliminate.

USAGE
    # baseline, before any fine-tuning
    python3 fim-finetuning/harness/probe_eval.py --port 8012 --label base \\
        --out fim-finetuning/data/probe_base.json

    # after
    python3 fim-finetuning/harness/probe_eval.py --port 8013 --label lora \\
        --out fim-finetuning/data/probe_lora.json

    # paired significance test
    python3 fim-finetuning/harness/probe_eval.py --compare \\
        fim-finetuning/data/probe_base.json fim-finetuning/data/probe_lora.json

READ THE STRATA SEPARATELY. `synth` has the n but shares a generator with the
training data; `real` and `stub` are the honest generalisation signal. A gain
confined to `synth` means the model memorised the generator, not the API.

WHAT THIS METRIC IS STRUCTURALLY BLIND TO
    It scores NAMES, so it cannot see a real name that must not be called here.
    `world.reset_physics()` is banned from behavior scripts (CLAUDE.md) but is
    a genuine binding, so this harness would score it `correct`. There is no
    `forbidden` verdict and adding one would not help: probes cut at `yope3d.`
    and the banned call is spelled `world.`, so it can never BE a probe target
    (verified: 0 of 296).

    The consequence is that invariant compliance is unmeasured here by
    construction, and that is a deliberate scope decision, not an oversight —
    it is why the corpus enforces the ban at generation time instead
    (synth_pyi.FORBIDDEN + validate() gate 4). Do not read a tier-2 gain as
    evidence the model respects the invariants. Nothing in this project
    measures that.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import statistics
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "corpus"))
from pyi_api import parse  # noqa: E402

IDENT = re.compile(r"^([A-Za-z_]\w*)")


def call(url: str, prefix: str, suffix: str, n_predict: int) -> dict:
    body = {"input_prefix": prefix, "input_suffix": suffix,
            "n_predict": n_predict, "seed": 1234, "temperature": 0.0,
            "top_k": 40, "top_p": 0.99,
            "samplers": ["top_k", "top_p", "infill"]}
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=180) as r:
        d = json.load(r)
    d["_wall"] = (time.perf_counter() - t0) * 1000
    return d


def classify(got: str, target: str, names: set[str]) -> str:
    m = IDENT.match(got)
    if not m:
        return "none"
    name = m.group(1)
    if name == target:
        return "correct"
    return "wrong" if name in names else "invented"


def run(a) -> dict:
    spec = json.loads(Path(a.probes).read_text())
    probes = spec["probes"]
    if a.limit:
        probes = probes[:a.limit]
    url = f"http://127.0.0.1:{a.port}/infill"
    names = parse().names

    print(f"[{a.label}] {len(probes)} probes, port {a.port}", flush=True)
    rows, walls = [], []
    for i, p in enumerate(probes, 1):
        try:
            d = call(url, p["prefix"], p["suffix"], a.n_predict)
        except Exception as e:
            print(f"  FAIL {p['origin']}:{p['line']}: {e}", flush=True)
            continue
        out = d.get("content", "")
        verdict = classify(out, p["target"], names)
        m = IDENT.match(out)
        rows.append({"id": p["id"], "origin": p["origin"], "line": p["line"],
                     "stratum": p["stratum"], "tier": p["tier"],
                     "target": p["target"], "got": m.group(1) if m else "",
                     "verdict": verdict})
        walls.append(d["_wall"])
        if i % 50 == 0:
            print(f"  ...{i}/{len(probes)}", flush=True)

    report(a.label, rows, walls)
    res = {"label": a.label, "port": a.port, "rows": rows,
           "p50_ms": statistics.median(walls) if walls else 0}
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1))
        print(f"\n-> {a.out}")
    return res


def _pct(n: int, d: int) -> str:
    return f"{100*n/d:>5.1f}%" if d else "    -"


def report(label: str, rows: list[dict], walls: list[float]) -> None:
    def block(title: str, sel: list[dict]) -> None:
        if not sel:
            return
        c = collections.Counter(r["verdict"] for r in sel)
        n = len(sel)
        print(f"{title:<18}{n:>5} {_pct(c['correct'], n)} {_pct(c['invented'], n)}"
              f" {_pct(c['wrong'], n)} {_pct(c['none'], n)}")

    print(f"\n{'':18}{'n':>5} {'correct':>6} {'invent':>6} {'wrong':>6} {'none':>6}")
    print("-" * 52)
    block("ALL", rows)
    print()
    for s in ("real", "stub", "synth"):
        block(f"  {s}", [r for r in rows if r["stratum"] == s])
    print()
    for t in ("head", "tail"):
        block(f"  {t}", [r for r in rows if r["tier"] == t])

    if walls:
        print(f"\np50 latency {statistics.median(walls):.0f} ms")

    inv = [r for r in rows if r["verdict"] == "invented"]
    if inv:
        top = collections.Counter(r["got"] for r in inv).most_common(10)
        print(f"\nmost-invented names ({len(inv)} events): {top}")


def compare(fa: str, fb: str) -> None:
    """Paired McNemar on correct/not-correct, per stratum."""
    # Keyed on the probe id, NOT (origin, line) — see probe_set.py.
    A = {r["id"]: r for r in json.loads(Path(fa).read_text())["rows"]}
    B = {r["id"]: r for r in json.loads(Path(fb).read_text())["rows"]}
    keys = sorted(set(A) & set(B))
    la = json.loads(Path(fa).read_text())["label"]
    lb = json.loads(Path(fb).read_text())["label"]
    print(f"paired probes: {len(keys)}  ({la} vs {lb})\n")

    def mcnemar(sel) -> None:
        b = sum(1 for k in sel if A[k]["verdict"] == "correct"
                and B[k]["verdict"] != "correct")
        c = sum(1 for k in sel if A[k]["verdict"] != "correct"
                and B[k]["verdict"] == "correct")
        na = sum(1 for k in sel if A[k]["verdict"] == "correct")
        nb = sum(1 for k in sel if B[k]["verdict"] == "correct")
        ia = sum(1 for k in sel if A[k]["verdict"] == "invented")
        ib = sum(1 for k in sel if B[k]["verdict"] == "invented")
        n = len(sel)
        if not n:
            return
        # Exact binomial two-sided p on the discordant pairs.
        p = 1.0
        if b + c:
            from math import comb
            tot, k = b + c, min(b, c)
            p = min(1.0, 2 * sum(comb(tot, i) for i in range(k + 1)) / 2 ** tot)
        print(f"  n={n:<5} correct {na:>3} -> {nb:<3} "
              f"({100*(nb-na)/n:+5.1f} pp)   invented {ia:>3} -> {ib:<3} "
              f"({100*(ib-ia)/n:+5.1f} pp)   discordant {b}/{c}  p={p:.3f}")

    print("ALL"); mcnemar(keys)
    for s in ("real", "stub", "synth"):
        sel = [k for k in keys if A[k]["stratum"] == s]
        if sel:
            print(s); mcnemar(sel)
    for t in ("head", "tail"):
        sel = [k for k in keys if A[k]["tier"] == t]
        if sel:
            print(t); mcnemar(sel)
    print("\nA gain confined to `synth` is generator memorisation, not API "
          "learning. `real` and `stub` are the ones to believe.")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--probes", default="fim-finetuning/data/probes.json")
    ap.add_argument("--port", type=int, default=8012)
    ap.add_argument("--label", default="run")
    ap.add_argument("--n-predict", type=int, default=12,
                    help="only need the identifier; short is fine")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--out", default=None)
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"))
    a = ap.parse_args()

    if a.compare:
        compare(*a.compare)
        return
    run(a)


if __name__ == "__main__":
    main()
