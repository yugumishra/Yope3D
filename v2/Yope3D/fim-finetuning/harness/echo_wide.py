#!/usr/bin/env python3
import sys, random, json, urllib.request
sys.path.insert(0, "/private/tmp/claude-501/-Users-me-Desktop-dev-Yope3D-v2-Yope3D/27a9bd89-d914-484b-850d-c3518c9850c2/scratchpad")
from fim_eval2 import make_cases
from pathlib import Path

URL = "http://127.0.0.1:8012/infill"
rng = random.Random(99)
cases = make_cases(rng, 120, 10, 3)
other = "".join(Path("/Users/me/Desktop/dev/Yope3D/v2/Yope3D/scripts/behaviors/logo_playback.py")
                .read_text().splitlines(keepends=True)[:60])


def call(c, chunks):
    b = {"input_prefix": c["prefix"], "input_suffix": c["suffix"], "n_predict": 32,
         "seed": 1234, "top_k": 40, "top_p": 0.99,
         "samplers": ["top_k", "top_p", "infill"]}
    if chunks:
        b["input_extra"] = chunks
    r = urllib.request.Request(URL, data=json.dumps(b).encode(),
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=180) as f:
        return json.load(f).get("content", "")


def echo(prefix, out):
    ol = [l.strip() for l in out.splitlines() if l.strip()]
    if not ol:
        return None
    tail = {l.strip() for l in prefix.splitlines()[-25:] if l.strip()}
    return sum(1 for l in ol if l in tail) / len(ol)


modes = {"none": None, "other": "o", "same": "s"}
agg = {k: [] for k in modes}
anyecho = {k: 0 for k in modes}
for i, c in enumerate(cases, 1):
    for k, v in modes.items():
        ch = None
        if v == "o":
            ch = [{"filename": "logo_playback.py", "text": other}]
        elif v == "s":
            ch = [{"filename": c["file"], "text": c["prefix"][-2500:]}]
        try:
            e = echo(c["prefix"], call(c, ch))
        except Exception:
            continue
        if e is not None:
            agg[k].append(e)
            if e > 0:
                anyecho[k] += 1
    if i % 15 == 0:
        print(f"  ...{i}/{len(cases)}", flush=True)

print(f"\ncases={len(cases)}")
print(f"{'ring-buffer content':24} {'mean echo':>10} {'any echo':>10}   n")
for k in modes:
    a = agg[k]
    if not a:
        continue
    print(f"{k:24} {100*sum(a)/len(a):9.1f}% {100*anyecho[k]/len(a):9.1f}%   {len(a)}")
