#!/usr/bin/env bash
#
# Sweep 2 — does the SYNTHETIC CORPUS cause the damage, or does LoRA?
#
#   ./fim-finetuning/sweep2.sh                # ~2.5 h, two runs
#   ./fim-finetuning/sweep2.sh --dry-run      # print the design and the costs
#
# WHAT SWEEP 1 ESTABLISHED (runs/_sweep/results.tsv, 2026-07-30)
#     Score = tier-2 %correct - %invented, over 296 paired probes.
#
#       base    lr    —      iters    —     correct 25.0  invented 12.2   +12.8
#       C1      1e-6  r8     400   lam 0.0          25.3           16.9    +8.4
#       B1      1e-6  r8     400   lam 0.25         24.7           16.6    +8.1
#       A1      3e-6  r8     400                    24.3           23.3    +1.0
#       B2      3e-6  r8     800                    24.3           31.4    -7.1
#       A2      1e-5  r8     400                    22.0           34.1   -12.2
#       07-29   1e-5  r8    2485   uniform          19.9           36.8   -16.9
#
#     Three facts, all from paired McNemar over the same 296 probes:
#
#     1. NOTHING BEAT BASE, and score is perfectly monotone in lr x iters.
#        The best node is significantly WORSE: base -> C1, invented 12.2 ->
#        16.9, p = 0.024, score -4.4 (95% CI [-8.4, -0.3]).
#     2. CORRECT% NEVER MOVES. base -> C1 is 0 probes lost, 1 gained out of
#        296. Across every config the LoRA has never taught the model an API
#        name it did not already produce. All of the score signal is invention.
#     3. MORE ITERATIONS IS STRICTLY HARMFUL. A1 -> B2 varies only 400 -> 800
#        iters: invented 23.3 -> 31.4, p = 0.0012, score -8.1. The 2485-iter
#        run is the worst score on record.
#
#     And validation loss is ANTI-correlated with the objective (Spearman -1.0
#     across the five nodes): the run with the best val loss (A2, 1.322) scored
#     worst, the run with the worst val loss (C1, 1.481) scored best. Val loss
#     measures conformity to the synthetic corpus, and that conformity is the
#     damage. Do not select on it.
#
# WHY THIS SWEEP IS TWO RUNS AND NOT FIVE
#     The lr axis is exhausted: score rises monotonically as lr x iters falls,
#     with base as the asymptote. Lowering lr further cannot beat base, it can
#     only converge to it. The lam axis is exhausted too — B1 -> C1 varies only
#     lam 0.25 -> 0.0 and moved +0.3 (95% CI [-1.4, +2.0]), a clean null.
#
#     One untested mechanism is left: the synthetic corpus itself. lam changed
#     the synthetic NAME DISTRIBUTION and did nothing, but it never touched the
#     synthetic STRUCTURE — 60 generated files of interchangeable method bodies,
#     62.5% of the source text. Removing them entirely is the experiment.
#
# THE DESIGN — two runs, one variable each
#     D1  no-synth, rank 8, lr 1e-6, 360 iters
#         Dose- and rank-matched to B1 (1e-6 x 400 = 4.0e-4 vs 3.6e-4). The
#         ONLY difference from B1 is the corpus. Isolates it exactly.
#
#     D2  no-synth, rank 8, lr 3e-6, 360 iters
#         Dose-matched to A1 (3e-6 x 400 = 1.2e-3 vs 1.08e-3). THE FALSIFIER.
#         If the synthetic text is the damage mechanism, tripling the dose
#         without it should NOT reproduce A1's invention spike. If invention
#         climbs back to ~23% anyway, the mechanism is LoRA on this task and
#         the honest call is to stop and ship the base model.
#
#     Rank stays at 8 deliberately. Lower rank is the intuitive move for a
#     smaller corpus, but rank is not what the data implicates — dose is, and
#     at 1e-6 x 360 the weights barely move, so overfitting is not the binding
#     constraint (800 iters at 3e-6 was). Dropping to rank 4 would confound the
#     one comparison worth making. Tune rank only if D1 shows life.
#
# THE CORPUS SWAP IS BACKED UP, NOT REGENERATED
#     data/{train,valid}.jsonl are shared state. This script copies them aside
#     and restores the exact bytes from an EXIT trap — on success, failure, or
#     Ctrl-C. Regenerating instead would depend on synth_pyi.py determinism and
#     cost minutes; a byte-exact restore depends on nothing.
#
set -euo pipefail

