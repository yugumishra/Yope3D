# Yope3D — Engine Capability Gaps for Shipping an Actual Game

A deep-dive audit of what the engine is missing between "simulation + renderer" and
"a game you could hand to a player." Each section documents **what exists today
(with code paths)**, **what's missing**, **why it blocks games**, and **what to
build**, so any item can be picked up as a work order.

**Scope exclusions** (already in progress, per direction): GJK/EPA narrowphase and
arbitrary-shape physics support.

Audited 2026-07-10 on branch `cppvers` (post-M15, post shadow mapping).

---

## Table of Contents

**Critical — blocks most game genres outright**
1. [No animation system of any kind](#1-no-animation-system-of-any-kind)
2. [In-game UI is display-only](#2-in-game-ui-is-display-only)
3. [No save / persistence layer](#3-no-save--persistence-layer)

**High — needed by most games; workarounds are painful**
4. [Physics gameplay features: triggers, joints, moving platforms](#4-physics-gameplay-features-triggers-joints-moving-platforms)
5. [No 3D transparency and no particle system](#5-no-3d-transparency-and-no-particle-system)
6. [Input: keyboard/mouse only, hardcoded, no action layer](#6-input-keyboardmouse-only-hardcoded-no-action-layer)

**Medium — quality/scale walls you hit mid-project**
7. [Renderer scaling and polish](#7-renderer-scaling-and-polish)
8. [Audio is SFX-grade, not soundtrack-grade](#8-audio-is-sfx-grade-not-soundtrack-grade)
9. [No prefab / entity-template system](#9-no-prefab--entity-template-system)
10. [One behavior script per entity](#10-one-behavior-script-per-entity)

**Lower priority / genre-dependent**
11. [No time scale (slow-mo / hitstop)](#11-no-time-scale-slow-mo--hitstop)
12. [Display & settings control not scriptable](#12-display--settings-control-not-scriptable)
13. [No AI / navigation](#13-no-ai--navigation)
14. [Camera niceties](#14-camera-niceties)
15. [Localization & fonts](#15-localization--fonts)

**Appendix**: [What's already game-ready](#appendix-whats-already-game-ready)

---

# CRITICAL

## 1. No animation system of any kind

**The single largest gap.** Nothing in the engine can animate anything over time
except a Python script mutating values in `update()`.

### What exists today

- `GltfLoader` (`src/assets/GltfLoader.h`) is explicitly static: the header
  states *"Animations, skins, cameras and lights are ignored (Milestone 16)."*
  It parses node TRS hierarchies (`nodeLocal()` / `traverse()` in
  `GltfLoader.cpp:275-304`) and preserves them as `LoadedNode` trees, which
  `World::importModel` reconstructs as `ecs::Parent`-linked entities — so the
  *skeleton of a scene graph* exists, but nothing drives it.
- The 32-byte `PackedVertex` format (`rendering/MeshBuild`) has position, oct-
  encoded normal/tangent, and UV. **No joint indices, no weights** — the vertex
  layout itself cannot express skinning.
- `Transform` (`src/world/Transform.h`) is pos/quat/scale; `TransformHierarchy`
  composes parent chains. `math::Quat::slerp` exists (`typings` line 350) — the
  math primitives for animation are all present.
- The only "animation" in the codebase is the loading-screen logo (offline-baked
  per-frame 2D line segments, `tools/logo_pack.py` → `assets/logo/logo.bin`) and
  `AnimatedBackground` (a UI texture flipbook, C++-only, see §2).

### What's missing

1. **Skeletal animation**: no skeleton/joint data structures, no skinning (CPU or
   GPU), no import of glTF `skins`/`animations` chunks, no vertex-shader palette.
   A humanoid character can only be a rigid capsule with a static mesh.
2. **Animation clips + playback**: no keyframe track sampling, no clip player
   component, no blending/crossfade, no animation state machine, no events on
   keyframes (footstep sounds etc.).
3. **Rigid/node animation**: even non-skinned animation (a door pivoting, a
   spinning coin, a bobbing pickup) has no clip path — glTF node animations are
   discarded at import.
4. **A general tween utility**: no engine- or script-side helper to interpolate a
   value/Transform over time with easing. `scripts/behaviors/_timers.py` exists
   (98 lines) as a Python timer helper, but there is no tween/ease library, so
   every fade, slide, and squash-stretch is hand-integrated per script.

### Why it blocks games

Characters, enemies, doors, chests, elevators, pickups, and UI flourish all read
as "animated" to players. Without clips, every motion is bespoke Python math in
`update()`, and skinned characters are simply impossible.

### What to build (suggested order)

1. **Tween/ease utility first** (cheapest, biggest coverage): a Python-side
   `yope3d.tween` module (or pure-Python helper next to `_timers.py`) with
   `ease_*` functions and a per-entity tween list ticked from a behavior. Zero
   C++ required; unlocks doors/platforms/UI fades immediately.
2. **Rigid node animation**: parse glTF `animations` targeting node TRS in
   `GltfLoader` (new `LoadedAnimation { channels[] }` on `LoadedModel`), store as
   an asset, add an `ecs::AnimationPlayer` component (clip name, time, speed,
   loop) whose system writes local `Transform`s before `TransformHierarchy`
   composes. Follow the `TextLabel3D` worked example for the full component
   lifecycle (serializer pair, `ComponentSnapshot`, inspector, create menu).
3. **Skinning** (the big one): extend `PackedVertex` (or a second vertex stream)
   with joints/weights, parse glTF `skins`, upload a joint-palette SSBO per
   skinned mesh, add a skinning path to `triangle.vert`, and sample clips into
   palettes on the render thread. Consider doing this as its own milestone (the
   header already earmarks it as M16).

---

## 2. In-game UI is display-only

> **Status: input routing + widget/hierarchy layer shipped (2026-07-11).** The
> "minimal path" below (all 4 steps) plus a native `UIButton` widget and a
> tween/easing utility are implemented: `src/ui/UIInput.{h,cpp}` routes the
> pointer to `view<UITransform>()` with hover/press/release/click events,
> delivered both as `Script` callbacks (`on_ui_press/release/enter/leave`,
> `on_text_input`) and polled (`World.ui_hit_test/ui_hovered/ui_consumed_click`);
> `src/platform/Input` gained a typed-char queue + cursor accessor for text
> input; `World.add_ui_textured_background` is now bound to Python;
> `UITransform` gained `anchor`/`size_mode`/`pixel_*`/`offset_*_px` fields
> (`src/ui/UILayout.h`) fixing aspect-ratio distortion for pinned HUD elements;
> `World.set_ui_parent`/`set_ui_group_visible`/`set_ui_group_opacity` group UI
> entities via the existing `ecs::Parent` link (`src/ui/UIHierarchy.h`);
> `ecs::UIButton` is a full-lifecycle native widget with hover/press/disabled
> visual states; `World.tween_ui_opacity` + `yope3d.EASE_*` (`src/ui/Tween.h`)
> animate fades. Still open: rich text, 9-slice/sprite-atlas, scroll views,
> clipping/masking, right/vertical text alignment, a visual 2D editor layout
> mode. See the sections below for the original audit this resolved.

Yope3D's UI is two parallel systems — a C++ `Label` system (`src/ui/`) that has a
minimal click path but is unreachable from games, and the ECS UI system
(`UITransform` + backgrounds/text) that games actually use but which is **pure
output**: rebuilt and drawn every frame with zero input routing.

### How it's wired today (the root of the problem)

- `Renderer::buildECSUIGeometry` (`src/rendering/Renderer.cpp:1232`) walks
  `view<UITransform>()`, depth-sorts (stable, ascending), and emits quads for
  whichever of `UIBackground` / `UICurvedBackground` / `UITexturedBackground` /
  `UIText` is present. That is the **entire** ECS UI pipeline — no update, no
  events, no state.
- Input goes elsewhere: `Engine::update` (`src/Engine.cpp:547-553`) detects an
  LMB **press edge only** and calls `UIManager::handleClick`
  (`src/ui/UIManager.cpp:92-101`), which hit-tests only the C++ `labels_` vector
  and invokes `Label::onClicked` on the topmost hit. ECS UI entities are never
  consulted. Scripts cannot create `Label`s — `UIManager` is not in the Python
  bindings at all.

A game menu built from Python gets rectangles and text on screen, and nothing else.

### 2.1 Event model — essentially absent

- **Only left-button press** is dispatched, hardcoded
  (`GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS` at `Engine.cpp:553`). No release events
  → no press-then-release button semantics, no drag, no click-cancel-by-moving-
  off. No right/middle button routing, no scroll routing (scrollable lists can't
  exist), no double-click.
- **No hover** — no enter/leave/move events anywhere, so no highlight-on-hover,
  tooltips, or cursor changes. `Label::hitTest` defaults to `return false`
  (`src/ui/Label.h:29`), so even C++ labels are click-transparent unless a
  subclass opts in.
- **No event consumption / click-through handling.** `handleClick` returns
  `void`; `Engine` never learns whether the UI ate the click, so gameplay
  scripts can't distinguish "clicked the pause button" from "clicked the world
  behind it" — clicking a menu also fires your shoot-on-click logic.
- **No focus system, no keyboard/gamepad navigation** — no focused-widget
  concept, tab order, or arrow-key/D-pad traversal. Combined with missing
  gamepad support (§6), controller-navigable menus are impossible.
- **The Python workaround is fully manual**: poll `window.get_cursor_pos()`,
  normalize by window size, compare against every `UITransform`'s
  `[minX,maxX]×[minY,maxY]`, and track per-element press/hover state machines.
  You end up writing an input system, not a game.

### 2.2 No widget layer

The component vocabulary (`src/ecs/Components.h:186-226`) is four leaf
primitives: solid rect (`UIBackground`), bottom-arc rect (`UICurvedBackground`),
textured rect (`UITexturedBackground`), text block (`UIText`). There is no
button, toggle, slider, progress bar, scroll view, list, dropdown, text field,
or tooltip — and with no events (§2.1), you can't compose them cleanly. There's
no widget state concept (normal/hover/pressed/disabled visuals).

Text entry is doubly blocked: even with a hand-built text-field widget,
`Window` (`src/platform/Window.cpp`) never installs a GLFW char callback, so
typed characters can't be received (keycodes ≠ text — no shift/layout/IME
handling).

### 2.3 Layout model — four absolute numbers, no relationships

`UITransform` is `{minX, minY, maxX, maxY, depth, visible}` in [0,1] screen
fractions (`Components.h:191-196`):

- **Aspect-ratio distortion**: fractional coords mean a 0.1×0.1 element is
  square only at square resolutions — at 16:9 it renders 1.78:1. Text
  compensates internally (`TextBox` lays out in em-space against a 1080p
  reference), but backgrounds don't: a "circular" curved background or icon
  stretches with the window. No pixel- or aspect-locked sizing mode exists.
- **No anchoring**: can't pin an element to a corner/edge with a fixed offset. A
  health bar meant to hug the bottom-left at constant size can't be expressed.
  `UIManager`'s centering helpers are C++-side and compute-once, not live.
- **No hierarchy**: `ecs::Parent` exists for 3D but `buildECSUIGeometry` ignores
  it — UI elements can't be grouped. "Move the whole pause menu" or "fade this
  panel and its children" means editing every element's four coordinates. No
  layout containers (stacks, grids, padding, margins), no auto-sizing a
  background to fit its text.
- **Z-order is a bare int** with back-to-front stable sort; the UI pass has no
  depth buffer (`Renderer.cpp:1310` — color-only framebuffer), so ordering and
  grouping are entirely manual.

### 2.4 Text limitations

`TextBox` (`src/ui/TextBox.cpp`) is one of the more finished pieces —
resolution-independent em-space layout, word wrap, `\n`, centered alignment,
vertical auto-fit shrink. But for game UI:

- **No rich text**: one color and one size per `UIText`. Inline colored/bold
  spans ("press <yellow>E</yellow>") require multiple manually-positioned
  overlapping entities.
- **No text measurement API for scripts** — can't ask "how wide will this string
  render," so sizing a button background to its label is guesswork.
- **Fixed buffers**: `UIText.text[1024]`, `TextLabel3D.text[256]`
  (`Components.h:222,234`) — dialogue boxes and logs cap silently.
- **Three pre-baked MSDF fonts** (monaco, nunito_sans, nunito_sans_bold); adding
  one needs the offline `bake_fonts` toolchain. Glyph coverage is bounded by the
  bake; no fallback font → effectively no non-Latin localization (§15).
- Two alignments (left/centered) — no right-align, no vertical alignment, no
  ellipsis-on-overflow (auto-fit shrinks instead, so long strings become
  comically small rather than truncated).

### 2.5 Visual/animation gaps

- **No 9-slice / sprite-border rendering** — textured panels stretch their
  corners; classic bordered frames can't scale. No sprite-atlas sub-rects (one
  texture = one file).
- **`AnimatedBackground`** (`src/ui/AnimatedBackground.h`, texture flipbook) is
  C++-`Label`-only — not an ECS component, not serializable, not scriptable.
- **`UITexturedBackground` has no Python creation binding** — only
  `add_ui_background`, `add_ui_curved_background`, `add_ui_text` exist
  (`src/scripting/python/bindings_world.cpp:199-203`). Scripts cannot put an
  *image* on screen unless the entity was authored in the editor scene.
- **No clipping/masking/scissor** — can't crop children to a panel (prereq for
  scroll views), no circular masks for portraits/minimaps.
- **No opacity groups or transitions** — fading a menu means lerping the `a`
  field of every component yourself; no tween/ease utilities (§1.4), no
  slide/scale animations.
- Curved background curvature is the **bottom arc only** — a loading-screen
  special, not a general rounded-rect (no per-corner radii, no borders/outlines).

### 2.6 Smaller but real

- No API to set a custom cursor image or hide/show it independent of lock state.
- No "center-ray → UI hit" helper for locked-cursor (FPS) menus.
- No integer-pixel snapping for backgrounds → HUD art sample-blurs at non-native
  sizes (text is fine; it's MSDF).
- Editor authoring is typing fractional coords into inspector fields — no visual
  2D layout mode.

### What to build (minimal path to "games can have menus")

1. **Route input to ECS UI**: in `Engine::update`, hit-test
   `view<UITransform>()` (topmost depth wins) on press *and* release, track
   hover from cursor pos, and surface to Python — behavior callbacks
   (`on_ui_press/release/enter/leave`) and/or polled
   `yope3d.ui_hit_test(x, y) -> Entity | None` plus `ui_consumed_click()` for
   click-through. This alone unlocks hand-built buttons.
2. **GLFW char callback + a focus flag** → text input becomes possible.
3. **`add_ui_textured_background` binding + anchor/pixel-size mode on
   `UITransform`** → HUDs stop stretching.
4. **A UI parent/panel grouping** for move/fade/show-hide of element sets.

Widgets, scroll views, 9-slice, and rich text layer on top incrementally.

---

## 3. No save / persistence layer

### What exists today

- **Scene save is editor-only**: `SceneSerializer::save(path, registry, world)`
  (`src/scene/serialization/SceneSerializer.h:18`) exists and works, but no
  Python binding exposes it (verified: zero `save` hits in
  `src/scripting/python/*.cpp`), and it serializes *authoring* state (component
  fields + `paramsBlob`), not *runtime* state.
- **Scene load** is one-way: `SceneManager::queueLoad()` → `flush()`
  (`src/scene/SceneManager.h:27-33`) destroys every current entity (calling
  `Script::onUnload` + delete) and instantiates the new file.
- Scripts have `serializeParams`/`deserializeParams`, but those round-trip the
  *inspector params*, not live gameplay state, and only through scene files.
- `yope3d.cfg` is parsed once at startup by `Config`
  (`src/scripting/Config.cpp:17-19` — exactly three keys: `startupScene`,
  `width`, `height`). Nothing ever writes it.

### What's missing

1. **Save games**: no API to capture world/entity/script state at runtime and
   restore it later. No slot management, no versioning, not even a
   "write this dict to disk" convenience binding (scripts *can* use Python's own
   `json`/`open`, but there's no sanctioned save directory, and nothing
   captures engine-side state like entity transforms/velocities for you —
   scripts would have to walk `view()` and rebuild the world by hand on load).
2. **Cross-scene state**: `load_scene` is scorched-earth. No
   "persistent entity" / "don't destroy on load" flag, no handoff payload
   (score, inventory, spawn-point id → next scene). The only escape hatch is
   Python module-level globals, which survive because the interpreter outlives
   scenes — undocumented and fragile (module reload would wipe them).
3. **User settings persistence**: resolution, volume, sensitivity, and bindings
   have nowhere to live. `Config` is read-only and three keys.

### Why it blocks games

Anything longer than one sitting needs saves; anything with more than one level
needs to carry state across `load_scene`. Right now a two-level game with a
score counter requires a Python global and a prayer.

### What to build

1. **Scene handoff payload** (cheapest): `yope3d.load_scene(path, payload: dict)`
   → stash the dict in `SceneManager`, expose `yope3d.scene_payload()` after
   load. ~30 lines across `SceneManager` + `bindings_world.cpp` + the stub.
2. **Sanctioned save-dir API**: `yope3d.save_path(name) -> str` returning a
   per-platform writable directory (bundle mode can't write next to the app —
   see `BundlePaths.cpp`), so scripts can persist JSON safely in dev *and* in a
   shipped `.app`.
3. **Runtime world snapshot** (bigger): reuse the machinery that already exists —
   `ComponentSnapshot` (`src/scene/ComponentSnapshot.h`) captures/restores the
   registry for editor Play/Stop. Marry it to `SceneSerializer` JSON plus a
   script hook (`def save_state(self) -> dict` / `def load_state(self, d)`) and
   you have real save games.
4. **Settings file**: a `yope3d.settings` get/set API writing an INI/JSON next
   to `yope3d.cfg` (or in the save dir), consumed at startup by `Config`.

---

# HIGH

## 4. Physics gameplay features: triggers, joints, moving platforms

### What exists today

- **Collision enter/exit events work** but are narrow: `World::detectCollisionEvents`
  (`src/world/World.cpp:1005-1032`) diffs this tick's contact-pair set against
  the previous tick's and queues `CollisionEvent { Entity a, b; bool enter; }`
  (`src/world/World.h:241`). Note the constraints:
  - Events only fire for pairs where **at least one side has a
    `ScriptComponent`** (`World.cpp:1011-1012`) — a script can't observe two
    non-scripted bodies colliding.
  - The event payload is **pair-level only** — no contact point, normal, or
    impulse magnitude reaches `on_collision_enter`. Impact-strength-scaled
    sound/damage/particles can't be driven from the callback.
  - Queue is capped at 8192 with silent oldest-drop (`World.cpp:1028-1030`).
- **`Hull.tangible = false` is not a trigger** — it's full removal from
  collision: `BroadphaseSAP` skips intangible bodies entirely
  (`src/physics/BroadphaseSAP.cpp:28`), so they generate **no contacts and
  therefore no events**. There is no overlap-without-response mode.
- **Kinematic bodies are invisible to the sim**: `World::addKinematicCapsule`
  (`World.cpp:445-450`, `World.h:50-51`) creates `Transform + CapsuleForm` with
  **no Hull** — the comment says it outright: *"physics sim ignores it."* The
  character controller works by querying (`capsule_cast` / `capsule_overlap` /
  `raycast`, `src/physics/KinematicQuery.cpp`, `Raycast.cpp`) and writing its
  own Transform.
- **Constraints = springs, full stop**: `physics::Spring` (`src/physics/Spring.h`)
  is a Hookean distance spring (plus visual helix proxies). That is the entire
  joint vocabulary.
- The Python `character_controller.py` (178 lines) is genuinely decent: step
  height, max slope + slide, sprint, jump, first/third person. Credit where due.

### What's missing

1. **Trigger volumes**: no `isTrigger` flag that keeps a shape in
   broadphase/narrowphase but skips the solver while still emitting
   enter/exit events. Checkpoints, pickups, damage zones, kill planes, door
   sensors, quest areas — all currently require every interested script to poll
   `capsule_overlap` each frame.
2. **Joints**: no hinge (doors, levers, wheels), ball (ragdolls, chains),
   slider (pistons, elevators), fixed (welds), or distance joint; no motors, no
   breakable joints. The PGS solver already iterates contact constraints
   per-island (`solveIsland`), so the solver loop is the natural insertion
   point — but zero joint infrastructure exists.
3. **Moving platforms / kinematic-vs-dynamic interaction**: because kinematic
   bodies have no Hull, dynamic bodies can't stand on or be pushed by them, and
   the solver never sees a platform's velocity — so a player on a moving
   platform doesn't inherit its motion. Conversely a `Fixed` body teleported by
   `set_position` imparts no velocity either.
4. **Contact data in script callbacks** (point/normal/impulse), per the payload
   note above.
5. **Collision-event scope**: the ScriptComponent gating means "spawn a particle
   wherever any two things collide" style systems can't be script-driven.

### What to build

1. **Triggers** (highest value/cost ratio): add `bool isTrigger` to `ecs::Hull`
   (or a zero-size `Trigger` tag). Let it pass broadphase + narrowphase, then
   (a) skip its contacts in the solver dispatch and (b) route its pairs into
   `detectCollisionEvents` regardless of ScriptComponent gating. Wire the
   serializer/inspector per the `TextLabel3D` checklist.
2. **Enrich `CollisionEvent`** with the deepest contact point/normal and the
   accumulated normal impulse from the solver's cached lambdas
   (`ContactCache` already stores them for warm-starting).
3. **Kinematic velocity**: give kinematic bodies an optional Hull with
   `inverseMass = 0` and a script-set velocity that integration applies and the
   solver reads — that alone makes dynamic boxes ride platforms.
4. **Joints**: start with hinge + fixed (covers doors, levers, welded props),
   as a `JointConstraint` component + a solve pass alongside contacts in
   `solveIsland`. Ball/slider/motors after.

---

## 5. No 3D transparency and no particle system

### What exists today

- The main mesh pipeline is **opaque-only**: its blend attachment sets only
  `colorWriteMask` and never enables blending
  (`src/rendering/Renderer.cpp:713-721`), and the draw loop
  (`Renderer.cpp:1131` area) iterates `view<Transform, MeshRenderer>()` in
  registry order — no sorted back-to-front pass exists.
- `Material.albedoFactor[4]` has an alpha slot (`Components.h:103`) but it can
  only darken opacity within the opaque pass' output; there is no translucent
  path for it to feed. glTF `alphaMode` (BLEND/MASK) is not honored.
- Blending *does* exist elsewhere — UI, 3D text (depth-write-off alpha blend),
  debug lines — so the pipeline-state recipes are all in the codebase already.
- **No particle system files exist anywhere** (no emitter, no billboard quad
  batch, no compute-sim). The closest thing is the debug-line stroke pipeline.

### Why it blocks games

- No glass, water, force fields, ghosts, holograms, fade-in/out of meshes
  (spawn/despawn effects, occlusion fading of walls in third-person cameras).
- No explosions, smoke, fire, sparks, dust, muzzle flashes, hit confirms, rain,
  pickup sparkles. Games communicate almost everything through particles;
  without them every interaction reads as dead.

### What to build

1. **Transparent mesh pass**: add a second graphics pipeline (same shaders,
   blend enabled, depth-test on / depth-write **off**), partition the mesh loop
   by a Material flag (`alphaMode`), sort transparent draws back-to-front by
   view-space depth, record after opaques. The skybox/text/line passes are
   templates for the pipeline setup.
2. **Particles v1 (CPU)**: an `ecs::ParticleEmitter` component (spawn rate,
   lifetime, velocity cone, size/color-over-life, texture) + a render-thread
   sim writing camera-facing quads into a per-frame vertex buffer — exactly the
   `Text3DBuffer` pattern (`src/gpu/Text3DBuffer.*`), drawn with the
   transparent-pass state. A few thousand particles is plenty for v1.
3. **Particles v2 (later)**: compute-shader sim + soft particles (needs depth
   sample), only if v1's CPU cost shows up in the profiler.

---

## 6. Input: keyboard/mouse only, hardcoded, no action layer

> **Status: action-map layer + configurable engine hotkeys shipped
> (2026-07-15).** `behaviors/_actions.py` is a pure-Python `ActionMap` layer
> over `Input` — named actions with multiple key/mouse bindings,
> `down/pressed/released/axis/vector2` queries, JSON save/load for rebinding,
> four ready-to-use presets (`PRESET_FPS`/`PLATFORMER`/`TOP_DOWN`/`MENU_NAV`),
> and `remap_for_layout(preset, "dvorak"|"colemak")` generators that
> translate letter/punctuation bindings to whichever physical key produces
> the same character under that layout (GLFW key tokens are physical-position
> codes, so WASD *movement* already works identically on every layout without
> remapping — the generator matters for character/mnemonic-preserving
> bindings like reload=R or a digit hotbar; see the module docstring for the
> distinction). `Input::isMouseDown(button)` (generic, bounds-checked) backs
> `ActionMap.down()` for mouse bindings; `Input::getKeyName(key)` (thin
> `glfwGetKeyName` wrapper, layout-aware for printable keys) backs
> `_actions.label(binding)` for rendering an accurate "Press ___" rebind-UI
> string — raw `KEY_*` codes are layout-*independent* (physical position), so
> this is a display-only concern, never something gameplay logic needs to
> care about. The window-level hotkeys are now
> opt-out: `Window::setEscapeCloses/setTabPauses/setF11Fullscreen`, each
> configurable via `yope3d.cfg` (`escapeCloses`/`tabPauses`/`f11Fullscreen`,
> default `true`) or from Python on `yope3d.window` — the physical key still
> reaches `Input` regardless of the flag, so a script can bind ESC/TAB/F11 to
> its own action once the built-in behavior is disabled. The full A-Z/digit/
> punctuation/function-key `yope3d.KEY_*` set is now bound (previously ~20
> keys). **Gamepad support remains out of scope** — excluded on request
> (not reliably testable in the current dev setup), so §6.1/§6.4's gamepad
> items below are still open; text input (§6.3, listed missing below) was
> actually already shipped by the UI overhaul (§2) before this audit item was
> written and is stale in the "What's missing" list.

### What exists today

- `Input` (`src/platform/Input.h`) is polling-based keyboard + 5 mouse buttons +
  delta + scroll, with careful one-shot press/release edge handling. It is
  solid for what it covers.
- Exposed to Python 1:1 (`is_key_down(key: int)` etc.) — scripts compare **raw
  GLFW keycodes** (`character_controller.py` hardcodes WASD/Space/Shift/V).
- **Window-level hotkeys are consumed before scripts see them**
  (`src/platform/Window.cpp:130-160`): ESC = quit (when `escapeCloses`),
  F11 = fullscreen toggle, TAB = engine pause. The comment confirms they are
  *"NOT forwarded to Input."*

### What's missing

1. **Gamepad/joystick**: zero GLFW gamepad references in `src/platform/`
   (verified). No `glfwGetGamepadState`, no axes/buttons/deadzones, no rumble,
   no hot-plug handling. Controller play is impossible.
2. **Action mapping**: no layer between "keycode" and "gameplay intent." Every
   script hardcodes physical keys, so rebindable controls, multiple bindings
   per action (key + pad button), or context-sensitive maps (menu vs. gameplay)
   would need rebuilding per game — and there's nowhere to persist bindings
   (§3.3).
3. **Text input**: no char callback (see §2.2).
4. **The hardcoded window hotkeys collide with games**: many games use TAB
   (scoreboard/inventory) and ESC (pause menu, *not* instant quit). These need
   to become configurable or script-visible.

### What to build

1. **Gamepad polling** in `Input`: wrap `glfwGetGamepadState` each frame,
   expose axes (with a default deadzone) + buttons + edges to Python
   symmetrically with keys.
2. **Action map**: a small table (JSON, lives with user settings) mapping action
   name → list of bindings; Python queries `input.action_down("jump")` /
   `action_pressed` / `action_axis("move_x")`. Pure additive layer over the
   existing pollers.
3. **Make ESC/TAB/F11 opt-out** via `Config`/API (`escapeCloses` already exists
   as a flag — extend the pattern), and forward them to `Input` when unbound.

---

# MEDIUM

## 7. Renderer scaling and polish

### What exists today

- Draw submission is `view<Transform, ecs::MeshRenderer>()` → one
  `vkCmdDrawIndexed` per mesh with a 64-byte model-matrix push constant
  (`Renderer.cpp:936/1028/1131` loops). Every mesh is drawn **every frame,
  no matter where it is**.
- Mipmaps are generated on texture upload (`src/gpu/Texture.cpp:111-137`) —
  good. `cullMode = VK_CULL_MODE_NONE` on the main pipeline
  (`Renderer.cpp:705`) — even back-face culling is off, so every triangle
  shades twice.
- Lights: per-frame SSBO rebuild capped at `YOPE_MAX_LIGHTS`
  (`Renderer.cpp:1882` breaks at the cap); the shader iterates all lights for
  every fragment — no light culling/clustering.
- Shadows: **exactly one caster**, spot/directional only, single `D32` map with
  manual PCF (`rendering/ShadowMap`, `World::setShadowCaster` radio behavior).
  No point-light shadows (needs cubemap), no cascades → outdoor directional
  shadows are one small crush-prone frustum.
- Post-processing: Reinhard tonemap + global exposure live **inside
  `triangle.frag`** (line ~227) — there is no post pass at all. MSAA is off
  (`VK_SAMPLE_COUNT_1_BIT`, `Renderer.cpp:711`) and no FXAA/TAA exists, so
  everything is aliased. No bloom (emissive materials exist but can't glow),
  no SSAO, no vignette/DoF/color-grading hooks.

### What's missing / what to build

1. **Back-face culling**: one-line-ish fix (`VK_CULL_MODE_BACK_BIT`) — verify
   imported meshes' winding first (tangents are recomputed at upload; winding
   is honored as-authored).
2. **Frustum culling**: extract 6 planes from `proj*view`, test each
   RenderMesh's world AABB (min/max already computable at upload) before
   recording. CPU-side, ~a day, and it's the single biggest scaling lever.
3. **Instancing**: batch identical `RenderMesh*` draws (the `meshPool_`
   non-owning pointer model makes identity trivial) into
   `vkCmdDrawIndexed(instanceCount=N)` with a per-instance SSBO. Matters the
   moment someone builds a forest/crowd/rubble pile.
4. **Shadow upgrades** in priority order: depth-clamp + slope-scaled bias
   polish → 2–4 cascade CSM for the directional caster → optional point-light
   cubemap for interiors.
5. **A real post chain**: render scene to an offscreen HDR target
   (`StorageImage` infra exists from the raytracer), then a fullscreen
   tonemap+FXAA pass to swapchain; add bloom (bright-pass + blur chain) after.
   This also unlocks §11-style effects (screen fades) and fixes "emissive
   doesn't glow."
6. **LOD**: lowest priority — needs authoring support; skip until content
   demands it.

## 8. Audio is SFX-grade, not soundtrack-grade

### What exists today

- OpenAL-Soft; `AudioLoader` decodes **Ogg Vorbis only**
  (`stb_vorbis_decode_filename`, `src/assets/AudioLoader.cpp:10` — no WAV/MP3/
  FLAC path exists) and decodes the **entire file into memory**.
- Stereo is force-down-mixed to mono at load (`src/audio/AudioSystem.cpp:60-76`)
  because OpenAL doesn't spatialize stereo — correct for SFX, but it means
  **music cannot play in stereo at all**: every buffer in the engine is
  `AL_FORMAT_MONO16`.
- Per-source gain/pitch/loop/position/velocity + Doppler + reference distance
  are exposed to Python; `pause_all`/`resume_all`/`stop_all` exist.
  `AudioSource` ECS component gives autoplay + editor authoring.

### What's missing

1. **Streaming**: a 3-minute 44.1 kHz track fully decoded is ~15 MB mono
   (30+ MB if stereo were kept) *per track*, decoded synchronously at load. No
   `stb_vorbis` push-API ring-buffer streaming source exists.
2. **A 2D/music path**: a non-spatialized stereo source type
   (`AL_SOURCE_RELATIVE` with position zero, stereo buffer kept intact) —
   currently impossible because down-mix happens unconditionally at load.
3. **Mixer buses**: no master/music/SFX/voice volume groups. A volume-options
   menu means scripts manually scaling every live source's gain.
4. **Fades/crossfades**: no engine helper; hand-lerp `set_gain` per source per
   frame (and there's no tween utility — §1.4).
5. Minor: no pitch/gain randomization helper for repeated SFX, no priority/
   voice-limit system if many sources play at once.

### What to build

1. `Music` source type: skip down-mix when flagged, `AL_SOURCE_RELATIVE`,
   stereo buffer; add `yope3d.play_music(path, loop, fade_in)`.
2. Streaming for that type (queue 2–3 one-second buffers via
   `alSourceQueueBuffers`, refill from `stb_vorbis` push API on a small timer —
   the physics `ThreadPool` or a dedicated thread).
3. Bus gains: three floats in `AudioSystem`, every source tagged with a bus,
   effective gain = source × bus × master. Persist via §3.4 settings.

## 9. No prefab / entity-template system

### What exists today

- Reuse mechanisms are: (a) copy-paste entity JSON inside scene files, (b)
  rebuild entities from Python at spawn time (`world.add_capsule(...)` +
  `attach_*` + `reg_add` + param wiring), (c) `add_model(path)` for pure visual
  glTF subtrees.
- The serialization layer (`scene/serialization/ComponentSerializers`,
  `buildSerTable`) already knows how to write/read every component — a prefab
  is "a scene file with one root entity," which is 90% of the implementation.

### Why it matters

A configured enemy = mesh + material + hull + collider form + script + params +
maybe audio + light. Spawning three of them at runtime, or using the same one in
five scenes, currently means duplicating that wiring in JSON or Python. Any
balance change then fans out to every copy — the classic pre-prefab content
maintenance trap.

### What to build

1. **File format**: reuse the scene JSON schema, rooted at one entity subtree
   (`.yprefab` under `assets/prefabs/`).
2. **Runtime**: `yope3d.spawn(prefab_path, pos, rot) -> Entity` — parse (cache
   the parsed form), instantiate via the existing deserializer table, remap
   `Parent` links, call script `init`s.
3. **Editor**: "Save selection as prefab" + drag-from-Asset-Browser-to-viewport.
   Nested prefabs and property overrides are v2 — don't build them first.

## 10. One behavior script per entity

### What exists today

- `ecs::ScriptComponent` (`src/ecs/Components.h:174-178`) is
  `{ scriptClass[64], paramsBlob[2048], Script* instance }` — one class name,
  one params blob, one live instance. The archetype registry stores **at most
  one instance of each component type per entity**, so a second
  `ScriptComponent` is structurally impossible, and `PythonScript` bridges to
  exactly one Python class.
- Everything downstream assumes the singular: `yope3d.get_behavior(entity)`
  (`bindings_ecs.cpp:405`) returns *the* instance; the collision-event router
  dispatches to *the* behavior; the editor inspector edits *the* class + params.

### Why it matters

The industry-standard authoring model (Unity `MonoBehaviour`s, Unreal actor
components, Godot child nodes) is *stacking* behaviors: an entity is
"enemy + health + burnable + lootable" by attaching four small scripts. Here
that entity must be one monolithic class, or hand-rolled delegation — every
capability combination becomes a new class or an if-forest, and reuse across
entity types (the same `health` logic on player, enemies, and crates) gets
awkward. The related constraint: `paramsBlob` caps at 2048 bytes of JSON, so
data-heavy configuration has to live outside the component anyway.

### What to build

1. **Pure-Python composite (recommended, zero C++)**: one blessed
   `CompositeBehavior` host class whose params list sub-behaviors, e.g.
   `{"behaviors": [{"module": "health", "class": "Health", "params": {...}},
   ...]}`. It instantiates each, forwards `init`/`update`/
   `on_collision_enter/exit`/`on_unload` in order, and exposes
   `get(name)` so `get_behavior(e).get("Health")` reaches a sub-behavior.
   ~60 lines, ships today, becomes the project convention.
2. **Engine-assisted variant (later)**: teach `PythonScript` itself to accept a
   class *list* in `paramsBlob` and fan out internally, plus inspector support
   for editing the stack — same data model, nicer authoring.
3. **Not recommended**: true multi-`ScriptComponent` support — one-per-type is
   load-bearing in the archetype ECS (`add<T>` migrates by type set); fighting
   it buys nothing over options 1/2.

Severity is "medium" only because the workaround is cheap — but decide the
convention **before** writing significant gameplay code, since it shapes every
script's structure.

---

# LOWER PRIORITY / GENRE-DEPENDENT

## 11. No time scale (slow-mo / hitstop)

`Engine` runs physics on a fixed 240 Hz accumulator thread (`Engine.h`,
`PhysicsConstants.h`); the only control is the binary `paused_` atomic exposed as
`set_paused` (`bindings_world.cpp:232`). No `timeScale` exists anywhere
(verified: zero grep hits). Slow-motion, hitstop, and bullet-time are out;
pause-menu-with-animated-UI works only because UI/scripts tick on the render
thread. **Fix**: a `World::timeScale_` multiplying the accumulator's dt feed
(keep `PHYSICS_DT` fixed, scale the *accumulation rate*), plus scaling the `dt`
passed to script `update()`; expose `yope3d.set_time_scale(s)`, and audit
scripts that integrate with wall-clock `yope3d.time()`.

## 12. Display & settings control not scriptable

Fullscreen exists only as the hardcoded F11 handler (`Window.cpp:139-155`);
the Python `Window` class exposes only size + cursor pos/lock (stub lines
1337-1362). No `set_fullscreen`, `set_resolution`, vsync control (swapchain
hard-picks MAILBOX-else-FIFO, `src/gpu/Swapchain.cpp:114-115`), or monitor
enumeration — so a graphics-options menu cannot do its job, and (§3.3) couldn't
persist its choices anyway. **Fix**: bind `Window::setFullscreen(bool)` /
`setSize(w,h)` (the swapchain-recreate path already handles resize), add a
present-mode preference, persist via the settings API.

## 13. No AI / navigation

Nothing exists: no navmesh (bake or runtime), no pathfinding, no steering, no
perception helpers (a raycast-based line-of-sight check is scriptable today via
`yope3d.raycast`), no behavior trees/state-machine utilities. Expected at this
engine stage, but any game with enemies needs at least: a grid/waypoint A*
helper in Python (zero engine work), then later a recast-style navmesh bake.
Flagging so it's on the roadmap, not because it blocks the next milestone.

## 14. Camera niceties

`Camera` (`src/rendering/Camera.h`, stub lines 1286-1312) is
position/rotation/fov + `screen_to_ray` + `look_at`, perspective-only (no
orthographic projection → no clean 2D/isometric mode). No follow/orbit/spring-
arm helper, no camera collision (third-person cameras clip through walls — the
character controller's TPS mode has `cam_distance` but nothing spheres-casts the
boom), no shake, no smoothing/damping utilities. All scriptable in Python today
(`character_controller.py` proves it), but every project rebuilds them —
candidates for a shared `scripts/behaviors/camera_rig.py` + a spherecast
binding (capsule_cast with r≈0 works already).

## 15. Localization & fonts

No string-table/locale system (fine to defer), but the harder constraint is
fonts: MSDF atlases are offline-baked with a fixed glyph set (`tools/msdf_bake.cpp`,
`FONT_BAKE_LIST` in `CMakeLists.txt`), three fonts committed, no runtime font
loading and no fallback chain. CJK/Cyrillic/Arabic text will render as missing
glyphs. UTF-8 handling in `TextBox` needs verification before any non-ASCII
content is promised. Cheap first step: bake an extended-Latin set; real i18n
needs a fallback-font chain and larger/multi-page atlases.

---

# Appendix: What's already game-ready

For contrast — these systems audited as genuinely solid foundations and are
**not** gaps:

- **Kinematic queries**: `raycast`, `capsule_cast`, `capsule_overlap` with layer
  masks and exclusion (`src/physics/KinematicQuery.cpp`, `Raycast.cpp`).
- **Collision layers/masks** (32 named layers) on every Hull.
- **Collision enter/exit callbacks** into Python (with the caveats in §4).
- **Character controller**: step-up, slope limit + slide, sprint/jump, FPS/TPS
  toggle — `scripts/behaviors/character_controller.py`.
- **Per-entity Python behaviors** with typed, inspector-editable params and
  exception-safe dispatch.
- **PBR material system** (Cook-Torrance, 5 maps + factors, `MaterialCache`),
  skybox, single-caster shadow mapping with full tuning surface.
- **MSDF text** in UI and world space (crisp at any scale).
- **Async scene loading** with an animated splash; safe mid-game scene swaps via
  `queueLoad`/`flush`.
- **Compound colliders** (static + dynamic, baked BVH, pivot compensation).
- **Editor loop**: Play/Stop with registry snapshot restore, undo/redo, picking,
  gizmo-free but functional inspectors.
- **Profiler + analyzer toolchain** (Phase E) for when the perf items in §7 get
  built.

## Suggested attack order (cross-category)

Cheapest → unlocks the most, first:

1. §4.1 Triggers + §2 step-1 UI input routing + §1.1 tween utility —
   *simple complete games (puzzle/platformer/arcade) become possible.*
2. §3.1 scene handoff + §3.2 save dir + §6.1 gamepad —
   *multi-level games with saves and controller support.*
3. §5.1 transparent pass + §5.2 particles v1 + §8.1-2 music path —
   *games start feeling alive.*
4. §1.2 rigid animation clips → §1.3 skinning (M16) — *characters.*
5. §7 renderer scaling as content size demands; the rest opportunistically.
