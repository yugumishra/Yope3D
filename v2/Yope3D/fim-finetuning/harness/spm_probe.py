#!/usr/bin/env python3
"""
Mechanism probe: PSM (8012) vs SPM (8014).

Hypothesis: in PSM order the suffix sits AFTER the prefix, so appending a keystroke
to the prefix invalidates the whole suffix. In SPM the suffix leads, so it should
stay cached and only the prefix tail re-prefills.

Measured by prompt_n — how many tokens the server had to re-evaluate.
"""
import json, time, urllib.request
from pathlib import Path

src   = Path("/Users/me/Desktop/dev/Yope3D/v2/Yope3D/scripts/behaviors/character_controller.py")
lines = src.read_text().splitlines(keepends=True)
cut   = 90
PREFIX = "".join(lines[max(0, cut-120):cut])
SUFFIX = "".join(lines[cut+1:cut+21])          # 20-line suffix, the tuned setting


def call(port, prefix, suffix, n=32):
    body = json.dumps({"input_prefix": prefix, "input_suffix": suffix,
                       "n_predict": n, "temperature": 0.0}).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{port}/infill", data=body,
                                 headers={"Content-Type": "application/json"})
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=120) as r:
        d = json.load(r)
    return (time.perf_counter()-t0)*1000, d.get("timings", {}), d.get("content", "")


for label, port in (("PSM (default, 8012)", 8012), ("SPM (--spm-infill, 8014)", 8014)):
    print(f"\n=== {label} ===")
    w, t, _ = call(port, PREFIX, SUFFIX)
    print(f"  cold          prompt_n {t['prompt_n']:5d}  prefill {t['prompt_ms']:7.1f} ms  wall {w:7.1f} ms")
    w, t, _ = call(port, PREFIX, SUFFIX)
    print(f"  warm (same)   prompt_n {t['prompt_n']:5d}  prefill {t['prompt_ms']:7.1f} ms  wall {w:7.1f} ms")
    for extra in ("        s", "        se", "        sel", "        self.y_v"):
        w, t, _ = call(port, PREFIX + extra, SUFFIX)
        print(f"  +{len(extra):>2} chars     prompt_n {t['prompt_n']:5d}  prefill {t['prompt_ms']:7.1f} ms  wall {w:7.1f} ms")

# sanity: do both actually produce sane completions?
print("\n=== output sanity (same input) ===")
for label, port in (("PSM", 8012), ("SPM", 8014)):
    _, _, c = call(port, PREFIX, SUFFIX)
    print(f"  {label}: {c.splitlines()[0][:90] if c.splitlines() else '<empty>'!r}")