REPO_ROOT="${YOPE_REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$REPO_ROOT"
FT="$REPO_ROOT/fim-finetuning"
TRAIN="$FT/train_lora.sh"
SWEEP_DIR="$FT/runs/_sweep2"
RESULTS="$SWEEP_DIR/results.tsv"
BACKUP="$SWEEP_DIR/corpus_backup"

BUDGET_H="${BUDGET_H:-3}"
ITERS_PER_RUN="${ITERS_PER_RUN:-360}"   # 2.0 epochs of the 180-example no-synth corpus
CUTS="${CUTS:-12}"                      # 12 -> 180 train examples, redundancy 4.5x
RANK_D="${RANK_D:-8}"
DRY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --budget)  BUDGET_H="$2"; shift 2 ;;
    --iters)   ITERS_PER_RUN="$2"; shift 2 ;;
    --cuts)    CUTS="$2"; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) sed -n '2,80p' "$0"; exit 0 ;;
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

say() { echo "$*"; }
hms() { local s=${1%.*}; printf '%dh%02dm' $((s/3600)) $(((s%3600)/60)); }
left() { echo $(( DEADLINE - $(date +%s) )); }

# ------------------------------------------------------- keep the mac awake
# Same rationale as sweep.sh: macOS idle sleep keys off HID input, not GPU
# load, and this machine's AC idle timer is 1 minute. `-w $$` scopes the
# assertion to this script's exact lifetime. Lid must stay open regardless.
CAFFEINATE_PID=""
keep_awake() {
  if [[ "${KEEP_AWAKE:-1}" != "1" ]]; then
    say "  ${DIM}KEEP_AWAKE=0 — not holding a wake assertion${R}"; return 0
  fi
  if ! command -v caffeinate >/dev/null 2>&1; then
    say "  ${YEL}caffeinate not found — the mac may sleep mid-run${R}"; return 0
  fi
  local flags="-ims"
  if [[ "${KEEP_DISPLAY:-0}" == "1" ]]; then flags="-dims"; fi
  caffeinate $flags -w $$ &
  CAFFEINATE_PID=$!
  say "  ${DIM}holding wake assertion (caffeinate $flags, pid $CAFFEINATE_PID) for this run${R}"
}

check_power() {
  local hours="$1" src pct
  src=$(pmset -g ps 2>/dev/null | head -1)
  pct=$(pmset -g ps 2>/dev/null | grep -oE '[0-9]+%' | head -1)
  if [[ "$src" == *"AC Power"* ]]; then say "  ${DIM}on AC power${R}"; return 0; fi
  say "${RED}ON BATTERY (${pct:-?}) — a ${hours}h run needs AC power.${R}"
  say "  Sustained GPU load drains an M4 Air in roughly an hour. The run will"
  say "  not finish, and macOS sleeps on low battery regardless of caffeinate."
  say "  Plug in, then re-run. Set ALLOW_BATTERY=1 to override."
  if [[ "${ALLOW_BATTERY:-0}" != "1" ]]; then return 1; fi
  say "  ${YEL}ALLOW_BATTERY=1 — proceeding anyway${R}"
}

# ------------------------------------------------------- corpus swap/restore
#
# The restore runs from the EXIT trap so a crash, a budget skip, or Ctrl-C all
# leave data/ exactly as found. Never `kill` a pid as the last command of an &&
# list in here: that is the set -e trap that silently ended two multi-hour runs
# (see stop_swap_watchdog in train_lora.sh).
CORPUS_SWAPPED=0
restore_corpus() {
  if [[ $CORPUS_SWAPPED -eq 0 ]]; then return 0; fi
  if [[ -f "$BACKUP/train.jsonl" && -f "$BACKUP/valid.jsonl" ]]; then
    cp "$BACKUP/train.jsonl" "$FT/data/train.jsonl"
    cp "$BACKUP/valid.jsonl" "$FT/data/valid.jsonl"
    rm -f "$FT/data/token_stats.json"
    say "  ${DIM}restored the mixed corpus from $BACKUP (byte-exact)${R}"
  else
    say "  ${RED}BACKUP MISSING — data/ still holds the no-synth corpus.${R}"
    say "  ${RED}Rebuild with: python3 fim-finetuning/corpus/make_dataset.py${R}"
  fi
  CORPUS_SWAPPED=0
}

