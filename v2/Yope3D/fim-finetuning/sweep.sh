#!/usr/bin/env bash
#
# Overnight hyperparameter search as a STATIC DECISION TREE, not a grid.
#
#   ./fim-finetuning/sweep.sh --budget 8      # 8 hours, ~5 runs
#   ./fim-finetuning/sweep.sh --dry-run       # print the tree and the costs
#
# WHY A TREE AND NOT A GRID
#     A grid over {LR} x {rank} x {iters} x {lam} is 24+ runs. At ~1.5 h each
#     that is two nights to learn something the first two runs already answer.
#     Each stage here is chosen so its OUTCOME determines the next stage, which
#     is what makes 5 runs worth more than 24 blind ones.
#
# WHAT THE FIRST RUN IS FOR — this is the important design decision.
#     Run A2 deliberately reuses the EXACT hyperparameters that failed
#     (LR 1e-5, rank 8) on the NEW corpus. It is not a candidate; it is a
#     control. The 2026-07-29 run changed nothing but produced invention
#     12.2% -> 36.8%, and the new corpus changes two things at once
#     (frequency-weighted names, 86% -> 58% synth share). Without A2 a good
#     result cannot be attributed and a bad one cannot be diagnosed.
#
# THE OBJECTIVE
#     score = tier2 %correct - tier2 %invented
#     base            25.0 - 12.2 = +12.8
#     2026-07-29 LoRA 19.9 - 36.8 = -16.9
#     Invention is what broke, so it is penalised directly rather than folded
#     into an accuracy number that can hide it. A config that does not beat
#     +12.8 is not a candidate at all.
#
# WHY TIER 2 ONLY DURING THE SWEEP
#     ~6 min instead of ~15, and it is the metric that regressed. But tiers 1
#     and 2 BOTH rise when a model overfits to Yope3D, so a tier-2 win is a
#     nomination, not a verdict. The winner gets a full 3-tier run at the end;
#     until tier 3 is checked, nothing here is a result.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
FT="$REPO_ROOT/fim-finetuning"
TRAIN="$FT/train_lora.sh"
SWEEP_DIR="$FT/runs/_sweep"
RESULTS="$SWEEP_DIR/results.tsv"

BUDGET_H="${BUDGET_H:-8}"
# ~0.45 epoch of the 896-example corpus. Sized so all five tree nodes fit an
# 8 h budget: at the measured 11.1 s/iter plus ~8 min of convert+tier-2 eval,
# a run costs ~1.5 h, and 5 x 1.5 = 7.4 h. 450 would fit only four.
ITERS_PER_RUN="${ITERS_PER_RUN:-400}"
DRY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --budget)  BUDGET_H="$2"; shift 2 ;;
    --iters)   ITERS_PER_RUN="$2"; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

if [[ -t 1 ]]; then B=$'\033[1m'; DIM=$'\033[2m'; R=$'\033[0m'
  GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'
else B=""; DIM=""; R=""; GRN=""; YEL=""; RED=""; fi

mkdir -p "$SWEEP_DIR"
T0=$(date +%s)
DEADLINE=$(awk -v h="$BUDGET_H" -v t="$T0" 'BEGIN{printf "%d", t + h*3600}')
BASE_SCORE=12.8

say() { echo "$*"; }   # the keep-awake helpers below are shared with train_lora.sh

