#!/usr/bin/env python3
"""Simulate the accept-then-complete-again loop.

Accept a suggestion, append it to the prefix, ask again — six times. This is the
path where "it just repeats the immediate prefix over and over" shows up, and it
is invisible to a single-shot eval.
"""
import json, urllib.request
from pathlib import Path

URL = "http://127.0.0.1:8012/infill"
ROOT = Path("/Users/me/Desktop/dev/Yope3D/v2/Yope3D/scripts/behaviors")

CASES = [("physics_gallery.py", 137), ("sandbox_gallery.py", 120),
         ("stress_test.py", 100), ("ui_pause_menu_demo.py", 140),
         ("ragdoll_generator.py", 150), ("vehicle_demo.py", 90)]

CHAINS = {
    "extension default": {"top_k": 40, "top_p": 0.99,
                          "samplers": ["top_k", "top_p", "infill"]},
    "+ penalties":       {"top_k": 40, "top_p": 0.99,
                          "samplers": ["penalties", "top_k", "top_p", "infill"],
                          "repeat_penalty": 1.15, "repeat_last_n": 256},
    "+ DRY":             {"top_k": 40, "top_p": 0.99,
                          "samplers": ["dry", "top_k", "top_p", "infill"],
                          "dry_multiplier": 0.8, "dry_base": 1.75,
                          "dry_allowed_length": 4, "dry_penalty_last_n": 512},
}
STEPS = 6


def call(prefix, suffix, extra):
    body = {"input_prefix": prefix, "input_suffix": suffix,
            "n_predict": 32, "seed": 1234}
    body.update(extra)
    req = urllib.request.Request(URL, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.load(r).get("content", "")


def run(fname, cut, extra):
    lines = (ROOT / fname).read_text().splitlines(keepends=True)
    prefix = "".join(lines[max(0, cut-120):cut])
    suffix = "".join(lines[cut+1:cut+11])
    accepted = []
    for _ in range(STEPS):
        c = call(prefix, suffix, extra)
        first = (c.splitlines() or [""])[0].strip()
        accepted.append(first)
        prefix += (c.splitlines(keepends=True) or ["\n"])[0]
    uniq = len({a for a in accepted if a})
    # longest run of identical consecutive suggestions
    run_len = best = 1
    for i in range(1, len(accepted)):
        run_len = run_len + 1 if accepted[i] == accepted[i-1] and accepted[i] else 1
        best = max(best, run_len)
    return accepted, uniq, best


print(f"{'chain':22} {'uniq/6':>8} {'max repeat run':>15}")
print("-" * 48)
detail = {}
for label, extra in CHAINS.items():
    us, rs = [], []
    for fname, cut in CASES:
        acc, u, r = run(fname, cut, extra)
        us.append(u); rs.append(r)
        detail.setdefault(label, []).append((fname, acc))
    print(f"{label:22} {sum(us)/len(us):8.1f} {sum(rs)/len(rs):15.1f}")

print("\n=== the loop, verbatim (physics_gallery.py) ===")
for label in CHAINS:
    fname, acc = detail[label][0]
    print(f"\n--- {label}")
    for i, a in enumerate(acc):
        print(f"   {i+1}. {a[:78]!r}")