CLEANED=0
cleanup() {
  local rc=$?
  if [[ $CLEANED -eq 1 ]]; then return 0; fi
  CLEANED=1
  if [[ -n "${CAFFEINATE_PID:-}" ]]; then
    kill "$CAFFEINATE_PID" 2>/dev/null || true
  fi
  printf '\n'
  restore_corpus
  if [[ $rc -ne 0 && $rc -ne 130 ]]; then
    say "${RED}sweep2 exited $rc after $(hms $(( $(date +%s) - T0 )))${R}"
    say "  logs: $SWEEP_DIR"
  fi
}
# An INT/TERM trap runs and then CONTINUES — bash does not exit for you. Without
# the explicit exit here, Ctrl-C during D1 restores the mixed corpus and then
# starts D2 on it: a run that looks normal in every log while training the wrong
# data. Measured, not theorised. The EXIT trap still fires, hence the guard.
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

swap_to_nosynth() {
  mkdir -p "$BACKUP"
  cp "$FT/data/train.jsonl" "$BACKUP/train.jsonl"
  cp "$FT/data/valid.jsonl" "$BACKUP/valid.jsonl"
  CORPUS_SWAPPED=1
  say "  ${DIM}backed up the mixed corpus -> $BACKUP${R}"
  python3 "$FT/corpus/make_dataset.py" scripts/behaviors --cuts "$CUTS" \
    | sed 's/^/    /'
  rm -f "$FT/data/token_stats.json"
}

# ------------------------------------------------------------------- scoring
score_of() {
  python3 - "$1" <<'PY'
import json, sys
tag = sys.argv[1]
try:
    rows = json.load(open(f"fim-finetuning/data/probe_{tag}.json"))["rows"]
except Exception:
    print("NA NA NA"); raise SystemExit
n = len(rows)
c = 100 * sum(1 for r in rows if r["verdict"] == "correct") / n
i = 100 * sum(1 for r in rows if r["verdict"] == "invented") / n
print(f"{c:.1f} {i:.1f} {c-i:.1f}")
PY
}

# run_cfg <tag> <lr> <iters> <why>
run_cfg() {
  local tag="$1" lr="$2" iters="$3" why="$4"
  if [[ -f "$SWEEP_DIR/$tag.done" ]]; then
    say "  ${DIM}$tag already done — skipping${R}"; return 0
  fi
  local est=$(( iters * 12 + 500 ))
  if (( $(left) < est )); then
    say "  ${YEL}SKIP $tag — needs ~$(hms $est), only $(hms $(left)) left in budget${R}"
    return 1
  fi
  say "  ${B}running $tag${R}  lr=$lr rank=$RANK_D iters=$iters  ${DIM}— $why${R}"
  if [[ $DRY -eq 1 ]]; then say "    ${DIM}(dry-run)${R}"; return 0; fi
  # TRIM_LEVEL=2 drops fused/, the f16 GGUF and the Q8_0 once tier 2 has read
  # it; adapters/ stays (8 MB) and re-fusing is ~21 s.
  if ! LR="$lr" RANK="$RANK_D" ITERS_OVERRIDE="$iters" EVAL_TIERS="2" \
       TRIM_LEVEL=2 MAX_HOURS=99 "$TRAIN" --tag "$tag" \
       >"$SWEEP_DIR/$tag.out" 2>&1; then
    say "  ${RED}$tag FAILED — see $SWEEP_DIR/$tag.out${R}"
    return 1
  fi
  local c i s
  read -r c i s <<<"$(score_of "$tag")"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$tag" "$lr" "$RANK_D" "$iters" "$c" "$i" "$s" >>"$RESULTS"
  touch "$SWEEP_DIR/$tag.done"
  local verdict="${RED}below base${R}"
  if awk -v s="$s" -v b="$BASE_SCORE" 'BEGIN{exit !(s>b)}'; then
    verdict="${GRN}BEATS BASE${R}"
  fi
  say "  -> correct ${c}%  invented ${i}%  score ${s}  $verdict"
  return 0
}

