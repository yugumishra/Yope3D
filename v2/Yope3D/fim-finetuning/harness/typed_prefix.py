#!/usr/bin/env python3
"""Accept rate vs. how much of the line the user has already typed.

The earlier harness cut at line starts with nothing typed — the hardest possible
case, and one real ghost text almost never fires in. This reveals the first K% of
the target line to the model and asks it to finish the rest, which is what actually
happens when you pause mid-identifier.

Also reports keystroke savings, which exact-match throws away entirely.
"""
import json, random, statistics, sys, urllib.request

sys.path.insert(0, "/private/tmp/claude-501/-Users-me-Desktop-dev-Yope3D-v2-Yope3D/27a9bd89-d914-484b-850d-c3518c9850c2/scratchpad")
from fim_eval2 import make_cases

URL = "http://127.0.0.1:8012/infill"
FRACTIONS = [0.0, 0.25, 0.5, 0.75]


def call(prefix, suffix, n=32):
    b = {"input_prefix": prefix, "input_suffix": suffix, "n_predict": n,
         "seed": 1234, "temperature": 0.0, "top_k": 40, "top_p": 0.99,
         "samplers": ["top_k", "top_p", "infill"]}
    r = urllib.request.Request(URL, data=json.dumps(b).encode(),
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=180) as f:
        return json.load(f).get("content", "")


def lcp(a, b):
    n = 0
    for x, y in zip(a, b):
        if x != y:
            break
        n += 1
    return n


rng = random.Random(4242)
cases = [c for c in make_cases(rng, 120, 10, 4) if c["truth"].strip()]
print(f"cases={len(cases)}  fractions={FRACTIONS}\n", flush=True)

rows = {f: {"exact": 0, "n": 0, "saved": [], "frac_done": []} for f in FRACTIONS}

for i, c in enumerate(cases, 1):
    target = c["truth"].splitlines()[0]
    stripped = target.rstrip()
    if len(stripped.strip()) < 8:
        continue
    for f in FRACTIONS:
        k = int(len(stripped) * f)
        typed, remainder = stripped[:k], stripped[k:]
        if not remainder:
            continue
        out = call(c["prefix"] + typed, c["suffix"])
        got = out.splitlines()[0] if out.splitlines() else ""
        r = rows[f]
        r["n"] += 1
        if got.rstrip() == remainder:
            r["exact"] += 1
        # keystrokes the suggestion would save under partial accept
        saved = lcp(got, remainder)
        r["saved"].append(saved)
        r["frac_done"].append(saved / max(len(remainder), 1))
    if i % 15 == 0:
        print(f"  ...{i}/{len(cases)}", flush=True)

print(f"\n{'typed':>7} {'n':>4} {'full-line exact':>16} {'chars saved (mean)':>19} {'% of remainder':>15}")
print("-" * 68)
for f in FRACTIONS:
    r = rows[f]
    if not r["n"]:
        continue
    print(f"{int(f*100):>6}% {r['n']:>4} {100*r['exact']/r['n']:>15.1f}% "
          f"{statistics.mean(r['saved']):>19.1f} {100*statistics.mean(r['frac_done']):>14.1f}%")