# ------------------------------------------------------- keep the mac awake
#
# WHY pmset ALONE DOES NOT WORK HERE. Two independent reasons, both measured on
# this machine on 2026-07-29:
#
#   1. macOS idle sleep keys off HID INPUT, not CPU/GPU load. A fully saturated
#      GPU does not count as activity; without an explicit power assertion the
#      machine sleeps mid-training and the run dies silently.
#   2. This machine's AC idle-sleep timer was set to ONE MINUTE
#      (`pmset -g custom` -> AC Power: sleep 1) while battery was 0/never.
#      Plugging in therefore made sleep MORE aggressive, not less, which is why
#      earlier pmset attempts appeared not to work.
#
# `caffeinate -w $$` ties the assertion to this script's exact lifetime: held
# for the whole run, released automatically on exit, crash, or Ctrl-C. No sudo,
# and no persistent system change to remember to undo.
#
#   -i  prevent idle SYSTEM sleep      <- the one that matters
#   -m  prevent disk idle sleep
#   -s  prevent system sleep (AC only)
#   -d  prevent DISPLAY sleep — deliberately OFF by default. This is a fanless
#       M4 Air running the GPU flat out for hours; letting the panel sleep costs
#       nothing and helps thermals. KEEP_DISPLAY=1 to override.
#
# NOTE: closing the lid sleeps regardless of any assertion unless an external
# display, power, and input device are attached. Leave the lid open.
CAFFEINATE_PID=""
keep_awake() {
  [[ "${KEEP_AWAKE:-1}" == "1" ]] || { say "  ${DIM}KEEP_AWAKE=0 — not holding a wake assertion${R}"; return 0; }
  command -v caffeinate >/dev/null 2>&1 || { say "  ${YEL}caffeinate not found — the mac may sleep mid-run${R}"; return 0; }
  local flags="-ims"
  [[ "${KEEP_DISPLAY:-0}" == "1" ]] && flags="-dims"
  caffeinate $flags -w $$ &
  CAFFEINATE_PID=$!
  say "  ${DIM}holding wake assertion (caffeinate $flags, pid $CAFFEINATE_PID) for this run${R}"
}

# check_power <hours> — an 8 h GPU run on battery is not physically possible.
check_power() {
  local hours="$1" src pct
  src=$(pmset -g ps 2>/dev/null | head -1)
  pct=$(pmset -g ps 2>/dev/null | grep -oE '[0-9]+%' | head -1)
  if [[ "$src" == *"AC Power"* ]]; then
    say "  ${DIM}on AC power${R}"
    return 0
  fi
  say "${RED}ON BATTERY (${pct:-?}) — a ${hours}h run needs AC power.${R}"
  say "  Sustained GPU load drains an M4 Air in roughly an hour. The run will"
  say "  not finish, and macOS sleeps on low battery regardless of caffeinate."
  say "  Plug in, then re-run. Set ALLOW_BATTERY=1 to override."
  [[ "${ALLOW_BATTERY:-0}" == "1" ]] || return 1
  say "  ${YEL}ALLOW_BATTERY=1 — proceeding anyway${R}"
}

hms() { local s=${1%.*}; printf '%dh%02dm' $((s/3600)) $(((s%3600)/60)); }
left() { echo $(( DEADLINE - $(date +%s) )); }

# score <tag> -> "correct invented score", from the paired probe JSON
score_of() {
  python3 - "$1" <<'PY'
import json, sys
tag = sys.argv[1]
p = f"fim-finetuning/data/probe_{tag}.json"
try:
    rows = json.load(open(p))["rows"]
except Exception:
    print("NA NA NA"); raise SystemExit
n = len(rows)
c = 100 * sum(1 for r in rows if r["verdict"] == "correct") / n
i = 100 * sum(1 for r in rows if r["verdict"] == "invented") / n
print(f"{c:.1f} {i:.1f} {c-i:.1f}")
PY
}

# regen_corpus <lam> <files>  — only when a stage actually varies the corpus
regen_corpus() {
  local lam="$1" files="$2"
  echo "  ${DIM}regenerating corpus: lam=$lam files=$files${R}"
  python3 "$FT/corpus/synth_pyi.py" --lam "$lam" --files "$files" >/dev/null
  rm -f "$FT/data/token_stats.json"
  python3 "$FT/corpus/make_dataset.py" >/dev/null
}