# ---------------------------------------------------------------- paired test
# The whole point of the sweep is a handful of ~5-point differences on 296
# probes, which is right at the edge of what that n can resolve. Reporting the
# point estimate alone would invite reading noise as signal, so every headline
# comparison gets an exact-binomial McNemar over the paired rows plus a
# bootstrap CI on the score delta.
paired() {  # paired <from> <to> <label>
  python3 - "$1" "$2" "$3" <<'PY'
import json, random, sys
from math import comb
a_t, b_t, label = sys.argv[1], sys.argv[2], sys.argv[3]
def load(t):
    return json.load(open(f"fim-finetuning/data/probe_{t}.json"))["rows"]
try:
    A, Bv = load(a_t), load(b_t)
except Exception as e:
    print(f"    {label}: unavailable ({e})"); raise SystemExit
if len(A) != len(Bv):
    print(f"    {label}: probe sets differ in length — not paired"); raise SystemExit
n = len(A)
def mcnemar(b, c):
    nn = b + c
    if nn == 0: return 1.0
    k = min(b, c)
    return min(1.0, 2 * sum(comb(nn, i) for i in range(k + 1)) / 2 ** nn)
print(f"    {label}   ({a_t} -> {b_t}, n={n})")
for v in ("correct", "invented"):
    x = [r["verdict"] == v for r in A]
    y = [r["verdict"] == v for r in Bv]
    lost = sum(1 for p, q in zip(x, y) if p and not q)
    gain = sum(1 for p, q in zip(x, y) if q and not p)
    print(f"      {v:9} {100*sum(x)/n:5.1f}% -> {100*sum(y)/n:5.1f}%"
          f"   lost {lost:3} gained {gain:3}   p={mcnemar(lost, gain):.4g}")
rnd = random.Random(7); d = []
for _ in range(4000):
    s = [rnd.randrange(n) for _ in range(n)]
    def sc(rows):
        rs = [rows[i] for i in s]
        return (100*sum(1 for r in rs if r["verdict"]=="correct")/n
                - 100*sum(1 for r in rs if r["verdict"]=="invented")/n)
    d.append(sc(Bv) - sc(A))
d.sort()
print(f"      score delta {d[len(d)//2]:+.1f}"
      f"   95% CI [{d[int(.025*len(d))]:+.1f}, {d[int(.975*len(d))]:+.1f}]")
PY
}

# ==========================================================================

# A dry run costs no power and no GPU time, so it must not be gated on AC.
if [[ $DRY -eq 0 ]]; then
  check_power "$BUDGET_H" || exit 1
  keep_awake
fi

if [[ ! -f "$RESULTS" ]]; then
  printf 'tag\tlr\trank\titers\tcorrect\tinvented\tscore\n' >"$RESULTS"
fi

cat <<EOF

${B}Yope3D FIM LoRA — sweep 2: is it the corpus, or is it LoRA?${R}
${DIM}budget ${BUDGET_H}h · ${ITERS_PER_RUN} iters/run · rank ${RANK_D} · tier-2 objective${R}

  reference   base ${BASE_SCORE}      best of sweep 1: C1 +8.4      07-29 LoRA -16.9

  ${B}corpus${R}  scripts/behaviors ONLY — no synthetic files at all
          sweep 1 ran at 62.5% synthetic source text

  ${B}D1${R}  lr 1e-6  ${ITERS_PER_RUN} iters   dose- and rank-matched to B1 (+8.1)
      ${DIM}only difference from B1 is the corpus — isolates it exactly${R}
  ${B}D2${R}  lr 3e-6  ${ITERS_PER_RUN} iters   dose-matched to A1 (+1.0)  ${YEL}<- falsifier${R}
      ${DIM}without synth, does 3x the dose still spike invention to ~23%?${R}

  ${B}how to read the result${R}
    D1 >= base and D2 flat   -> the synthetic corpus was the damage. Continue,
                                then tune rank and dose on the real corpus.
    D1 ~ +8 and D2 ~ +1      -> synth was never the mechanism. LoRA on this
                                task adds invention and no correctness at any
                                dose. Stop, and ship the base model.
EOF

say ""
say "${B}=== corpus ===${R}"
if [[ $DRY -eq 1 ]]; then
  say "  ${DIM}(dry-run — corpus left untouched)${R}"
else
  swap_to_nosynth
fi

say ""
say "${B}=== runs ===${R}"
run_cfg D1 1e-6 "$ITERS_PER_RUN" "corpus isolated against B1" || true
run_cfg D2 3e-6 "$ITERS_PER_RUN" "falsifier: dose slope without synth" || true

if [[ $DRY -eq 1 ]]; then
  say ""
  say "${DIM}dry-run: 2 runs x ~$(hms $(( ITERS_PER_RUN * 12 + 500 ))) = ~$(hms $(( 2 * (ITERS_PER_RUN * 12 + 500) )))${R}"
  exit 0
fi

# ------------------------------------------------------------------- report
say ""
say "${B}=== results ===${R}"
{ printf 'tag\tlr\trank\titers\tcorr%%\tinv%%\tscore\n'
  printf 'base\t—\t—\t0\t25.0\t12.2\t+12.8\n'
  printf 'B1*\t1e-6\t8\t400\t24.7\t16.6\t+8.1\n'
  printf 'A1*\t3e-6\t8\t400\t24.3\t23.3\t+1.0\n'
  tail -n +2 "$RESULTS"
} | column -t -s$'\t' | sed 's/^/  /'
say "  ${DIM}* sweep-1 nodes on the 62.5%-synthetic corpus, shown for comparison${R}"

