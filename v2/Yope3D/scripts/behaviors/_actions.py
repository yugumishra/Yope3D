"""Action-map layer over yope3d.Input (pure Python, no C++ engine changes).

Input already exposes raw keys/mouse buttons 1:1 to scripts; this module adds
the missing layer between "physical key" and "gameplay intent": named
actions with multiple bindings, rebinding, JSON persistence, and per-layout
preset generation for non-QWERTY keyboards. Gamepad support is intentionally
out of scope (see limitations.md §6 — not reliably testable right now).

Basic use:

    from behaviors import _actions

    class Player:
        def init(self, world, entity, params):
            self.actions = _actions.ActionMap(_actions.PRESET_FPS)

        def update(self, world, entity, dt):
            move = Vec3()
            if self.actions.down("move_forward"): move += fwd
            if self.actions.down("move_back"):    move -= fwd
            if self.actions.pressed("jump"):
                ...
            x, z = self.actions.vector2("move_left", "move_right",
                                         "move_back", "move_forward")

Bindings are ``(device, code)`` pairs — build them with ``key()``/``mouse()``
rather than raw tuples so the intent reads clearly:

    self.actions.bind("jump", _actions.key(yope3d.KEY_SPACE))
    self.actions.add_binding("jump", _actions.mouse(yope3d.MOUSE_LEFT))

Persistence (rebindable controls). There's no sanctioned save directory yet
(limitations.md §3.2 — settings persistence), so callers pass an explicit
path today; swap in ``yope3d.save_path(...)`` once that lands:

    self.actions.save("saves/keybinds.json")
    self.actions = _actions.ActionMap.load("saves/keybinds.json",
                                            fallback=_actions.PRESET_FPS)

Keyboard layouts — two GLFW input paths report different things, and mixing
them up is the classic bug here:

  - ``glfwGetKey`` / the key callback (what ``Input`` and this module's
    ``down``/``pressed``/``released`` are built on) reports a *physical*
    scancode-derived token: ``yope3d.KEY_W`` always means "the key at the
    physical W position on a US-QWERTY board," regardless of which layout the
    OS has active. Gameplay bindings ride this path, which is exactly why
    physical WASD already lands on the same key cluster on *every* layout
    with zero remapping — no code needed for that case.
  - The char callback (``Input.get_typed_chars``, used for text fields) is
    layout-aware on purpose: it reports the actual Unicode character the OS
    types for that physical key, because text entry must respect the user's
    layout. **Do not use it to figure out "what letter is this KEY_* on the
    player's keyboard"** — it only fires on real keypresses, not as a
    lookup table, and it exists for typing, not key identification.
  - Rendering a "Press ___" label in a rebind UI needs neither of the above —
    it needs ``yope3d.input.get_key_name(key)`` (a thin ``glfwGetKeyName``
    wrapper), which translates a physical key token to the localized
    character its keycap/layout shows, e.g. ``KEY_W`` returns ``","`` under
    Dvorak but ``"w"`` under QWERTY/Colemak. It returns ``""`` for
    non-printable keys (arrows, F-keys, modifiers, ...) — GLFW has no name
    for those — which is what ``label()`` below exists to paper over.

Given physical WASD already works unremapped, ``remap_for_layout`` is for two
narrower, both fully opt-in, cases — no preset calls it automatically:

  1. A player who prefers the "same character" convention over the "same
     physical block" one (mainly a Dvorak thing, since Dvorak moves W/S/D far
     from their QWERTY spot — Colemak deliberately keeps WASD in place, so
     remapping a WASD-based preset for Colemak is normally a no-op) — they'd
     rather bind by letter meaning than by physical position.
  2. Mnemonic bindings where the *letter itself* is the point (R for reload,
     E for interact, a hotbar on 1-9) — those should keep meaning the same
     letter/digit across layouts, not the same physical key. This is the case
     that actually needs the generator regardless of layout preference.

``remap_for_layout(preset, "dvorak")`` (or ``"colemak"``) translates every
letter/punctuation binding in a preset to whichever physical key produces
that *same character* under the target layout. Non-letter bindings (arrows,
mouse, Space, Shift, ...) pass through unchanged. If you want physical-block
WASD movement (the default, layout-agnostic behavior), just don't remap
movement actions — leave the QWERTY preset as-is, it's already correct.

    actions = _actions.ActionMap(_actions.remap_for_layout(_actions.PRESET_FPS, "dvorak"))

Display labels. Use ``label(binding)`` for rebind-UI text — it wraps
``get_key_name`` for printable keys and falls back to a fixed name table for
the rest (arrows, Space, F-keys, modifiers, ...), which ``get_key_name``
can't cover since GLFW has no name for a key that types nothing:

    _actions.label(_actions.key(yope3d.KEY_W))   # "," on Dvorak, "W" on QWERTY
    _actions.label(_actions.mouse(yope3d.MOUSE_LEFT))  # "Mouse Left" (mouse buttons aren't layout-dependent)
"""
import json
import os

