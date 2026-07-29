# Corpus tooling

Builds the FIM fine-tuning dataset. Read `../PLAN.txt` first — sections 9.2
(format fidelity) and 9.4 (the split) are what these scripts implement.

## Pipeline

```bash
# 1. GATE FIRST — needs a live llama-server. Nothing below is valid until this
#    prints GATE PASSED.
llama-server -hf ggml-org/Qwen2.5-Coder-1.5B-Q8_0-GGUF --port 8012 \
  -c 8192 -np 1 -fa on --cache-reuse 256 -ngl 99 -ub 1024 &
python3 corpus/verify_format.py --port 8012

# 2. Then build
python3 corpus/synth_pyi.py                  # .pyi -> 400 synthetic .py files
python3 corpus/make_dataset.py               # .py  -> train.jsonl / valid.jsonl
```

Generated output (`corpus/synth/`, `data/`) is gitignored — it is deterministic
from a fixed seed, so the scripts are the artifact, not their output.

## Files

| File | Role |
|---|---|
| `fim_format.py` | **The** FIM wire format. Nothing else may hand-roll `<\|fim_prefix\|>`. |
| `pyi_api.py` | Parses the stub into a structured API model via `ast`. |
| `synth_pyi.py` | Generates correct-by-construction Python from that model. |
| `make_dataset.py` | Cuts .py (real + synthetic) into FIM JSONL, file-level split. |
| `verify_format.py` | Token-ID diff against a live server. **Must pass before training.** PASSED 2026-07-28. |

## The format gate — PASSED 2026-07-28

