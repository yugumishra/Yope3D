#!/usr/bin/env python3
"""Run mlx_lm's LoRA trainer behind a memory watchdog that actually works.

WHY THIS EXISTS
    The first attempt at PLAN 9.9 froze the machine. mlx-lm reported
    `Peak mem 30.752 GB` on a 24 GB M4 Air. Unified memory means an
    over-allocating job does not get an allocator error — macOS satisfies it
    out of swap. The process kept running, every other process started faulting
    on disk, and the system went unresponsive. Wall clock hit ~500 s/iteration:
    a 2,485-iteration run on track for ~14 days, presenting as "training is
    slow". No error, no OOM kill, no signal of any kind.

WHAT DOES NOT WORK, MEASURED
    mx.set_memory_limit() is NOT a guard. Its documented behaviour is to raise
    only when RAM *and swap* are exhausted, which is long after the machine has
    become unusable. Verified directly: with a 0.5 GB limit set, allocating
    2.5 GB succeeded silently. It is called below because it helps the
    allocator, NOT because it protects anything.

    mlx-lm's TrainingCallback fires at iteration boundaries. The first run's
    damage was done inside iteration 1, before any callback could run.

WHAT DOES WORK
    A daemon thread sampling mx.get_peak_memory() on a wall-clock timer,
    independent of the training loop, calling os._exit() the moment the budget
    is passed. It fires mid-iteration. os._exit rather than an exception
    because the point is to stop allocating NOW, not to unwind cleanly through
    a framework that is already thrashing.

    Exit code 3 means the watchdog fired. The caller treats that as "this
    config is unsafe", not as a crash.

USAGE
    python3 mlx_train_guarded.py --limit-gb 14 -c config.yaml [mlx_lm lora args]
"""

from __future__ import annotations

import os
import sys
import threading
import time

import mlx.core as mx

GB = 1024 ** 3
POLL_S = 0.5


def _watchdog(limit_bytes: int, started: threading.Event) -> None:
    started.wait()
    while True:
        peak = mx.get_peak_memory()
        if peak > limit_bytes:
            sys.stderr.write(
                f"\n[guard] WATCHDOG FIRED — peak {peak / GB:.2f} GB exceeded the "
                f"{limit_bytes / GB:.1f} GB budget.\n"
                "[guard] Killing now, before the machine starts swapping.\n"
                "[guard] This is the guard working. The same config without it is\n"
                "[guard] what froze the machine on 2026-07-29 (30.75 GB on 24 GB).\n"
                "[guard] Reduce max_seq_length or batch_size, or check that\n"
                "[guard] grad_checkpoint is true (mlx-lm defaults it to FALSE).\n")
            sys.stderr.flush()
            os._exit(3)
        time.sleep(POLL_S)


def main() -> int:
    argv = sys.argv[1:]
    limit_gb = None
    passthrough: list[str] = []
    i = 0
    while i < len(argv):
        if argv[i] == "--limit-gb":
            limit_gb = float(argv[i + 1]); i += 2
        else:
            passthrough.append(argv[i]); i += 1

    if limit_gb is None:
        print("mlx_train_guarded: --limit-gb is required", file=sys.stderr)
        return 2

    limit_bytes = int(limit_gb * GB)
    try:
        info = mx.device_info()
        rec, total = info.get("max_recommended_working_set_size", 0), info.get("memory_size", 0)
    except Exception:
        rec = total = 0

    # Helps the allocator; does not enforce. See the module docstring.
    mx.set_memory_limit(limit_bytes)

    print(f"[guard] watchdog budget    {limit_gb:.1f} GB, sampled every {POLL_S}s", flush=True)
    if rec:
        print(f"[guard] metal recommended  {rec / GB:.1f} GB of {total / GB:.1f} GB total",
              flush=True)

    started = threading.Event()
    threading.Thread(target=_watchdog, args=(limit_bytes, started), daemon=True).start()
    started.set()

    sys.argv = ["mlx_lm.lora"] + passthrough
    from mlx_lm.lora import main as lora_main
    lora_main()
    print(f"[guard] finished. peak {mx.get_peak_memory() / GB:.2f} GB", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
