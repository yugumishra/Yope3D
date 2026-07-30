#!/usr/bin/env python3
"""
FIM eval v3 — scores what ghost text actually delivers.

v2 cut at line starts with nothing typed and scored exact match. That is the
hardest invocation point and a binary metric, and it discarded every partial win
even though the editor supports word/line partial accept.

v3 changes both:
  * sweeps the typed-prefix fraction (0/25/50/75%) — the real invocation curve
  * primary metric is CHARACTERS SAVED per invocation (longest common prefix of
    the suggestion against what the author actually wrote)

Held-out-set aware: --exclude takes a file list to keep out of the sample, so the
same harness can score a fine-tuned model without leaking training data.
"""
import argparse, json, re, random, statistics, sys, time, urllib.request
from pathlib import Path

ROOT = Path("/Users/me/Desktop/dev/Yope3D/v2/Yope3D")
BEHAVIOR = ROOT / "scripts" / "behaviors"
STUB = ROOT / "typings" / "yope3d" / "__init__.pyi"

CALL_RE = re.compile(r"yope3d\.([A-Za-z_]\w*)")
FRACTIONS = [0.0, 0.25, 0.5, 0.75]


def load_api():
    names = set()
    for line in STUB.read_text().splitlines():
        m = re.match(r"^(?:def|class)\s+([A-Za-z_]\w*)", line) or \
            re.match(r"^([A-Za-z_]\w*)\s*[:=]", line)
        if m:
            names.add(m.group(1))
    return names


API = load_api()


def target_files(exclude, directory=None, only=""):
    """Files to cut. Defaults to scripts/behaviors (tier 1).

    `directory` points the same harness at the tier-3 control corpus, so the
    control is scored by identical code and the two numbers are comparable.
    A separately-written control harness would drift from this one and the
    comparison would quietly stop meaning anything.
    """
    d = Path(directory) if directory else BEHAVIOR
    if not d.is_absolute():
        d = ROOT / d
    ex = {e.strip() for e in exclude.split(",") if e.strip()}
    inc = {e.strip() for e in only.split(",") if e.strip()}
    return sorted(p for p in d.glob("*.py")
                  if not p.name.startswith("_") and p.name != "__init__.py"
                  and p.name not in ex and (not inc or p.name in inc))


def cuttable(lines, i):
    ln = lines[i]; s = ln.strip()
    return bool(s) and not s.startswith("#") and ln[:1].isspace() and len(s) > 8


def make_cases(rng, files, prefix_lines, suffix_lines, cuts):
    cases = []
    for path in files:
        lines = path.read_text().splitlines(keepends=True)
        idxs = [i for i in range(len(lines))
                if cuttable(lines, i) and 12 < i < len(lines) - 6]
        for i in sorted(idxs if len(idxs) < cuts else rng.sample(idxs, cuts)):
            cases.append({
                "file": path.name, "line": i + 1,
                "prefix": "".join(lines[max(0, i - prefix_lines):i]),
                "target": lines[i].rstrip("\n").rstrip(),
                "suffix": "".join(lines[i + 1:i + 1 + suffix_lines]),
            })
    return cases


def lcp(a, b):
    n = 0
    for x, y in zip(a, b):
        if x != y:
            break
        n += 1
    return n


def call(url, prefix, suffix, n_predict):
    body = {"input_prefix": prefix, "input_suffix": suffix, "n_predict": n_predict,
            "seed": 1234, "temperature": 0.0, "top_k": 40, "top_p": 0.99,
            "samplers": ["top_k", "top_p", "infill"]}
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=180) as r:
        d = json.load(r)
    d["_wall"] = (time.perf_counter() - t0) * 1000
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8012)
    ap.add_argument("--label", default="run")
    ap.add_argument("--prefix", type=int, default=120)
    ap.add_argument("--suffix", type=int, default=10)
    ap.add_argument("--cuts", type=int, default=5)
    ap.add_argument("--n-predict", type=int, default=32)
    ap.add_argument("--exclude", default="", help="comma-separated held-out filenames")
    ap.add_argument("--only", default="",
                    help="comma-separated files to score EXCLUSIVELY — the "
                         "inverse of --exclude, for scoring the held-out set")
    ap.add_argument("--dir", default=None,
                    help="directory of .py to cut (default scripts/behaviors); "
                         "used to point tier 1's harness at the tier-3 control corpus")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    url = f"http://127.0.0.1:{a.port}/infill"
    files = target_files(a.exclude, a.dir, a.only)
    cases = make_cases(random.Random(4242), files, a.prefix, a.suffix, a.cuts)
    print(f"[{a.label}] {len(cases)} cut points x {len(FRACTIONS)} typed-fractions "
          f"= {len(cases)*len(FRACTIONS)} calls  ({len(files)} files, port {a.port})",
          flush=True)

    rows = {f: {"n": 0, "exact": 0, "saved": [], "frac": [], "wall": []}
            for f in FRACTIONS}
    calls_total = bad_total = 0
    detail = []

    for i, c in enumerate(cases, 1):
        for f in FRACTIONS:
            k = int(len(c["target"]) * f)
            typed, remainder = c["target"][:k], c["target"][k:]
            if not remainder.strip():
                continue
            try:
                d = call(url, c["prefix"] + typed, c["suffix"], a.n_predict)
            except Exception as e:
                print(f"  FAIL {c['file']}:{c['line']} f={f}: {e}", flush=True)
                continue
            out = d.get("content", "")
            got = out.splitlines()[0] if out.splitlines() else ""
            saved = lcp(got, remainder)
            r = rows[f]
            r["n"] += 1
            r["exact"] += int(got.rstrip() == remainder)
            r["saved"].append(saved)
            r["frac"].append(saved / max(len(remainder), 1))
            r["wall"].append(d["_wall"])
            names = CALL_RE.findall(got)
            calls_total += len(names)
            bad_total += sum(1 for n in names if n not in API)
            detail.append({"file": c["file"], "line": c["line"], "f": f,
                           "saved": saved, "exact": got.rstrip() == remainder,
                           "got": got, "want": remainder})
        if i % 20 == 0:
            print(f"  ...{i}/{len(cases)}", flush=True)

    print(f"\n{'typed':>7} {'n':>4} {'full-line':>10} {'chars saved':>12} "
          f"{'% remainder':>12} {'p50 ms':>8}")
    print("-" * 60)
    all_saved = []
    for f in FRACTIONS:
        r = rows[f]
        if not r["n"]:
            continue
        w = sorted(r["wall"])
        all_saved += r["saved"]
        print(f"{int(f*100):>6}% {r['n']:>4} {100*r['exact']/r['n']:>9.1f}% "
              f"{statistics.mean(r['saved']):>12.1f} "
              f"{100*statistics.mean(r['frac']):>11.1f}% {w[len(w)//2]:>8.0f}")

    print(f"\nPRIMARY  mean chars saved / invocation : {statistics.mean(all_saved):.1f}")
    print(f"         yope3d.* invented              : {bad_total}/{calls_total}"
          f" ({100*bad_total/max(calls_total,1):.1f}%)")
    print(f"         files sampled                  : {len(files)}"
          + (f"  (excluded: {a.exclude})" if a.exclude else ""))

    if a.out:
        Path(a.out).write_text(json.dumps(
            {"label": a.label, "port": a.port, "excluded": a.exclude,
             "mean_saved": statistics.mean(all_saved),
             "invented": bad_total, "calls": calls_total,
             "rows": {str(f): {k: v for k, v in rows[f].items() if k != "wall"}
                      for f in FRACTIONS},
             "detail": detail}, indent=1))


if __name__ == "__main__":
    main()
