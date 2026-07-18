"""Save-game (yope3d.save_game / load_game) smoketest.

Attach to any entity in a runtime scene, launch the engine, and watch the
console. In one process it proves the whole round-trip:

  1. init() mutates runtime state (self.magic), then save_game() writes the whole
     world to a save file with a meta header.
  2. The save file is read back with plain json and checked field-by-field
     (saveVersion, meta, world settings, entity list, and this script's inlined
     scriptState) -- proving the SAVE path independent of load.
  3. load_game() is queued; on the next frame the engine tears the world down and
     rebuilds it from the save file. After the reloaded script init()s, its
     load_state(dict) fires with the saved state -- proving the LOAD path and the
     params->init->load_state ordering.

A module-level global survives the scorched-earth reload (the interpreter
outlives scene swaps), so the reloaded instance knows not to save/load again --
otherwise it would reload forever.

Exercises: yope3d.save_game/load_game/save_path, Script.save_state/load_state,
the Transient-aware runtime save basis, and the scriptState round-trip.
"""
import os
import json
import yope3d

_STATE = {"ran": False}   # persists across the load_game reload

_SAVE_NAME = "smoke/savegame_smoketest.ysave"
_MAGIC = 4242


class SaveGameSmoketest:
    def init(self, world, entity, params):
        self.magic = 1                 # default; overwritten below on the first run
        if _STATE["ran"]:
            # Post-reload init(): do nothing here, just wait for load_state().
            print("[savegame] (reloaded) init done, awaiting load_state...")
            return
        _STATE["ran"] = True

        self.magic = _MAGIC            # the runtime state we expect to survive
        meta = {"score": 999, "level": "smoketest"}

        ok = yope3d.save_game(_SAVE_NAME, meta)
        print(f"[savegame] save_game() -> {ok}")
        if not ok:
            print("[savegame] FAIL save_game returned False")
            return

        self._verify_file()
        print("[savegame] queuing load_game()...")
        yope3d.load_game(_SAVE_NAME)

    def save_state(self):
        # Runtime state to persist. Kept small; travels as JSON, not paramsBlob.
        return {"magic": self.magic}

    def load_state(self, state):
        got = state.get("magic")
        ok = (got == _MAGIC)
        print(f"[savegame] load_state magic={got} "
              f"{'PASS -- full round-trip verified' if ok else 'FAIL'}")
        # Clean up the save file so a rerun starts fresh.
        try:
            os.remove(yope3d.save_path(_SAVE_NAME))
            binp = yope3d.save_path(_SAVE_NAME).rsplit(".", 1)[0] + ".meshbin"
            if os.path.exists(binp):
                os.remove(binp)
        except OSError:
            pass

    def update(self, world, entity, dt):
        pass

    # --- helpers -----------------------------------------------------------

    def _verify_file(self):
        path = yope3d.save_path(_SAVE_NAME)
        print(f"[savegame] save file: {path}")
        ok = True
        with open(path) as f:
            doc = json.load(f)

        ok &= self._c("saveVersion", doc.get("saveVersion"), 1)
        ok &= self._c("savedScene present", isinstance(doc.get("savedScene"), str), True)
        ok &= self._c("meta.score", (doc.get("meta") or {}).get("score"), 999)
        ok &= self._c("meta.level", (doc.get("meta") or {}).get("level"), "smoketest")
        ok &= self._c("world gravity present", "gravity" in doc, True)

        ents = doc.get("entities", [])
        ok &= self._c("has entities", len(ents) >= 1, True)

        states = [e["scriptState"] for e in ents if "scriptState" in e]
        ok &= self._c("exactly one scriptState", len(states), 1)
        if states:
            st = json.loads(states[0])
            ok &= self._c("scriptState.magic", st.get("magic"), _MAGIC)

        print(f"[savegame] file check {'PASS' if ok else 'FAIL'}")

    def _c(self, label, got, want):
        if got == want:
            print(f"[savegame]   ok   {label}: {got!r}")
            return True
        print(f"[savegame]   FAIL {label}: got {got!r}, want {want!r}")
        return False
