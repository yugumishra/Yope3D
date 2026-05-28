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


def classify_stage(stage: str, thread: str) -> str:
    if thread == "physics":
        if stage in PHYSICS_WALL_LEAVES:    return "main_wall_leaf"
        if stage in PHYSICS_WORKER_CPU:     return "worker_cpu"
        if stage in PHYSICS_SUB_BREAKDOWN:  return "sub_breakdown"
    elif thread == "render":
        if stage in RENDER_WALL_LEAVES:     return "main_wall_leaf"
        if stage in RENDER_SUB_BREAKDOWN:   return "sub_breakdown"
    return "other"


# ============================================================================
# Loading
# ============================================================================

_N_PAT = re.compile(r"_N(\d+)", re.IGNORECASE)


def n_from_filename(path: str) -> int | None:
    m = _N_PAT.search(os.path.basename(path))
    return int(m.group(1)) if m else None


def load_one(path: str) -> pd.DataFrame:
    """Read a single CSV, pad missing columns, tag with source/N."""
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
      main_wall_leaf   — counts toward physics step wall time
      worker_cpu       — runs in parallel on a worker thread
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

    leaf_totals = (summary.loc[leaf_mask]
                   .groupby(["source_file", "thread"])["total_us"].sum()
                   .rename("wall_denom").reset_index())
    cpu_totals  = (summary.loc[cpu_mask]
                   .groupby(["source_file", "thread"])["total_us"].sum()
                   .rename("cpu_denom").reset_index())

    summary = summary.merge(leaf_totals, on=["source_file", "thread"], how="left")
    summary = summary.merge(cpu_totals,  on=["source_file", "thread"], how="left")

    summary["share_wall_pct"] = 100.0 * summary["total_us"] / summary["wall_denom"]
    summary["share_cpu_pct"]  = 100.0 * summary["total_us"] / summary["cpu_denom"]
    summary = summary.drop(columns=["wall_denom", "cpu_denom"])

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


# ============================================================================
# Plots
# ============================================================================

def plot_stage_breakdown(summary: pd.DataFrame, outdir: Path) -> None:
    """Per-file stacked bar: mean stage time as a fraction of total."""
    plt = _mpl()
    for src, g in summary.groupby("source_file"):
        # Show main-thread wall leaves only — that's the budget bar. pgs_island
        # belongs in its own conversation (the parallel-efficiency table).
        phys = (g[(g["thread"] == "physics") & (g["kind"] == "main_wall_leaf")]
                .sort_values("share_wall_pct", ascending=True))
        if phys.empty:
            continue
        fig, ax = plt.subplots(figsize=(8, 5))
        ax.barh(phys["stage"], phys["mean_us"])
        for i, (stage, share) in enumerate(zip(phys["stage"], phys["share_wall_pct"])):
            ax.text(phys["mean_us"].iloc[i], i, f"  {share:5.1f}%", va="center", fontsize=8)
        ax.set_xlabel("mean µs per physics step (wall, main thread)")
        ax.set_title(f"physics stage breakdown — {src}")
        fig.tight_layout()
        out = outdir / f"breakdown_{Path(src).stem}.png"
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
# Main
# ============================================================================

def main(argv=None):
    ap = argparse.ArgumentParser(description="Yope3D Phase E profile analyzer")
    ap.add_argument("inputs", nargs="+", help="profile CSV files")
    ap.add_argument("-o", "--outdir", default=DEFAULT_OUT, help="output directory")
    ap.add_argument("--no-plots", action="store_true", help="skip PNG generation")
    args = ap.parse_args(argv)

    df = load_all(args.inputs)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    # Tables — always written.
    summary = stage_summary(df)
    summary.to_csv(outdir / "stage_summary.csv", index=False)
    print(f"wrote {outdir/'stage_summary.csv'} ({len(summary)} rows)")

    steptotals = physics_step_totals(df)
    steptotals.to_csv(outdir / "physics_step_totals.csv", index=False)
    print(f"wrote {outdir/'physics_step_totals.csv'} ({len(steptotals)} rows)")

    # Parallel efficiency table — written and printed if pgs_island scopes
    # exist (i.e. profile was taken after A2 instrumentation landed).
    par_eff = parallel_efficiency(df)
    if not par_eff.empty:
        par_eff.to_csv(outdir / "parallel_efficiency.csv", index=False)
        print(f"wrote {outdir/'parallel_efficiency.csv'} ({len(par_eff)} rows)")

    # Top-5 main-thread wall stages per file — what fits in the physics budget.
    # share_wall_pct is the meaningful number here: fraction of physics step
    # wall time spent in this stage. (pgs_island shows up huge in share_cpu_pct
    # but is intentionally absent here because it runs on workers, not the
    # main thread budget.)
    print("\n--- top 5 PHYSICS main-thread WALL stages per file ---")
    print("    share_wall_pct = fraction of physics-step wall time on main thread")
    phys = summary[(summary["thread"] == "physics") &
                   (summary["kind"] == "main_wall_leaf")]
    top = (phys.sort_values(["source_file", "share_wall_pct"], ascending=[True, False])
           .groupby("source_file").head(5))
    print(top[["source_file", "stage", "mean_us", "p99_us", "share_wall_pct"]]
          .to_string(index=False, float_format=lambda x: f"{x:9.2f}"))

    # CPU breakdown including workers — answers "where does the actual work
    # happen?" Useful for picking optimization targets.
    print("\n--- top 5 PHYSICS stages by aggregate CPU share (incl. workers) ---")
    print("    share_cpu_pct = fraction of (main wall + worker CPU) per file")
    phys_cpu = summary[(summary["thread"] == "physics") &
                       (summary["kind"].isin(["main_wall_leaf", "worker_cpu"]))]
    top_cpu = (phys_cpu.sort_values(["source_file", "share_cpu_pct"],
                                    ascending=[True, False])
               .groupby("source_file").head(5))
    print(top_cpu[["source_file", "stage", "kind", "mean_us", "p99_us", "share_cpu_pct"]]
          .to_string(index=False, float_format=lambda x: f"{x:9.2f}"))

    if not par_eff.empty:
        print("\n--- PGS parallel efficiency per file ---")
        print("    effective_workers = sum(pgs_island µs) / pgs_solve µs")
        print("    near hardware_concurrency-1 = fully parallel; near 1.0 = serialized")
        print(par_eff.to_string(index=False, float_format=lambda x: f"{x:9.2f}"))

    if args.no_plots:
        return 0

    print("\n--- plots ---")
    plot_stage_breakdown   (summary, outdir)
    plot_scaling_loglog    (steptotals, df, outdir)
    plot_query_vs_work     (df, outdir)
    plot_island_distribution(df, outdir)
    plot_frametime_cdf     (df, outdir)
    plot_steptime_series   (steptotals, outdir)
    plot_crossthread       (df, steptotals, outdir)
    plot_narrowphase_mix   (df, outdir)
    print(f"\n[done] outputs in {outdir.resolve()}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
