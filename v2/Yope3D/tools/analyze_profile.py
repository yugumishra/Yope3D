#!/usr/bin/env python3
"""
Yope3D Phase E profile analyzer.

Reads one or more yope_profile_*.csv files and produces tables + plots that
answer the Phase E migration questions: which stage dominates, what's the
asymptote per stage, is the cost iteration or work, where does parallelism
collapse, does archetype churn correlate with step-time spikes.

Usage:
    python3 tools/analyze_profile.py profile_runs/*.csv
    python3 tools/analyze_profile.py yope_profile_*.csv -o my_analysis
    python3 tools/analyze_profile.py --no-plots profile_runs/*.csv   # tables only

Input CSV schema (12 columns since the Phase E instrumentation; older 8-column
CSVs are still readable — missing columns become NaN).

Filenames matching `*_N<number>.csv` are auto-tagged with that N for the
scaling sweep. Without N in the filename, object_count is used as a proxy.
"""
from __future__ import annotations

import argparse
import math
import os
import re
import sys
from pathlib import Path
from typing import Iterable
from contextlib import redirect_stdout  # <-- 1. Import this

import numpy as np
import pandas as pd

# Matplotlib is only needed for plots — import lazily so --no-plots works in
# environments without it.
def _mpl():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    return plt


WARMUP_SECONDS = 2.0
DEFAULT_OUT    = "analysis"

# Stages we group together for the stacked-bar physics breakdown.
PHYSICS_STAGES_ORDERED = [
    "entity_list_build_query", "entity_list_build",
    "spring_proxy_sync",
    "broadphase_sap",
    "wheel_raycast",
    "narrowphase_detect",
    "island_build",
    "pgs_dispatch", "pgs_solve", "pgs_wait",
    "integration_query", "integration",
    "spring_update",
    "publish_snapshot",
]
RENDER_STAGES_ORDERED = [
    "script_update", "ui_update", "snapshot_sync",
    "view_lightsource", "view_meshrenderer", "raster_cmdbuffer_record",
    "renderer_drawframe", "total_frame",
]
# GPU-timestamp stages (YOPE_PROF_ENABLED, see Renderer::GpuStage) — actual
# device execution time per pass, not CPU command-buffer recording time. In
# submission order within a frame; raytrace stages are 0 rows unless the
# engine ran in RenderMode::RAYTRACE.
GPU_STAGES_ORDERED = [
    "gpu_shadow_pass", "gpu_skybox", "gpu_scene_meshes",
    "gpu_debug_lines", "gpu_text3d", "gpu_ui_pass",
    "gpu_rt_dispatch", "gpu_rt_blit",
]
NARROWPHASE_STAGES = [
    "nphase_sph_sph", "nphase_sph_aabb", "nphase_sph_obb",
    "nphase_aabb_aabb", "nphase_aabb_obb", "nphase_obb_obb",
]

# Query/work pairs — used by the AoS-vs-SoA plot.
QUERY_WORK_PAIRS = [
    ("entity_list_build_query", "entity_list_build"),
    ("integration_query",       "integration"),
    ("view_meshrenderer",       "raster_cmdbuffer_record"),
]

CSV_COLUMNS = [
    "thread", "step", "stage", "duration_us", "timestamp_s", "scene",
    "object_count", "island_count", "contact_count",
    "archetype_count", "archetype_migrations", "scope_n",
]

# ----------------------------------------------------------------------------
# Stage taxonomy — drives the share% computation. The naïve "sum of all
# stage totals" denominator double-counts parent scopes that contain their
# children, and conflates wall time on the main thread with aggregate CPU
# time across worker threads. Each stage gets a `kind` so the table can
# split share_wall_pct (main-thread budget) from share_cpu_pct (all-thread
# budget).
# ----------------------------------------------------------------------------

# Leaves on the main physics thread — sum of these per step ≈ physics step
# wall time. pgs_solve is treated as a leaf at this granularity (its children
# pgs_dispatch / pgs_wait are sub-breakdowns, not counted in the denominator).
PHYSICS_WALL_LEAVES = {
    "entity_list_build_query", "entity_list_build",
    "spring_proxy_sync",
    "broadphase_sap",
    "wheel_raycast",
    "narrowphase_detect",
    "island_build",
    "pgs_solve",
    "integration_query", "integration",
    "spring_update",
    "publish_snapshot",
}

# Worker-thread per-island scope. Sums across workers — additive to wall
# leaves only when reasoning about total CPU work, not wall time.
PHYSICS_WORKER_CPU = {"pgs_island"}

# Sub-breakdowns of a parent — present in the table but excluded from the
# denominator so they don't double-count.
PHYSICS_SUB_BREAKDOWN = {
    "pgs_dispatch", "pgs_wait",            # children of pgs_solve
    "sap_build", "sap_sort", "sap_sweep",  # decompose broadphase_sap
    "island_id_assign", "island_unionfind",
    "island_partition", "island_entity_cache", "island_wake",
    "island_partition_n", "island_entity_cache_n",  # zero-duration stamps
    *NARROWPHASE_STAGES,                    # decompose narrowphase_detect
}

# Render thread is single-threaded so "wall" and "cpu" are identical.
# Leaves = non-overlapping scopes that together cover the frame.
RENDER_WALL_LEAVES = {
    "script_update", "ui_update",          # children of total_frame
    "snapshot_sync",
    "renderer_drawframe",                  # parent of view_*/raster_*
}
RENDER_SUB_BREAKDOWN = {
    "total_frame",                          # parent of script+ui
    "view_lightsource",                     # child of renderer_drawframe
    "raster_cmdbuffer_record",              # child of renderer_drawframe
    "view_meshrenderer",                    # child of raster_cmdbuffer_record
}

# GPU-timestamp leaves — a *separate* budget from RENDER_WALL_LEAVES. These
# measure actual device execution time, not CPU recording time; they overlap
# in wall-clock with CPU stages (the GPU is still finishing frame N's passes
# while the CPU records frame N+1), so they get their own denominator
# (share_gpu_pct) rather than being summed into the CPU render wall budget.
GPU_WALL_LEAVES = set(GPU_STAGES_ORDERED)


def classify_stage(stage: str, thread: str) -> str:
    if thread == "physics":
        if stage in PHYSICS_WALL_LEAVES:    return "main_wall_leaf"
        if stage in PHYSICS_WORKER_CPU:     return "worker_cpu"
        if stage in PHYSICS_SUB_BREAKDOWN:  return "sub_breakdown"
    elif thread == "render":
        if stage in GPU_WALL_LEAVES:        return "gpu_wall_leaf"
        if stage in RENDER_WALL_LEAVES:     return "main_wall_leaf"
        if stage in RENDER_SUB_BREAKDOWN:   return "sub_breakdown"
    return "other"


