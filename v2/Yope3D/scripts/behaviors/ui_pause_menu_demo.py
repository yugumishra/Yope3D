"""In-game pause/settings menu — self-contained UI system demo.

Attach PauseMenuDemo to any single entity (an empty, a light, whatever) and
press Play. Builds a persistent HUD plus a pause overlay entirely from
Python, laid out as a flat set of independently-positioned entities so
individual pieces are easy to grab and nudge in the editor afterward.

Controls:
  P            toggle the pause menu (fades in/out)
  Left click   Settings/Credits tabs, Resume, and the name field
  Typing       fills the name field while it's focused (click it first)
  Backspace    deletes the last character of the name field

Layout:
  - HUD (always on, root-level): four UIText labels using anchor mode —
    CenterTop, CenterBottom, BottomLeft, BottomRight — so they stay pinned
    to their edge/corner at a fixed pixel size regardless of window size or
    aspect ratio.
  - Pause overlay: a fullscreen backdrop (root) -> centered panel (child) ->
    title / tab buttons / tab content / Resume+Quit buttons (children of the
    panel, positioned as simple LOCAL [0,1] fractions of the panel's own
    rect — moving or resizing the panel later re-flows everyone inside it).
  - Settings tab: three ToggleButton instances (Fullscreen/Music/SFX, each
    creates its own on/off label) plus a NameField text-input row.
  - Credits tab: a static multi-line UIText.
  - Quit button is created with UIButton.enabled = False — demonstrates the
    disabled visual state and that disabled buttons are skipped by hit-testing.

Exercises: add_ui_background/add_ui_textured_background/add_ui_text/
add_ui_button, set_ui_parent, set_ui_group_visible, tween_ui_opacity (with a
manual close-timer since tweens have no completion callback), UITransform
anchor modes (incl. the new Center-Top/Bottom/Left/Right), on_ui_press/
release/enter/leave, on_text_input + get_ui_focus, ui_consumed_click,
UIButton.enabled.
"""
import yope3d

# The active PauseMenuDemo instance — child button scripts (ToggleButton,
# TabButton, ResumeButton) live on separate entities with no direct handle to
# each other, so they reach back into the host via this module-level pointer
# instead of threading entity ids through JSON script params.
_menu = None


def _label(world, parent, text, depth):
    """Create a UIText filling its parent's local [0,1] rect and parent it."""
    e = world.add_ui_text("fonts/monaco.ttf", text,
                           yope3d.Vec2(0.0, 0.0), yope3d.Vec2(1.0, 1.0), depth=depth)
    world.set_ui_parent(e, parent)
    return e


