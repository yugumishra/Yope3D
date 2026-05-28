#!/usr/bin/env bash
#
# Phase E scaling sweep — runs the Stress Test scene at a series of entity
# counts, each for a fixed wall-clock duration, and renames the per-run
# profile CSV with the N value. Output drops into ./profile_runs/.
#
# Usage:
#   tools/run_scaling_sweep.sh               # default sweep: 500..16000
#   tools/run_scaling_sweep.sh 1000 4000     # custom N values
#   DURATION=60 tools/run_scaling_sweep.sh   # 60s per run instead of 30
#
# Analyze with:
#   python3 tools/analyze_profile.py profile_runs/*.csv
#
# Notes:
#  * The engine still opens a window per run (Vulkan needs a surface).
#    Move it to a back desktop, or minimize between runs.
#  * COOLDOWN gives the CPU a chance to settle between runs so the next
#    one isn't biased by residual thermal throttling.
#  * The script assumes a debug build at build/mac-debug/yope3d. Set
#    BIN= to override. Profile macros are NDEBUG-gated; release builds
#    will produce empty CSVs.

set -euo pipefail

# Engine reads yope.cfg via relative path — it must run from the project root
# (one level above tools/). Resolve via the script's own directory so the
# sweep behaves the same whether invoked as `tools/run_scaling_sweep.sh`,
# `./run_scaling_sweep.sh` (from inside tools/), or via an absolute path.
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
cd "$PROJECT_ROOT"

if [[ ! -f yope.cfg ]]; then
    echo "warning: no yope.cfg in $PROJECT_ROOT — engine will fall back to" >&2
    echo "         the default script name ('Sandbox') which isn't registered." >&2
    echo "         Create yope.cfg with: script=SandboxScript" >&2
fi

BIN="${BIN:-./build/mac-debug/yope3d}"
DURATION="${DURATION:-30}"
COOLDOWN="${COOLDOWN:-10}"
OUT_DIR="${OUT_DIR:-profile_runs}"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN not found or not executable" >&2
    echo "       build first:   cmake --build build/mac-debug --config Debug" >&2
    exit 1
fi

# Custom N list from args, else default sweep.
if [[ $# -gt 0 ]]; then
    N_VALUES=("$@")
else
    N_VALUES=(500 1000 2000 4000 8000 12000 16000)
fi

mkdir -p "$OUT_DIR"

echo "binary:     $BIN"
echo "duration:   ${DURATION}s per run"
echo "cooldown:   ${COOLDOWN}s between runs"
echo "output:     $OUT_DIR"
echo "N values:   ${N_VALUES[*]}"
echo

# macOS ships bash 3.2 — no negative array subscripts. Compute the last
# index up front so the "skip cooldown after last run" check works.
LAST_IDX=$(( ${#N_VALUES[@]} - 1 ))

for IDX in "${!N_VALUES[@]}"; do
    N="${N_VALUES[$IDX]}"
    echo ">>> running N=$N for ${DURATION}s"
    # Stamp time so we know which CSV to grab — the profiler appends an
    # epoch suffix, and `ls -t` only sorts by mtime, not by content, so we
    # need to filter on creation time to avoid grabbing stale files.
    START=$(date +%s)
    LOG="$OUT_DIR/engine_N${N}.log"
    # Show engine output live AND save to a log via `tee`. `tee -a "$LOG"`
    # writes to the log file in addition to stdout. The `( … )` subshell
    # plus `|| true` keeps `set -e` from killing the whole sweep when the
    # engine exits non-zero (Abort trap etc.) — we want to keep going.
    set +e
    YOPE_STRESS_N=$N YOPE_PROFILE_DURATION=$DURATION "$BIN" 2>&1 | tee "$LOG"
    EXIT=${PIPESTATUS[0]}
    set -e
    if (( EXIT != 0 )); then
        echo "    !! engine exited with status $EXIT — log saved to $LOG"
    fi

    # Find profile CSVs created at or after START.
    LATEST=""
    for f in yope_profile_*.csv; do
        [[ -f "$f" ]] || continue
        # File's mtime as epoch.
        if command -v stat >/dev/null; then
            if stat -f '%m' "$f" >/dev/null 2>&1; then
                MT=$(stat -f '%m' "$f")             # BSD/macOS
            else
                MT=$(stat -c '%Y' "$f")             # GNU/Linux
            fi
            if (( MT >= START )); then
                LATEST="$f"
            fi
        fi
    done

    if [[ -z "$LATEST" ]]; then
        echo "    !! no CSV produced — is the binary a debug build? see $LOG"
    else
        DEST="$OUT_DIR/yope_profile_N${N}.csv"
        mv "$LATEST" "$DEST"
        ROWS=$(wc -l <"$DEST" | tr -d ' ')
        echo "    -> $DEST  ($ROWS rows)"
    fi

    if (( IDX != LAST_IDX )); then
        echo "    cooldown ${COOLDOWN}s..."
        sleep "$COOLDOWN"
    fi
done

echo
echo "done. analyze with:"
echo "  python3 tools/analyze_profile.py $OUT_DIR/*.csv"