import yope3d

# ==============================================================================
# Binding helpers
# ==============================================================================

def key(code):
    """Build a keyboard binding for ``code`` (a ``yope3d.KEY_*`` constant)."""
    return ("key", code)


def mouse(code):
    """Build a mouse-button binding for ``code`` (a ``yope3d.MOUSE_*`` constant)."""
    return ("mouse", code)


# ==============================================================================
# ActionMap
# ==============================================================================

class ActionMap:
    """Named gameplay actions, each bound to zero or more physical inputs."""

    def __init__(self, preset=None):
        self.bindings = {}  # action name -> list[(device, code)]
        if preset:
            self.load_dict(preset)

    # --- authoring / rebinding ------------------------------------------------

    def load_dict(self, mapping):
        """Replace all bindings from ``{action: [(device, code), ...]}``."""
        self.bindings = {name: [tuple(b) for b in binds] for name, binds in mapping.items()}

    def to_dict(self):
        """Return a plain ``{action: [[device, code], ...]}`` dict (JSON-safe)."""
        return {name: [list(b) for b in binds] for name, binds in self.bindings.items()}

    def bind(self, action, *binds):
        """Replace ``action``'s bindings entirely (e.g. ``bind("jump", key(KEY_SPACE))``)."""
        self.bindings[action] = list(binds)

    def add_binding(self, action, binding):
        """Append one more binding to ``action`` without disturbing existing ones."""
        self.bindings.setdefault(action, []).append(binding)

    def clear(self, action):
        """Remove every binding for ``action`` (it becomes permanently "not pressed")."""
        self.bindings[action] = []

    # --- querying ---------------------------------------------------------------

    def down(self, action):
        """True while any binding for ``action`` is held."""
        inp = yope3d.input
        for device, code in self.bindings.get(action, ()):
            if device == "key" and inp.is_key_down(code):
                return True
            if device == "mouse" and inp.is_mouse_down(code):
                return True
        return False

    def pressed(self, action):
        """True only on the frame any binding for ``action`` transitioned to down."""
        inp = yope3d.input
        for device, code in self.bindings.get(action, ()):
            if device == "key" and inp.is_key_pressed(code):
                return True
            if device == "mouse" and inp.is_mouse_pressed(code):
                return True
        return False

    def released(self, action):
        """True only on the frame any binding for ``action`` transitioned to up."""
        inp = yope3d.input
        for device, code in self.bindings.get(action, ()):
            if device == "key" and inp.is_key_released(code):
                return True
            if device == "mouse" and inp.is_mouse_released(code):
                return True
        return False

    def axis(self, negative, positive):
        """Return -1.0/0.0/1.0 from a pair of opposing actions (e.g. move_left/move_right)."""
        v = 0.0
        if self.down(negative):
            v -= 1.0
        if self.down(positive):
            v += 1.0
        return v

    def vector2(self, neg_x, pos_x, neg_y, pos_y):
        """Convenience for WASD-style movement: returns ``(x, y)``, each in [-1, 1]."""
        return (self.axis(neg_x, pos_x), self.axis(neg_y, pos_y))

    # --- persistence --------------------------------------------------------

    def save(self, path):
        """Write bindings to ``path`` as JSON."""
        with open(path, "w") as f:
            json.dump(self.to_dict(), f, indent=2)

    @classmethod
    def load(cls, path, fallback=None):
        """Load bindings from ``path``; falls back to ``fallback`` (a preset dict)
        if the file doesn't exist or fails to parse (e.g. first run, or a
        preset schema change)."""
        am = cls(fallback)
        if os.path.exists(path):
            try:
                with open(path) as f:
                    am.load_dict(json.load(f))
            except (OSError, ValueError):
                pass
        return am


# ==============================================================================
# Presets — authored assuming a physical QWERTY keyboard. Copy and edit
# per-project, or pass through remap_for_layout() for other layouts.
# ==============================================================================

PRESET_FPS = {
    "move_forward": [key(yope3d.KEY_W)],
    "move_back":    [key(yope3d.KEY_S)],
    "move_left":    [key(yope3d.KEY_A)],
    "move_right":   [key(yope3d.KEY_D)],
    "jump":         [key(yope3d.KEY_SPACE)],
    "sprint":       [key(yope3d.KEY_LEFT_SHIFT)],
    "crouch":       [key(yope3d.KEY_LEFT_CONTROL)],
    "interact":     [key(yope3d.KEY_E)],
    "reload":       [key(yope3d.KEY_R)],
    "fire":         [mouse(yope3d.MOUSE_LEFT)],
    "aim":          [mouse(yope3d.MOUSE_RIGHT)],
    "pause":        [key(yope3d.KEY_ESCAPE)],
}

