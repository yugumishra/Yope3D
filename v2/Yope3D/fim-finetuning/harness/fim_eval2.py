#!/usr/bin/env python3
"""
Parameterised FIM eval for Yope3D.

  fim_eval2.py --suffix 20 --cuts 4 --label "suffix=20"
  fim_eval2.py --port 8013 --label 3B

Adds a per-keystroke latency probe: after each case it re-sends the same window
with a few extra prefix chars, which is what the editor actually does and what
the suffix window taxes.
"""
import argparse, json, re, random, statistics, time, urllib.request
from pathlib import Path

ROOT     = Path("/Users/me/Desktop/dev/Yope3D/v2/Yope3D")
BEHAVIOR = ROOT / "scripts" / "behaviors"
STUB     = ROOT / "typings" / "yope3d" / "__init__.pyi"

REG_FAMILY = {"reg_get", "reg_add", "reg_has", "reg_remove", "reg_valid"}
MEMORISED  = {"get_component", "add_component", "has_component", "remove_component",
              "GetComponent", "AddComponent", "TryGetComponent"}


def load_api():
    names = set()
    for line in STUB.read_text().splitlines():
        m = re.match(r"^(?:def|class)\s+([A-Za-z_]\w*)", line)
        if m: names.add(m.group(1)); continue
        m = re.match(r"^([A-Z][A-Z0-9_]*)\s*[:=]", line)
        if m: names.add(m.group(1))
    return names


API = load_api()


def target_files():
    return sorted(p for p in BEHAVIOR.glob("*.py")
                  if not p.name.startswith("_") and p.name != "__init__.py")


def is_cuttable(lines, i):
    ln = lines[i]; s = ln.strip()
    if not s or s.startswith("#"): return False
    if not ln[:1].isspace(): return False
    if s in {"pass", "return", "}", ")", "]", "})", "),"}: return False
    return len(s) > 8


def make_cases(rng, prefix_lines, suffix_lines, cuts):
    cases = []
    for path in target_files():
        lines = path.read_text().splitlines(keepends=True)
        idxs = [i for i in range(len(lines))
                if is_cuttable(lines, i) and 12 < i < len(lines) - 6]
        picks = idxs if len(idxs) < cuts else rng.sample(idxs, cuts)
        for i in sorted(picks):
            span = min(rng.choice([1, 1, 2, 3]), len(lines) - 6 - i)
            if span < 1: continue
            cases.append({
                "file": path.name, "line": i + 1, "span": span,
                "prefix": "".join(lines[max(0, i - prefix_lines):i]),
                "truth":  "".join(lines[i:i + span]),
                "suffix": "".join(lines[i + span:i + span + suffix_lines]),
            })
    return cases


def infill(url, prefix, suffix, n_predict):
    body = json.dumps({"input_prefix": prefix, "input_suffix": suffix,
                       "n_predict": n_predict, "temperature": 0.0,
                       "seed": 1234}).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json"})
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=180) as r:
        d = json.load(r)
    d["_wall"] = (time.perf_counter() - t0) * 1000
    return d


def norm(s): return re.sub(r"\s+", " ", s).strip()


def char_agree(a, b):
    n = 0
    for x, y in zip(a, b):
        if x != y: break
        n += 1
    return n / max(len(b), 1)


