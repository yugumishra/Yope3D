#!/usr/bin/env bash
#
# One-shot recovery + relaunch for the 2026-07-30 sweep.
#
#   ./fim-finetuning/resume_sweep.sh [budget_hours]      # default 6
#
# WHY THIS EXISTS
#     Every node of the first sweep trained successfully and then died at
#     exit 1, two lines later, in stop_swap_watchdog: the swap watchdog
#     self-exits within 3 s of the trainer finishing, so `kill` hit a dead pid,
#     and `kill` sits in the one position `set -e` does not exempt (last command
#     of an && list). No message, no stage marked done — so a plain resume would
#     have retrained. Node A2 lost 1 h 10 m of GPU time this way, and the
#     2026-07-29 run lost 7 h 39 m to the same line.
#
#     train_lora.sh is fixed. But runs already in flight execute from their own
#     runs/<tag>/train_lora.snapshot.sh (the deliberate guard against mid-run
#     edits), so an in-flight node still carries the bug and will still die
#     after saving its weights. This script waits that out, then repairs state.
#
# WHAT IT DOES
#     1. waits for any trainer left over from the buggy invocation
#     2. marks train.done for every run whose weights are actually on disk —
#        mlx logs "Saved final weights" only on a clean finish, so that plus a
#        non-empty adapter file is proof the stage completed, whatever exit code
#        the harness reported
#     3. re-enters sweep.sh, which skips scored nodes (<tag>.done) and picks
#        unscored ones up at the fuse stage — ~8 min each instead of ~78
#
set -euo pipefail

FT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUDGET_H="${1:-6}"

B=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'; R=$'\033[0m'

echo
echo "${B}sweep recovery${R}  ${DIM}budget ${BUDGET_H}h${R}"
echo

# The in-flight run holds its own wake assertion, and sweep.sh will take one —
# but only after this script's wait loop ends. That leaves a gap between the old
# run dying and the new one starting, and AC idle-sleep on this machine is set
# to 1 minute. Hold one across the whole handoff. -w $$ releases it on exit.
if command -v caffeinate >/dev/null 2>&1; then
  caffeinate -ims -w $$ &
  echo "  ${DIM}holding wake assertion across the handoff (caffeinate -ims, pid $!)${R}"
fi

# --- 1. wait out anything still training from the old invocation -------------
waited=0
while pgrep -f 'mlx_train_guarded\.py' >/dev/null 2>&1; do
  it=""
  for d in "$FT"/runs/*/; do
    [[ -f "$d/train.log" ]] || continue
    [[ $(( $(date +%s) - $(stat -f %m "$d/train.log") )) -lt 120 ]] || continue
    it=$(grep -oE '^Iter [0-9]+' "$d/train.log" | tail -1 || true)
    it="$(basename "$d") ${it}"
  done
  printf '\r%s  waiting on the in-flight trainer  %s%s' \
    "$(date '+%H:%M:%S')" "${it:-…}" "$(printf '%*s' 12 '')"
  sleep 60
  waited=$((waited + 60))
done
[[ $waited -gt 0 ]] && printf '\n'
echo "  ${DIM}no trainer running${R}"

# The buggy snapshot exits on its own the moment its trainer does. Give it a
# beat; only then treat a leftover as a stale orphan.
for _ in 1 2 3 4 5 6; do
  pgrep -f 'train_lora\.snapshot\.sh' >/dev/null 2>&1 || break
  sleep 5
done
if pgrep -f 'train_lora\.snapshot\.sh' >/dev/null 2>&1; then
  echo "  ${YEL}stale snapshot script with no trainer — clearing${R}"
  pkill -f 'train_lora\.snapshot\.sh' 2>/dev/null || true
  sleep 2
fi

# --- 2. reclaim training that really did finish -----------------------------
reclaimed=0
for d in "$FT"/runs/*/; do
  [[ -s "$d/adapters/adapters.safetensors" ]] || continue
  grep -q 'Saved final weights' "$d/train.log" 2>/dev/null || continue
  [[ -d "$d/.runstate" ]] || continue
  if [[ ! -f "$d/.runstate/train.done" ]]; then
    touch "$d/.runstate/train.done"
    vl=$(grep -oE 'Val loss [0-9.]+' "$d/train.log" | grep -oE '[0-9.]+' | tail -1 || true)
    echo "  ${GRN}reclaimed $(basename "$d")${R} — weights on disk (final val ${vl:-?}), resumes at fuse"
    reclaimed=$((reclaimed + 1))
  fi
done
[[ $reclaimed -eq 0 ]] && echo "  ${DIM}nothing to reclaim${R}"

# --- 3. back into the decision tree ----------------------------------------
echo
echo "  ${DIM}re-entering sweep.sh — scored nodes skip, reclaimed nodes fuse+eval only${R}"
exec "$FT/sweep.sh" --budget "$BUDGET_H"