class PauseMenuDemo:
    PARAMS = {}

    def init(self, world, entity, params):
        global _menu
        _menu = self
        yope3d.window.set_cursor_locked(False)

        self.tab = "settings"
        self.overlay_visible = False
        self.closing = False
        self.close_timer = 0.0
        self.fade_duration = 0.3

        self._build_hud(world)
        self._build_overlay(world)

        # Hide the whole overlay after every child exists, so nothing flashes
        # on screen for a frame before the first close.
        world.set_ui_group_visible(self.overlay_root, False)

        print(f"[pause_menu] ready — press P to toggle. overlay={self.overlay_root}")

    # ---- HUD: always-on, anchor-pinned corner/edge readouts ----
    # auto_size=True (UIText) sizes each label's box to fit its text at
    # display_px instead of us guessing a pixel_width/pixel_height by hand —
    # that guess was the "text far too tiny" bug: an undersized fixed box
    # triggers TextBox's auto-fit-down-to-avoid-clipping safety clamp, which
    # was silently shrinking the glyphs well below display_px.
    def _build_hud(self, world):
        def hud_label(text, anchor, offset_x_px=0.0, offset_y_px=16.0, display_px=32):
            e = world.add_ui_text("fonts/monaco.ttf", text,
                                   yope3d.Vec2(0.0, 0.0), yope3d.Vec2(0.0, 0.0), depth=0)
            tf = yope3d.reg_get(e, "UITransform")
            tf.anchor = anchor
            tf.offset_x_px = offset_x_px
            tf.offset_y_px = offset_y_px
            ut = yope3d.reg_get(e, "UIText")
            ut.display_px = display_px
            ut.auto_size = True
            return e

        hud_label("Objective: Reach the Beacon", anchor=6)          # CenterTop
        hud_label("Press P to Pause", anchor=7)                      # CenterBottom
        hud_label("HP: 100", anchor=3, offset_x_px=16.0)             # BottomLeft
        hud_label("Ammo: 30", anchor=4, offset_x_px=16.0)            # BottomRight

    # ---- Pause overlay: backdrop -> panel -> tabs/content/buttons ----
    def _build_overlay(self, world):
        self.overlay_root = world.add_ui_background(
            yope3d.Vec2(0.0, 0.0), yope3d.Vec2(1.0, 1.0),
            yope3d.Vec4(0.0, 0.0, 0.0, 0.55), depth=10)

        self.panel = world.add_ui_textured_background(
            yope3d.Vec2(0.25, 0.12), yope3d.Vec2(0.75, 0.88),
            yope3d.Vec4(0.15, 0.15, 0.18, 0.92), "textures/test.png", depth=11)
        world.set_ui_parent(self.panel, self.overlay_root)

        title = world.add_ui_text("fonts/monaco.ttf", "PAUSED",
                                   yope3d.Vec2(0.05, 0.03), yope3d.Vec2(0.95, 0.15), depth=12)
        world.set_ui_parent(title, self.panel)

        # ---- Tabs ----
        self.tab_settings = world.add_ui_button(
            yope3d.Vec2(0.06, 0.18), yope3d.Vec2(0.48, 0.27),
            yope3d.Vec4(0.25, 0.25, 0.30, 1.0), depth=12)
        world.set_ui_parent(self.tab_settings, self.panel)
        _label(world, self.tab_settings, "Settings", depth=13)
        yope3d.attach_script(self.tab_settings, "behaviors.ui_pause_menu_demo", "TabButton",
                              {"tab": "settings"})

        self.tab_credits = world.add_ui_button(
            yope3d.Vec2(0.52, 0.18), yope3d.Vec2(0.94, 0.27),
            yope3d.Vec4(0.25, 0.25, 0.30, 1.0), depth=12)
        world.set_ui_parent(self.tab_credits, self.panel)
        _label(world, self.tab_credits, "Credits", depth=13)
        yope3d.attach_script(self.tab_credits, "behaviors.ui_pause_menu_demo", "TabButton",
                              {"tab": "credits"})

        # ---- Tab content (same rect, only one visible at a time) ----
        content_min, content_max = yope3d.Vec2(0.06, 0.30), yope3d.Vec2(0.94, 0.76)

        self.settings_content = world.add_ui_background(
            content_min, content_max, yope3d.Vec4(0, 0, 0, 0), depth=12)
        world.set_ui_parent(self.settings_content, self.panel)

        self.credits_content = world.add_ui_background(
            content_min, content_max, yope3d.Vec4(0, 0, 0, 0), depth=12)
        world.set_ui_parent(self.credits_content, self.panel)

        self._build_settings_tab(world)
        self._build_credits_tab(world)

        # ---- Bottom row: Resume + (disabled) Quit ----
        self.resume_btn = world.add_ui_button(
            yope3d.Vec2(0.10, 0.86), yope3d.Vec2(0.46, 0.96),
            yope3d.Vec4(0.20, 0.45, 0.25, 1.0), depth=12)
        world.set_ui_parent(self.resume_btn, self.panel)
        _label(world, self.resume_btn, "Resume", depth=13)
        yope3d.attach_script(self.resume_btn, "behaviors.ui_pause_menu_demo", "ResumeButton", {})

        self.quit_btn = world.add_ui_button(
            yope3d.Vec2(0.54, 0.86), yope3d.Vec2(0.90, 0.96),
            yope3d.Vec4(0.45, 0.20, 0.20, 1.0), depth=12)
        world.set_ui_parent(self.quit_btn, self.panel)
        _label(world, self.quit_btn, "Quit (WIP)", depth=13)
        qtf = yope3d.reg_get(self.quit_btn, "UIButton")
        qtf.enabled = False

        self.switch_tab(world, "settings")

    def _build_settings_tab(self, world):
        row_h = 0.22
        for i, prefix in enumerate(["Fullscreen", "Music", "SFX"]):
            y0 = i * 0.24
            btn = world.add_ui_button(
                yope3d.Vec2(0.0, y0), yope3d.Vec2(0.95, y0 + row_h),
                yope3d.Vec4(0.22, 0.22, 0.26, 1.0), depth=13)
            world.set_ui_parent(btn, self.settings_content)
            yope3d.attach_script(btn, "behaviors.ui_pause_menu_demo", "ToggleButton",
                                  {"prefix": prefix})

        name_y0 = 3 * 0.24
        name_btn = world.add_ui_button(
            yope3d.Vec2(0.0, name_y0), yope3d.Vec2(0.95, name_y0 + row_h),
            yope3d.Vec4(0.22, 0.22, 0.26, 1.0), depth=13)
        world.set_ui_parent(name_btn, self.settings_content)
        yope3d.attach_script(name_btn, "behaviors.ui_pause_menu_demo", "NameField", {})

    def _build_credits_tab(self, world):
        credits_text = (
            "Yope3D Engine\n\n"
            "UI System Demo\n\n"
            "Engine: Yope3D\n"
            "Scene: uiOverhaulDemo.json\n\n"
            "Thanks for watching!"
        )
        e = world.add_ui_text("fonts/monaco.ttf", credits_text,
                               yope3d.Vec2(0.0, 0.0), yope3d.Vec2(1.0, 1.0), depth=13)
        world.set_ui_parent(e, self.credits_content)

    # ---- Menu state machine ----
    def switch_tab(self, world, tab):
        self.tab = tab
        world.set_ui_group_visible(self.settings_content, tab == "settings")
        world.set_ui_group_visible(self.credits_content, tab == "credits")

    def toggle_menu(self, world):
        if self.overlay_visible and not self.closing:
            self.close_menu(world)
        else:
            self.open_menu(world)

    def open_menu(self, world):
        self.closing = False
        world.set_ui_group_visible(self.overlay_root, True)
        # set_ui_group_visible above just force-showed the ENTIRE overlay
        # subtree, including whichever tab content switch_tab had hidden —
        # re-apply the active tab filter so only it ends up visible again.
        self.switch_tab(world, self.tab)
        tf = yope3d.reg_get(self.overlay_root, "UITransform")
        tf.opacity = 0.0
        world.tween_ui_opacity(self.overlay_root, 1.0, self.fade_duration, yope3d.EASE_CUBIC_OUT)
        self.overlay_visible = True

    def close_menu(self, world):
        world.tween_ui_opacity(self.overlay_root, 0.0, self.fade_duration, yope3d.EASE_CUBIC_IN)
        self.closing = True
        self.close_timer = self.fade_duration
        self.overlay_visible = False

    def update(self, world, entity, dt):
        if yope3d.input.is_key_pressed(yope3d.KEY_P):
            self.toggle_menu(world)

        if self.closing:
            self.close_timer -= dt
            if self.close_timer <= 0.0:
                # Rendering is skipped entirely while UITransform.visible is
                # False, so this has to wait for the fade-out tween to finish
                # (no completion callback exists) rather than hiding instantly.
                world.set_ui_group_visible(self.overlay_root, False)
                self.closing = False

        # Proves click-through suppression: a world click only counts when
        # the UI didn't consume it this frame (e.g. menu closed, or click
        # missed every UI rect).
        if yope3d.input.is_mouse_pressed(yope3d.MOUSE_LEFT) and not world.ui_consumed_click():
            print("[pause_menu] world click passed through (UI did not consume it)")