# ============================================================================
# Loading
# ============================================================================

_N_PAT     = re.compile(r"_N(\d+)", re.IGNORECASE)
# Shape + optional scenario suffix from the sweep harness:
#   yope_profile_N<N>_<shape>.csv              (grid runs — legacy naming kept)
#   yope_profile_N<N>_<shape>_<scenario>.csv   (other scenarios, e.g. funnel)
# Shapes recognized by scripts/behaviors/stress_test.py; unrecognized
# suffixes (or no suffix at all — legacy single-shape sweeps) fall through
# to "sphere" as a sensible default since that was the original behavior.
_SHAPE_PAT = re.compile(r"_N\d+_(sphere|aabb|obb|mixed)(?:_([a-z]+))?\.csv$",
                        re.IGNORECASE)


def n_from_filename(path: str) -> int | None:
    m = _N_PAT.search(os.path.basename(path))
    return int(m.group(1)) if m else None


def shape_from_filename(path: str) -> str:
    m = _SHAPE_PAT.search(os.path.basename(path))
    return m.group(1).lower() if m else "sphere"


def scenario_from_filename(path: str) -> str:
    m = _SHAPE_PAT.search(os.path.basename(path))
    return m.group(2).lower() if m and m.group(2) else "grid"


def load_one(path: str) -> pd.DataFrame:
    """Read a single CSV, pad missing columns, tag with source/N/shape."""
    df = pd.read_csv(path)
    for col in CSV_COLUMNS:
        if col not in df.columns:
            df[col] = np.nan
    # Coerce numerics in case the header column names accidentally landed
    # somewhere in the data (the profiler has guards but be defensive).
    for col in ("duration_us", "timestamp_s",
                "object_count", "island_count", "contact_count",
                "archetype_count", "archetype_migrations", "scope_n", "step"):
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=["duration_us", "timestamp_s", "stage"])
    df["source_file"] = os.path.basename(path)
    n = n_from_filename(path)
    df["N"] = n if n is not None else df["object_count"].median()
    df["shape"] = shape_from_filename(path)
    df["scenario"] = scenario_from_filename(path)
    return df


def load_all(paths: Iterable[str]) -> pd.DataFrame:
    dfs = [load_one(p) for p in paths]
    if not dfs:
        raise SystemExit("no input CSVs")
    df = pd.concat(dfs, ignore_index=True)
    # Warmup filter.
    before = len(df)
    df = df[df["timestamp_s"] >= WARMUP_SECONDS].copy()
    print(f"loaded {len(dfs)} CSV(s); {before:,} rows -> {len(df):,} after warmup filter "
          f"(>{WARMUP_SECONDS:.1f}s)")
    return df


# ============================================================================
# Tables
# ============================================================================

def stage_summary(df: pd.DataFrame) -> pd.DataFrame:
    """Per-(scene, stage) quantile + share-of-total table.

    Reports two share columns:
      share_wall_pct  — numerator: stage total_us
                        denominator: sum of `main_wall_leaf` stages on
                        the same thread (the actual frame/step budget).
                        For pgs_island this is intentionally >100% because
                        worker-thread CPU time isn't part of wall budget.
      share_cpu_pct   — numerator: stage total_us
                        denominator: sum of `main_wall_leaf` + `worker_cpu`.
                        For pgs_island this is ≤100% and meaningful.

    `kind` column labels each row so you can filter:
      main_wall_leaf   — counts toward physics-step / render-CPU wall time
      worker_cpu       — runs in parallel on a worker thread
      gpu_wall_leaf     — GPU-timestamp pass (thread=="render"); own budget,
                          see share_gpu_pct — NOT part of share_wall_pct,
                          since GPU execution overlaps CPU recording across
                          frames rather than being a CPU wall-clock leaf
      sub_breakdown    — child of another scope; not summed in denominators
      other            — unrecognized stage (e.g. nphase_* on an old build)
    """
    g = df.groupby(["source_file", "scene", "stage", "thread"])
    summary = g["duration_us"].agg(
        n="count",
        mean_us="mean",
        p50_us=lambda s: s.quantile(0.50),
        p95_us=lambda s: s.quantile(0.95),
        p99_us=lambda s: s.quantile(0.99),
        max_us="max",
    ).reset_index()

    summary["total_us"] = summary["mean_us"] * summary["n"]
    summary["kind"] = summary.apply(
        lambda r: classify_stage(r["stage"], r["thread"]), axis=1)

    # Denominators per (source_file, thread).
    leaf_mask  = summary["kind"] == "main_wall_leaf"
    cpu_mask   = summary["kind"].isin(["main_wall_leaf", "worker_cpu"])
    gpu_mask   = summary["kind"] == "gpu_wall_leaf"

    leaf_totals = (summary.loc[leaf_mask]
                   .groupby(["source_file", "thread"])["total_us"].sum()
                   .rename("wall_denom").reset_index())
    cpu_totals  = (summary.loc[cpu_mask]
                   .groupby(["source_file", "thread"])["total_us"].sum()
                   .rename("cpu_denom").reset_index())
    gpu_totals  = (summary.loc[gpu_mask]
                   .groupby(["source_file", "thread"])["total_us"].sum()
                   .rename("gpu_denom").reset_index())

    summary = summary.merge(leaf_totals, on=["source_file", "thread"], how="left")
    summary = summary.merge(cpu_totals,  on=["source_file", "thread"], how="left")
    summary = summary.merge(gpu_totals,  on=["source_file", "thread"], how="left")

    summary["share_wall_pct"] = 100.0 * summary["total_us"] / summary["wall_denom"]
    summary["share_cpu_pct"]  = 100.0 * summary["total_us"] / summary["cpu_denom"]
    summary["share_gpu_pct"]  = 100.0 * summary["total_us"] / summary["gpu_denom"]
    summary = summary.drop(columns=["wall_denom", "cpu_denom", "gpu_denom"])

    return summary.sort_values(
        ["source_file", "thread", "share_wall_pct"],
        ascending=[True, True, False],
        na_position="last",
    )


