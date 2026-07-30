#!/usr/bin/env python3
"""True validation loss for every saved LoRA checkpoint, on the FULL valid set.

WHY THIS EXISTS
    mlx-lm's in-training "Val loss" is not comparable across evals. evaluate()
    never passes a seed to iterate_batches, so `if seed:` is falsy, no
    np.random.seed() happens, and the batch permutation continues from whatever
    state the intervening training batches left the global RNG in. Every
    validation therefore scores a DIFFERENT random subset.

    That is survivable with a large sample and fatal with a small one. Worse,
    iterate_batches length-SORTS the dataset before forming batches, so batches
    are length-homogeneous and two draws can land in entirely different length
    regimes — and FIM loss depends strongly on how much context a cut has. The
    numbers are not merely noisy, they are systematically incomparable.

    The 2026-07-29 run used val_batches=8 at batch_size=1: 8 of 147 examples,
    5.4% of the set, redrawn each time. It produced 1.197 -> 1.592, which reads
    as textbook overfitting and is not evidence of anything.

WHAT THIS DOES
    Evaluates each checkpoint over the ENTIRE valid set (num_batches=-1, which
    with loop=False makes exactly one full pass). Same examples every time, so
    the numbers are comparable and a rising curve means what it looks like.

USAGE
    # after training finishes — do NOT run it alongside a live run, two MLX
    # processes on 24 GB unified memory is how the machine froze once already
    python3 fim-finetuning/harness/eval_checkpoints.py --run fim-finetuning/runs/lora
"""

from __future__ import annotations

import argparse
import json
import re
import types
from pathlib import Path

import mlx.core as mx
import yaml
from mlx_lm.tuner.datasets import CacheDataset, load_dataset
from mlx_lm.tuner.trainer import evaluate
from mlx_lm.tuner.utils import linear_to_lora_layers
from mlx_lm.utils import load

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--run", default=None,
                    help="runs/<tag> dir, or just a tag. Defaults to the most "
                         "recently modified directory under fim-finetuning/runs.")
    ap.add_argument("--config", default=None, help="defaults to <run>/lora_config.yaml")
    ap.add_argument("--out", default=None, help="defaults to <run>/checkpoint_val.json")
    a = ap.parse_args()

    # Anchor to the repo root, never the cwd — every other script under
    # corpus/ and harness/ does this, and the one that did not (synth_pyi.py)
    # silently wrote a phantom 400-file corpus. Accepts an absolute path, a
    # repo-relative path, or a bare tag.
    runs_dir = ROOT / "fim-finetuning" / "runs"
    if a.run is None:
        # Require a real run, not just any directory: a stray dir under runs/
        # would otherwise be picked as "most recent" and fail confusingly.
        cands = [d for d in runs_dir.iterdir()
                 if d.is_dir() and (d / "lora_config.yaml").exists()] \
                if runs_dir.exists() else []
        if not cands:
            raise SystemExit(f"no run directories under {runs_dir}")
        run = max(cands, key=lambda d: d.stat().st_mtime)
        print(f"(no --run given; using most recent: {run.name})")
    else:
        p = Path(a.run)
        run = p if p.is_absolute() else (ROOT / p if (ROOT / p).exists() else runs_dir / p)
    run = run.resolve()
    if not run.is_dir():
        raise SystemExit(f"not a directory: {run}")
    cfg_path = Path(a.config) if a.config else run / "lora_config.yaml"
    cfg = yaml.safe_load(cfg_path.read_text())

    adapters = run / "adapters"
    ckpts = sorted(adapters.glob("*_adapters.safetensors"),
                   key=lambda p: int(re.match(r"(\d+)", p.name).group(1)))
    final = adapters / "adapters.safetensors"
    if final.exists():
        ckpts.append(final)
    if not ckpts:
        raise SystemExit(f"no checkpoints in {adapters} (save_every={cfg.get('save_every')})")

    print(f"model   {cfg['model']}")
    print(f"data    {cfg['data']}")
    print(f"ckpts   {len(ckpts)}")

    model, tokenizer = load(cfg["model"])
    args = types.SimpleNamespace(
        data=cfg["data"], train=True, test=False,
        prompt_feature=None, completion_feature=None, chat_feature=None,
        mask_prompt=False, hf_datasets=None,
    )
    _, valid_set, _ = load_dataset(args, tokenizer)
    print(f"valid   {len(valid_set)} examples (FULL set, every checkpoint)\n")

    linear_to_lora_layers(model, cfg.get("num_layers", -1), cfg["lora_parameters"])

    rows = []
    print(f"{'checkpoint':<28}{'val loss':>10}{'vs first':>10}")
    print("-" * 48)
    first = None
    for c in ckpts:
        model.load_weights(str(c), strict=False)
        model.eval()
        loss = evaluate(
            model=model,
            dataset=CacheDataset(valid_set),
            batch_size=cfg.get("batch_size", 1),
            num_batches=-1,                       # -1 + loop=False = one full pass
            max_seq_length=cfg.get("max_seq_length", 3072),
        )
        first = loss if first is None else first
        rows.append({"checkpoint": c.name, "val_loss": loss})
        print(f"{c.name:<28}{loss:>10.4f}{loss - first:>+10.4f}")
        mx.clear_cache()

    out = Path(a.out) if a.out else run / "checkpoint_val.json"
    out.write_text(json.dumps(rows, indent=1))

    best = min(rows, key=lambda r: r["val_loss"])
    print(f"\nbest    {best['checkpoint']}  ({best['val_loss']:.4f})")
    if best["checkpoint"] != rows[-1]["checkpoint"]:
        print("The final checkpoint is NOT the best one. Fuse the best instead:")
        print(f"  cp {adapters / best['checkpoint']} {adapters / 'adapters.safetensors'}")
        print("  then re-run train_lora.sh --from fuse")
    else:
        print("The final checkpoint is the best — no early-stopping needed.")
    print(f"-> {out}")


if __name__ == "__main__":
    main()