# run <tag> <lr> <rank> <iters> [lam] [files]
run_cfg() {
  local tag="$1" lr="$2" rank="$3" iters="$4" lam="${5:-}" files="${6:-}"
  if [[ -f "$SWEEP_DIR/$tag.done" ]]; then
    echo "  ${DIM}$tag already done — skipping${R}"; return 0
  fi
  local est=$(( iters * 12 + 500 ))
  if (( $(left) < est )); then
    echo "  ${YEL}SKIP $tag — needs ~$(hms $est), only $(hms $(left)) left in budget${R}"
    return 1
  fi
  [[ -n "$lam" ]] && regen_corpus "$lam" "$files"
  echo "  ${B}running $tag${R}  lr=$lr rank=$rank iters=$iters ${lam:+lam=$lam files=$files}"
  if [[ $DRY -eq 1 ]]; then echo "    ${DIM}(dry-run)${R}"; return 0; fi
  # TRIM_LEVEL=2: drop fused/, the f16 GGUF, and the Q8_0 once tier 2 has read
  # it. Five configs would otherwise leave ~37 GB behind on a machine with ~46
  # GB free — the sweep would fill the disk around run 4, at 3am. adapters/ is
  # kept (42 MB/config) and re-fusing is ~21 s.
  LR="$lr" RANK="$rank" ITERS_OVERRIDE="$iters" EVAL_TIERS="2" TRIM_LEVEL=2 \
    MAX_HOURS=99 "$TRAIN" --tag "$tag" >"$SWEEP_DIR/$tag.out" 2>&1 || {
      echo "  ${RED}$tag FAILED — see $SWEEP_DIR/$tag.out${R}"; return 1; }
  read -r c i s <<<"$(score_of "$tag")"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$tag" "$lr" "$rank" "$iters" "${lam:-0.25}" "$c" "$i" "$s" >>"$RESULTS"
  touch "$SWEEP_DIR/$tag.done"
  local verdict="${RED}worse than base${R}"
  awk -v s="$s" -v b="$BASE_SCORE" 'BEGIN{exit !(s>b)}' && verdict="${GRN}BEATS BASE${R}"
  echo "  -> correct ${c}%  invented ${i}%  score ${s}  $verdict"
  return 0
}

better() {  # better <tagA> <tagB> -> 0 if A scored higher
  local a b
  a=$(awk -v t="$1" '$1==t{print $8}' "$RESULTS" 2>/dev/null | tail -1)
  b=$(awk -v t="$2" '$1==t{print $8}' "$RESULTS" 2>/dev/null | tail -1)
  [[ -z "$a" || -z "$b" ]] && return 1
  awk -v a="$a" -v b="$b" 'BEGIN{exit !(a>b)}'
}

check_power "$BUDGET_H" || exit 1
[[ $DRY -eq 1 ]] || keep_awake

[[ -f "$RESULTS" ]] || printf 'tag\tlr\trank\titers\tlam\tcorrect\tinvented\tscore\n' >"$RESULTS"

cat <<EOF

${B}Yope3D FIM LoRA — decision-tree sweep${R}
${DIM}budget ${BUDGET_H}h · ${ITERS_PER_RUN} iters/run · tier-2 objective (correct% - invented%)${R}

  reference   base $BASE_SCORE      2026-07-29 LoRA -16.9

  ${B}stage A — isolate the corpus fix from the hyperparameters${R}
    A2  lr 1e-5  rank 8   <- CONTROL: the exact config that failed, new corpus
    A1  lr 3e-6  rank 8   <- the recommended direction (less overwriting)

  ${B}stage B — branch on A${R}
    if A1 > A2  (lower LR helps)   -> B1 lr 1e-6 · B2 lr 3e-6 @ 2x iters
    if A2 >= A1 (corpus was it)    -> B1 rank 4  · B2 lr 2e-5

  ${B}stage C — corpus axis on the stage-A/B winner${R}
    C1  lam 0.0 (purely empirical names, no uniform floor)

  Runs that cannot finish inside the budget are skipped, not truncated.
