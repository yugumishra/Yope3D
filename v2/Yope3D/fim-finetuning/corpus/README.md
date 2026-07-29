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

**Does not:** the CLAUDE.md invariants. `commitEntities` + `commitFinalizeScoped`
for live spawning, physics-bodies-are-hierarchy-roots, never `reset_physics()`
from a script. Those are *ordering and composition* rules that no type signature
encodes, so no signature-driven generator can emit them — and they are the
failures nothing catches. A pyright squiggle catches a wrong name; nothing
catches a correctly-named call in a subtly wrong order. That is the hand-written
tier's job (PLAN.txt 9.3 item 3).

So this output is **broad but shallow**. Weight it at training time rather than
letting line count decide.

## Three gates on generated code

`synth_pyi.py` self-validates every file, every run, and exits non-zero on any
failure:

1. **it parses** — the first draft emitted method bodies at the wrong indent and
   was not valid Python at all
2. **no invented bindings** — every `yope3d.X` resolves against the stub
3. **no undefined names** — approximate in-order dataflow check

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

## Still missing

- The **non-Yope3D control set** (PLAN.txt 9.4) — a few thousand lines of
  ordinary Python the LoRA never sees, as a catastrophic-forgetting detector. If
  general Python accept rate drops while Yope3D rises, the trade was bad.
- The **API-usage probe set** (PLAN.txt 10) — the invented-binding metric fires
  on ~3 events per run, far too sparse to track progress against.
- The **hand-written invariant tier**.
