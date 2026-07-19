#!/usr/bin/env python3
"""Plots for the archetype-ECS article, from yope_ecs_bench CSV output.

Usage:
    ./build/mac-release/yope_ecs_bench tools/articleAnalysis/data
    python3 tools/articleAnalysis/plot_ecs_bench.py

Reads  data/ecs_bench_sweep.csv + data/ecs_bench_migration.csv (sibling dir),
writes plots/ecs_bench_sweep.png + plots/ecs_bench_migration.png (sibling dir,
gitignored). Pass --data-dir / --out-dir to override.
"""

import argparse
import csv
from collections import defaultdict
from pathlib import Path

import math

import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FuncFormatter, NullLocator

# Categorical palette (validated light-mode set; magenta/yellow carry direct
# labels per the contrast relief rule).
SERIES = {
    "view":    ("#2a78d6", "archetype view (engine path)"),
    "get":     ("#008300", "same registry, shuffled get()"),
    "oop":     ("#e87ba4", "pointer-chase OOP objects"),
    "fat_aos": ("#eda100", "fat contiguous objects"),
}
MODES = (
    ("hot", "hot loop — best case for every layout"),
    ("evict", "cache evicted between frames — frame-realistic"),
)
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
SURFACE = "#fcfcfb"
GRID = "#e4e3e0"


def read_csv(path):
    meta, rows = {}, []
    with open(path) as f:
        header = None
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                for tok in line[1:].strip().split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        meta[k] = v
                continue
            if header is None:
                header = line.split(",")
                continue
            rows.append(dict(zip(header, line.split(","))))
    return meta, rows


def fmt_count(n, _pos=None):
    n = int(n)
    if n >= 1_000_000:
        return f"{n // 1_000_000}M"
    if n >= 1_000:
        return f"{n // 1_000}k"
    return str(n)


def style_axes(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9)
    ax.grid(True, axis="y", color=GRID, linewidth=0.7)
    ax.set_axisbelow(True)


def draw_cache_markers(ax, meta):
    # N at which the per-frame hot working set (Transform + Hull + Bounds +
    # matrix slot) exceeds each cache level.
    hot = (int(meta.get("sizeof_transform", 0)) + int(meta.get("sizeof_hull", 0))
           + int(meta.get("sizeof_bounds", 0)) + int(meta.get("sizeof_mat4", 0)))
    y_lo, y_hi = ax.get_ylim()
    # Per-marker label heights in axis space (handles the log y-scale): L1d
    # low to clear the legend, L2 high to clear the evicted panel's crossings.
    for key, label, frac in (("l1d", "hot set = L1d", 0.30),
                             ("l2", "hot set = L2", 0.82)):
        size = int(meta.get(key, 0))
        if size and hot:
            n_at = size / hot
            y_at = ax.transData.inverted().transform(
                ax.transAxes.transform((0, frac)))[1]
            ax.axvline(n_at, color=TEXT_SECONDARY, linestyle=(0, (4, 4)),
                       linewidth=0.9, zorder=2)
            ax.annotate(label, (n_at, y_at),
                        xytext=(4, 0), textcoords="offset points",
                        rotation=90, va="center", fontsize=8.5,
                        color=TEXT_SECONDARY)


