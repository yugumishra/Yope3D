#!/usr/bin/env python3
"""
yope3d FIM validation proxy — sits between llama.vscode and llama-server.

The completion model has zero Yope3D in its training data, so it invents
plausible-looking bindings (`yope3d.color`, `yope3d.Vec`, `yope3d.reg_set`).
A base model cannot be told to stop; it can only be checked after the fact.
This proxy does that, and doubles as the only place to A/B what goes into
`input_extra`, which the extension does not expose.

    llama.vscode  ->  :8015 (this)  ->  :8012 (llama-server)

Point `llama-vscode.endpoint` at the proxy. Every non-/infill route is a
transparent pass-through, so /health, /props and /metrics behave normally.

Usage:
    python3 tools/fim_proxy.py                       # validate + retry
    python3 tools/fim_proxy.py --inject-stub 40      # + targeted .pyi context
    python3 tools/fim_proxy.py --on-invalid truncate # never pay a retry
    python3 tools/fim_proxy.py --off                 # pure pass-through (A/B control)

    curl -s localhost:8015/proxy/stats | python3 -m json.tool
"""
import argparse, json, re, sys, threading, time, urllib.error, urllib.request
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CALL_RE = re.compile(r"yope3d\.([A-Za-z_]\w*)")
# Component-name string literals, e.g. reg_get(e, "Material") — used to pick
# which stub lines are worth injecting.
LIT_RE = re.compile(r"[\"']([A-Z][A-Za-z0-9_]*)[\"']")


def load_api(stub: Path):
    """Names reachable as `yope3d.X`, plus the stub line(s) that define each.

    Matches top-level `def`/`class` AND bare module attributes — the latter
    covers the singletons (`world`, `camera`, `input`, `audio`, `scene_manager`,
    `window`, `settings`) that a def/class-only scan misses and then reports as
    hallucinations.
    """
    names, sigs = set(), {}
    for line in stub.read_text().splitlines():
        m = re.match(r"^(?:def|class)\s+([A-Za-z_]\w*)", line)
        if not m:
            m = re.match(r"^([A-Za-z_]\w*)\s*[:=]", line)
        if m:
            n = m.group(1)
            names.add(n)
            sigs.setdefault(n, []).append(line.rstrip())
    return names, sigs


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.n_req = self.n_infill = self.n_with_calls = 0
        self.n_invalid = self.n_retried = self.n_retry_fixed = self.n_truncated = 0
        self.bad = Counter()
        self.injected_lines = 0
        self.handler_ms = 0.0    # wall time inside the handler
        self.upstream_ms = 0.0   # of which: waiting on llama-server

    def snapshot(self):
        with self.lock:
            infill = max(self.n_infill, 1)
            return {
                "requests": self.n_req,
                "infill": self.n_infill,
                "completions_with_yope3d_calls": self.n_with_calls,
                "invalid_completions": self.n_invalid,
                "invalid_rate_pct": round(100.0 * self.n_invalid / infill, 2),
                "retried": self.n_retried,
                "retry_fixed": self.n_retry_fixed,
                "truncated": self.n_truncated,
                "stub_lines_injected": self.injected_lines,
                "mean_upstream_ms": round(self.upstream_ms / infill, 2),
                "mean_proxy_overhead_ms": round(
                    (self.handler_ms - self.upstream_ms) / infill, 3),
                "most_hallucinated": self.bad.most_common(10),
            }


def stub_slice(prefix, suffix, sigs, max_lines):
    """Signature lines for the yope3d names actually referenced near the cursor.

    Targeted retrieval rather than a fixed blob: the full stub is ~35K tokens and
    will not fit in an 8K window, but the handful of signatures the surrounding
    code already mentions will.
    """
    refs = set(CALL_RE.findall(prefix)) | set(CALL_RE.findall(suffix))
    refs |= set(LIT_RE.findall(prefix[-2000:]))
    out = []
    for n in sorted(refs):
        for line in sigs.get(n, [])[:4]:
            out.append(line)
            if len(out) >= max_lines:
                return out
    return out


class Proxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    cfg = None  # set in main()

    def log_message(self, *a):
        pass  # keep the terminal usable; use --verbose instead

    # ---- plumbing -------------------------------------------------------
    def _upstream(self, path, body=None, headers=None, timeout=180):
        url = self.cfg.upstream + path
        req = urllib.request.Request(
            url, data=body,
            headers=headers or {"Content-Type": "application/json"},
            method="POST" if body is not None else "GET")
        t0 = time.perf_counter()
        with urllib.request.urlopen(req, timeout=timeout) as r:
            out = r.status, r.read(), dict(r.headers)
        with self.cfg.stats.lock:
            self.cfg.stats.upstream_ms += (time.perf_counter() - t0) * 1000
        return out

    def _send(self, status, payload: bytes, ctype="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _fail(self, e):
        msg = json.dumps({"error": f"proxy upstream failure: {e}"}).encode()
        self._send(502, msg)

    # ---- routes ---------------------------------------------------------
    def do_GET(self):
        if self.path.startswith("/proxy/stats"):
            return self._send(200, json.dumps(self.cfg.stats.snapshot(), indent=1).encode())
        with self.cfg.stats.lock:
            self.cfg.stats.n_req += 1
        try:
            st, body, _ = self._upstream(self.path, timeout=30)
            self._send(st, body)
        except Exception as e:
            self._fail(e)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        with self.cfg.stats.lock:
            self.cfg.stats.n_req += 1

        is_fim = self.path.rstrip("/").endswith(("/infill", "/completion"))
        if not is_fim or self.cfg.off:
            try:
                st, body, _ = self._upstream(self.path, raw)
                return self._send(st, body)
            except Exception as e:
                return self._fail(e)

        t0 = time.perf_counter()
        try:
            req = json.loads(raw)
        except Exception:
            try:
                st, body, _ = self._upstream(self.path, raw)
                return self._send(st, body)
            except Exception as e:
                return self._fail(e)

        # ---- optional: targeted stub context --------------------------
        if self.cfg.inject_stub:
            lines = stub_slice(req.get("input_prefix", ""), req.get("input_suffix", ""),
                               self.cfg.sigs, self.cfg.inject_stub)
            if lines:
                extra = list(req.get("input_extra") or [])
                extra.insert(0, {"filename": "yope3d.pyi", "text": "\n".join(lines)})
                req["input_extra"] = extra
                with self.cfg.stats.lock:
                    self.cfg.stats.injected_lines += len(lines)

        try:
            st, body, _ = self._upstream(self.path, json.dumps(req).encode())
            resp = json.loads(body)
        except Exception as e:
            return self._fail(e)

        content = resp.get("content")
        if not isinstance(content, str) or not content.strip():
            self._bump_overhead(t0)
            return self._send(st, json.dumps(resp).encode())

        bad = self._invalid(content)
        with self.cfg.stats.lock:
            if CALL_RE.search(content):
                self.cfg.stats.n_with_calls += 1
        if not bad:
            self._bump_overhead(t0)
            return self._send(st, json.dumps(resp).encode())

        with self.cfg.stats.lock:
            self.cfg.stats.n_invalid += 1
            for name, _ in bad:
                self.cfg.stats.bad[name] += 1
        if self.cfg.verbose:
            print(f"[proxy] invalid: {[b[0] for b in bad]}  in {content[:60]!r}",
                  file=sys.stderr, flush=True)

        # ---- retry once off-policy ------------------------------------
        if self.cfg.on_invalid == "retry":
            retry = dict(req)
            retry["temperature"] = self.cfg.retry_temp
            retry["seed"] = int(time.time() * 1000) % 2**31
            try:
                _, body2, _ = self._upstream(self.path, json.dumps(retry).encode())
                resp2 = json.loads(body2)
                with self.cfg.stats.lock:
                    self.cfg.stats.n_retried += 1
                c2 = resp2.get("content") or ""
                if c2.strip() and not self._invalid(c2):
                    with self.cfg.stats.lock:
                        self.cfg.stats.n_retry_fixed += 1
                    self._bump_overhead(t0)
                    return self._send(st, json.dumps(resp2).encode())
            except Exception:
                pass  # fall through to truncation

        # ---- truncate at the first invented name ----------------------
        cut = bad[0][1]
        resp["content"] = content[:cut]
        with self.cfg.stats.lock:
            self.cfg.stats.n_truncated += 1
        self._bump_overhead(t0)
        self._send(st, json.dumps(resp).encode())

    def _invalid(self, text):
        return [(m.group(1), m.start()) for m in CALL_RE.finditer(text)
                if m.group(1) not in self.cfg.api]

    def _bump_overhead(self, t0):
        with self.cfg.stats.lock:
            self.cfg.stats.n_infill += 1
            self.cfg.stats.handler_ms += (time.perf_counter() - t0) * 1000


def main():
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8015)
    ap.add_argument("--upstream", default="http://127.0.0.1:8012")
    ap.add_argument("--stub", default=str(root / "typings" / "yope3d" / "__init__.pyi"))
    ap.add_argument("--on-invalid", choices=["retry", "truncate", "passthrough"],
                    default="retry")
    ap.add_argument("--retry-temp", type=float, default=0.4,
                    help="temperature for the retry (temp 0 would reproduce the miss)")
    ap.add_argument("--inject-stub", type=int, default=0, metavar="N",
                    help="inject up to N lines of relevant .pyi signatures into input_extra")
    ap.add_argument("--off", action="store_true", help="pure pass-through (A/B control)")
    ap.add_argument("--verbose", action="store_true")
    cfg = ap.parse_args()

    stub = Path(cfg.stub)
    if not stub.exists():
        sys.exit(f"stub not found: {stub}")
    cfg.api, cfg.sigs = load_api(stub)
    cfg.stats = Stats()
    if cfg.on_invalid == "passthrough":
        cfg.on_invalid = None
    Proxy.cfg = cfg

    print(f"yope3d FIM proxy  :{cfg.port} -> {cfg.upstream}", file=sys.stderr)
    print(f"  api surface   {len(cfg.api)} names from {stub.name}", file=sys.stderr)
    print(f"  on-invalid    {cfg.on_invalid or 'passthrough'}"
          f"{'  (OFF: pass-through)' if cfg.off else ''}", file=sys.stderr)
    print(f"  inject-stub   {cfg.inject_stub or 'disabled'}", file=sys.stderr)
    print(f"  stats         curl localhost:{cfg.port}/proxy/stats", file=sys.stderr, flush=True)

    ThreadingHTTPServer(("127.0.0.1", cfg.port), Proxy).serve_forever()


if __name__ == "__main__":
    main()
