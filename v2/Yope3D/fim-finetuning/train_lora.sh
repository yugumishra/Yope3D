#!/usr/bin/env bash
#
# PLAN.txt section 9.9 (day 3) end to end: gate -> calibrate -> train -> fuse ->
# GGUF -> quantize -> serve -> evaluate all three metric tiers -> report.
#
# Resumable: every stage records a marker in .runstate/, so re-running skips
# completed stages. Ctrl-C at any point and re-run to continue.
#
#   ./fim-finetuning/train_lora.sh --dry-run    # print the plan, do nothing
#   ./fim-finetuning/train_lora.sh --calibrate  # measure ONLY, then stop
#   ./fim-finetuning/train_lora.sh              # full run
#   RANK=16 EPOCHS=2 ./fim-finetuning/train_lora.sh --tag r16e2
#
# ---------------------------------------------------------------------------
# READ THIS IF YOU RAN THE 2026-07-29 09:33 VERSION
#
# That version froze the machine. mlx-lm reported Peak mem 30.752 GB on 24 GB
# of RAM, macOS absorbed the overflow into swap instead of failing, and the
# whole system thrashed at ~500 s/iteration — a 14-day run presenting itself as
# "training is slow". Three defaults were wrong: grad_checkpoint defaulted to
# false, batch_size 2 doubled activations for no measured benefit, and nothing
# checked the footprint before committing to 2,485 iterations.
#
# Fixed by measuring instead of assuming:
#   - grad_checkpoint: true, batch_size 1        (config)
#   - mx.set_memory_limit via mlx_train_guarded  (raises instead of swapping)
#   - a CALIBRATE stage that runs a few iterations, reads the real peak memory
#     and iteration rate out of mlx's own output, and refuses to start the full
#     run if either is out of budget
#
# The ETA was also wrong, and for a related reason: it extrapolated from stage
# weights I had guessed. Preflight was budgeted 3% and took ~40% of the session,
# so a bar 1.6% of the way through a 90-minute session reported hours. ETAs are
# now shown ONLY where there is a measured rate to extrapolate from, and say so
# where there isn't.
# ---------------------------------------------------------------------------
#
set -euo pipefail

# --------------------------------------------------------------- configuration

# Resolve our own path to an ABSOLUTE one before anything cd's. Invoked as
# `./train_lora.sh` from inside fim-finetuning/, BASH_SOURCE[0] is a relative
# path that stops resolving the moment we cd to REPO_ROOT below — which broke
# the snapshot copy with "cp: ./train_lora.sh: No such file or directory".
SELF_ABS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

# Honour an inherited root: after the snapshot re-exec below, BASH_SOURCE points
# at runs/<tag>/train_lora.snapshot.sh, so deriving the root from it would give
# fim-finetuning/runs/.. and every path would be wrong.
REPO_ROOT="${YOPE_REPO_ROOT:-$(cd "$(dirname "$SELF_ABS")/.." && pwd)}"
FT="$REPO_ROOT/fim-finetuning"
# Several corpus/harness scripts take repo-relative paths (--dir, --out) and
# resolve them against the CWD. Running this from fim-finetuning/ instead of
# the repo root therefore wrote a phantom 400-file corpus to
# fim-finetuning/fim-finetuning/corpus/synth and validated THAT. Pin the cwd so
# the script behaves identically wherever it is invoked from.
cd "$REPO_ROOT"

TAG="${TAG:-lora}"
BASE_HF="${BASE_HF:-Qwen/Qwen2.5-Coder-1.5B}"   # fp16 safetensors, TRAINABLE
LLAMA_TAG="${LLAMA_TAG:-b9890}"

# Hyperparameters — PLAN 9.6 as corrected by 9.6a, and by the 07-29 incident.
RANK="${RANK:-8}"
ALPHA="${ALPHA:-$((RANK * 2))}"
EPOCHS="${EPOCHS:-1}"              # 1-2 ONLY: 10x redundancy, 1 epoch ~= 10 passes
BATCH="${BATCH:-1}"                # was 2 — halved after the 30.7 GB incident
SEQ_LEN="${SEQ_LEN:-3072}"         # measured: p99 3043, max 3516, 0.8% truncate
LR="${LR:-1e-5}"
GRAD_CKPT="${GRAD_CKPT:-true}"     # mlx-lm defaults FALSE. This is the big lever.
# -1 = evaluate the FULL valid set (all 147 examples), which is the only setting
# that produces comparable numbers across evals. mlx-lm's evaluate() does not
# seed its sampler, so any finite val_batches draws a DIFFERENT random subset
# each time; worse, iterate_batches length-sorts before batching, so two draws
# can land in entirely different length regimes and FIM loss is strongly
# length-dependent. A previous run used 8 (at batch_size 1, that is 8 of 147
# examples = 5.4%) and produced a val curve that looked like textbook
# overfitting but was pure sampling noise. Costs ~2 min per eval. Worth it —
# this is the ONLY in-training overfitting detector there is.
VAL_BATCHES="${VAL_BATCHES:--1}"
LORA_KEYS='["self_attn.q_proj","self_attn.k_proj","self_attn.v_proj","self_attn.o_proj"]'

# Safety budget. Metal reports max_recommended_working_set_size ~17.8 GB of the
# 24 GB on an M4 Air; past that the GPU pages and the machine stops responding.
MEM_LIMIT_GB="${MEM_LIMIT_GB:-14}" # hard ceiling handed to mx.set_memory_limit
MEM_WARN_GB="${MEM_WARN_GB:-12}"   # calibration refuses to proceed above this
MAX_HOURS="${MAX_HOURS:-10}"       # projected run longer than this needs --allow-long
CAL_ITERS="${CAL_ITERS:-6}"
EVAL_TIERS="${EVAL_TIERS:-1 2 3}"  # sweep runs set this to "2"

# Artifact trimming. A single run leaves 7.4 GB in runs/<tag>, of which 5.8 GB
# is intermediate: fused/ (2.9 GB fp16 safetensors) and model-f16.gguf (2.9 GB)
# exist only to be consumed by the next stage. Five sweep configs would add
# ~29 GB of it overnight, on a machine with ~46 GB free.
#   0  keep everything
#   1  drop fused/ + f16 GGUF once Q8_0 exists          (default)
#   2  also drop the Q8_0 GGUF after eval               (sweep)
# Level 2 is safe because adapters/ (42 MB) is kept and re-fusing from it is
# ~21 s end to end — cheaper to regenerate than to store 1.5 GB per config.
TRIM_LEVEL="${TRIM_LEVEL:-1}"