`verify_format.py` compares *token IDs* (not strings — `<|fim_prefix|>` is one
token, a typo'd `<|fim-prefix|>` is five that look identical in a terminal)
between a server-built prompt and ours. Status against llama.cpp b9890:

```
no input_extra (repo header still emitted)     PASS — 34 tokens
one input_extra chunk                          PASS — 50 tokens
two input_extra chunks (separator boundary)    PASS — 64 tokens
chunk text with no trailing newline            PASS — 47 tokens
```

**Re-run it after any llama.cpp upgrade.** The format is not a stable contract.

### It caught two real errors, and the second was the dangerous one

The first run failed at token 0. Both errors were in the reconstruction:

1. **There is no file-level-only shape.** llama.cpp emits the repo header
   unconditionally, even with no `input_extra`. Our bare `<|fim_prefix|>...`
   diverged immediately.

2. **The repo name and target filename are hardcoded placeholders** — the
   literal strings `myproject` and `filename`. Probing `repo_name`, `filename`,
   and `path` request fields showed *none* of them change the output. Only
   `input_extra` entries carry real paths.

Error 2 is the one worth understanding. The natural thing to do — and what the
reconstruction did — is put the real repo name and real file path in the
training examples. That produces a corpus which is structurally correct,
tokenizes fine, trains without complaint, and mismatches inference on precisely
the tokens that anchor the repo-level format. It would have cost a full training
run plus conversion plus eval to find, and the symptom would have been "the
fine-tune just didn't help much" — indistinguishable from a bad hyperparameter.

The verified shape, which `fim_format.render_prompt` now emits:

```
<|repo_name|>myproject
<|file_sep|>{extra.path}
{extra.text}<|file_sep|>filename
<|fim_prefix|>PREFIX<|fim_suffix|>SUFFIX<|fim_middle|>MIDDLE
```

## What the generator does and does not teach

**Does:** uniform coverage of the callable API — 167/187 top-level names, all 27
components, the `reg_add` → configure pattern, `view()` loops, `KEY_*`/`EASE_*`
constants in real call sites. The 19 hand-written behaviors cover a narrow slice
(`add_sphere`, `get_position`, some UI); this fills the rest.

**Does not:** the CLAUDE.md invariants — physics-bodies-are-hierarchy-roots, the
composite-behavior stacking pattern. Those are *ordering and composition* rules
that no type signature encodes, so no signature-driven generator can emit them.
A pyright squiggle catches a wrong name; nothing catches a correctly-named call
in a subtly wrong order.

So this output is **broad but shallow**. Weight it at training time rather than
letting line count decide.

**Must not:** teach a violation. A signature-driven generator cannot know a
binding is forbidden — `reset_physics` presents as an ordinary zero-arg `World`
method with the docstring *"Clear all velocities/contacts and wake everything"*,
while the prohibition lives in prose. So the generator emitted it **56 times
across 53 of 400 files** (~550 examples at 10× redundancy) against **zero**
counter-examples: the one place the rule is written in Python is `_gallery.py`,
which `make_dataset.py:103` skips as `_`-prefixed.

That is worse than an untaught invariant, and it is invisible to every metric
here — tier 2 scores it `correct` because the name is real. Fixed at generation
time via `FORBIDDEN` + `validate()` gate 4 rather than by writing counter-
examples, which would have left the 56 in place. Read the ban as *wrong caller*,
not *wrong name*: it is legitimate from the editor and from C++, and this corpus
is 100% behavior scripts.

The hand-written invariant tier was **cut** on 2026-07-29 — PLAN.txt §6.8 has
the full reasoning, including why the deciding argument was the absence of a
metric rather than the rarity of the calls.

## Four gates on generated code

`synth_pyi.py` self-validates every file, every run, and exits non-zero on any
failure:

1. **it parses** — the first draft emitted method bodies at the wrong indent and
   was not valid Python at all
2. **no invented bindings** — every `yope3d.X` resolves against the stub
3. **no undefined names** — approximate in-order dataflow check
4. **no forbidden calls** — nothing in `FORBIDDEN` is called, however spelled

Gate 4 is redundant with the sampler filter *today*, which is the point: the
filter lives at the call sites that exist now, and a snippet function added
later would bypass it with nothing downstream complaining — a forbidden call is
real, parses, type-checks, and scores as `correct` on tier 2. Only the gate
fails loudly. It was tested in both directions before being trusted.

Gates 2 and 3 both caught real bugs in the generator itself. It emitted
`yope3d.normalize(...)` and `yope3d.quat_from_axis_angle(...)`, neither of which
exists — `length`/`normalize` are methods on `Vec3`, `from_axis_angle` is a
staticmethod on `Quat`. That is precisely the C++-flavoured mis-mapping the model
already makes, and the corpus exists to *fix* it. A generator that invents
bindings would train the exact failure it was built to remove.

## Numbers that shape the training config

Measured on the current corpus (419 files → 4,971 train / 145 valid examples):

- **9.9× redundancy.** Every cut point re-embeds up to 120 lines of prefix, so
  one source line appears in ~10 examples. **One epoch is already ~10 passes
  over the unique text.** Budget 1–2 epochs, not the 2–4 in PLAN.txt 9.6 — at
  this ratio 3 epochs is ~30 passes and straight memorisation.
- **p95 ≈ 2,190 tokens.** Use seq len **3072**, not 2048. Trainers truncate from
  the end, which is where `MIDDLE` lives; a truncated example has no target and
  teaches nothing.
- **145 validation examples** across 4 held-out files spanning the measured
  difficulty tiers (62.5% / 37.5% / 0% / 0% exact). Thin but usable; the split is
  by *file* because a random-line split leaks — train on line 100, evaluate on
  line 101 whose prefix contains line 100, and you measure memorisation.

## The API-usage probe set (tier-2 metric)

```bash
# probe-only synth batch — different seed, NOT a training source
python3 corpus/synth_pyi.py --files 120 --seed 777 --body-min 4 --body-max 8 \
  --out fim-finetuning/corpus/probe_synth
python3 corpus/probe_set.py                                   # -> data/probes.json
python3 harness/probe_eval.py --port 8012 --label base \
  --out fim-finetuning/data/probe_base.json
python3 harness/probe_eval.py --compare data/probe_base.json data/probe_lora.json
```

Each probe cuts the prefix at exactly `yope3d.`; the model must name the
binding. It solves the sparsity that made tier 2 unusable — **36 invention
events from 296 probes**, against ~3 from the old 152-case harness.

Four verdicts, and the middle split is the point: `correct` / **`invented`**
(name doesn't exist — what the LoRA must remove) / `wrong` (real name, wrong
one — confusion, reviewable) / `none`. Pooling invented and wrong as
"not correct" would hide the only distinction that matters.

### Baseline — 1.5B, no fine-tuning

| | n | correct | invented | wrong | none |
|---|---:|---:|---:|---:|---:|
| **ALL** | 296 | 25.0% | 12.2% | 57.1% | 5.7% |
| real | 60 | 80.0% | 10.0% | 8.3% | 1.7% |
| stub | 36 | 50.0% | 11.1% | 22.2% | 16.7% |
| synth | 200 | 4.0% | 13.0% | 78.0% | 5.0% |
| **head** | 79 | **74.7%** | 11.4% | 7.6% | 6.3% |
| **tail** | 217 | **6.9%** | 12.4% | 75.1% | 5.5% |

**The 68-point head/tail gap is the headline.** The base model handles common
names it can infer from context and has essentially no knowledge of the
engine-unique surface. That gap is the fine-tuning target, and it is now
visible. Typical inventions: `reg_set` (plausible partner to `reg_get`,
doesn't exist), `set_mesh_color`, `set_camera_position`, `AudioFadeType`.

### Read this before interpreting a post-LoRA run

**The most trustworthy stratum has the least headroom.** `real` is 92% head
names (55/60) and already scores 80% — it cannot show a large gain even if the
fine-tune works perfectly. Movement will appear in `tail`/`synth` first. A flat
`real` number is a ceiling, not a failure. Fixing this properly needs held-out
real files with rare-API usage, which the 19-file behavior corpus doesn't have.

And a gain confined to `synth` is generator memorisation, not API learning —
it shares a generator with the training data even though it shares no files.

### Two method bugs found while building it

- Probes were keyed on `(origin, line)`, which **isn't unique** — a line can
  carry two calls (`yope3d.camera.set_rotation(yope3d.Vec3(p, y, 0))`). 19 of
  296 collided. The paired comparison would have silently dropped one side of
  each and could mispair the rest, corrupting the exact base-vs-LoRA test the
  set exists for. Probes now carry an explicit `id`.
- Synthetic sampling was tail-first with no per-name cap, so `play_sound` took
  51 of 200 slots and the set measured one binding rather than the surface.
  Capped at 4/name; distinct targets 72 → 95.

Reproducibility was **checked, not assumed**: two identical 120-probe runs
agreed 119/119 on both emitted name and verdict. Deltas are signal.

## Tier 3 — the control set (forgetting detector)

```bash
python3 corpus/control_set.py          # -> corpus/control/{stdlib,local}
python3 harness/fim_eval3.py --port 8012 --dir fim-finetuning/corpus/control/stdlib \
  --cuts 8  --out fim-finetuning/data/ctl_stdlib_base.json
python3 harness/fim_eval3.py --port 8012 --dir fim-finetuning/corpus/control/local \
  --cuts 18 --out fim-finetuning/data/ctl_local_base.json
```

**Not optional.** Tiers 1 and 2 *both rise* when a LoRA overfits to Yope3D —
they cannot tell "learned the API" from "forgot everything else". Tier 3 is the
only thing here that sees the cost, and it must be captured **before** training.

Two strata, because they fail differently:

- **stdlib** — 21 modules, 9,154 lines. Real and diverse, but almost certainly
  in Qwen2.5-Coder's *pretraining* data, probably verbatim. That's right for a
  forgetting detector (retained knowledge is what we guard) and weak as a proxy
  for novel code.
- **local** — 4 files, 763 lines from `tools/`. Recent, plausibly unmemorised,
  so it's the **more sensitive** detector. Small n, directional only. Anything
  importing `yope3d` is auto-rejected as in-domain.

The memorisation gap is visible in the baseline and validates the split: at 0%
typed, **stdlib scores 39.3% full-line exact vs local 23.6%** — same harness,
same model, the difference is prior exposure. Expect `local` to move first.

## Full three-tier baseline (1.5B, no fine-tuning)

```
python3 harness/report_all.py                        # baseline only
python3 harness/report_all.py --cand-tag lora        # after training
```

| tier | metric | base |
|---|---|---:|
| 1 · yope3d held-out | mean chars saved | 16.6 |
| 2 · API probes | % correct | 25.0 |
| | % invented | 12.2 |
| 3 · control stdlib | mean chars saved | 13.6 |
| 3 · control local | mean chars saved | 12.5 |

Tier 1 is the **held-out files only** (`--only`) — the number a fine-tune has to
beat. It reads higher than the all-files 14.3 elsewhere because those four files
aren't representative of the whole corpus; **don't compare across the two.**

`report_all.py` prints all three tiers in one table and states a verdict,
because reading them in separate scrollbacks is how a regression gets missed. It
was checked against a simulated overfit (tier 1 +4.0, tier 2 +12.5pp, tier 3
down) and correctly refuses to call that a win. Its −0.5 flag threshold sits
inside the ~±1 noise floor deliberately — it prompts a re-run, and says so,
rather than pretending to be a significance test.

## Still missing

- **Composite-behavior stacking** and **physics-bodies-are-roots** stay
  untaught. Both are multi-line composition — the case where "FIM only emits
  1–2 lines" genuinely holds — and neither is currently *mis*-taught, which is
  the distinction that matters. Accepted, not deferred: see PLAN.txt §6.8.
- **No tier measures invariant compliance**, and none is planned. Do not read a
  tier-2 gain as evidence the model respects them.
- Held-out **real** files exercising rare APIs — the probe `real` stratum is at
  its ceiling without them, and the 19-file behavior corpus doesn't have them.

## Regenerating: what NOT to rebuild

`corpus/probe_synth/` is **not** a training source and must not be regenerated
casually — `data/probes.json` is built from it and `data/probe_base.json` is
keyed to those exact probe ids. Regenerating it silently unpairs the baseline
and degrades the base-vs-LoRA comparison to whatever ids happen to intersect.
It was deliberately left untouched by the 2026-07-29 denylist change: the banned
call is spelled `world.reset_physics()`, probes cut at `yope3d.`, so it can
never be a probe target (verified 0 of 296) and the baseline stays valid.
