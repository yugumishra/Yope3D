#!/usr/bin/env python3
"""Article 2 diagrams — the spiral of death, and the two-thread split.

Both diagrams are just labeled blocks laid out on a millisecond time axis, so
everything you'd want to tweak (block widths = ms costs, how many catch-up
steps per frame, the Hz of each lane) lives in the CONSTANTS block up top.
Edit a number, re-run, done — no layout code to touch.

    python3 tools/articleAnalysis/article2_diagrams.py

Outputs two PNGs into site/src/content/blog/images/ (reference them from the
article as ./images/spiral_of_death.png and ./images/thread_split.png).
"""
import os
import matplotlib
matplotlib.use("Agg")                     # headless: no display needed
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyArrowPatch

# --------------------------------------------------------------------------- #
# CONSTANTS — tweak these, not the drawing code
# --------------------------------------------------------------------------- #
OUT_DIR = os.path.join(os.path.dirname(__file__),
                       "..", "..", "site", "src", "content", "blog", "images")
DPI     = 160

# palette (white bg, works on both light/dark article themes when inlined)
C_RENDER  = "#3b82f6"   # blue  — render work
C_PHYS    = "#f59e0b"   # amber — physics steps
C_OVER    = "#ef4444"   # red   — work that spilled past the budget
C_BUDGET  = "#1f2937"   # near-black dashed budget line
C_TEXT_HI = "#ffffff"
C_TEXT_LO = "#111827"
C_MUTED   = "#6b7280"

BUDGET_MS = 16.0        # single-thread frame budget (60 Hz)

# Spiral: each row is one frame. (render_ms, n_physics_steps). Physics step is
# a fixed cost (PHYS_MS) — the debt grows because MORE steps pile up each frame.
PHYS_MS      = 4.0
SPIRAL_FRAMES = [
    ("healthy",  8.0, 1),   # fits with slack
    ("Frame N",  10.0, 2),  # just over
    ("Frame N+1", 10.0, 3), # further over
    ("Frame N+2", 10.0, 4), # runaway
]

# Thread split: lane block widths (ms) and how long a window to draw.
RENDER_HZ   = 60
PHYS_HZ     = 240
WINDOW_MS   = 50.0      # total time span shown


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def block(ax, x, w, y, h, label, color, tcolor=C_TEXT_HI, fs=9):
    ax.add_patch(Rectangle((x, y), w, h, facecolor=color,
                           edgecolor="white", linewidth=1.2, zorder=2))
    if label:
        ax.text(x + w / 2, y + h / 2, label, ha="center", va="center",
                color=tcolor, fontsize=fs, zorder=3)


def budget_line(ax, x, y0, y1):
    ax.plot([x, x], [y0, y1], ls="--", lw=1.4, color=C_BUDGET, zorder=4)
    ax.text(x, y1 + 0.06, f"{BUDGET_MS:.0f} ms budget", ha="center",
            va="bottom", fontsize=8.5, color=C_BUDGET, style="italic")


# --------------------------------------------------------------------------- #
# Diagram 1 — the spiral of death
# --------------------------------------------------------------------------- #
def draw_spiral():
    xmax = max(r + n * PHYS_MS for _, r, n in SPIRAL_FRAMES) + 4
    fig, ax = plt.subplots(figsize=(9.5, 3.4))
    row_h, gap = 0.62, 0.34
    n = len(SPIRAL_FRAMES)

    for i, (name, render_ms, steps) in enumerate(SPIRAL_FRAMES):
        y = (n - 1 - i) * (row_h + gap)
        block(ax, 0, render_ms, y, row_h, f"render {render_ms:.0f}",
              C_RENDER)
        x = render_ms
        for s in range(steps):
            over = x >= BUDGET_MS            # this step spilled past budget
            block(ax, x, PHYS_MS, y, row_h, "phys",
                  C_OVER if over else C_PHYS, fs=8)
            x += PHYS_MS

        tag = "fits — 60 fps held" if name == "healthy" else name
        ax.text(-0.4, y + row_h / 2, tag, ha="right", va="center",
                fontsize=9, color=C_MUTED if name == "healthy" else C_TEXT_LO,
                fontweight="normal" if name == "healthy" else "bold")
        if x > BUDGET_MS:
            ax.annotate("", xy=(x + 0.3, y + row_h / 2),
                        xytext=(BUDGET_MS, y + row_h / 2),
                        arrowprops=dict(arrowstyle="->", color=C_OVER, lw=1.6))

    budget_line(ax, BUDGET_MS, -gap, n * (row_h + gap) - gap + 0.02)
    ax.text(xmax - 0.2, 0 + row_h / 2, "debt grows\neach frame",
            ha="right", va="center", fontsize=8.5, color=C_OVER, style="italic")

    ax.set_xlim(-3.2, xmax)
    ax.set_ylim(-gap - 0.1, n * (row_h + gap))
    ax.axis("off")
    ax.set_title("One thread: catch-up steps push every frame further over budget",
                 fontsize=11, loc="left", color=C_TEXT_LO, pad=8)
    _save(fig, "spiral_of_death.png")