PORT_BASE="${PORT_BASE:-8012}"
PORT_CAND="${PORT_CAND:-8013}"
SERVE_ARGS="-c 8192 -np 1 -fa on --cache-reuse 256 -ngl 99 -ub 1024"

ORIG_ARGS=("$@")   # kept for the snapshot re-exec below; the parser shifts $@

DRY_RUN=0; CAL_ONLY=0; ALLOW_LONG=0; FROM=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)    DRY_RUN=1; shift ;;
    --calibrate)  CAL_ONLY=1; shift ;;
    --allow-long) ALLOW_LONG=1; shift ;;
    --tag)        TAG="$2"; shift 2 ;;
    --from)       FROM="$2"; shift 2 ;;
    -h|--help)    sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN="$FT/runs/$TAG"; STATE="$RUN/.runstate"; LOG="$RUN/run.log"
TRAINLOG="$RUN/train.log"; CALLOG="$RUN/calibrate.log"
ADAPTERS="$RUN/adapters"; FUSED="$RUN/fused"
GGUF_F16="$RUN/model-f16.gguf"; GGUF_Q8="$RUN/model-q8_0.gguf"
VENV="$FT/.venv-train"; LLAMA_SRC="$FT/.cache/llama.cpp"
GUARD="$FT/harness/mlx_train_guarded.py"
# $0 becomes the snapshot after the re-exec below; SELF stays the real script
# so user-facing "re-run with..." advice is copy-pasteable.
SELF="${YOPE_SELF:-$SELF_ABS}"

# ---------------------------------------------------------- snapshot re-exec
#
# Bash reads a script file INCREMENTALLY, holding a byte offset into it. Editing
# this file during a multi-hour run therefore makes the still-running shell read
# from a stale offset into changed content, and it dies at an arbitrary point
# with no useful message.
#
# That is not hypothetical. On 2026-07-29 this file was edited 47 minutes into a
# 7h39m run. Training completed perfectly — 2485/2485 iterations, final weights
# saved, memory guard exited clean — and the wrapper then died with exit 1
# BEFORE it could touch .runstate/train.done. A plain resume would have thrown
# away the entire run and started training again.
#
# So: copy self to the run directory and re-exec from that immutable snapshot.
# Edits to the working file during a run are then irrelevant, and the snapshot
# also records exactly which version produced the artifacts.
if [[ "${YOPE_TRAIN_SNAPSHOT:-}" != "1" && $DRY_RUN -eq 0 ]]; then
  mkdir -p "$RUN"
  SNAP="$RUN/train_lora.snapshot.sh"
  cp "$SELF_ABS" "$SNAP"
  export YOPE_TRAIN_SNAPSHOT=1 YOPE_REPO_ROOT="$REPO_ROOT" YOPE_SELF="$SELF"
  exec bash "$SNAP" ${ORIG_ARGS[@]+"${ORIG_ARGS[@]}"}
fi

# ------------------------------------------------------------------ ui helpers