EOF

echo
echo "${B}=== stage A ===${R}"
run_cfg A2 1e-5 8 "$ITERS_PER_RUN" || true
run_cfg A1 3e-6 8 "$ITERS_PER_RUN" || true

echo
echo "${B}=== stage B ===${R}"
if better A1 A2; then
  echo "  ${DIM}A1 (low LR) won -> pushing lower and longer${R}"
  run_cfg B1 1e-6 8 "$ITERS_PER_RUN" || true
  run_cfg B2 3e-6 8 $(( ITERS_PER_RUN * 2 )) || true
else
  echo "  ${DIM}A2 (original LR) held -> the corpus was the problem; vary capacity${R}"
  run_cfg B1 1e-5 4 "$ITERS_PER_RUN" || true
  run_cfg B2 2e-5 8 "$ITERS_PER_RUN" || true
fi

echo
echo "${B}=== stage C — corpus axis ===${R}"
WINNER=$(sort -k8 -g -r "$RESULTS" 2>/dev/null | awk 'NR==1&&$1!="tag"{print $1}' || true)
if [[ -n "${WINNER:-}" ]]; then
  WLR=$(awk -v t="$WINNER" '$1==t{print $2}' "$RESULTS" | tail -1)
  WRK=$(awk -v t="$WINNER" '$1==t{print $3}' "$RESULTS" | tail -1)
  WIT=$(awk -v t="$WINNER" '$1==t{print $4}' "$RESULTS" | tail -1)
  echo "  ${DIM}best so far: $WINNER (lr=$WLR rank=$WRK iters=$WIT)${R}"
  run_cfg C1 "$WLR" "$WRK" "$WIT" 0.0 60 || true
  # Leave the corpus in the default state regardless of how stage C went.
  regen_corpus 0.25 60
fi

echo
echo "${B}=== results ===${R}"
{ printf 'tag\tlr\trank\titers\tlam\tcorr%%\tinv%%\tscore\n'
  tail -n +2 "$RESULTS" | sort -k8 -g -r; } | column -t
BEST=$(tail -n +2 "$RESULTS" | sort -k8 -g -r | head -1)
BEST_TAG=$(echo "$BEST" | cut -f1); BEST_SCORE=$(echo "$BEST" | cut -f8)

echo
if [[ -z "${BEST_TAG:-}" ]]; then
  echo "${RED}No runs completed.${R}"
elif awk -v s="$BEST_SCORE" -v b="$BASE_SCORE" 'BEGIN{exit !(s>b)}'; then
  cat <<EOF
${GRN}$BEST_TAG beats base ($BEST_SCORE vs $BASE_SCORE) on tier 2.${R}

That is a NOMINATION, not a result. Tiers 1 and 2 both rise when a LoRA
overfits to Yope3D; only tier 3 sees the cost. Confirm with a full run:

  EVAL_TIERS="1 2 3" TRIM_LEVEL=1 ./fim-finetuning/train_lora.sh \\
    --tag ${BEST_TAG} --from fuse

(--from fuse, not --from eval: the sweep trimmed the GGUF after reading tier 2.
Re-fusing from the kept adapters takes ~21 s.)
EOF
else
  cat <<EOF
${YEL}Nothing beat base ($BASE_SCORE). Best was $BEST_TAG at $BEST_SCORE.${R}

If even the low-LR runs invent more than the base model, the hyperparameters
are not the binding constraint and further sweeping is wasted time. The next
lever is the corpus: 58% synth is still the majority of the training signal,
and its name distribution is an approximation of 15 real files. Try
--files 20 (about 35% synth) before spending another night on LR.
EOF
fi
echo
echo "${DIM}elapsed $(hms $(( $(date +%s) - T0 )))  ·  budget $(hms $(( BUDGET_H*3600 )))  ·  log $SWEEP_DIR${R}"