def parallel_efficiency(df: pd.DataFrame) -> pd.DataFrame:
    """Per-file parallel efficiency of the PGS solver.

    For each physics step compute:
        effective_workers = sum(pgs_island.duration_us) / pgs_solve.duration_us

    Interpretation:
      ≈ hardware_concurrency - 1   — full parallel utilization
      < that                       — load imbalance (one big island dominates)
                                     or thread-pool overhead
      ≈ 1.0                        — solver is effectively serial

    Reports mean, p5, p95, plus solver share of physics step wall time.
    """
    isl = df[(df["thread"] == "physics") & (df["stage"] == "pgs_island")]
    sol = df[(df["thread"] == "physics") & (df["stage"] == "pgs_solve")]
    if isl.empty or sol.empty:
        return pd.DataFrame(columns=["source_file"])

    isl_per_step = isl.groupby(["source_file", "step"])["duration_us"].sum().rename("island_cpu_us")
    sol_per_step = sol.groupby(["source_file", "step"])["duration_us"].first().rename("solve_wall_us")
    joined = pd.concat([isl_per_step, sol_per_step], axis=1).dropna()
    joined = joined[joined["solve_wall_us"] > 0]
    joined["effective_workers"] = joined["island_cpu_us"] / joined["solve_wall_us"]

    out = joined.groupby("source_file").agg(
        steps             =("effective_workers", "count"),
        eff_workers_mean  =("effective_workers", "mean"),
        eff_workers_p5    =("effective_workers", lambda s: s.quantile(0.05)),
        eff_workers_p95   =("effective_workers", lambda s: s.quantile(0.95)),
        solve_wall_mean_us=("solve_wall_us", "mean"),
        island_cpu_mean_us=("island_cpu_us", "mean"),
    ).reset_index()
    return out


def physics_step_totals(df: pd.DataFrame) -> pd.DataFrame:
    """Sum all physics-thread stage durations per (file, step) to get
    'physics step total' — gives a CDF/time-series of the physics tick cost
    that doesn't exist as a single scope."""
    phys = df[(df["thread"] == "physics") &
              (~df["stage"].str.startswith("nphase_")) &
              (df["stage"] != "pgs_island")]
    return phys.groupby(["source_file", "scene", "N", "step"]).agg(
        physics_step_us=("duration_us", "sum"),
        object_count   =("object_count", "max"),
        contact_count  =("contact_count", "max"),
        island_count   =("island_count", "max"),
        archetype_count=("archetype_count", "max"),
        archetype_migrations=("archetype_migrations", "max"),
        ts             =("timestamp_s", "min"),
    ).reset_index()


def gpu_frame_totals(df: pd.DataFrame) -> pd.DataFrame:
    """Sum GPU-timestamp stages (see GPU_STAGES_ORDERED) per (file, step) —
    the actual measured GPU time per frame, distinct from total_frame (CPU
    wall clock) or renderer_drawframe (CPU submission cost). Empty unless
    the CSV was captured with YOPE_PROF_ENABLED and real GPU timestamp
    support (see Renderer::createGpuTimestampPool)."""
    gpu = df[(df["thread"] == "render") & (df["stage"].isin(GPU_STAGES_ORDERED))]
    if gpu.empty:
        return pd.DataFrame(columns=["source_file", "scene", "step", "gpu_frame_us", "ts"])
    return gpu.groupby(["source_file", "scene", "step"]).agg(
        gpu_frame_us=("duration_us", "sum"),
        ts          =("timestamp_s", "min"),
    ).reset_index()


# ============================================================================
# Plots
# ============================================================================

# (thread, kind, share_col, file_prefix, title, xlabel) — one breakdown chart
# per track. gpu_wall_leaf is its own track (see GPU_WALL_LEAVES) since it's
# a separate device-time budget, not summed into the CPU render wall budget.
_BREAKDOWN_TRACKS = [
    ("physics", "main_wall_leaf", "share_wall_pct", "breakdown",
     "physics stage breakdown", "mean µs per physics step (wall, main thread)"),
    ("render", "main_wall_leaf", "share_wall_pct", "breakdown_rendercpu",
     "render-thread CPU stage breakdown", "mean µs per frame (CPU wall, render thread)"),
    ("render", "gpu_wall_leaf", "share_gpu_pct", "breakdown_gpu",
     "GPU pass breakdown (device timestamps)", "mean µs per frame (GPU execution time)"),
]


def plot_stage_breakdown(summary: pd.DataFrame, outdir: Path) -> None:
    """Per-file stacked bar: mean stage time as a fraction of total, one
    chart per track (physics / render CPU / GPU passes)."""
    plt = _mpl()
    for thread, kind, share_col, prefix, title, xlabel in _BREAKDOWN_TRACKS:
        for src, g in summary.groupby("source_file"):
            # pgs_island belongs in its own conversation (parallel-efficiency table).
            sub = (g[(g["thread"] == thread) & (g["kind"] == kind)]
                   .sort_values(share_col, ascending=True))
            if sub.empty:
                continue
            fig, ax = plt.subplots(figsize=(8, 5))
            ax.barh(sub["stage"], sub["mean_us"])
            for i, (stage, share) in enumerate(zip(sub["stage"], sub[share_col])):
                ax.text(sub["mean_us"].iloc[i], i, f"  {share:5.1f}%", va="center", fontsize=8)
            ax.set_xlabel(xlabel)
            ax.set_title(f"{title} — {src}")
            fig.tight_layout()
            out = outdir / f"{prefix}_{Path(src).stem}.png"
            fig.savefig(out, dpi=120)
            plt.close(fig)
            print(f"  wrote {out}")


def plot_scaling_loglog(steptotals: pd.DataFrame, df: pd.DataFrame, outdir: Path) -> None:
    """Log-log of mean stage time vs N. Fits a power-law per stage; legend
    shows exponent so you can pick out the worst-scaling stages at a glance."""
    plt = _mpl()
    # Group by stage, N — mean duration_us. Only include physics-thread
    # non-nphase stages (nphase is its own plot).
    phys = df[(df["thread"] == "physics") &
              (~df["stage"].str.startswith("nphase_"))]
    pivot = phys.groupby(["stage", "N"])["duration_us"].mean().unstack("stage")
    if pivot.empty or len(pivot) < 2:
        print("  (skipping scaling plot — need at least 2 N values)")
        return

    fig, ax = plt.subplots(figsize=(9, 6))
    ns = pivot.index.values.astype(float)
    for stage in pivot.columns:
        ys = pivot[stage].values
        valid = ~np.isnan(ys) & (ys > 0)
        if valid.sum() < 2:
            continue
        x_log = np.log(ns[valid])
        y_log = np.log(ys[valid])
        slope, intercept = np.polyfit(x_log, y_log, 1)
        ax.plot(ns[valid], ys[valid], marker="o", label=f"{stage}  (α≈{slope:.2f})")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("N (entities)")
    ax.set_ylabel("mean duration µs")
    ax.set_title("scaling per stage (slope = power-law exponent)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=7, loc="upper left", ncol=2)
    fig.tight_layout()
    out = outdir / "scaling_loglog.png"
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"  wrote {out}")