if [[ -t 1 ]]; then
  B=$'\033[1m'; DIM=$'\033[2m'; R=$'\033[0m'
  GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'; CYN=$'\033[36m'
else
  B=""; DIM=""; R=""; GRN=""; YEL=""; RED=""; CYN=""
fi

STAGES=(preflight gate calibrate train fuse convert quantize eval report)
WEIGHTS=(3        3    2         67    3    9       4        7    2)
STAGE_IDX=0
T0=$(date +%s)

# Seconds per full run, measured during calibration. Until it is set, the
# script does NOT claim to know how long anything will take — that guess is
# what produced "eta 22 hours" on a stage that finished in minutes.
ETA_RATE=""        # measured seconds per training iteration
POST_TRAIN_S=900   # fuse+convert+quantize+eval, rough but bounded and stated

hms() { local s=${1%.*}; printf '%d:%02d:%02d' $((s/3600)) $(((s%3600)/60)) $((s%60)); }

say() {
  printf '%s\n' "$*"
  printf '%s\n' "$(printf '%s' "$*" | sed 's/\x1b\[[0-9;]*m//g')" >>"$LOG"
}

stage_base() {
  local i acc=0 tot=0
  for i in "${!WEIGHTS[@]}"; do tot=$((tot + WEIGHTS[i])); done
  for i in "${!WEIGHTS[@]}"; do [[ $i -ge $STAGE_IDX ]] && break; acc=$((acc + WEIGHTS[i])); done
  awk -v a="$acc" -v t="$tot" 'BEGIN{printf "%.6f", a/t}'
}
stage_span() {
  local i tot=0
  for i in "${!WEIGHTS[@]}"; do tot=$((tot + WEIGHTS[i])); done
  awk -v w="${WEIGHTS[$STAGE_IDX]}" -v t="$tot" 'BEGIN{printf "%.6f", w/t}'
}

# bar <inner_fraction 0..1> <label> [remaining_seconds]
#
# ETA is shown only when a caller passes a remaining-seconds figure derived from
# something MEASURED. There is no global extrapolation: the previous version
# inferred total runtime from hand-guessed stage weights, and a 3%-weighted
# preflight that actually took 40% of the session reported multi-hour ETAs for
# work that finished in minutes. An honest blank beats a confident wrong number.
bar() {
  local inner="$1" label="$2" rem="${3:-}"
  local frac now elapsed pct filled eta b_str i
  frac=$(awk -v b="$(stage_base)" -v s="$(stage_span)" -v i="$inner" \
         'BEGIN{f=b+s*i; if(f>1)f=1; printf "%.6f", f}')
  now=$(date +%s); elapsed=$((now - T0))
  pct=$(awk -v f="$frac" 'BEGIN{printf "%.1f", f*100}')
  filled=$(awk -v f="$frac" 'BEGIN{printf "%d", f*40}')
  if [[ -n "$rem" ]]; then eta="$(hms "$rem")"; else eta="measuring"; fi
  b_str=""
  for ((i=0; i<40; i++)); do
    if   ((i < filled));  then b_str+="█"
    elif ((i == filled)); then b_str+="▓"
    else                       b_str+="░"; fi
  done
  printf '\r\033[K%s[%s]%s %s%5s%%%s  %selapsed %s  eta %-9s%s %s' \
    "$CYN" "$b_str" "$R" "$B" "$pct" "$R" "$DIM" "$(hms $elapsed)" "$eta" "$R" "$label"
}

banner() {
  STAGE_IDX="$1"; shift
  bar 0 "$*"; printf '\n'
  say "${B}==>${R} ${B}$*${R}   ${DIM}[stage $((STAGE_IDX+1))/${#STAGES[@]}]${R}"
}

done_marker() { [[ -f "$STATE/$1.done" ]]; }
mark_done()   { touch "$STATE/$1.done"; }
should_run() {
  local name="$1" i from_idx=-1 this_idx=-1
  for i in "${!STAGES[@]}"; do
    [[ "${STAGES[$i]}" == "$FROM" ]] && from_idx=$i
    [[ "${STAGES[$i]}" == "$name" ]] && this_idx=$i
  done
  if [[ -n "$FROM" && $this_idx -ge $from_idx ]]; then return 0; fi
  ! done_marker "$name"
}


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

CAND_PID=""; BASE_PID=""
cleanup() {
  local rc=$?
  [[ -n "${CAFFEINATE_PID:-}" ]] && kill "$CAFFEINATE_PID" 2>/dev/null || true
  [[ -n "${SWAP_WD_PID:-}" ]] && kill "$SWAP_WD_PID" 2>/dev/null || true
  [[ -n "$CAND_PID" ]] && kill "$CAND_PID" 2>/dev/null || true
  [[ -n "$BASE_PID" ]] && kill "$BASE_PID" 2>/dev/null || true
  wait 2>/dev/null || true
  printf '\n'
  if [[ $rc -ne 0 && $rc -ne 130 ]]; then
    say "${RED}FAILED${R} (exit $rc) after $(hms $(( $(date +%s) - T0 )))"
    say "  log:    $LOG"
    say "  resume: $SELF --tag $TAG"
  fi
}
trap cleanup EXIT INT TERM

wait_health() {
  local port="$1" limit="${2:-180}" i=0
  until curl -sf "http://127.0.0.1:$port/health" >/dev/null 2>&1; do
    sleep 2; i=$((i+2))
    [[ $i -ge $limit ]] && { say "${RED}server on :$port never became healthy${R}"; return 1; }
    bar "$(awk -v i="$i" -v l="$limit" 'BEGIN{printf "%.3f", i/l}')" "waiting for :$port" $((limit - i))
  done
}

# Pull the last "Peak mem N GB" / "It/sec N" out of an mlx-lm log.
peak_mem() { grep -oE 'Peak mem [0-9.]+' "$1" 2>/dev/null | grep -oE '[0-9.]+' | sort -n | tail -1; }
it_per_s() { grep -oE 'It/sec [0-9.]+'   "$1" 2>/dev/null | grep -oE '[0-9.]+' | tail -1; }

# MEDIAN It/sec, discarding the first `skip` iterations.
#
# `tail -1` was wrong twice over. At steps_per_report=1 each It/sec is the
# INSTANTANEOUS rate for one iteration, and iterate_batches length-sorts then
# shuffles, so cost tracks sequence length: the observed spread on this corpus
# is 0.050 to 1.251 it/s, a 25x range. One sample is not an estimate.
# Separately the first iterations are warmup (kernel compilation, lazy graph
# build) and are systematically slow, so they must be dropped rather than
# averaged in. Using the last of 6 calibration samples projected 15.9 s/iter
# against a true 7.2 — a 2.2x overestimate that falsely tripped the time ceiling.
# MEDIAN Tokens/sec, discarding the first `skip` warmup iterations.
#
# WHY TOKENS AND NOT ITERATIONS. Cost is proportional to tokens processed, and
# iterate_batches length-sorts before shuffling, so at batch_size 1 each
# iteration is a different length: the observed It/sec spread on this corpus is
# 25x (0.050 to 1.251). Tokens/sec spreads only 1.8-2.7x, and the calibration
# median (204) matched the full training median (198) to 3%.
#
# The old estimator took `tail -1` of It/sec — ONE sample from a 25x-wide
# distribution — and projected 10.98 h against a true 4.94 h. Taking the median
# of It/sec instead did not help (16.7 s/iter); neither did projecting from
# calibration's own token count, because 6 samples also mis-estimate mean
# LENGTH (they averaged 2827 tok against a corpus mean of 1333).
#
# The fix is to sample only the thing that is stable (rate) and take the thing
# that is exactly knowable (total tokens) from the dataset itself.
tok_per_s_median() {  # tok_per_s_median <log> [skip]
  local skip="${2:-2}"
  grep -oE 'Tokens/sec [0-9.]+' "$1" 2>/dev/null | grep -oE '[0-9.]+' \
    | awk -v s="$skip" 'NR>s' | sort -n \
    | awk '{v[n++]=$1} END{if(n) printf "%.3f",
             (n%2 ? v[int(n/2)] : (v[n/2-1]+v[n/2])/2)}'
}

# Exact mean tokens/example, tokenized once and cached. This removes the length
# dimension from the estimate entirely — it is a property of the dataset, not
# something to infer from a handful of sampled batches.
ensure_token_stats() {
  local cache="$FT/data/token_stats.json"
  if [[ -f "$cache" ]]; then cat "$cache"; return; fi
  python3 - "$FT/data/train.jsonl" "$cache" <<'PY' 2>/dev/null || echo '{}'
import json, sys, pathlib
try:
    from transformers import AutoTokenizer
except ImportError:
    print("{}"); raise SystemExit
tok = AutoTokenizer.from_pretrained("Qwen/Qwen2.5-Coder-1.5B")
L = [len(tok(json.loads(l)["text"])["input_ids"]) for l in open(sys.argv[1])]
L.sort(); n = len(L)
q = lambda p: L[min(int(p / 100 * n), n - 1)]
d = {"n": n, "mean": sum(L) / n, "total": sum(L),
     "p50": q(50), "p90": q(90), "p95": q(95), "p99": q(99), "max": L[-1]}
pathlib.Path(sys.argv[2]).write_text(json.dumps(d))
print(json.dumps(d))
PY
}