class TabButton:
    """Attached to a tab button — switches the host's active tab on click."""
    PARAMS = {"tab": "settings"}

    def init(self, world, entity, params):
        self.tab = params.get("tab", "settings")

    def update(self, world, entity, dt):
        pass

    def on_ui_release(self, world, entity):
        if _menu:
            _menu.switch_tab(world, self.tab)


class ResumeButton:
    """Attached to the Resume button — closes the pause overlay on click."""
    PARAMS = {}

    def init(self, world, entity, params):
        pass

    def update(self, world, entity, dt):
        pass

    def on_ui_release(self, world, entity):
        if _menu:
            _menu.close_menu(world)


class ToggleButton:
    """Reusable ON/OFF row — creates its own label and flips state on click."""
    PARAMS = {"prefix": "Setting"}

    def init(self, world, entity, params):
        self.prefix = params.get("prefix", "Setting")
        self.on = True
        self.label = _label(world, entity, self._text(), depth=14)

    def _text(self):
        return f"{self.prefix}: {'ON' if self.on else 'OFF'}"

    def update(self, world, entity, dt):
        pass

    def on_ui_release(self, world, entity):
        self.on = not self.on
        yope3d.set_text(self.label, self._text())


class NameField:
    """A UIButton acting as a text-entry row — click to focus, then type."""
    PARAMS = {}
    MAX_LEN = 16
    BLINK_INTERVAL = 0.5  # seconds per caret on/off phase

    def init(self, world, entity, params):
        self.name = ""
        self.blink_timer = 0.0
        self.caret_on = True
        self.label = _label(world, entity, "Name: _", depth=14)

    def _refresh(self):
        # Blinks between "_" and nothing on the trailing character while
        # focused — a steady "_" otherwise, so it isn't distracting when
        # you're not actually typing into it.
        caret = "_" if self.caret_on else " "
        yope3d.set_text(self.label, f"Name: {self.name}{caret}")

    def update(self, world, entity, dt):
        # get_ui_focus() returns None when nothing is focused, and
        # Entity.__eq__ only accepts another Entity (raises on None) — check
        # for None first rather than comparing directly.
        focus = world.get_ui_focus()
        focused = focus is not None and focus == entity

        if focused:
            self.blink_timer += dt
            if self.blink_timer >= self.BLINK_INTERVAL:
                self.blink_timer -= self.BLINK_INTERVAL
                self.caret_on = not self.caret_on
                self._refresh()
        elif not self.caret_on or self.blink_timer != 0.0:
            # Lost focus mid-blink (or mid-off-phase) — snap back to a
            # steady, always-visible caret instead of freezing on "off".
            self.caret_on = True
            self.blink_timer = 0.0
            self._refresh()

        if not focused:
            return
        if yope3d.input.is_key_pressed(yope3d.KEY_BACKSPACE) and self.name:
            self.name = self.name[:-1]
            self._refresh()

    def on_text_input(self, world, entity, codepoint):
        ch = chr(codepoint)
        if len(self.name) < self.MAX_LEN and ch.isprintable():
            self.name += ch
            self._refresh()