def plot_query_vs_work(df: pd.DataFrame, outdir: Path) -> None:
    """Side-by-side bar of *_query vs *_full mean µs, per source file.
    A large gap = work-bound; a small gap = iteration-bound (SoA candidate)."""
    plt = _mpl()
    means = df.groupby(["source_file", "stage"])["duration_us"].mean().unstack("stage")
    rows = []
    for query, full in QUERY_WORK_PAIRS:
        if query not in means.columns or full not in means.columns:
            continue
        for src in means.index:
            rows.append({
                "source_file": src,
                "pair": full,
                "query_us":  means.at[src, query] if query in means.columns else np.nan,
                "full_us":   means.at[src, full],
            })
    if not rows:
        print("  (skipping query-vs-work — no paired stages found)")
        return
    qw = pd.DataFrame(rows)
    qw["work_us"] = qw["full_us"] - qw["query_us"]
    fig, ax = plt.subplots(figsize=(9, 5))
    labels = [f"{s}\n{p}" for s, p in zip(qw["source_file"], qw["pair"])]
    x = np.arange(len(labels))
    ax.bar(x, qw["query_us"], label="query (iteration only)")
    ax.bar(x, qw["work_us"],  bottom=qw["query_us"], label="work (math + lookups)")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=45, ha="right", fontsize=7)
    ax.set_ylabel("mean µs per step")
    ax.set_title("query vs. work — iteration cost as a fraction of total")
    ax.legend()
    fig.tight_layout()
    out = outdir / "query_vs_work.png"
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"  wrote {out}")


def plot_island_distribution(df: pd.DataFrame, outdir: Path) -> None:
    """Histogram of pgs_island sizes + scatter of solve time vs island size."""
    plt = _mpl()
    iso = df[df["stage"] == "pgs_island"]
    if iso.empty:
        print("  (skipping island plot — no pgs_island records; was A2 instrumentation applied?)")
        return
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))

    iso["scope_n"].dropna().astype(int).hist(ax=ax1, bins=50)
    ax1.set_xlabel("island size (contacts)")
    ax1.set_ylabel("island count")
    ax1.set_title("island size distribution")
    ax1.set_yscale("log")

    sample = iso.sample(min(len(iso), 5000), random_state=0)
    ax2.scatter(sample["scope_n"], sample["duration_us"], alpha=0.2, s=8)
    # O(s²) reference line through the median point.
    ss = sample["scope_n"].astype(float)
    if (ss > 0).any():
        med_s = float(ss[ss > 0].median())
        med_t = float(sample.loc[ss > 0, "duration_us"].median())
        xs = np.linspace(1, ss.max(), 100)
        k = med_t / (med_s * med_s) if med_s > 0 else 0.0
        ax2.plot(xs, k * xs * xs, color="red", linestyle="--", label="O(s²) reference")
        ax2.legend()
    ax2.set_xlabel("island size (contacts)")
    ax2.set_ylabel("solve time µs")
    ax2.set_title("per-island solve time")
    ax2.set_xscale("log")
    ax2.set_yscale("log")
    fig.tight_layout()
    out = outdir / "island_distribution.png"
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"  wrote {out}")


def plot_frametime_cdf(df: pd.DataFrame, outdir: Path) -> None:
    """CDF of total_frame durations per source file. Tail = hitches."""
    plt = _mpl()
    tf = df[df["stage"] == "total_frame"]
    if tf.empty:
        return
    fig, ax = plt.subplots(figsize=(8, 5))
    for src, g in tf.groupby("source_file"):
        d = np.sort(g["duration_us"].values)
        y = np.arange(1, len(d) + 1) / len(d)
        ax.plot(d, y, label=src)
    ax.set_xscale("log")
    ax.set_xlabel("frame time µs")
    ax.set_ylabel("cumulative probability")
    ax.set_title("frame-time CDF (look at the right tail for hitches)")
    ax.axhline(0.99, color="grey", linestyle="--", alpha=0.4)
    ax.text(ax.get_xlim()[0], 0.99, " p99", fontsize=8, va="bottom")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    out = outdir / "frametime_cdf.png"
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"  wrote {out}")


def plot_gpu_frametime_cdf(gputotals: pd.DataFrame, outdir: Path) -> None:
    """CDF of per-frame summed GPU-pass time (see gpu_frame_totals) — the
    actual device-time budget consumed each frame, distinct from
    frametime_cdf.png's CPU total_frame. Empty unless captured with
    YOPE_PROF_ENABLED on a device with timestamp query support."""
    plt = _mpl()
    if gputotals.empty:
        print("  (skipping GPU frametime CDF — no gpu_* stages in CSV; "
              "requires -DYOPE_ENABLE_PROFILER=ON + timestamp query support)")
        return
    fig, ax = plt.subplots(figsize=(8, 5))
    for src, g in gputotals.groupby("source_file"):
        d = np.sort(g["gpu_frame_us"].values)
        y = np.arange(1, len(d) + 1) / len(d)
        ax.plot(d, y, label=src)
    ax.set_xscale("log")
    ax.set_xlabel("summed GPU pass time µs (per frame)")
    ax.set_ylabel("cumulative probability")
    ax.set_title("GPU frame-time CDF (real device timestamps)")
    for frac, fps in ((1_000_000/120, "120fps"), (1_000_000/60, "60fps")):
        ax.axvline(frac, color="grey", linestyle="--", alpha=0.4)
        ax.text(frac, 0.02, f" {fps} budget", fontsize=7, rotation=90, va="bottom")
    ax.axhline(0.99, color="grey", linestyle="--", alpha=0.4)
    ax.text(ax.get_xlim()[0], 0.99, " p99", fontsize=8, va="bottom")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    out = outdir / "gpu_frametime_cdf.png"
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"  wrote {out}")


def plot_steptime_series(steptotals: pd.DataFrame, outdir: Path) -> None:
    """Physics-step time vs wall-clock, overlaid with archetype_migrations
    delta. Spikes correlated with migration spikes indicate churn-driven
    hitches."""
    plt = _mpl()
    for src, g in steptotals.groupby("source_file"):
        g = g.sort_values("step").copy()
        g["mig_delta"] = g["archetype_migrations"].diff().fillna(0).clip(lower=0)
        fig, ax1 = plt.subplots(figsize=(11, 4))
        ax1.plot(g["ts"], g["physics_step_us"], lw=0.6, alpha=0.8, color="C0",
                 label="physics step µs")
        # Rolling p99.
        roll = g["physics_step_us"].rolling(60, min_periods=10).quantile(0.99)
        ax1.plot(g["ts"], roll, color="C3", lw=1.2, label="rolling p99 (60-step window)")
        ax1.set_xlabel("wall time s")
        ax1.set_ylabel("physics step µs", color="C0")
        ax1.set_yscale("log")
        ax2 = ax1.twinx()
        ax2.bar(g["ts"], g["mig_delta"], width=0.05, color="C2", alpha=0.5,
                label="archetype migrations / step")
        ax2.set_ylabel("migrations / step", color="C2")
        ax1.set_title(f"physics step time + archetype churn — {src}")
        fig.tight_layout()
        out = outdir / f"steptime_series_{Path(src).stem}.png"
        fig.savefig(out, dpi=120)
        plt.close(fig)
        print(f"  wrote {out}")