PRESET_PLATFORMER = {
    "move_left":  [key(yope3d.KEY_A), key(yope3d.KEY_LEFT)],
    "move_right": [key(yope3d.KEY_D), key(yope3d.KEY_RIGHT)],
    "jump":       [key(yope3d.KEY_SPACE), key(yope3d.KEY_UP)],
    "crouch":     [key(yope3d.KEY_S), key(yope3d.KEY_DOWN)],
    "run":        [key(yope3d.KEY_LEFT_SHIFT)],
    "interact":   [key(yope3d.KEY_E)],
    "pause":      [key(yope3d.KEY_ESCAPE)],
}

PRESET_TOP_DOWN = {
    "move_forward": [key(yope3d.KEY_W)],
    "move_back":    [key(yope3d.KEY_S)],
    "move_left":    [key(yope3d.KEY_A)],
    "move_right":   [key(yope3d.KEY_D)],
    "interact":     [key(yope3d.KEY_E)],
    "attack":       [mouse(yope3d.MOUSE_LEFT)],
    "ability":      [mouse(yope3d.MOUSE_RIGHT)],
    "inventory":    [key(yope3d.KEY_TAB)],
    "pause":        [key(yope3d.KEY_ESCAPE)],
}

PRESET_MENU_NAV = {
    "nav_up":     [key(yope3d.KEY_UP), key(yope3d.KEY_W)],
    "nav_down":   [key(yope3d.KEY_DOWN), key(yope3d.KEY_S)],
    "nav_left":   [key(yope3d.KEY_LEFT), key(yope3d.KEY_A)],
    "nav_right":  [key(yope3d.KEY_RIGHT), key(yope3d.KEY_D)],
    "confirm":    [key(yope3d.KEY_ENTER), key(yope3d.KEY_SPACE)],
    "cancel":     [key(yope3d.KEY_ESCAPE), key(yope3d.KEY_BACKSPACE)],
}

PRESETS = {
    "fps":        PRESET_FPS,
    "platformer": PRESET_PLATFORMER,
    "top_down":   PRESET_TOP_DOWN,
    "menu_nav":   PRESET_MENU_NAV,
}


# ==============================================================================
# Keyboard layout remap generators
# ==============================================================================
#
# Each layout is described as three 10-key physical rows (top/home/bottom),
# aligned column-for-column with QWERTY so row[i][col] is "what character
# this layout types when you press the physical key at that position."
# GLFW key tokens are named after physical position on a standard QWERTY
# board (glfwGetKey is layout-independent), so for every non-QWERTY layout
# we build a reverse lookup: "to type character X, press the physical key
# that QWERTY calls Y" -> yope3d.KEY_<Y>.

_QWERTY_ROWS = (
    "qwertyuiop",
    "asdfghjkl;",
    "zxcvbnm,./",
)
_DVORAK_ROWS = (
    "',.pyfgcrl",
    "aoeuidhtns",
    ";qjkxbmwvz",
)
_COLEMAK_ROWS = (
    "qwfpgjluy;",
    "arstdhneio",
    "zxcvbkm,./",
)

_PUNCT_KEY_NAMES = {
    ";": "SEMICOLON", "'": "APOSTROPHE", ",": "COMMA", ".": "PERIOD",
    "/": "SLASH", "-": "MINUS", "=": "EQUAL",
    "[": "LEFT_BRACKET", "]": "RIGHT_BRACKET", "\\": "BACKSLASH", "`": "GRAVE_ACCENT",
}


def _qwerty_char_to_key(ch):
    """Map a QWERTY physical-position character to its yope3d.KEY_* constant."""
    if ch.isalpha():
        return getattr(yope3d, "KEY_" + ch.upper())
    if ch in _PUNCT_KEY_NAMES:
        return getattr(yope3d, "KEY_" + _PUNCT_KEY_NAMES[ch])
    return None


def _build_layout_table(layout_rows):
    """Return {character: yope3d.KEY_* constant} — which physical key must be
    pressed to type `character` under this layout."""
    table = {}
    for qwerty_row, layout_row in zip(_QWERTY_ROWS, layout_rows):
        for qwerty_ch, layout_ch in zip(qwerty_row, layout_row):
            key_const = _qwerty_char_to_key(qwerty_ch)
            if key_const is not None:
                table[layout_ch] = key_const
    return table


