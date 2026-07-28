#!/usr/bin/env python3
"""Article 2 diagrams -> editable Excalidraw scenes (not flat PNGs).

Emits .excalidraw JSON you open at excalidraw.com or in the VS Code Excalidraw
extension, then drag / recolor / nudge visually and export PNG or SVG into
site/src/content/blog/images/.

    python3 tools/articleAnalysis/article2_diagrams_excalidraw.py

Everything measured (block widths = ms) is scripted so it's exact; everything
aesthetic is yours to drag afterwards. Text is BOUND to its box (containerId),
so it stays centred no matter how you resize the box by hand.
"""
import json, os, random

OUT = os.path.join(os.path.dirname(__file__), "diagrams")
PX  = 26          # pixels per millisecond (spiral). bump for a wider figure.

COL = {
    "render": "#4dabf7",   # blue  — render work
    "phys":   "#ffd43b",   # yellow — physics step
    "over":   "#ff8787",   # red   — step that spilled past the budget
    "budget": "#1e1e1e",
    "grid":   "#adb5bd",
    "text":   "#1e1e1e",
}


def _n():  # random nonce/seed
    return random.randint(1, 2**31)


def _common(x, y, w, h, **kw):
    d = dict(id=str(_n()), x=x, y=y, width=w, height=h, angle=0,
             strokeColor=COL["text"], backgroundColor="transparent",
             fillStyle="solid", strokeWidth=1, strokeStyle="solid",
             roughness=0, opacity=100, groupIds=[], frameId=None,
             roundness=None, seed=_n(), version=1, versionNonce=_n(),
             isDeleted=False, boundElements=[], updated=1, link=None,
             locked=False)
    d.update(kw)
    return d


def rect(x, y, w, h, bg, text=None, fs=16, opacity=100):
    r = _common(x, y, w, h, type="rectangle", backgroundColor=bg, opacity=opacity)
    els = [r]
    if text is not None:
        t = _common(x, y, w, h, type="text", text=text, originalText=text,
                    fontSize=fs, fontFamily=2, textAlign="center",
                    verticalAlign="middle", lineHeight=1.25,
                    baseline=int(fs * 0.8), containerId=r["id"])
        r["boundElements"] = [{"type": "text", "id": t["id"]}]
        els.append(t)
    return els


def text(x, y, s, fs=16, color=None, align="left"):
    lines = s.split("\n")
    w = max(len(l) for l in lines) * fs * 0.6
    return [_common(x, y, w, len(lines) * fs * 1.3, type="text", text=s,
            originalText=s, fontSize=fs, fontFamily=2, textAlign=align,
            verticalAlign="top", lineHeight=1.25, baseline=int(fs * 0.8),
            containerId=None, strokeColor=color or COL["text"])]


def line(x, y, dx, dy, color=None, dashed=False, width=1):
    return [_common(x, y, abs(dx), abs(dy), type="line",
            strokeColor=color or COL["text"], strokeWidth=width,
            strokeStyle="dashed" if dashed else "solid",
            points=[[0, 0], [dx, dy]], lastCommittedPoint=None,
            startBinding=None, endBinding=None,
            startArrowhead=None, endArrowhead=None)]


def arrow(x1, y1, x2, y2, color=None):
    return [_common(x1, y1, abs(x2 - x1), abs(y2 - y1), type="arrow",
            strokeColor=color or COL["grid"], strokeWidth=1.5,
            points=[[0, 0], [x2 - x1, y2 - y1]], lastCommittedPoint=None,
            startBinding=None, endBinding=None,
            startArrowhead=None, endArrowhead="arrow")]