say ""
say "${B}=== paired tests ===${R}"
if [[ -f "$FT/data/probe_D1.json" ]]; then
  paired base D1 "D1 vs base       — did removing synth reach parity?"
  paired B1   D1 "D1 vs B1         — the corpus, isolated"
fi
if [[ -f "$FT/data/probe_D2.json" ]]; then
  paired A1 D2 "D2 vs A1         — same dose, no synth: did the spike survive?"
  paired D1 D2 "D2 vs D1         — the dose slope on the real-only corpus"
fi

# The verdict is mechanical on purpose. Sweep 1's lesson is that these numbers
# are easy to read hopefully — val loss looked like progress while the model
# got worse — so the decision rule is written down before the data arrives.
say ""
say "${B}=== verdict ===${R}"
python3 - "$BASE_SCORE" <<'PY' || true
import json, os, sys
base = float(sys.argv[1])
def sc(t):
    p = f"fim-finetuning/data/probe_{t}.json"
    if not os.path.exists(p): return None
    rows = json.load(open(p))["rows"]; n = len(rows)
    return (100*sum(1 for r in rows if r["verdict"]=="correct")/n
            - 100*sum(1 for r in rows if r["verdict"]=="invented")/n)
def inv(t):
    p = f"fim-finetuning/data/probe_{t}.json"
    if not os.path.exists(p): return None
    rows = json.load(open(p))["rows"]; n = len(rows)
    return 100*sum(1 for r in rows if r["verdict"]=="invented")/n
d1, d2 = sc("D1"), sc("D2")
i1, i2 = inv("D1"), inv("D2")
if d1 is None:
    print("  D1 did not produce a score — nothing to decide."); raise SystemExit
print(f"  D1 {d1:+.1f}   (base {base:+.1f}, B1 +8.1)")
if d2 is not None:
    print(f"  D2 {d2:+.1f}   (A1 +1.0)   invented {i2:.1f}% vs A1 23.3%")
print()
if d1 >= base:
    print("  D1 reached or beat base. The synthetic corpus WAS the damage.")
    print("  Next: confirm with a full 3-tier run, then tune rank and dose")
    print("  on the real-only corpus.")
elif d1 > 8.1 + 2.0:
    print("  D1 improved clearly on B1 but is still under base. The corpus is")
    print("  part of the mechanism, not all of it. A rank-4 / lower-dose node")
    print("  on this corpus is worth one more run before deciding.")
else:
    print("  D1 did not beat B1. Removing the synthetic corpus entirely bought")
    print("  nothing, which — with the lam null (B1->C1, +0.3) and the flat")
    print("  correct% across every config — says the corpus was never the")
    print("  mechanism. LoRA on this task adds invention and no correctness.")
    print("  RECOMMENDATION: stop fine-tuning. Ship the base model and put the")
    print("  effort into context (repo-level FIM chunks, .pyi injection),")
    print("  which is the only lever that has ever moved correct%.")
if d2 is not None and i2 is not None:
    print()
    if i2 < 18.0:
        print("  D2 held invention near the low-dose level at 3x the dose — the")
        print("  dose/invention slope flattened without synth. That is real")
        print("  headroom: push dose up on this corpus.")
    else:
        print(f"  D2 spiked invention to {i2:.1f}% at 3x dose, like A1 did with")
        print("  synth. The slope is a property of LoRA here, not the corpus.")
PY

say ""
say "${B}=== confirm a winner (only if one beat base) ===${R}"
say "  ${DIM}tier 2 alone is a nomination, not a verdict — tiers 1 and 2 both rise"
say "  when a model overfits to Yope3D, so tier 3 (catastrophic forgetting) has"
say "  to be checked before anything is called a result.${R}"
say "    EVAL_TIERS=\"1 2 3\" TRIM_LEVEL=1 ./fim-finetuning/train_lora.sh \\"
say "        --tag <BEST> --from fuse"
say "  ${YEL}that run needs the no-synth corpus back in data/ — this script"
say "  restores the mixed one on exit. Rebuild it with:${R}"
say "    python3 fim-finetuning/corpus/make_dataset.py scripts/behaviors --cuts $CUTS"

say ""
say "${DIM}elapsed $(hms $(( $(date +%s) - T0 )))  ·  budget $(hms $(( BUDGET_H*3600 )))  ·  logs $SWEEP_DIR${R}"