def plot_sweep(meta, rows, out_path):
    # series[mode][layout] -> [(n, ns), ...]
    series = defaultdict(lambda: defaultdict(list))
    for r in rows:
        series[r["mode"]][r["layout"]].append((int(r["n"]), float(r["ns_per_entity"])))

    fig, axes = plt.subplots(1, 2, figsize=(13.5, 5.6), dpi=200, sharey=True)
    fig.patch.set_facecolor(SURFACE)

    for ax, (mode, mode_title) in zip(axes, MODES):
        style_axes(ax)
        ax.set_xscale("log")
        # Log y: the working-scale gaps (4k-16k) and the cliff tails differ by
        # 15x — a linear axis flattens the region the article argues from.
        ax.set_yscale("log")
        ends = []
        for name, (color, label) in SERIES.items():
            pts = sorted(series[mode][name])
            if not pts:
                continue
            xs, ys = zip(*pts)
            lw = 2.6 if name == "view" else 1.8
            ax.plot(xs, ys, color=color, linewidth=lw, marker="o",
                    markersize=4.5, label=label, zorder=3)
            ends.append((ys[-1], xs[-1]))
        # Direct label at each line's right end (relief for the low-contrast
        # hues, and the money numbers for the article), nudged apart when
        # neighbors would collide.
        ends.sort()
        log_ys = [math.log10(y) for y, _ in ends]
        span = math.log10(ax.get_ylim()[1]) - math.log10(ax.get_ylim()[0])
        min_gap = 0.045 * span
        for i in range(1, len(log_ys)):
            log_ys[i] = max(log_ys[i], log_ys[i - 1] + min_gap)
        for (y, x), ly in zip(ends, log_ys):
            ax.annotate(f"{y:.0f} ns", (x, 10 ** ly),
                        xytext=(7, 0), textcoords="offset points",
                        va="center", fontsize=8.5, color=TEXT_PRIMARY)
        ticks = [10, 20, 50, 100, 150]
        ax.yaxis.set_major_locator(FixedLocator(ticks))
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:g}"))
        ax.yaxis.set_minor_locator(NullLocator())
        ax.set_title(mode_title, fontsize=10.5, color=TEXT_PRIMARY, loc="left")
        ax.xaxis.set_major_formatter(FuncFormatter(fmt_count))
        ax.set_xlabel("entities", fontsize=10, color=TEXT_PRIMARY)
        ax.margins(x=0.08)

    for ax, _ in zip(axes, MODES):
        draw_cache_markers(ax, meta)

    axes[0].set_ylabel("ns per entity per frame (lower is better)",
                       fontsize=10, color=TEXT_PRIMARY)
    axes[0].legend(loc="upper left", fontsize=9, frameon=False,
                   labelcolor=TEXT_PRIMARY)

    cpu = meta.get("cpu", "").replace("_", " ")
    fig.suptitle("Iteration cost by storage layout — one 3-pass frame "
                 "(integrate · bounds · snapshot)",
                 fontsize=13, color=TEXT_PRIMARY, x=0.01, ha="left")
    if cpu:
        fig.text(0.01, 0.925, f"7-component entities · median of 11 samples · {cpu}",
                 fontsize=9, color=TEXT_SECONDARY)
    fig.subplots_adjust(top=0.85, wspace=0.08)

    fig.savefig(out_path, facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out_path}")


def plot_migration(meta, rows, out_path):
    fig, ax = plt.subplots(figsize=(8, 2.8), dpi=200)
    fig.patch.set_facecolor(SURFACE)
    style_axes(ax)
    ax.grid(True, axis="x", color=GRID, linewidth=0.7)
    ax.grid(False, axis="y")

    labels = {
        "tag_migration": "Sleeping as a tag component\n(2 archetype migrations per toggle,\nall 7 components memcpy-moved)",
        "flag_flip": "Sleeping as Hull.asleep flag\n(2 bool writes per toggle)",
    }
    colors = {"tag_migration": TEXT_SECONDARY, "flag_flip": "#2a78d6"}

    names, values, bar_colors = [], [], []
    for r in rows:
        names.append(labels.get(r["mechanism"], r["mechanism"]))
        values.append(float(r["ns_per_toggle"]))
        bar_colors.append(colors.get(r["mechanism"], TEXT_SECONDARY))

    bars = ax.barh(names, values, color=bar_colors, height=0.5, zorder=3)
    for bar, v in zip(bars, values):
        ax.annotate(f"{v:.1f} ns", (v, bar.get_y() + bar.get_height() / 2),
                    xytext=(6, 0), textcoords="offset points",
                    va="center", fontsize=10, color=TEXT_PRIMARY)

    ax.invert_yaxis()
    ax.tick_params(axis="y", labelsize=9.5, labelcolor=TEXT_PRIMARY)
    ax.set_xlabel("ns per sleep/wake toggle (lower is better)",
                  fontsize=10, color=TEXT_PRIMARY)
    n = rows[0]["n"] if rows else "?"
    ax.set_title(f"Why Sleeping stopped being a tag — toggle cost, {fmt_count(n)} bodies",
                 fontsize=12, color=TEXT_PRIMARY, pad=10, loc="left")
    ax.margins(x=0.12)

    fig.savefig(out_path, facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out_path}")


def main():
    here = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data-dir", type=Path, default=here / "data")
    p.add_argument("--out-dir", type=Path, default=here / "plots")
    args = p.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    meta, rows = read_csv(args.data_dir / "ecs_bench_sweep.csv")
    plot_sweep(meta, rows, args.out_dir / "ecs_bench_sweep.png")

    meta_m, rows_m = read_csv(args.data_dir / "ecs_bench_migration.csv")
    plot_migration(meta_m, rows_m, args.out_dir / "ecs_bench_migration.png")


if __name__ == "__main__":
    main()