# --------------------------------------------------------------------------- #
# Diagram 1 — spiral of death (ms ruler, no arrows, ms-labelled steps)
# --------------------------------------------------------------------------- #
def spiral():
    els = []
    x0, y0, rh, gap = 150, 60, 46, 30
    budget_ms, phys_ms = 16, 4
    frames = [("fits — 60 fps held", 8, 1),
              ("Frame N",   10, 2),
              ("Frame N+1", 10, 3),
              ("Frame N+2", 10, 4)]
    n = len(frames)
    maxms = max(r + s * phys_ms for _, r, s in frames)

    for i, (name, rms, steps) in enumerate(frames):
        y = y0 + i * (rh + gap)
        els += text(18, y + rh / 2 - 9, name, fs=14)
        els += rect(x0, y, rms * PX, rh, COL["render"], f"render {rms}ms")
        x = x0 + rms * PX
        for s in range(steps):
            over = (rms + s * phys_ms) >= budget_ms
            els += rect(x, y, phys_ms * PX, rh,
                        COL["over"] if over else COL["phys"], f"{phys_ms}ms")
            x += phys_ms * PX

    # budget line spanning all rows
    bx = x0 + budget_ms * PX
    top, bot = y0 - 16, y0 + (n - 1) * (rh + gap) + rh + 44
    els += line(bx, top, 0, bot - top, color=COL["budget"], dashed=True, width=2)
    els += text(bx - 34, top - 24, "16 ms budget", fs=13, color=COL["budget"])

    # millisecond ruler under the last row
    ry = y0 + n * (rh + gap) - gap + 18
    els += line(x0, ry, maxms * PX, 0, color=COL["grid"])
    for ms in range(0, int(maxms) + 1, 2):
        tx = x0 + ms * PX
        els += line(tx, ry, 0, 9 if ms % 4 == 0 else 5, color=COL["grid"])
        if ms % 4 == 0:
            els += text(tx - 6, ry + 12, str(ms), fs=12, color=COL["grid"])
    els += text(x0 + maxms * PX / 2 - 34, ry + 34, "milliseconds",
                fs=12, color=COL["grid"])
    return els


# --------------------------------------------------------------------------- #
# Diagram 2 — two-thread split (bound-centred text, caption cleared of arrows)
# --------------------------------------------------------------------------- #
def split():
    els = []
    x0, PXs = 150, 8            # 8 px/ms — these blocks are ~16ms wide
    r_ms, p_ms = 1000 / 60, 1000 / 240
    lane_h = 70
    y_r = 60
    y_p = y_r + lane_h + 80
    window = 50

    x, k, r_starts = x0, 0, []
    while (x - x0) / PXs < window:
        r_starts.append(x)
        els += rect(x, y_r, r_ms * PXs - 4, lane_h, COL["render"], f"frame {k}")
        x += r_ms * PXs
        k += 1

    x, p_ends = x0, []
    while (x - x0) / PXs < window:
        els += rect(x, y_p, p_ms * PXs - 3, lane_h, COL["phys"], "")
        p_ends.append(x + p_ms * PXs - 3)
        x += p_ms * PXs
    els += text(x0 + p_ms * PXs * 1.4, y_p + lane_h / 2 - 10,
                "step  step  step ...", fs=13)

    els += text(30, y_r + lane_h / 2 - 18, "Render\n60 Hz", fs=15, align="right")
    els += text(30, y_p + lane_h / 2 - 18, "Physics\n240 Hz", fs=15, align="right")

    # caption sits in the gap ABOVE the arrows (clearance fix)
    els += text(x0 + window * PXs * 0.44, y_r + lane_h + 8,
                "render reads the latest published snapshot",
                fs=13, color=COL["grid"])
    # snapshot-read arrows start below the caption band
    for rs in r_starts[1:]:
        latest = max(e for e in p_ends if e <= rs + 2)
        els += arrow(latest, y_p, rs + (r_ms * PXs - 4) / 2, y_r + lane_h + 34)
    return els