def plot_crossthread(df: pd.DataFrame, steptotals: pd.DataFrame, outdir: Path) -> None:
    """Render frame time + summed physics step time on shared time axis."""
    plt = _mpl()
    tf = df[df["stage"] == "total_frame"]
    if tf.empty:
        return
    for src in tf["source_file"].unique():
        rend = tf[tf["source_file"] == src].sort_values("timestamp_s")
        phys = steptotals[steptotals["source_file"] == src].sort_values("ts")
        if rend.empty or phys.empty:
            continue
        fig, ax = plt.subplots(figsize=(11, 4))
        ax.plot(rend["timestamp_s"], rend["duration_us"], lw=0.6, alpha=0.7,
                color="C0", label="render frame µs")
        ax.plot(phys["ts"], phys["physics_step_us"], lw=0.6, alpha=0.7,
                color="C3", label="physics step µs (summed)")
        ax.set_xlabel("wall time s")
        ax.set_ylabel("µs")
        ax.set_yscale("log")
        ax.set_title(f"cross-thread overlay — {src}")
        ax.legend()
        fig.tight_layout()
        out = outdir / f"crossthread_{Path(src).stem}.png"
        fig.savefig(out, dpi=120)
        plt.close(fig)
        print(f"  wrote {out}")


def plot_narrowphase_mix(df: pd.DataFrame, outdir: Path) -> None:
    """Per shape-pair mean µs + count. Reveals which dispatch entries
    dominate at the largest N — feeds the OBB/AABB optimization question."""
    plt = _mpl()
    nphase = df[df["stage"].isin(NARROWPHASE_STAGES)]
    if nphase.empty:
        return
    # Take the largest-N run if multiple.
    largest_n = nphase["N"].max()
    g = nphase[nphase["N"] == largest_n].groupby("stage").agg(
        mean_us=("duration_us", "mean"),
        mean_n =("scope_n",     "mean"),
    ).reset_index().sort_values("mean_us", ascending=True)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4))
    ax1.barh(g["stage"], g["mean_us"])
    ax1.set_xlabel("mean µs per step")
    ax1.set_title(f"narrowphase µs by pair (N={int(largest_n) if not math.isnan(largest_n) else '?'})")
    ax2.barh(g["stage"], g["mean_n"], color="C1")
    ax2.set_xlabel("mean pair count per step")
    ax2.set_title("narrowphase pair counts")
    fig.tight_layout()
    out = outdir / "narrowphase_mix.png"
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"  wrote {out}")


# ============================================================================
# Reports — named text printouts that can be filtered via --reports.
# Each function takes (df, summary, par_eff, outdir) for uniformity even
# when it doesn't use all of them. Returns None.
# ============================================================================

NUM_WORKERS_DEFAULT = 9  # hardware_concurrency()-1 on M-series; override via --workers

# Query/work pair stages from A1 instrumentation. Used by report_pairs.
PAIRS_PHYSICS_SIBLINGS = [
    # (query_stage, full_stage) — sibling scopes; work = full - query.
    ("entity_list_build_query", "entity_list_build"),
    ("integration_query",       "integration"),
]
PAIRS_RENDER_NESTED = [
    # (child_query_stage, parent_stage) — child is inside parent;
    # work = parent - child.
    ("view_meshrenderer", "raster_cmdbuffer_record"),
]


def _slopes(Ns, values):
    """Power-law slopes between consecutive pairs. Returns list of slopes
    of length len(Ns)-1; entries are NaN where either endpoint is non-positive."""
    out = []
    for i in range(1, len(Ns)):
        a, b = values[i-1], values[i]
        if a > 0 and b > 0:
            out.append(math.log(b/a) / math.log(Ns[i]/Ns[i-1]))
        else:
            out.append(float("nan"))
    return out


def report_top5_wall(df, summary, par_eff, outdir):
    """Top-5 main-thread wall stages per file — the physics step budget.
    pgs_island is excluded because it runs on workers, not the main budget."""
    print("\n=== top5_wall: top 5 PHYSICS main-thread WALL stages per file ===")
    print("    share_wall_pct = fraction of physics-step wall time on main thread")
    phys = summary[(summary["thread"] == "physics") &
                   (summary["kind"] == "main_wall_leaf")]
    top = (phys.sort_values(["source_file", "share_wall_pct"], ascending=[True, False])
           .groupby("source_file").head(5))
    print(top[["source_file", "stage", "mean_us", "p99_us", "share_wall_pct"]]
          .to_string(index=False, float_format=lambda x: f"{x:9.2f}"))


def report_top5_cpu(df, summary, par_eff, outdir):
    """Top-5 stages by aggregate CPU share, including worker threads.
    Answers 'where does the actual work happen' regardless of threading."""
    print("\n=== top5_cpu: top 5 PHYSICS stages by aggregate CPU share (incl. workers) ===")
    print("    share_cpu_pct = fraction of (main wall + worker CPU) per file")
    phys_cpu = summary[(summary["thread"] == "physics") &
                       (summary["kind"].isin(["main_wall_leaf", "worker_cpu"]))]
    top_cpu = (phys_cpu.sort_values(["source_file", "share_cpu_pct"],
                                    ascending=[True, False])
               .groupby("source_file").head(5))
    print(top_cpu[["source_file", "stage", "kind", "mean_us", "p99_us", "share_cpu_pct"]]
          .to_string(index=False, float_format=lambda x: f"{x:9.2f}"))


def report_top5_render_wall(df, summary, par_eff, outdir):
    """Top-5 render-thread CPU wall stages per file — script/UI/submission cost.
    This is CPU command-buffer recording time, not GPU execution time —
    see top5_gpu for actual per-pass device cost."""
    print("\n=== top5_render_wall: top 5 RENDER-THREAD CPU wall stages per file ===")
    print("    share_wall_pct = fraction of render-thread CPU wall time per file")
    rend = summary[(summary["thread"] == "render") &
                   (summary["kind"] == "main_wall_leaf")]
    if rend.empty:
        print("    SKIPPED — no render-thread CPU wall stages found "
              "(script_update/ui_update/snapshot_sync/renderer_drawframe)")
        return
    top = (rend.sort_values(["source_file", "share_wall_pct"], ascending=[True, False])
           .groupby("source_file").head(5))
    print(top[["source_file", "stage", "mean_us", "p99_us", "share_wall_pct"]]
          .to_string(index=False, float_format=lambda x: f"{x:9.2f}"))