# --------------------------------------------------------------------------- #
# Diagram 2 — the two-thread split
# --------------------------------------------------------------------------- #
def draw_split():
    fig, ax = plt.subplots(figsize=(9.5, 3.2))
    r_ms = 1000.0 / RENDER_HZ                 # ~16.6
    p_ms = 1000.0 / PHYS_HZ                   # ~4.16
    lane_h = 0.9
    y_r, y_p = 1.5, 0.0

    # render lane (wide blocks)
    x, k = 0.0, 0
    render_starts = []
    while x < WINDOW_MS:
        render_starts.append(x)
        block(ax, x, r_ms - 0.3, y_r, lane_h, f"frame {k}", C_RENDER, fs=8.5)
        x += r_ms
        k += 1

    # physics lane (narrow blocks, 4x as many)
    x = 0.0
    phys_ends = []
    while x < WINDOW_MS:
        block(ax, x, p_ms - 0.25, y_p, lane_h, "", C_PHYS)
        phys_ends.append(x + p_ms - 0.25)
        x += p_ms
    ax.text(p_ms * 2, y_p + lane_h / 2, "step  step  step  step ...",
            ha="left", va="center", fontsize=8, color=C_TEXT_HI)

    # snapshot-read arrows: each render frame reads the latest published step
    for rs in render_starts[1:]:
        latest = max(e for e in phys_ends if e <= rs)
        arr = FancyArrowPatch((latest, y_p + lane_h),
                              (rs + (r_ms - 0.3) / 2, y_r),
                              arrowstyle="-|>", mutation_scale=12,
                              lw=1.4, color=C_MUTED, zorder=5,
                              connectionstyle="arc3,rad=-0.15")
        ax.add_patch(arr)
    ax.text(WINDOW_MS * 0.52, (y_r + y_p + lane_h) / 2 + 0.15,
            "render reads the latest\npublished snapshot",
            ha="center", va="center", fontsize=8.5, color=C_MUTED,
            style="italic")

    ax.text(-0.5, y_r + lane_h / 2, f"Render\n{RENDER_HZ} Hz", ha="right",
            va="center", fontsize=9.5, fontweight="bold", color=C_TEXT_LO)
    ax.text(-0.5, y_p + lane_h / 2, f"Physics\n{PHYS_HZ} Hz", ha="right",
            va="center", fontsize=9.5, fontweight="bold", color=C_TEXT_LO)

    ax.set_xlim(-5.0, WINDOW_MS + 1)
    ax.set_ylim(-0.5, y_r + lane_h + 0.9)
    ax.axis("off")
    ax.set_title("Two threads: physics ticks 4x faster, render samples a snapshot",
                 fontsize=11, loc="left", color=C_TEXT_LO, pad=8)
    _save(fig, "thread_split.png")


def _save(fig, name):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.abspath(os.path.join(OUT_DIR, name))
    fig.savefig(path, dpi=DPI, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", path)


if __name__ == "__main__":
    draw_spiral()
    draw_split()
