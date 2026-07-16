"""Action-map (behaviors._actions) smoketest.

Attach to any entity, press Play, and watch the console:
  - builds an ActionMap from PRESET_FPS
  - prints the Dvorak/Colemak remaps of that preset once at init (letter
    bindings translate, arrow/mouse bindings pass through unchanged)
  - round-trips bindings through a JSON file
  - update(): reports down()/pressed()/released() edges + the WASD-derived
    vector2 axis, once per second plus on jump/fire edges

Exercises: behaviors._actions (ActionMap, key/mouse, PRESET_*,
remap_for_layout), Input.is_mouse_down (new), and the expanded A-Z/digit/
punctuation yope3d.KEY_* constant set.
"""
import os
import tempfile

from behaviors import _actions


class ActionsSmoketest:
    def init(self, world, entity, params):
        self.actions = _actions.ActionMap(_actions.PRESET_FPS)

        for layout in ("dvorak", "colemak"):
            remapped = _actions.remap_for_layout(_actions.PRESET_FPS, layout)
            print(f"[actions] {layout} move_forward={remapped['move_forward']} "
                  f"move_back={remapped['move_back']} move_left={remapped['move_left']} "
                  f"move_right={remapped['move_right']} fire={remapped['fire']} "
                  f"jump={remapped['jump']}")

        path = os.path.join(tempfile.gettempdir(), "yope3d_actions_smoketest.json")
        self.actions.save(path)
        loaded = _actions.ActionMap.load(path, fallback=_actions.PRESET_TOP_DOWN)
        assert loaded.bindings["jump"] == self.actions.bindings["jump"], "save/load round-trip mismatch"
        print(f"[actions] save/load round-trip OK ({path})")

        self._t = 0.0

    def update(self, world, entity, dt):
        self._t += dt
        if self._t >= 1.0:
            self._t = 0.0
            x, z = self.actions.vector2("move_left", "move_right", "move_back", "move_forward")
            print(f"[actions] move axis=({x:.0f},{z:.0f}) sprint={self.actions.down('sprint')}")

        if self.actions.pressed("jump"):
            print("[actions] jump pressed")
        if self.actions.released("jump"):
            print("[actions] jump released")
        if self.actions.pressed("fire"):
            print("[actions] fire pressed (mouse binding)")