def score(case, content):
    got = "".join(content.splitlines(keepends=True)[:case["span"]])
    truth = case["truth"]
    calls = re.findall(r"yope3d\.([A-Za-z_]\w*)", got)
    truth_reg = set(re.findall(r"\b(reg_\w+)", truth)) & REG_FAMILY
    got_reg = set(re.findall(r"\b(reg_\w+)", got))
    return {
        "exact": got.rstrip() == truth.rstrip(),
        "normalized": norm(got) == norm(truth),
        "first_line": got.splitlines()[:1] == truth.splitlines()[:1],
        "char_agree": char_agree(got, truth),
        "n_calls": len(calls),
        "n_bad": len([c for c in calls if c not in API]),
        "bad": [c for c in calls if c not in API],
        "memorised": len([n for n in re.findall(r"\b([A-Za-z_]\w*)\s*\(", got)
                          if n in MEMORISED]),
        "reg_needed": bool(truth_reg),
        "reg_hit": bool(truth_reg) and truth_reg <= got_reg,
        "got": got, "truth": truth,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--suffix", type=int, default=80)
    ap.add_argument("--prefix", type=int, default=120)
    ap.add_argument("--cuts", type=int, default=8)
    ap.add_argument("--n-predict", type=int, default=32)
    ap.add_argument("--port", type=int, default=8012)
    ap.add_argument("--label", default="run")
    ap.add_argument("--out", default=None)
    ap.add_argument("--typing-probe", action="store_true",
                    help="also measure incremental-keystroke latency")
    a = ap.parse_args()

    url = f"http://127.0.0.1:{a.port}/infill"
    rng = random.Random(1234)
    cases = make_cases(rng, a.prefix, a.suffix, a.cuts)

    print(f"[{a.label}] {len(cases)} cases  prefix={a.prefix}L suffix={a.suffix}L "
          f"n_predict={a.n_predict} port={a.port}", flush=True)

    res, cold, typing, pre_tok = [], [], [], []
    for k, c in enumerate(cases, 1):
        try:
            d = infill(url, c["prefix"], c["suffix"], a.n_predict)
        except Exception as e:
            print(f"  FAIL {c['file']}:{c['line']} {e}", flush=True); continue
        cold.append(d["_wall"])
        t = d.get("timings", {})
        pre_tok.append(t.get("prompt_n", 0))
        r = score(c, d.get("content", ""))
        r.update(file=c["file"], line=c["line"], span=c["span"])
        res.append(r)
        if a.typing_probe and k % 4 == 0:
            d2 = infill(url, c["prefix"] + "        self.x", c["suffix"], a.n_predict)
            typing.append(d2.get("timings", {}).get("prompt_ms", 0))
        if k % 40 == 0:
            print(f"  …{k}/{len(cases)}", flush=True)

    n = len(res)
    def pct(f): return 100.0 * sum(1 for r in res if f(r)) / max(n, 1)
    cold.sort()
    ones = [r for r in res if r["span"] == 1]

    summary = {
        "label": a.label, "suffix_lines": a.suffix, "prefix_lines": a.prefix,
        "n": n,
        "exact": pct(lambda r: r["exact"]),
        "first_line": pct(lambda r: r["first_line"]),
        "exact_1line": 100.0*sum(1 for r in ones if r["exact"])/max(len(ones),1),
        "char_agree": 100*statistics.mean(r["char_agree"] for r in res),
        "calls": sum(r["n_calls"] for r in res),
        "bad": sum(r["n_bad"] for r in res),
        "memorised": sum(r["memorised"] for r in res),
        "reg_needed": sum(1 for r in res if r["reg_needed"]),
        "reg_hit": sum(1 for r in res if r["reg_hit"]),
        "p50": cold[len(cold)//2], "p90": cold[int(len(cold)*0.9)],
        "mean_prompt_tok": statistics.mean(pre_tok),
        "typing_prefill_ms": statistics.mean(typing) if typing else None,
    }

    print(f"  exact {summary['exact']:.1f}%  1-line {summary['exact_1line']:.1f}%  "
          f"first-line {summary['first_line']:.1f}%  char {summary['char_agree']:.1f}%")
    print(f"  api {summary['bad']}/{summary['calls']} bad  memorised {summary['memorised']}  "
          f"reg {summary['reg_hit']}/{summary['reg_needed']}")
    print(f"  p50 {summary['p50']:.0f}ms  p90 {summary['p90']:.0f}ms  "
          f"prompt {summary['mean_prompt_tok']:.0f}tok  "
          f"typing-prefill {summary['typing_prefill_ms'] or float('nan'):.0f}ms\n", flush=True)

    if a.out:
        Path(a.out).write_text(json.dumps({"summary": summary, "results": res}, indent=1))
    return summary


if __name__ == "__main__":
    main()