# --- swap backstop ----------------------------------------------------------
# The in-process watchdog (mlx_train_guarded.py) only sees MLX allocations.
# This sees the actual symptom that made the machine unusable — swap growth —
# whatever causes it. Cheap: sysctl costs ~2 ms.
swap_used_mb() {
  sysctl -n vm.swapusage 2>/dev/null | awk '{
    for (i = 1; i <= NF; i++) if ($i == "used") {
      v = $(i+2); u = substr(v, length(v));
      gsub(/[MGK]$/, "", v);
      if (u == "G") v *= 1024; else if (u == "K") v /= 1024;
      printf "%.0f", v; exit
    }
  }'
}
SWAP_BASE=$(swap_used_mb); SWAP_BASE=${SWAP_BASE:-0}
SWAP_GROWTH_MB="${SWAP_GROWTH_MB:-3072}"   # kill if swap grows this far past baseline
SWAP_WD_PID=""

start_swap_watchdog() {  # start_swap_watchdog <pid_to_kill> <logfile>
  local target="$1" wlog="$2"
  (
    while kill -0 "$target" 2>/dev/null; do
      local now; now=$(swap_used_mb); now=${now:-0}
      if (( now > SWAP_BASE + SWAP_GROWTH_MB )); then
        printf '\n[swap-watchdog] swap grew %s MB past baseline (%s -> %s MB).\n' \
          "$SWAP_GROWTH_MB" "$SWAP_BASE" "$now" | tee -a "$wlog"
        printf '[swap-watchdog] Killing the trainer. This is the 2026-07-29 freeze\n' | tee -a "$wlog"
        printf '[swap-watchdog] signature: allocation absorbed by swap, no error raised.\n' | tee -a "$wlog"
        kill -9 "$target" 2>/dev/null
        exit 0
      fi
      sleep 3
    done
  ) &
  SWAP_WD_PID=$!
}
# The watchdog loop is `while kill -0 $target`, so it SELF-EXITS within 3 s of
# the trainer finishing normally — and then `kill` here fails with ESRCH.
#
# That one failure ended two multi-hour runs. `kill` is the LAST command of an
# `&&` list, which is the one position `set -e` does NOT exempt, so a dead-pid
# kill exited the whole script with status 1 — silently, no message, seconds
# after mlx had already saved its final weights. Signature: "FAILED (exit 1)"
# immediately following a 100% training bar (2026-07-29 run, sweep node A2).
# The calibrate stage survived only because its call site sits inside set +e.
#
# Reaping with `wait` also stops the EXIT trap's bare `wait` from blocking.
stop_swap_watchdog() {
  if [[ -n "${SWAP_WD_PID:-}" ]]; then
    kill "$SWAP_WD_PID" 2>/dev/null || true
    wait "$SWAP_WD_PID" 2>/dev/null || true
  fi
  SWAP_WD_PID=""
  return 0
}

# Bytes of a file or directory, 0 if absent.
_sz() { [[ -e "$1" ]] && du -sk "$1" 2>/dev/null | awk '{print $1*1024}' || echo 0; }
_hsz() { awk -v b="$1" 'BEGIN{split("B KB MB GB TB",u," ");i=1;
          while(b>=1024&&i<5){b/=1024;i++} printf "%.1f %s", b, u[i]}'; }

# trim_intermediates — drop fused/ and the f16 GGUF once Q8_0 is real.
#
# Guarded on the Q8_0 existing AND being plausibly sized: deleting the inputs
# to a conversion that silently produced a 0-byte output would turn a
# recoverable failure into a re-train. 1.5 GB is the expected Q8_0 size for
# 1.5B, so 500 MB is a loose floor that still catches a truncated write.
trim_intermediates() {
  [[ "$TRIM_LEVEL" -ge 1 ]] || return 0
  local q8; q8=$(_sz "$GGUF_Q8")
  if (( q8 < 500*1024*1024 )); then
    say "  ${YEL}not trimming: $GGUF_Q8 is $(_hsz "$q8") — too small to trust${R}"
    return 0
  fi
  local freed=$(( $(_sz "$FUSED") + $(_sz "$GGUF_F16") ))
  (( freed == 0 )) && return 0
  rm -rf "$FUSED" "$GGUF_F16"
  say "  ${DIM}trimmed $(_hsz "$freed") of intermediates (fused/, f16 GGUF);"
  say "  adapters/ kept — re-fusing from it is ~21 s${R}"
}

# trim_servable — sweep only. Drops the Q8_0 too, after eval has consumed it.
trim_servable() {
  [[ "$TRIM_LEVEL" -ge 2 ]] || return 0
  local freed; freed=$(_sz "$GGUF_Q8")
  (( freed == 0 )) && return 0
  rm -f "$GGUF_Q8"
  say "  ${DIM}trimmed $(_hsz "$freed") (Q8_0 GGUF) — TRIM_LEVEL=2."
  say "  Rebuild with: $SELF --tag $TAG --from fuse${R}"
}

# A trimmed intermediate makes `--from convert` unsatisfiable. Say so plainly
# rather than failing inside convert_hf_to_gguf with a path error.
need_input() {  # need_input <path> <stage-that-produces-it>
  [[ -e "$1" ]] && return 0
  say "${RED}$(basename "$1") is missing — it was trimmed after a previous run.${R}"
  say "  Re-run from the stage that produces it:"
  say "    $SELF --tag $TAG --from $2"
  exit 1
}

write_config() {  # write_config <path> <iters> <adapter_path> <val_batches> <steps_per_eval>
  cat >"$1" <<EOF
model: "$BASE_HF"
train: true
data: "$FT/data"
adapter_path: "$3"
iters: $2
batch_size: $BATCH
max_seq_length: $SEQ_LEN
learning_rate: $LR
num_layers: -1
grad_checkpoint: $GRAD_CKPT
steps_per_report: 1
steps_per_eval: $5
val_batches: $4
save_every: 500
lora_parameters:
  rank: $RANK
  scale: $(awk -v a="$ALPHA" -v r="$RANK" 'BEGIN{printf "%.4f", a/r}')
  dropout: 0.0
  keys: $LORA_KEYS
EOF
}

# ============================================================== overview

mkdir -p "$RUN" "$STATE" "$FT/.cache"
printf '\n===== session %s =====\n' "$(date '+%Y-%m-%d %H:%M:%S')" >>"$LOG"

read -r N_TRAIN N_VALID <<<"$(
python3 - "$FT/data" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1])
c = lambda f: sum(1 for _ in (d / f).open()) if (d / f).exists() else 0
print(c("train.jsonl"), c("valid.jsonl"))
PY
)"
[[ "$N_TRAIN" -eq 0 ]] && { echo "No training data at $FT/data/train.jsonl" >&2; exit 1; }

