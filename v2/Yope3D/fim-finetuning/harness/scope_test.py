#!/usr/bin/env python3
"""How far back can the model see a variable you defined?

Plants a distinctively-named attribute N lines above the cursor, then asks for a
completion that should reference it. Sweeps N across the n_prefix=120 boundary.
Filler is realistic code, not blank lines (blanks compress and would flatter the result).
"""
import json, urllib.request

URL = "http://127.0.0.1:8012/infill"
TARGET = "wobble_damping_factor"
DISTANCES = [10, 30, 60, 90, 110, 130, 160, 220]
TRIALS = 3


def filler(n):
    """n lines of plausible, non-repeating method bodies."""
    out, i = [], 0
    while len(out) < n:
        i += 1
        out += [f"    def _helper_{i}(self, world, dt):",
                f"        scale_{i} = {i}.0 * dt",
                f"        offset_{i} = scale_{i} + {i * 2}.0",
                f"        return offset_{i}",
                ""]
    return "\n".join(out[:n]) + "\n"


def build(dist):
    head = ("import yope3d, math\n\n\nclass WobbleBehavior:\n"
            "    def init(self, world, entity, params):\n"
            f"        self.{TARGET} = 0.37\n"
            "        self.spin_rate = 2.0\n\n")
    tail = ("    def update(self, world, entity, dt):\n"
            "        tf = yope3d.reg_get(entity, \"Transform\")\n"
            "        damping = self.")
    return head + filler(dist) + tail


def call(prefix, suffix, seed):
    b = {"input_prefix": prefix, "input_suffix": suffix, "n_predict": 16,
         "seed": seed, "top_k": 40, "top_p": 0.99,
         "samplers": ["top_k", "top_p", "infill"]}
    r = urllib.request.Request(URL, data=json.dumps(b).encode(),
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=180) as f:
        return json.load(f).get("content", "")


SUFFIX = "\n        tf.position.y *= damping\n"

print(f"target attribute: self.{TARGET}")
print(f"n_prefix window = 120 lines\n")
print(f"{'lines above cursor':>19} {'in window?':>11} {'hit rate':>9}   sample")
print("-" * 78)
for d in DISTANCES:
    prefix = build(d)
    total_lines = len(prefix.splitlines())
    # llama.vscode sends only the last n_prefix lines
    sent = "\n".join(prefix.splitlines()[-120:])
    visible = TARGET in sent
    hits, sample = 0, ""
    for t in range(TRIALS):
        c = call(sent, SUFFIX, 1234 + t)
        if TARGET in c:
            hits += 1
        if not sample:
            sample = c.strip().replace("\n", " ")[:44]
    print(f"{d:19d} {str(visible):>11} {hits}/{TRIALS:<7}   {sample!r}")

print("\nNote: 'in window' is whether the definition survives the 120-line truncation")
print("that llama.vscode applies before sending.")