def report_top5_gpu(df, summary, par_eff, outdir):
    """Top GPU passes by device execution time per file (YOPE_PROF_ENABLED
    GPU timestamp queries — see Renderer::GpuStage). This is actual GPU ms,
    not CPU recording time; use it to see where a frame's device budget
    actually goes before adding a new pass (e.g. post-processing)."""
    print("\n=== top5_gpu: GPU pass execution time per file (real device timestamps) ===")
    print("    share_gpu_pct = fraction of summed GPU-pass time per file "
          "(its own budget, separate from CPU wall time)")
    gpu = summary[(summary["thread"] == "render") &
                  (summary["kind"] == "gpu_wall_leaf")]
    if gpu.empty:
        print("    SKIPPED — no gpu_* stages found. Requires a build configured with "
              "-DYOPE_ENABLE_PROFILER=ON on a device with timestamp query support "
              "(see Renderer::createGpuTimestampPool).")
        return
    top = (gpu.sort_values(["source_file", "share_gpu_pct"], ascending=[True, False])
           .groupby("source_file").head(len(GPU_STAGES_ORDERED)))
    print(top[["source_file", "stage", "mean_us", "p99_us", "max_us", "share_gpu_pct"]]
          .to_string(index=False, float_format=lambda x: f"{x:9.2f}"))
    frame_budget_us = 1_000_000.0 / 120.0
    print(f"\n    (for reference: 120fps frame budget = {frame_budget_us:.0f}us; "
          f"60fps = {1_000_000.0/60.0:.0f}us)")


def report_parallel(df, summary, par_eff, outdir):
    """PGS solver parallel efficiency — effective workers per step."""
    if par_eff.empty:
        print("\n=== parallel: SKIPPED — no pgs_island scopes found in CSV ===")
        return
    print("\n=== parallel: PGS parallel efficiency per file ===")
    print("    effective_workers = sum(pgs_island µs) / pgs_solve µs")
    print("    near hardware_concurrency-1 = fully parallel; near 1.0 = serialized")
    print(par_eff.to_string(index=False, float_format=lambda x: f"{x:9.2f}"))


def report_pairs(df, summary, par_eff, outdir):
    """Query-vs-work analysis for A1 sentinel pairs.

    For each pair across all N values, prints:
      query µs (sentinel walk only), full µs (parent scope), work µs (delta),
      query/full ratio, per-entity work cost.

    Then prints scaling slopes for query and work columns to reveal whether
    iteration cost is degrading with N (slope > 1 = cache misses growing
    super-linearly; slope ≈ 1 = scaling cleanly).

    Use this to answer §5.9 AoS-vs-SoA: high query ratio → iteration-bound,
    low ratio → work-bound. If slopes diverge with N, that's a different
    problem (cache or random-access pattern degrading).
    """
    pairs_all = (
        [(q, f, "physics", "siblings")  for q, f in PAIRS_PHYSICS_SIBLINGS] +
        [(q, f, "render",  "nested")    for q, f in PAIRS_RENDER_NESTED]
    )

    print("\n=== pairs: query-vs-work breakdown per pair, per N ===")
    print("    siblings: query and full are independent scopes; work = full - query")
    print("    nested:   query is INSIDE parent; work = parent - query")
    print(f"\n{'N':>6}  {'pair':30}  {'query µs':>10}  {'full µs':>10}  "
          f"{'work µs':>10}  {'query/full':>11}  {'work/N µs':>11}")
    print("-" * 110)

    # Collect per-N values for the scaling-slope summary below.
    table = {}  # (full_stage, N) -> (query, full)
    for src in sorted(df["source_file"].unique(),
                      key=lambda s: df.loc[df.source_file == s, "N"].iloc[0]):
        sub = df[df.source_file == src]
        N = int(sub["N"].iloc[0]) if not sub["N"].empty else 0
        for q_stage, f_stage, thread, kind in pairs_all:
            q = sub[(sub.thread == thread) & (sub.stage == q_stage)]["duration_us"].mean()
            ful = sub[(sub.thread == thread) & (sub.stage == f_stage)]["duration_us"].mean()
            if math.isnan(q) or math.isnan(ful):
                continue
            work = ful - q
            ratio = q / ful if ful > 0 else 0.0
            work_per_N = work / N if N > 0 else 0.0
            table[(f_stage, N)] = (q, ful)
            print(f"{N:>6}  {f_stage:30}  {q:>10.1f}  {ful:>10.1f}  "
                  f"{work:>10.1f}  {ratio*100:>10.1f}%  {work_per_N:>10.4f}")
        print()

    # Scaling slopes per pair.
    print(f"--- power-law slopes per pair (consecutive N) ---")
    print(f"{'pair':30}  {'series':>8}  slopes")
    print("-" * 80)
    for q_stage, f_stage, thread, kind in pairs_all:
        pts = sorted([(N, q, ful) for (s, N), (q, ful) in table.items() if s == f_stage])
        if len(pts) < 2:
            continue
        Ns  = [p[0] for p in pts]
        qs  = [p[1] for p in pts]
        ws  = [p[2] - p[1] for p in pts]
        q_slopes = _slopes(Ns, qs)
        w_slopes = _slopes(Ns, ws)
        print(f"{f_stage:30}  {'query':>8}  " +
              " ".join(f"{s:5.2f}" for s in q_slopes))
        print(f"{'':30}  {'work':>8}  " +
              " ".join(f"{s:5.2f}" for s in w_slopes))


def report_scaling(df, summary, par_eff, outdir):
    """Per-stage scaling table: mean µs at each N, plus power-law slopes
    between consecutive N pairs. The same shape as the broadphase /
    island_build inline extractions we used earlier."""
    # Determine which stages to include. Skip stages with insufficient data
    # (need at least 2 N values), and stages with all-zero duration (e.g.
    # nphase_aabb_aabb in spheres-only stress test).
    Ns = sorted(df["N"].dropna().unique())
    Ns = [int(n) for n in Ns]
    if len(Ns) < 2:
        print("\n=== scaling: SKIPPED — need at least 2 N values to compute slopes ===")
        return
    print(f"\n=== scaling: per-stage mean µs across N + power-law slopes ===")
    print(f"    N values: {Ns}")
    # Aggregate mean µs per (stage, N) only for physics-thread stages.
    phys = df[df["thread"] == "physics"]
    pivot = phys.groupby(["stage", "N"])["duration_us"].mean().unstack("N")
    # Keep only stages that have at least one nonzero value and at least two
    # data points (so we can compute a slope).
    pivot = pivot.loc[(pivot.fillna(0).sum(axis=1) > 0) & (pivot.notna().sum(axis=1) >= 2)]
    # Order rows by mean µs at the largest N — heaviest stages on top.
    largest_N = Ns[-1]
    if largest_N in pivot.columns:
        pivot = pivot.sort_values(largest_N, ascending=False)

    # Header: stage name + one column per N + slope columns.
    n_hdr = "  ".join(f"{n:>10}" for n in Ns)
    slope_hdr = "  ".join(f"{f'{Ns[i-1]}→{Ns[i]}':>10}"
                          for i in range(1, len(Ns)))
    print(f"\n{'stage':28}  {n_hdr}  | slopes:  {slope_hdr}")
    print("-" * (28 + 12 * len(Ns) + 12 + 12 * (len(Ns) - 1) + 4))
    for stage, row in pivot.iterrows():
        vals = [row.get(n, float("nan")) for n in Ns]
        slopes = _slopes(Ns, vals)
        vals_str   = "  ".join(f"{v:>10.1f}" if not math.isnan(v) else f"{'—':>10}"
                                for v in vals)
        slopes_str = "  ".join(f"{s:>10.2f}" if not math.isnan(s) else f"{'—':>10}"
                                for s in slopes)
        print(f"{stage:28}  {vals_str}  | slopes:  {slopes_str}")


