#!/usr/bin/env python3
"""MANDATORY PRE-TRAINING GATE — does our FIM format match llama.cpp's?

Run this and get a PASS before generating a dataset or starting a training run.
Nothing else in this directory is worth anything if this fails.

WHY
    Training examples must be structurally identical to what the server sends
    at inference. Get it wrong and you do not get a slightly worse model — you
    get no transfer at all, and you will not find out until after the training
    run, the conversion, and an eval. The cost of being wrong here is the whole
    pipeline; the cost of checking is one HTTP round trip.

    This is not hypothetical. --spm-infill reorders the same content (suffix
    before prefix) and Qwen2.5-Coder's score goes to 0.0% on every metric —
    it starts emitting file headers inside method bodies. Token order is
    load-bearing.

WHY TOKEN IDS AND NOT STRINGS
    A string diff can pass while the tokenisation differs. `<|fim_prefix|>` is
    one token; a typo'd `<|fim-prefix|>` is five ordinary tokens that render
    identically in a terminal. Likewise a stray space before the prefix fuses
    into the neighbouring token and shifts every position after it. Only the
    token IDs settle it.

HOW
    llama-server echoes the fully-assembled prompt in its /infill response
    (the `prompt` field). So:
        1. ask the server to build a prompt from known prefix/suffix
        2. render the same inputs through fim_format.render_prompt
        3. tokenize both via /tokenize and diff the ID sequences

USAGE
    llama-server -hf ggml-org/Qwen2.5-Coder-1.5B-Q8_0-GGUF --port 8012 \
      -c 8192 -np 1 -fa on --cache-reuse 256 -ngl 99 -ub 1024
    python3 fim-finetuning/corpus/verify_format.py --port 8012
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request

from fim_format import Chunk, Example, render_prompt

PREFIX = "def apply_boost(world, entity):\n    hull = yope3d.reg_get(entity, "
SUFFIX = "\n    return hull\n"


def post(url: str, body: dict, timeout: int = 120) -> dict:
    req = urllib.request.Request(
        url, data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def tokenize(base: str, text: str) -> list[int]:
    """Token IDs for `text`. `with_pieces` is off, so this is IDs only."""
    d = post(f"{base}/tokenize", {"content": text, "add_special": False})
    return d.get("tokens", [])


def pieces(base: str, text: str) -> list[tuple[int, str]]:
    d = post(f"{base}/tokenize",
             {"content": text, "add_special": False, "with_pieces": True})
    out = []
    for t in d.get("tokens", []):
        if isinstance(t, dict):
            p = t.get("piece", "")
            out.append((t.get("id", -1),
                        p if isinstance(p, str) else repr(p)))
        else:
            out.append((t, ""))
    return out


def first_divergence(a: list[int], b: list[int]) -> int:
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    return min(len(a), len(b)) if len(a) != len(b) else -1


def show_context(base: str, ours: str, theirs: str, at: int) -> None:
    """Print the tokens around the first divergence, both sides."""
    pa, pb = pieces(base, ours), pieces(base, theirs)
    lo, hi = max(0, at - 6), at + 6
    print(f"\n  first divergence at token index {at}")
    print(f"  {'idx':>5}  {'OURS':<28} {'SERVER':<28}")
    for i in range(lo, max(len(pa), len(pb))):
        if i >= hi:
            break
        x = f"{pa[i][0]}:{pa[i][1]!r}" if i < len(pa) else "-"
        y = f"{pb[i][0]}:{pb[i][1]!r}" if i < len(pb) else "-"
        mark = "  <<<" if i == at else ""
        print(f"  {i:>5}  {x:<28} {y:<28}{mark}")


def check(base: str, label: str, body: dict, ex: Example) -> bool:
    """One case: server-built prompt vs our rendered prompt."""
    print(f"\n=== {label} ===")
    try:
        d = post(f"{base}/infill", body)
    except urllib.error.URLError as e:
        print(f"  FAIL — cannot reach {base}/infill: {e}")
        return False

    theirs = d.get("prompt")
    if not isinstance(theirs, str):
        print("  INCONCLUSIVE — this llama-server build does not echo `prompt` "
              "in the /infill response, so the server side cannot be read.")
        print("  Fix: rerun llama-server with --verbose, or compare against a "
              "request captured by tools/fim_proxy.py --log-jsonl.")
        return False

    ours = render_prompt(ex)
    ta, tb = tokenize(base, ours), tokenize(base, theirs)

    if ta == tb:
        print(f"  PASS — {len(ta)} tokens, byte-identical token IDs")
        return True

    at = first_divergence(ta, tb)
    print(f"  FAIL — ours {len(ta)} tokens, server {len(tb)} tokens")
    show_context(base, ours, theirs, max(at, 0))
    if ours != theirs:
        print("\n  our render   :", repr(ours[:180]))
        print("  server prompt:", repr(theirs[:180]))
    return False


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=8012)
    ap.add_argument("--host", default="127.0.0.1")
    a = ap.parse_args()
    base = f"http://{a.host}:{a.port}"

    try:
        post(f"{base}/tokenize", {"content": "x"})
    except Exception as e:
        print(f"cannot reach {base} — is llama-server running?\n  {e}")
        sys.exit(2)

    common = {"n_predict": 1, "temperature": 0.0, "seed": 1}
    results = []

    # 1. No input_extra. Note this is NOT a "file-level" shape: llama.cpp
    #    still emits the full repo header. There is no bare-FIM mode.
    results.append(check(
        base, "no input_extra (repo header still emitted)",
        {"input_prefix": PREFIX, "input_suffix": SUFFIX, **common},
        Example(prefix=PREFIX, suffix=SUFFIX, middle="")))

    # 2. One chunk. PLAN.txt 5.8 measured ~4 tokens of overhead for this,
    #    consistent with <|file_sep|> + path — i.e. llama.cpp really is using
    #    Qwen's trained repo-level format rather than improvising one.
    one = [Chunk("scripts/behaviors/helper.py",
                 "def helper(world):\n    return world\n")]
    results.append(check(
        base, "one input_extra chunk",
        {"input_prefix": PREFIX, "input_suffix": SUFFIX,
         "input_extra": [{"filename": c.path, "text": c.text} for c in one],
         **common},
        Example(prefix=PREFIX, suffix=SUFFIX, middle="", extra=one)))

    # 3. Two chunks — the multi-chunk boundary is where a missing or doubled
    #    separator would hide, and llama.vscode sends up to ring_n_chunks=8.
    two = one + [Chunk("scripts/behaviors/other.py", "X = 1\n")]
    results.append(check(
        base, "two input_extra chunks (separator boundary)",
        {"input_prefix": PREFIX, "input_suffix": SUFFIX,
         "input_extra": [{"filename": c.path, "text": c.text} for c in two],
         **common},
        Example(prefix=PREFIX, suffix=SUFFIX, middle="", extra=two)))

    # 4. Chunk text without a trailing newline — checks we concatenate
    #    verbatim rather than helpfully inserting a separator.
    nonl = [Chunk("scripts/behaviors/nonl.py", "Y = 2")]
    results.append(check(
        base, "chunk text with no trailing newline",
        {"input_prefix": PREFIX, "input_suffix": SUFFIX,
         "input_extra": [{"filename": c.path, "text": c.text} for c in nonl],
         **common},
        Example(prefix=PREFIX, suffix=SUFFIX, middle="", extra=nonl)))

    print("\n" + "=" * 64)
    if all(results):
        print("GATE PASSED — fim_format.py matches the server. Safe to build a "
              "dataset.")
        sys.exit(0)
    print("GATE FAILED — do NOT train. Fix fim_format.render_prompt until both "
          "cases pass.")
    print("The token dump above shows exactly where the two disagree.")
    sys.exit(1)


if __name__ == "__main__":
    main()
