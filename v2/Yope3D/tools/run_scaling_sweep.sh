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

# Run from the project root (one level above tools/) so the profile CSV lands
# there and relative BIN paths work. Resolve via the script's own directory so
# the sweep behaves the same whether invoked as `tools/run_scaling_sweep.sh`,
# `./run_scaling_sweep.sh` (from inside tools/), or via an absolute path.
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
cd "$PROJECT_ROOT"

BIN="${BIN:-./build/mac-debug/yope3d}"
# Stress scene (ScriptComponent → behaviors/stress_test.py); passed via --scene
# so yope3d.cfg's startupScene is left alone. Relative to assets/.
SCENE="${SCENE:-scenes/stress.json}"
DURATION="${DURATION:-30}"
COOLDOWN="${COOLDOWN:-10}"
OUT_DIR="${OUT_DIR:-profile_runs}"
# Comma-separated list of shapes to sweep. Each shape × each N is one run.
# Recognized by scripts/behaviors/stress_test.py via YOPE_STRESS_SHAPE:
#   sphere (default) — add_sphere + icosphere mesh
#   aabb             — add_aabb   + box mesh
#   obb              — add_obb    + box mesh
#   mixed            — random shape per body (cross-shape narrowphase pairs)
# Mix-and-match e.g. SHAPES=sphere,obb tools/run_scaling_sweep.sh 8000 16000
SHAPES="${SHAPES:-sphere}"
# Scenario (YOPE_STRESS_SCENARIO): grid (default, classic Phase E stacks) or
# funnel (bodies rain through angled plates into one large pile). Non-grid
# runs get a _<scenario> filename suffix, which the analyzer parses.
SCENARIO="${SCENARIO:-grid}"

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

# Parse SHAPES into array (comma-separated).
#IFS=',' read -r -a SHAPE_VALUES <<< "$SHAPES"
SHAPE_VALUES=( ${SHAPES//,/ } )

mkdir -p "$OUT_DIR"

TOTAL_RUNS=$(( ${#N_VALUES[@]} * ${#SHAPE_VALUES[@]} ))

echo "binary:     $BIN"
echo "duration:   ${DURATION}s per run"
echo "cooldown:   ${COOLDOWN}s between runs"
echo "output:     $OUT_DIR"
echo "N values:   ${N_VALUES[*]}"
echo "shapes:     ${SHAPE_VALUES[*]}"
echo "scenario:   $SCENARIO"
echo "total runs: $TOTAL_RUNS"
echo

LAST_N_IDX=$(( ${#N_VALUES[@]} - 1 ))
LAST_SHAPE_IDX=$(( ${#SHAPE_VALUES[@]} - 1 ))
RUN=0

for SHAPE_IDX in "${!SHAPE_VALUES[@]}"; do
    SHAPE="${SHAPE_VALUES[$SHAPE_IDX]}"
    for N_IDX in "${!N_VALUES[@]}"; do
        N="${N_VALUES[$N_IDX]}"
        RUN=$((RUN + 1))
        echo ">>> [$RUN/$TOTAL_RUNS] N=$N shape=$SHAPE for ${DURATION}s"
        START=$(date +%s)
        LOG="$OUT_DIR/engine_N${N}_${SHAPE}.log"
        set +e
        YOPE_STRESS_N=$N YOPE_STRESS_SHAPE=$SHAPE YOPE_STRESS_SCENARIO=$SCENARIO \
            YOPE_PROFILE_DURATION=$DURATION "$BIN" --scene "$SCENE" 2>&1 | tee "$LOG"
        EXIT=${PIPESTATUS[0]}
        set -e
        if (( EXIT != 0 )); then
            echo "    !! engine exited with status $EXIT — log saved to $LOG"
        fi

        # Find profile CSVs created at or after START.
        LATEST=""
        for f in yope_profile_*.csv; do
            [[ -f "$f" ]] || continue
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
            # Filename pattern: yope_profile_N<N>_<shape>.csv
            # The analyzer regex matches _N<digits> for the N field and
            # _<shape>.csv for the shape field — both stay independent.
            # Grid keeps the legacy name (comparable with old Phase E CSVs);
            # other scenarios append their name for the analyzer to parse.
            if [[ "$SCENARIO" == "grid" ]]; then
                DEST="$OUT_DIR/yope_profile_N${N}_${SHAPE}.csv"
            else
                DEST="$OUT_DIR/yope_profile_N${N}_${SHAPE}_${SCENARIO}.csv"
            fi
            mv "$LATEST" "$DEST"
            ROWS=$(wc -l <"$DEST" | tr -d ' ')
            echo "    -> $DEST  ($ROWS rows)"
        fi

        # Cooldown unless this was the very last run of the sweep.
        IS_LAST_RUN=0
        if (( SHAPE_IDX == LAST_SHAPE_IDX && N_IDX == LAST_N_IDX )); then
            IS_LAST_RUN=1
        fi
        if (( IS_LAST_RUN == 0 )); then
            echo "    cooldown ${COOLDOWN}s..."
            sleep "$COOLDOWN"
        fi
    done
done

echo
echo "done. analyze with:"
echo "  python3 tools/analyze_profile.py $OUT_DIR/*.csv"
echo "  python3 tools/analyze_profile.py $OUT_DIR/*.csv --reports shape_compare"