def report_load_balance(df, summary, par_eff, outdir, num_workers=NUM_WORKERS_DEFAULT):
    """Per-N pgs_island load-balance metrics.

    Reports the biggest island's share of total per-step CPU work, the
    theoretical minimum wall time given perfect scheduling, and how much
    of pgs_solve wall time was 'lost' relative to that lower bound.

    Low biggest_share + low inefficiency = work is well-distributed; the
    solver is near-optimally parallel.
    """
    isl = df[(df["thread"] == "physics") & (df["stage"] == "pgs_island")]
    sol = df[(df["thread"] == "physics") & (df["stage"] == "pgs_solve")]
    if isl.empty or sol.empty:
        print("\n=== load_balance: SKIPPED — no pgs_island or pgs_solve scopes ===")
        return

    print(f"\n=== load_balance: per-N pgs_island load distribution ===")
    print(f"    biggest_share  = max island's share of total per-step CPU (lower=better)")
    print(f"    parallel_lb_us = max(biggest_island_us, total_cpu_us / {num_workers}) per step")
    print(f"    inefficiency   = (solve_wall - parallel_lb) / solve_wall (lower=better)")

    print(f"\n{'N':>6}  {'steps':>6}  {'mean_islands':>13}  {'max_island_us':>14}  "
          f"{'sum_island_us':>14}  {'biggest_share':>14}  {'parallel_lb_us':>14}  "
          f"{'observed_solve':>14}  {'inefficiency':>13}")
    print("-" * 130)
    for src in sorted(df["source_file"].unique(),
                      key=lambda s: df.loc[df.source_file == s, "N"].iloc[0]):
        sub = df[df.source_file == src]
        N = int(sub["N"].iloc[0]) if not sub["N"].empty else 0
        isl_sub = sub[sub.stage == "pgs_island"]
        sol_sub = sub[sub.stage == "pgs_solve"]
        if isl_sub.empty or sol_sub.empty:
            continue
        per_step = isl_sub.groupby("step")["duration_us"].agg(
            ["count", "sum", "max"]).rename(
                columns={"count": "n_islands", "sum": "sum_us", "max": "max_us"})
        sol_by_step = sol_sub.groupby("step")["duration_us"].first().rename("solve_wall")
        per_step = per_step.join(sol_by_step, how="inner")
        per_step["biggest_share"] = per_step["max_us"] / per_step["sum_us"]
        per_step["parallel_lb"]   = np.maximum(
            per_step["max_us"], per_step["sum_us"] / num_workers)
        per_step["efficiency"]    = per_step["parallel_lb"] / per_step["solve_wall"]

        print(f"{N:>6}  {len(per_step):>6}  {per_step['n_islands'].mean():>13.1f}  "
              f"{per_step['max_us'].mean():>14.1f}  {per_step['sum_us'].mean():>14.1f}  "
              f"{per_step['biggest_share'].mean()*100:>13.1f}%  "
              f"{per_step['parallel_lb'].mean():>14.1f}  "
              f"{per_step['solve_wall'].mean():>14.1f}  "
              f"{(1 - per_step['efficiency'].mean())*100:>12.1f}%")


def report_island_buckets(df, summary, par_eff, outdir):
    """Contact-weighted island size distribution at the largest N.

    The plot_island_distribution histogram shows island COUNT by size,
    which is misleading because small islands dominate by count but
    contribute negligible CPU work. This report instead shows what
    fraction of total solve CPU comes from islands in each size bucket.
    """
    isl = df[(df["thread"] == "physics") & (df["stage"] == "pgs_island")]
    if isl.empty:
        print("\n=== island_buckets: SKIPPED — no pgs_island scopes ===")
        return
    largest_N = isl["N"].max()
    if math.isnan(largest_N):
        return
    iso = isl[isl["N"] == largest_N].copy()
    iso["size_bucket"] = pd.cut(
        iso["scope_n"],
        bins=[0, 1, 3, 5, 10, 20, 50, 100, 500, 5000, 100000],
        labels=["1", "2-3", "4-5", "6-10", "11-20", "21-50", "51-100",
                "101-500", "501-5k", "5k+"])
    buckets = iso.groupby("size_bucket", observed=True).agg(
        n_islands=("duration_us", "count"),
        total_cpu_us=("duration_us", "sum"),
        mean_us_per_island=("duration_us", "mean"),
    ).reset_index()
    total = buckets["total_cpu_us"].sum()
    buckets["pct_of_solve_cpu"] = 100 * buckets["total_cpu_us"] / total if total > 0 else 0.0

    print(f"\n=== island_buckets: contact-weighted island size distribution (N={int(largest_N)}) ===")
    print(f"    What fraction of total solve CPU comes from islands in each size bucket.")
    print(f"    (compare to plot_island_distribution which weighs by COUNT not WORK)")
    print()
    print(buckets.to_string(index=False, float_format=lambda x: f"{x:11.1f}"))