ITERS=$(( (N_TRAIN * EPOCHS + BATCH - 1) / BATCH ))
# Cap iterations without touching the dataset. mlx-lm length-sorts examples into
# batches then shuffles BATCH ORDER (np.random.permutation), so a capped run is
# a random sample across the whole length range — not a prefix of the corpus.
ITERS_NOTE=""
if [[ -n "${ITERS_OVERRIDE:-}" ]]; then
  ITERS_NOTE="   ${DIM}(ITERS_OVERRIDE — random subset of batches)${R}"
  ITERS="$ITERS_OVERRIDE"
fi
# Exposure computed from the ACTUAL iteration count, not from EPOCHS, so an
# override cannot leave the overview claiming a dose the run will not deliver.
EFF_EPOCHS=$(awk -v i="$ITERS" -v b="$BATCH" -v n="$N_TRAIN" 'BEGIN{printf "%.2f", i*b/n}')
EFF_PASSES=$(awk -v e="$EFF_EPOCHS" 'BEGIN{printf "%.0f", e*10}')
RAM_GB=$(sysctl -n hw.memsize 2>/dev/null | awk '{printf "%.0f", $1/1073741824}')

# Exact token statistics — the estimator's one non-sampled input.
TOK_JSON=$(ensure_token_stats)
read -r MEAN_TOK TOK_SUMMARY <<<"$(
python3 - "$SEQ_LEN" <<PY
import json, sys
d = json.loads('''$TOK_JSON''' or "{}")
if not d:
    print("  (tokenizer unavailable — computed after preflight)")
else:
    cap = int(sys.argv[1])
    print(d["mean"], f'p50 {d["p50"]} · p90 {d["p90"]} · p95 {d["p95"]} · '
                     f'p99 {d["p99"]} · max {d["max"]}  (mean {d["mean"]:.0f})')
PY
)"
DISK_FREE=$(df -g "$REPO_ROOT" | tail -1 | awk '{print $4}')

cat <<EOF