# name -> {character: physical yope3d.KEY_* constant}
LAYOUTS = {
    "qwerty": _build_layout_table(_QWERTY_ROWS),
    "dvorak": _build_layout_table(_DVORAK_ROWS),
    "colemak": _build_layout_table(_COLEMAK_ROWS),
}

# Reverse of the QWERTY table: yope3d.KEY_* constant -> the character a
# QWERTY-authored preset means by that binding (e.g. KEY_W -> "w").
_KEY_TO_QWERTY_CHAR = {v: k for k, v in LAYOUTS["qwerty"].items()}


def remap_for_layout(preset, layout):
    """Return a new preset dict with every letter/punctuation key binding
    translated so the same *character* is typed on `layout` as was intended
    on QWERTY. Arrow/mouse/whitespace/modifier bindings pass through
    unchanged (they aren't affected by layout). `layout` is "qwerty"
    (no-op), "dvorak", or "colemak".

    This does not touch already-loaded ActionMap instances — call it before
    constructing/loading one, or on a preset dict before ActionMap(preset).
    """
    if layout not in LAYOUTS:
        raise ValueError(f"unknown layout {layout!r}; expected one of {sorted(LAYOUTS)}")
    table = LAYOUTS[layout]

    remapped = {}
    for action, binds in preset.items():
        new_binds = []
        for device, code in binds:
            if device == "key" and code in _KEY_TO_QWERTY_CHAR:
                ch = _KEY_TO_QWERTY_CHAR[code]
                new_binds.append(("key", table.get(ch, code)))
            else:
                new_binds.append((device, code))
        remapped[action] = new_binds
    return remapped


# ==============================================================================
# Display labels (rebind UI)
# ==============================================================================
#
# get_key_name() only names *printable* keys, and only reflects the layout
# for those (it's a glfwGetKeyName wrapper, which is itself layout-aware only
# for characters — arrows/F-keys/modifiers etc. produce no character on any
# layout, so GLFW has no name to give and this table fills the gap). Mouse
# buttons aren't affected by keyboard layout at all, so they're a fixed table.

_SPECIAL_KEY_LABELS = {
    "KEY_SPACE": "Space", "KEY_TAB": "Tab", "KEY_ESCAPE": "Esc",
    "KEY_ENTER": "Enter", "KEY_BACKSPACE": "Backspace", "KEY_CAPS_LOCK": "Caps Lock",
    "KEY_LEFT_SHIFT": "L Shift", "KEY_RIGHT_SHIFT": "R Shift",
    "KEY_LEFT_CONTROL": "L Ctrl", "KEY_RIGHT_CONTROL": "R Ctrl",
    "KEY_LEFT_ALT": "L Alt", "KEY_RIGHT_ALT": "R Alt",
    "KEY_LEFT": "Left", "KEY_RIGHT": "Right", "KEY_UP": "Up", "KEY_DOWN": "Down",
    "KEY_INSERT": "Insert", "KEY_DELETE": "Delete", "KEY_HOME": "Home", "KEY_END": "End",
    "KEY_PAGE_UP": "Page Up", "KEY_PAGE_DOWN": "Page Down",
    "KEY_F1": "F1", "KEY_F2": "F2", "KEY_F3": "F3", "KEY_F4": "F4",
    "KEY_F5": "F5", "KEY_F6": "F6", "KEY_F7": "F7", "KEY_F8": "F8",
    "KEY_F9": "F9", "KEY_F10": "F10", "KEY_F11": "F11", "KEY_F12": "F12",
}
# yope3d.KEY_* constant -> fixed fallback label, for keys get_key_name() can't name.
_SPECIAL_KEY_CODES = {
    getattr(yope3d, name): label
    for name, label in _SPECIAL_KEY_LABELS.items()
    if hasattr(yope3d, name)
}

_MOUSE_LABELS = {
    yope3d.MOUSE_LEFT: "Mouse Left",
    yope3d.MOUSE_RIGHT: "Mouse Right",
    yope3d.MOUSE_MIDDLE: "Mouse Middle",
}


def label(binding):
    """Human-readable display label for one (device, code) binding, for a
    rebind UI's "Press ___ to jump" text. Printable keys are named via
    get_key_name() and therefore reflect the player's current OS keyboard
    layout; non-printable keys and mouse buttons come from a fixed table."""
    device, code = binding
    if device == "mouse":
        return _MOUSE_LABELS.get(code, f"Mouse Button {code}")
    name = yope3d.input.get_key_name(code)
    if name:
        return name.upper()
    return _SPECIAL_KEY_CODES.get(code, f"Key {code}")