def report_shape_compare(df, summary, par_eff, outdir):
    """Per-stage mean µs at the largest N, pivoted by shape.

    For multi-shape sweeps (YOPE_STRESS_SHAPE × YOPE_STRESS_N). Drops stages
    that are zero across every shape (e.g. nphase_obb_obb when no OBB run
    happened). Highlights ratios so you can see which dispatch path is the
    actual outlier:

       stage              sphere    aabb     obb     max/min
       nphase_sph_sph     4500      0        0       —
       nphase_aabb_aabb   0         2300     0       —
       nphase_obb_obb     0         0        12000   —
       broadphase_sap     20000     19000    19500   1.05×
       integration        11000     9500     11000   1.16×

    Use to spot per-shape outliers and pick optimization targets per
    dispatch entry.
    """
    shapes = sorted(df["shape"].dropna().unique())
    if len(shapes) < 2:
        print(f"\n=== shape_compare: SKIPPED — only one shape present ({shapes}) ===")
        print(f"    To compare, run: SHAPES=sphere,aabb,obb tools/run_scaling_sweep.sh")
        return

    # Pick the largest N that exists for at least 2 shapes.
    by_N_shape = df.groupby(["shape", "N"]).size().unstack("shape", fill_value=0)
    common_Ns  = [N for N, row in by_N_shape.iterrows()
                  if (row > 0).sum() >= 2]
    if not common_Ns:
        print("\n=== shape_compare: SKIPPED — no N value common to multiple shapes ===")
        return
    target_N = max(common_Ns)

    print(f"\n=== shape_compare: per-stage mean µs at N={int(target_N)} by shape ===")
    print(f"    shapes present at this N: {shapes}")

    sub = df[(df["N"] == target_N) & (df["thread"] == "physics")]
    pivot = sub.groupby(["stage", "shape"])["duration_us"].mean().unstack("shape")
    # Drop stages that are zero/NaN across every shape.
    pivot = pivot.loc[(pivot.fillna(0).sum(axis=1) > 0)]
    # Sort by total time across shapes — biggest stages first.
    pivot["__row_sum"] = pivot.fillna(0).sum(axis=1)
    pivot = pivot.sort_values("__row_sum", ascending=False).drop(columns="__row_sum")

    # max/min ratio per row, ignoring zeros (which signal "no work for this shape").
    def row_ratio(row):
        nz = row.dropna()
        nz = nz[nz > 0]
        if len(nz) < 2: return float("nan")
        return nz.max() / nz.min()
    pivot["max/min"] = pivot.apply(row_ratio, axis=1)

    # Pretty-print with dashes for zeros/NaN.
    def fmt(v):
        if pd.isna(v): return f"{'—':>10}"
        if isinstance(v, float) and v == 0: return f"{'—':>10}"
        return f"{v:>10.1f}"
    rows = []
    for stage, row in pivot.iterrows():
        cells = [fmt(row[s]) for s in shapes]
        ratio = row.get("max/min", float("nan"))
        ratio_str = (f"{ratio:>6.2f}×" if not pd.isna(ratio)
                                       else f"{'—':>7}")
        rows.append(f"{stage:28}  " + "  ".join(cells) + f"  | {ratio_str}")

    header = f"{'stage':28}  " + "  ".join(f"{s:>10}" for s in shapes) + f"  | {'max/min':>7}"
    print()
    print(header)
    print("-" * len(header))
    for r in rows:
        print(r)


# Report name → function.
REPORTS = {
    "top5_wall":       report_top5_wall,
    "top5_cpu":        report_top5_cpu,
    "top5_render_wall": report_top5_render_wall,
    "top5_gpu":        report_top5_gpu,
    "parallel":        report_parallel,
    "pairs":           report_pairs,
    "scaling":         report_scaling,
    "load_balance":    report_load_balance,
    "island_buckets":  report_island_buckets,
    "shape_compare":   report_shape_compare,
}


# ============================================================================
# Main
# ============================================================================

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Yope3D Phase E profile analyzer",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="available reports: " + ", ".join(REPORTS.keys()))
    ap.add_argument("inputs", nargs="*", help="profile CSV files")
    ap.add_argument("-o", "--outdir", default=DEFAULT_OUT, help="output directory")
    ap.add_argument("--no-plots", action="store_true", help="skip PNG generation")
    ap.add_argument("--reports", default="all",
                    help="comma-separated report names (default: all). "
                         "use --list-reports to see options.")
    ap.add_argument("--list-reports", action="store_true",
                    help="print available report names and exit")
    ap.add_argument("--workers", type=int, default=NUM_WORKERS_DEFAULT,
                    help=f"thread-pool worker count for parallel-efficiency math "
                         f"(default: {NUM_WORKERS_DEFAULT}, matches M-series default)")
    args = ap.parse_args(argv)

    if args.list_reports:
        print("Available reports (use --reports to filter):")
        for name, fn in REPORTS.items():
            doc = (fn.__doc__ or "").strip().split("\n")[0]
            print(f"  {name:18}  {doc}")
        return 0

    if not args.inputs:
        ap.error("the following arguments are required: inputs")

    # Resolve --reports filter.
    if args.reports == "all":
        selected = list(REPORTS.keys())
    else:
        selected = [r.strip() for r in args.reports.split(",") if r.strip()]
        unknown = [r for r in selected if r not in REPORTS]
        if unknown:
            ap.error(f"unknown report(s): {unknown}. "
                     f"available: {list(REPORTS.keys())}")

    df = load_all(args.inputs)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    report_file_path = outdir / "profile_report.txt"
    
    with open(report_file_path, "w", encoding = "utf-8") as f, redirect_stdout(f):

        # Always-written tables — referenced by multiple reports.
        summary = stage_summary(df)
        summary.to_csv(outdir / "stage_summary.csv", index=False)
        print(f"wrote {outdir/'stage_summary.csv'} ({len(summary)} rows)")

        steptotals = physics_step_totals(df)
        steptotals.to_csv(outdir / "physics_step_totals.csv", index=False)
        print(f"wrote {outdir/'physics_step_totals.csv'} ({len(steptotals)} rows)")

        gputotals = gpu_frame_totals(df)
        if not gputotals.empty:
            gputotals.to_csv(outdir / "gpu_frame_totals.csv", index=False)
            print(f"wrote {outdir/'gpu_frame_totals.csv'} ({len(gputotals)} rows)")

        par_eff = parallel_efficiency(df)
        if not par_eff.empty:
            par_eff.to_csv(outdir / "parallel_efficiency.csv", index=False)
            print(f"wrote {outdir/'parallel_efficiency.csv'} ({len(par_eff)} rows)")

        # Reports.
        for name in selected:
            fn = REPORTS[name]
            # load_balance is the only one that takes the worker-count override.
            if name == "load_balance":
                fn(df, summary, par_eff, outdir, num_workers=args.workers)
            else:
                fn(df, summary, par_eff, outdir)

        if args.no_plots:
            return 0

        print("\n--- plots ---")
        plot_stage_breakdown    (summary, outdir)
        plot_scaling_loglog     (steptotals, df, outdir)
        plot_query_vs_work      (df, outdir)
        plot_island_distribution(df, outdir)
        plot_frametime_cdf      (df, outdir)
        plot_gpu_frametime_cdf  (gputotals, outdir)
        plot_steptime_series    (steptotals, outdir)
        plot_crossthread        (df, steptotals, outdir)
        plot_narrowphase_mix    (df, outdir)
    print(f"\n[done] outputs in {outdir.resolve()}/")
    print(f"\n[done] Text report dumped to: {report_file_path.resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