# --------------------------------------------------------------------------- #
# Diagram 1b — spiral as ONE continuous timeline, ideal 16ms slots ghosted
# under it so the cumulative drift (the real spiral) is visible. Healthy
# reference lane below.
# --------------------------------------------------------------------------- #
def spiral_sequential():
    els = []
    x0, pxms = 150, 9           # local px/ms — halved so the figure isn't too wide
    lane_h = 52
    render_ms, phys_ms, budget = 10, 4, 16
    steps_per = [2, 3, 4, 5]    # catch-up steps pile up each frame -> the spiral
    #            ^ every frame already over budget; drift grows monotonically.
    #              try [1,2,3,4] for a gentler ramp, or add rungs for a longer one.
    n = len(steps_per)
    y_spiral = 90
    y_ideal  = y_spiral + lane_h + 18     # tight gap: lanes nearly touch to compare
    total_actual = sum(render_ms + s * phys_ms for s in steps_per)
    maxms = max(budget * n, total_actual)

    # --- ghost fill behind the overload lane (each frame's ideal 16ms slot) ---
    for k in range(n):
        gx = x0 + budget * k * pxms
        els += rect(gx, y_spiral - 6, budget * pxms, lane_h + 12,
                    "#ced4da", None, opacity=25)
    els += text(x0, y_spiral - 38,
                "ghost = ideal 16 ms slots (where each frame SHOULD end)",
                fs=12, color=COL["grid"])

    # --- overload lane: frames end to end, growing (drawn on top of ghost) ---
    els += text(14, y_spiral + lane_h / 2 - 9, "Overload", fs=13)
    x = x0
    for k, s in enumerate(steps_per):
        els += rect(x, y_spiral, render_ms * pxms, lane_h, COL["render"], f"F{k}")
        x += render_ms * pxms
        for _ in range(s):
            over = (x - x0) / pxms + phys_ms > budget * (k + 1)
            els += rect(x, y_spiral, phys_ms * pxms, lane_h,
                        COL["over"] if over else COL["phys"], "")
            x += phys_ms * pxms
    els += text(x + 8, y_spiral + lane_h / 2 - 9, "debt\ngrows →",
                fs=13, color=COL["over"])

    # --- healthy lane directly below (each frame fits its 16ms slot) ---
    els += text(14, y_ideal + lane_h / 2 - 9, "On budget", fs=13)
    for k in range(n):
        gx = x0 + budget * k * pxms
        els += rect(gx, y_ideal, render_ms * pxms, lane_h, COL["render"], f"F{k}")
        els += rect(gx + render_ms * pxms, y_ideal, phys_ms * pxms, lane_h,
                    COL["phys"], "")

    # --- one dashed 16ms grid spanning BOTH lanes: trace a line straight down,
    #     overload pokes past it, on-budget sits inside. That's the comparison. ---
    top, bot = y_spiral - 16, y_ideal + lane_h + 8
    for k in range(n + 1):
        els += line(x0 + budget * k * pxms, top, 0, bot - top,
                    color=COL["grid"], dashed=True)

    # --- ms ruler under everything ---
    ry = bot + 14
    els += line(x0, ry, maxms * pxms, 0, color=COL["grid"])
    for ms in range(0, int(maxms) + 1, 8):
        els += line(x0 + ms * pxms, ry, 0, 8 if ms % 16 == 0 else 5,
                    color=COL["grid"])
        if ms % 16 == 0:
            els += text(x0 + ms * pxms - 6, ry + 11, str(ms), fs=12,
                        color=COL["grid"])
    return els


def save(name, els):
    doc = {"type": "excalidraw", "version": 2, "source": "article2-gen",
           "elements": els,
           "appState": {"viewBackgroundColor": "#ffffff", "gridSize": None},
           "files": {}}
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, name)
    with open(p, "w") as f:
        json.dump(doc, f, indent=2)
    print("wrote", os.path.abspath(p), "-", len(els), "elements")


if __name__ == "__main__":
    save("spiral_of_death.excalidraw", spiral())
    save("spiral_sequential.excalidraw", spiral_sequential())
    save("thread_split.excalidraw", split())