${B}Yope3D FIM LoRA — PLAN.txt 9.9${R}
${DIM}$(date '+%Y-%m-%d %H:%M:%S')  tag=${TAG}  run=${RUN#$REPO_ROOT/}${R}

${B}corpus${R}          ${DIM}tokenized once with the real Qwen tokenizer, then cached${R}
  train / valid         ${N_TRAIN} / ${N_VALID} examples
  token lengths         ${TOK_SUMMARY}
                        ${DIM}truncation cuts from the END, where MIDDLE lives, so a
                        truncated example has no target and teaches nothing.${R}

${B}hyperparameters${R}
  base model            ${BASE_HF}
  rank / alpha          ${RANK} / ${ALPHA}   attention q,k,v,o only
  learning rate         ${LR}
  epochs / iterations   ${EPOCHS} / ${ITERS}${ITERS_NOTE}
  effective exposure    ${EFF_EPOCHS} epochs ~= ${EFF_PASSES} passes over unique text
                        ${DIM}corpus redundancy is ~10x, so 1 epoch is already ~10
                        passes (PLAN 9.6a). Past ~20, this is memorising.${R}
  batch / seq len       ${BATCH} / ${SEQ_LEN}
  grad checkpointing    ${GRAD_CKPT}   ${DIM}mlx-lm default is FALSE; false is what OOM'd${R}

${B}memory budget${R}    ${DIM}the 07-29 run peaked at 30.8 GB here and froze the machine${R}
  physical RAM          ${RAM_GB} GB
  metal recommended     ~17.8 GB   ${DIM}past this the GPU pages to disk${R}
  watchdog budget       ${MEM_LIMIT_GB} GB   ${DIM}sampled every 0.5s, kills mid-iteration${R}
  calibration aborts at ${MEM_WARN_GB} GB
  swap backstop         +${SWAP_GROWTH_MB} MB over baseline (${SWAP_BASE} MB now)
                        ${DIM}mx.set_memory_limit does NOT guard — measured: 0.5 GB limit
                        set, 2.5 GB allocated, no error. Swap growth is the real
                        symptom, so it is watched directly.${R}
  disk free             ${DISK_FREE} GB   ${DIM}(needs ~15 GB)${R}
EOF

if [[ $DRY_RUN -eq 1 ]]; then
  say ""
  say "${B}stages${R}"
  for i in "${!STAGES[@]}"; do
    printf '  %-10s %2d%%%s\n' "${STAGES[$i]}" "${WEIGHTS[$i]}" \
      "$(done_marker "${STAGES[$i]}" && printf '  %s(done)%s' "$GRN" "$R" || true)"
  done
  say ""; say "${DIM}--dry-run: nothing executed.${R}"
  trap - EXIT; exit 0
fi

# ============================================================== 1. preflight

banner 0 "preflight — toolchain, weights, disk"
check_power "$(awk -v i="$ITERS" 'BEGIN{printf "%.0f", (i*11.1+900)/3600}')" \
  || exit 1
keep_awake
if should_run preflight; then
  [[ -d "$VENV" ]] || { say "  creating venv $VENV"; python3 -m venv "$VENV"; }
  # shellcheck disable=SC1091
  source "$VENV/bin/activate"
  bar 0.15 "installing mlx-lm"
  python3 -m pip install -q --upgrade pip >>"$LOG" 2>&1
  python3 -m pip install -q "mlx-lm>=0.21" huggingface_hub >>"$LOG" 2>&1
  bar 0.35 "fetching llama.cpp $LLAMA_TAG (convert script)"
  if [[ ! -d "$LLAMA_SRC/.git" ]]; then
    git clone -q --depth 1 --branch "$LLAMA_TAG" \
      https://github.com/ggml-org/llama.cpp "$LLAMA_SRC" >>"$LOG" 2>&1 \
      || git clone -q --depth 1 https://github.com/ggml-org/llama.cpp "$LLAMA_SRC" >>"$LOG" 2>&1
  fi
  bar 0.55 "installing convert deps (torch — the slow one, ~10 min cold)"
  python3 -m pip install -q -r "$LLAMA_SRC/requirements/requirements-convert_hf_to_gguf.txt" >>"$LOG" 2>&1
  bar 0.80 "downloading base weights ($BASE_HF, ~3.1 GB)"
  python3 - "$BASE_HF" <<'PY' >>"$LOG" 2>&1
import sys
from huggingface_hub import snapshot_download
snapshot_download(sys.argv[1], allow_patterns=["*.safetensors","*.json","*.txt","*.model"])
PY
  bar 0.95 "corpus gates"
  python3 "$FT/corpus/synth_pyi.py" >>"$LOG" 2>&1 \
    || { say "${RED}corpus validation FAILED${R}"; exit 1; }
  command -v llama-server   >/dev/null || { say "${RED}llama-server missing${R}"; exit 1; }
  command -v llama-quantize >/dev/null || { say "${RED}llama-quantize missing${R}"; exit 1; }
  mark_done preflight; bar 1 "preflight ok"; printf '\n'
else
  source "$VENV/bin/activate"; say "  ${DIM}skipped${R}"
fi

# ============================================================== 2. format gate

banner 1 "format gate — token-ID diff vs live server (MANDATORY)"
if should_run gate; then
  # shellcheck disable=SC2086
  llama-server -hf ggml-org/Qwen2.5-Coder-1.5B-Q8_0-GGUF --port "$PORT_BASE" \
    $SERVE_ARGS >>"$LOG" 2>&1 &
  BASE_PID=$!
  wait_health "$PORT_BASE" 180
  bar 0.6 "verify_format.py"
  if python3 "$FT/corpus/verify_format.py" --port "$PORT_BASE" 2>&1 | tee -a "$LOG" | grep -q "GATE PASSED"; then
    say "  ${GRN}GATE PASSED${R}"
  else
    say "${RED}FORMAT GATE FAILED. Do not train on this corpus.${R}"; exit 1
  fi
  kill "$BASE_PID" 2>/dev/null || true; BASE_PID=""
  mark_done gate; bar 1 "gate passed"; printf '\n'
else
  say "  ${DIM}skipped${R}"
fi

# ============================================================== 3. calibrate

banner 2 "calibrate — measure real memory + speed before committing"
# This stage exists because the previous version did not have it. It runs a
# handful of real iterations at the real config and reads mlx's own numbers,
# so the decision to start a multi-hour run is made from measurement rather
# than from my estimate of what a 1.5B LoRA "should" cost.
if should_run calibrate; then
  write_config "$RUN/cal_config.yaml" "$CAL_ITERS" "$RUN/.cal_adapters" 1 100000
  : >"$CALLOG"
  say "  running $CAL_ITERS iterations under a ${MEM_LIMIT_GB} GB hard ceiling"
  set +e
  python3 "$GUARD" --limit-gb "$MEM_LIMIT_GB" -c "$RUN/cal_config.yaml" >>"$CALLOG" 2>&1 &
  CAL_PID=$!
  start_swap_watchdog "$CAL_PID" "$CALLOG"
  while kill -0 "$CAL_PID" 2>/dev/null; do
    it=$(grep -oE '^Iter [0-9]+' "$CALLOG" | tail -1 | grep -oE '[0-9]+' || true)
    bar "$(awk -v a="${it:-0}" -v b="$CAL_ITERS" 'BEGIN{printf "%.4f", a/b}')" \
        "calibrating — iter ${it:-0}/$CAL_ITERS  peak $(peak_mem "$CALLOG" || echo -)GB"
    sleep 3
  done
  wait "$CAL_PID"; CAL_RC=$?
  stop_swap_watchdog
  set -e
  printf '\n'

  PEAK=$(peak_mem "$CALLOG" || true)
  RATE=$(tok_per_s_median "$CALLOG" 2 || true)

  if [[ $CAL_RC -eq 3 ]]; then
    say "${RED}CALIBRATION HIT THE ${MEM_LIMIT_GB} GB CEILING — not starting the run.${R}"
    say "  The guard did its job: this is the config that froze the machine."
    say "  Try:  BATCH=1 SEQ_LEN=2048 $SELF --tag $TAG --from calibrate"
    say "  (SEQ_LEN 2048 truncates 18.3% of examples — real cost, but a"
    say "   finishing run beats a perfect one that never starts.)"
    exit 1
  fi
  [[ $CAL_RC -ne 0 ]] && { say "${RED}calibration failed:${R}"; tail -20 "$CALLOG"; exit 1; }

  say "  ${B}measured${R}"
  say "    peak memory         ${PEAK:-?} GB   ${DIM}(ceiling ${MEM_LIMIT_GB}, abort above ${MEM_WARN_GB})${R}"
  say "    iterations/sec      ${RATE:-?}"

  if [[ -n "${PEAK:-}" ]] && awk -v p="$PEAK" -v w="$MEM_WARN_GB" 'BEGIN{exit !(p>w)}'; then
    say "${RED}ABORT: peak ${PEAK} GB is above the ${MEM_WARN_GB} GB budget.${R}"
    say "  Running anyway risks the swap-thrash freeze from 2026-07-29."
    say "  Lower SEQ_LEN (2048 -> 18.3% truncated) or raise MEM_WARN_GB knowingly."
    exit 1
  fi

  if [[ -n "${RATE:-}" && -n "${MEAN_TOK:-}" ]] && awk -v r="$RATE" 'BEGIN{exit !(r>0)}'; then
    # tokens = ITERS * BATCH * exact-mean-tokens-per-example (from the dataset)
    # rate   = median Tokens/sec over calibration, warmup dropped
    TOT_TOK=$(awk -v i="$ITERS" -v b="$BATCH" -v m="$MEAN_TOK" 'BEGIN{printf "%.0f", i*b*m}')
    PROJ=$(awk -v t="$TOT_TOK" -v r="$RATE" -v p="$POST_TRAIN_S" 'BEGIN{printf "%d", t/r+p}')
    ETA_RATE=$(awk -v t="$TOT_TOK" -v r="$RATE" -v i="$ITERS" 'BEGIN{printf "%.4f", (t/r)/i}')
    say "    tokens/sec (median) ${RATE}   ${DIM}stable to ~2x; It/sec spreads 25x and is useless here${R}"
    say "    tokens to process   $(printf "%'d" "$TOT_TOK")   ${DIM}(${ITERS} x ${BATCH} x ${MEAN_TOK%.*} exact mean)${R}"
    say "    projected full run  $(hms "$PROJ")   ${DIM}+-10%; refined live once training starts${R}"
    echo "$ETA_RATE" >"$STATE/rate"
    if awk -v p="$PROJ" -v m="$MAX_HOURS" 'BEGIN{exit !(p > m*3600)}' && [[ $ALLOW_LONG -eq 0 ]]; then
      # Mark calibration done BEFORE aborting. The measurement succeeded — only
      # the policy check failed — so re-running with a bigger budget should not
      # pay for the measurement again.
      rm -rf "$RUN/.cal_adapters"; mark_done calibrate
      say ""
      say "${YEL}Projected $(hms "$PROJ") exceeds the ${MAX_HOURS}h budget — not starting.${R}"
      say "  Peak memory ${PEAK:-?} GB is fine; this is purely the time ceiling."
      say ""
      say "  ${B}accept it and go${R}"
      say "    MAX_HOURS=24 $SELF --tag $TAG"
      say "    $SELF --tag $TAG --allow-long          ${DIM}(skip the check entirely)${R}"
      say ""
      say "  ${B}or make it shorter${R} ${DIM}— calibration is cached, these are instant to try${R}"
      say "    ITERS_OVERRIDE=$((ITERS / 2)) $SELF --tag $TAG"
      say "      ${DIM}half the batches, picked at random (mlx shuffles batch order).${R}"
      say "      ${DIM}At ~10x corpus redundancy that is still ~5 passes over the${R}"
      say "      ${DIM}unique text — PLAN 9.6a warns 1 epoch is ALREADY ~10 passes,${R}"
      say "      ${DIM}so this is closer to the intended dose, not a shortcut.${R}"
      say "    BATCH=2 $SELF --tag $TAG --from calibrate"
      say "      ${DIM}halves iteration count; re-calibrates since memory changes.${R}"
      say "      ${DIM}You have headroom: ${PEAK:-?} GB used of a ${MEM_WARN_GB} GB abort.${R}"
      exit 1
    fi
  else
    say "    ${YEL}could not read an iteration rate; ETA will stay unmeasured${R}"
  fi
  rm -rf "$RUN/.cal_adapters"
  mark_done calibrate; bar 1 "calibrated"; printf '\n'
else
  say "  ${DIM}skipped${R}"
  [[ -f "$STATE/rate" ]] && ETA_RATE=$(cat "$STATE/rate")
fi

if [[ $CAL_ONLY -eq 1 ]]; then
  say ""; say "${GRN}--calibrate: measurement only, stopping here.${R}"
  say "Run without --calibrate to start the full run (calibration is cached)."
  trap - EXIT; exit 0
fi

# ============================================================== 4. train

banner 3 "train — LoRA rank $RANK, $ITERS iterations"
if should_run train; then
  write_config "$RUN/lora_config.yaml" "$ITERS" "$ADAPTERS" "$VAL_BATCHES" 200
  : >"$TRAINLOG"
  python3 "$GUARD" --limit-gb "$MEM_LIMIT_GB" -c "$RUN/lora_config.yaml" >>"$TRAINLOG" 2>&1 &
  TRAIN_PID=$!
  start_swap_watchdog "$TRAIN_PID" "$TRAINLOG"
  T_TRAIN0=$(date +%s)
  last_it=0
  while kill -0 "$TRAIN_PID" 2>/dev/null; do
    it=$(grep -oE '^Iter [0-9]+' "$TRAINLOG" | tail -1 | grep -oE '[0-9]+' || true)
    [[ -n "${it:-}" ]] && last_it="$it"
    tl=$(grep -oE 'Train loss [0-9.]+' "$TRAINLOG" | tail -1 | grep -oE '[0-9.]+' || true)
    vl=$(grep -oE 'Val loss [0-9.]+'   "$TRAINLOG" | tail -1 | grep -oE '[0-9.]+' || true)
    pm=$(peak_mem "$TRAINLOG" || true)
    # Live memory guard. The ceiling should make this unreachable; if it fires,
    # something drifted and stopping now beats discovering it via a frozen laptop.
    if [[ -n "${pm:-}" ]] && awk -v p="$pm" -v l="$MEM_LIMIT_GB" 'BEGIN{exit !(p>l)}'; then
      kill "$TRAIN_PID" 2>/dev/null || true
      printf '\n'; say "${RED}ABORT: peak ${pm} GB exceeded the ${MEM_LIMIT_GB} GB ceiling.${R}"; exit 1
    fi
    # LIVE, self-correcting: once enough iterations have actually run, the
    # observed rate replaces the calibration estimate. Cumulative wall/iters is
    # exact by construction and converges regardless of how badly calibration
    # sampled — which is the real guarantee, since 6 iterations demonstrably
    # cannot predict a 5-hour run.
    rem=""; now_s=$(date +%s); el_train=$((now_s - T_TRAIN0))
    if (( last_it >= 20 )); then
      rem=$(awk -v e="$el_train" -v i="$last_it" -v n="$ITERS" -v p="$POST_TRAIN_S" \
            'BEGIN{printf "%d", (e/i)*(n-i)+p}')
    elif [[ -n "$ETA_RATE" ]]; then
      rem=$(awk -v s="$ETA_RATE" -v i="$last_it" -v n="$ITERS" -v p="$POST_TRAIN_S" \
            'BEGIN{printf "%d", s*(n-i)+p}')
    fi
    bar "$(awk -v a="$last_it" -v b="$ITERS" 'BEGIN{printf "%.5f", (b?a/b:0)}')" \
        "it $last_it/$ITERS  train ${tl:-–}  val ${vl:-–}  ${pm:-–}GB" "$rem"
    sleep 5
  done
  set +e; wait "$TRAIN_PID"; TRAIN_RC=$?; stop_swap_watchdog; set -e

  # Hours of GPU time must never be lost to a harness bug that fires AFTER the
  # weights land. mlx logs "Saved final weights" only on a clean finish, so
  # that plus a non-empty adapter file IS the completion signal — mark the
  # stage done here, before any later line can fail. A resume then starts at
  # fuse instead of retraining. Both prior runs had to be salvaged by hand.
  if grep -q 'Saved final weights' "$TRAINLOG" 2>/dev/null \
     && [[ -s "$ADAPTERS/adapters.safetensors" ]]; then
    mark_done train
  fi

  if [[ $TRAIN_RC -eq 3 ]]; then
    printf '\n'
    say "${RED}MEMORY WATCHDOG FIRED during training — stopped before any thrash.${R}"
    say "  Calibration passed but the full run drifted over budget. Adapters up to"
    say "  the last checkpoint are in $ADAPTERS."
    say "  Re-run with a smaller SEQ_LEN, or raise MEM_LIMIT_GB only if you have"
    say "  measured headroom (physical RAM is ${RAM_GB} GB, Metal recommends ~17.8)."
    exit 1
  fi
  [[ $TRAIN_RC -ne 0 ]] && { printf '\n'; say "${RED}training failed:${R}"; tail -30 "$TRAINLOG"; exit 1; }
  mark_done train; bar 1 "training complete"; printf '\n'

  say ""; say "  ${B}loss trajectory${R}"
  grep -E 'Val loss' "$TRAINLOG" | tail -12 | sed 's/^/    /' | tee -a "$LOG"
  best=$(grep -oE 'Val loss [0-9.]+' "$TRAINLOG" | grep -oE '[0-9.]+' | sort -n | head -1 || true)
  final=$(grep -oE 'Val loss [0-9.]+' "$TRAINLOG" | grep -oE '[0-9.]+' | tail -1 || true)
  say "    best ${best:-n/a}   final ${final:-n/a}"
  if [[ -n "${best:-}" && -n "${final:-}" ]] && awk -v b="$best" -v f="$final" 'BEGIN{exit !(f > b*1.02)}'; then
    say "    ${YEL}final val loss above the best — overfitting past the minimum.${R}"
    say "    ${YEL}Consider an earlier checkpoint in $ADAPTERS.${R}"
  fi
else
  say "  ${DIM}skipped${R}"
fi

# ============================================================== 5. fuse

banner 4 "fuse — merge adapters into base weights"
if should_run fuse; then
  bar 0.3 "mlx_lm.fuse"
  python3 -m mlx_lm fuse --model "$BASE_HF" --adapter-path "$ADAPTERS" \
    --save-path "$FUSED" >>"$LOG" 2>&1
  mark_done fuse; bar 1 "fused"; printf '\n'
else say "  ${DIM}skipped${R}"; fi

# ============================================================== 6. convert

banner 5 "convert — safetensors -> f16 GGUF"
if should_run convert; then
  need_input "$FUSED" fuse
  bar 0.3 "convert_hf_to_gguf.py"
  python3 "$LLAMA_SRC/convert_hf_to_gguf.py" "$FUSED" --outfile "$GGUF_F16" --outtype f16 >>"$LOG" 2>&1
  mark_done convert; bar 1 "$(du -h "$GGUF_F16" | cut -f1)"; printf '\n'
else say "  ${DIM}skipped${R}"; fi

# ============================================================== 7. quantize

banner 6 "quantize — f16 -> Q8_0"
if should_run quantize; then
  need_input "$GGUF_F16" convert
  bar 0.3 "llama-quantize"
  llama-quantize "$GGUF_F16" "$GGUF_Q8" Q8_0 >>"$LOG" 2>&1
  mark_done quantize; bar 1 "$(du -h "$GGUF_Q8" | cut -f1)"; printf '\n'
  trim_intermediates
else say "  ${DIM}skipped${R}"; fi

# ============================================================== 8. evaluate

banner 7 "evaluate — all three metric tiers"
if should_run eval; then
  need_input "$GGUF_Q8" fuse
  # shellcheck disable=SC2086
  llama-server -m "$GGUF_Q8" --port "$PORT_CAND" $SERVE_ARGS >>"$LOG" 2>&1 &
  CAND_PID=$!
  wait_health "$PORT_CAND" 240
  HELD=$(python3 -c "import sys; sys.path.insert(0,'$FT/corpus'); import splits; print(','.join(splits.HELDOUT_BEHAVIORS))")

  # EVAL_TIERS lets a sweep run only tier 2 (~6 min instead of ~15) since that
  # is the metric the first LoRA actually broke. The winner then gets a full
  # 3-tier run — a sweep that never checks tier 3 cannot declare a winner,
  # because tiers 1 and 2 both rise when the model overfits to Yope3D.
  want() { [[ " $EVAL_TIERS " == *" $1 "* ]]; }

  if want 1; then
    bar 0.10 "tier 1 — yope3d held-out"
    python3 "$FT/harness/fim_eval3.py" --port "$PORT_CAND" --label "$TAG" \
      --only "$HELD" --out "$FT/data/eval_heldout_${TAG}.json" >>"$LOG" 2>&1
  fi
  if want 2; then
    bar 0.35 "tier 2 — API probes (296)"
    python3 "$FT/harness/probe_eval.py" --port "$PORT_CAND" --label "$TAG" \
      --out "$FT/data/probe_${TAG}.json" >>"$LOG" 2>&1
  fi
  if want 3; then
    bar 0.65 "tier 3 — control stdlib"
    python3 "$FT/harness/fim_eval3.py" --port "$PORT_CAND" --label "$TAG" \
      --dir "fim-finetuning/corpus/control/stdlib" --cuts 8 \
      --out "$FT/data/ctl_stdlib_${TAG}.json" >>"$LOG" 2>&1
    bar 0.85 "tier 3 — control local"
    python3 "$FT/harness/fim_eval3.py" --port "$PORT_CAND" --label "$TAG" \
      --dir "fim-finetuning/corpus/control/local" --cuts 18 \
      --out "$FT/data/ctl_local_${TAG}.json" >>"$LOG" 2>&1
  fi

  kill "$CAND_PID" 2>/dev/null || true; CAND_PID=""
  mark_done eval; bar 1 "all tiers evaluated"; printf '\n'
  trim_servable
else say "  ${DIM}skipped${R}"; fi

# ============================================================== 9. report

banner 8 "report"
STAGE_IDX=8; bar 1 "done" 0; printf '\n\n'
mark_done report
python3 "$FT/harness/report_all.py" --cand-tag "$TAG" 2>&1 | tee -a "$LOG"

cat <<EOF

${B}run summary${R}
  wall time             $(hms $(( $(date +%s) - T0 )))
  tag / iterations      ${TAG} / ${ITERS} @ rank ${RANK}, lr ${LR}, seq ${SEQ_LEN}
  peak memory           $(peak_mem "$TRAINLOG" 2>/dev/null || echo n/a) GB of ${MEM_LIMIT_GB} GB ceiling
  best / final val loss ${best:-n/a} / ${final:-n/a}
  serving artifact      ${GGUF_Q8#$REPO_ROOT/}
  logs                  ${LOG#$REPO_ROOT/}, ${TRAINLOG#$REPO_ROOT/}

${B}to serve it${R}
  llama-server -m ${GGUF_Q8#$REPO_ROOT/} --port 8012 $SERVE_ARGS
  python3 tools/fim_proxy.py

${DIM}Read the verdict above before switching. A tier-1/tier-2 gain with a tier-3
regression is not a win.${R}
EOF

trap - EXIT
