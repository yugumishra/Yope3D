#!/usr/bin/env python3
"""The train/held-out split — one definition, imported by everything.

make_dataset.py must exclude exactly what probe_set.py and fim_eval3.py
evaluate on. If these lists are written out twice they will drift, and the
failure is silent: numbers keep coming out, they just measure memorisation.
"""

from __future__ import annotations

# Held out from training. Chosen to span the measured difficulty tiers so no
# single regime dominates validation loss (PLAN.txt 9.4):
HELDOUT_BEHAVIORS = [
    "attach_script_demo.py",   # top tier    62.5% exact
    "vehicle_demo.py",         # mid tier    37.5%
    "physics_gallery.py",      # bottom tier 0%
    "sandbox_gallery.py",      # bottom tier 0%
]

# Directory of probe-only synthetic files. Generated with a different seed AND
# different structural parameters, and deliberately NOT one of make_dataset's
# default sources — so probes cannot leak into training.
PROBE_SYNTH_DIR = "fim-finetuning/corpus/probe_synth"

# Sources make_dataset reads by default. PROBE_SYNTH_DIR is absent on purpose.
TRAIN_SOURCES = [
    "scripts/behaviors",
    "fim-finetuning/corpus/synth",
]
