# Composite Behaviors — Design Work Order

**Status:** designed, not implemented. Supersedes `limitations.md` §10 ("One behavior
script per entity"), which should be read as the problem statement and this as the
decision record.

**Audited** 2026-07-17 on branch `cppvers`. Line numbers are hints as of that date —
trust the symbol names, re-grep the lines.

**Goal:** an entity can carry several small behaviors (`Health`, `Burnable`, `Lootable`)
instead of one monolithic class, *without* the developer ever seeing the composition
machinery — in the editor, in scene JSON, or in the Python API.

---

## 1. The constraint (not negotiable)

`ecs::ScriptComponent` (`ecs/Components.h:231-235`):

```cpp
struct ScriptComponent {
    char    scriptClass[64]   = {};
    char    paramsBlob[2048]  = "{}";
    Script* instance          = nullptr;
};
```

The archetype registry stores **at most one instance of each component type per
entity** — `Registry::add<T>` migrates by type set, and `add` asserts if the type is
already present (`ecs/Registry.h:123`). A second `ScriptComponent` is therefore
structurally impossible.

**This design does not fight that.** One component stays one component. What changes is
that the component can host a *stack* of scripts instead of exactly one, and that every
developer-facing surface presents the stack, not the host.

`limitations.md` §10 lists "true multi-`ScriptComponent` support" as *not recommended*.
That is a different proposal from this one and its rejection still stands. This is §10's
option 2 ("engine-assisted variant"), which the doc endorses as the destination.

---

## 2. Why not the pure-Python `CompositeBehavior` (§10 option 1)

§10 recommends a Python `CompositeBehavior` host class whose params list sub-behaviors,
as a zero-C++ convention. **Rejected.** It destroys a real feature.

`ScriptComponentInspector.cpp` is a 329-line **typed** param editor, not a JSON textarea.
`BehaviorRegistry::refresh` (`scripting/python/BehaviorRegistry.h`) imports every
`scripts/behaviors/*.py`, harvests each class's `PARAMS` dict, and builds a `ParamDef`
per entry carrying a type (`float`/`int`/`bool`/`str`/`enum`/`strlist`) plus a default.
The inspector renders real widgets from that. See `character_controller.py:25` for a
`PARAMS` declaration.

Under a Python composite, `BehaviorRegistry` harvests `CompositeBehavior.PARAMS` — one
nested `behaviors` list. `Health.PARAMS` is never reached, because `Health` isn't a
`Script`, it's a plain object inside a blob. The typed editor degrades to a multiline
JSON field **on every entity**, since the composite is the universal case.

The same argument kills `Script::drawInspector` (`Script.h:62`) and
`serializeParams`/`deserializeParams` (`Script.h:57-58`) for sub-behaviors: they're
`Script` virtuals, and sub-behaviors wouldn't be `Script`s.

Net: option 1 trades the best editor UX in the scripting stack for an afternoon of work.

---

## 3. Recommended design

### 3.1 `CompositeScript` is a C++ `Script` subclass

```cpp
class CompositeScript : public Script {
    std::vector<std::unique_ptr<Script>> children_;   // each a PythonScript
public:
    void update(ScriptContext& c, ecs::Entity s, float dt) override {
        for (auto& ch : children_) ch->update(c, s, dt);
    }
    // ...identical fan-out for every other virtual
};
```

**This is the whole trick.** The `Script` vtable *is* already the fan-out interface, so
every existing dispatch site keeps calling `inst->onCollisionEnter(...)` and **changes
not at all**. Each child is an ordinary `PythonScript` with its own class and its own
params, so `BehaviorRegistry` sees real `PARAMS` per child and the typed inspector
survives intact.

Register it with `YOPE_REGISTER_SCRIPT(CompositeScript)` — `ScriptFactory` registers
under the class name verbatim, and `SceneManager::instantiateScript` (`SceneManager.cpp:126`)
already does `ScriptFactory::create(sc->scriptClass)`, so a `scriptClass` of
`"CompositeScript"` instantiates through the existing path with no changes.

### 3.2 The dispatch surface it must fan out

Complete list of `Script` virtual dispatch sites (verified — every one of these is
untouched by this design):

| Virtual | Dispatched from |
|---|---|
| `deserializeParams` | `SceneManager.cpp:64`, `:138` |
| `init` | `SceneManager.cpp:71`, `:146` |
| `onUnload` | `SceneManager.cpp:32` |
| `onUIPress/Release/Enter/Leave` | `Engine.cpp:570-573` |
| `onTextInput` | `Engine.cpp:595` |
| `update` | `Engine.cpp:631` |
| `onCollisionEnter/Exit` | `Engine.cpp:653-654` |
| `drawInspector` | `ScriptComponentInspector.cpp:298`, `:326` |
| `pyInstanceHandle` | `bindings_ecs.cpp:505` (backs `get_behavior`) |
| `serializeParams` | overridden in `PythonScript.cpp:174`; no external caller |
| `onScroll` | **nothing.** Declared at `Script.h:28`, never dispatched. |

`Script::onScroll` being dead is pre-existing, not something this change introduces —
but `CompositeScript` should fan it out anyway so it works if it's ever wired up.

### 3.3 Only materialize the composite at 2+

A single script stores exactly as today (`scriptClass` = the real class, `paramsBlob` =
its params, no wrapper). `CompositeScript` appears only when an entity has two or more.

This makes the migration **non-breaking**: every existing scene file, `.ytemplated`,
`ComponentSnapshot`, and `attach_script` call keeps working untouched, and the common
one-script case pays nothing.

---

## 4. Decisions the implementer must make

### 4.1 `get_behavior(e)` semantics — **DECIDED 2026-07-17**

`get_behavior` (`bindings_ecs.cpp:500-505`) returns *the* instance via
`pyInstanceHandle()`. With a stack, returning the `CompositeScript` leaks the very
abstraction this design hides; returning `children_[0]` is arbitrary.

**Decision — identity is the Python class name:**

- `get_behavior(e, "Health")` looks up by **Python class name** (not module path, not
  an author-assigned id).
- The 1-arg form `get_behavior(e)` keeps working when the stack is exactly 1, and
  **raises** rather than silently picking when it's ambiguous.
- `CompositeScript::pyInstanceHandle()` returns `nullptr` so the composite can never
  be mistaken for a behavior.
- **Duplicate classes on one entity are rejected with a clear error** at attach/load
  time (two `Health` pools is conceivable but rare enough to not design for; a game
  that needs it subclasses). This is load-bearing beyond `get_behavior`: class names
  are also the keys for per-script save-state (`limitations.md` §3.3 — see §6), so
  they must be unique per entity.

### 4.2 Params storage — `paramsBlob[2048]` is now shared by the whole stack

Four scripts' nested JSON against a 2048-byte fixed array is tight, and it fails
**silently**: `ComponentSerializers.cpp:456-461` is a bare `strncpy` that truncates
without error. Truncated JSON doesn't degrade to "one script lost its params" — it's
unparseable, so the entity's whole stack dies. The inspector bakes the same assumption
(`s_lastBlob[2048]`, `LiveParamValue::listBuf[2048]`).

This hits the pure-Python option identically, so it isn't an argument against this
design — it's an argument *for* it: engine-owned storage is storage the engine can fix.

**Recommendation:** keep the blob for v1, but make overflow a **loud error** instead of
a silent truncation. That's cheap and removes the trap.

If it needs to grow, the codebase already has a blessed pattern: `assets/AnimationClip.h`
states outright that variable-length data can't live in archetype storage, so clips sit
in `World::animationClips_` keyed by name with only a POD key in `ecs::AnimationPlayer`.
A script-params side table would follow that precedent exactly.

### 4.3 Update order

Load-bearing the moment two scripts touch the same `Transform`. Define it (list order),
make it reorderable in the inspector, and make it explicit in the serialized form. Cheap
now, expensive to retrofit after gameplay code depends on an accident.

### 4.4 Serialized shape — **DECIDED 2026-07-17**

**Decision:** scene JSON uses `"scripts": [ {module, class, params}, ... ]` — an
array of per-script objects, list order = update order (§4.3), never a nested blob.
A single-script entity may keep the legacy singular form on disk (the deserializer
accepts both), but anything the serializer *writes* going forward targets the array
shape. Note this is *not* the same data model as §10 option 1 (which hides the stack
inside `paramsBlob`) — which is precisely why going straight to this design avoids a
scene-file rewrite later.

The save-game format (`limitations.md` §3.3) builds on this same shape: a save file's
entity node carries `"scriptState": { "<ClassName>": {...}, ... }` alongside
`"scripts"`, keyed by the §4.1 identity. Committing to the array shape now is what
lets save games start before `CompositeScript` is implemented.

---

## 5. Gotchas

- **`ComponentSnapshot` restores `ScriptComponent` by value** (`ComponentSnapshot.h`,
  `hasScript` / `instance` always null). If params ever move to a side table, the
  snapshot must capture that table too or editor Play/Stop silently drops param edits.
- **Instance deletion is not `onUnload`.** `World.cpp:888` does a bare
  `delete sc->instance` on mid-frame entity removal with no `onUnload` call (deliberate —
  see the comment there). `CompositeScript`'s destructor must free `children_`;
  `unique_ptr` handles it, but don't add a raw-pointer child list.
- **`onUnload` fan-out order** should probably be reverse of `init` order. Decide it.
- **Editor undo**: `SetComponentCommand` operates on whole components, so a per-script
  edit snapshots the whole stack. That already matches how the blob behaves today.
- **Exception isolation**: `PythonScript` try/catches every call so one script's
  exception logs instead of crashing. `CompositeScript` must not let child *i*'s throw
  skip children *i+1..n* — each child call needs its own guard, or the isolation
  regresses for stacks.

---

## 6. Sequencing note — `limitations.md` §3.3 is now unblocked

Save games (§3 item 3) want `save_state`/`load_state` script hooks. The earlier version
of this note assumed that state's natural home was `paramsBlob` and therefore gated
§3.3 on this design's implementation. **That coupling is gone**: save-state dicts are
streamed by the save path directly from the live script instance into the save file's
JSON (`"scriptState"` node, §4.4) and fed back via `load_state(dict)` after
`deserializeParams` → `init` on load. They never transit `paramsBlob`, so the 2048-byte
budget (§4.2) doesn't apply to them.

What §3.3 actually needed from this document was two *decisions*, both now made:
per-script identity (§4.1 — Python class name, unique per entity) and the serialized
shape (§4.4 — the `"scripts"` array). Save games can proceed on those; when
`CompositeScript` lands, its save/load fan-out asks each child for state under its
class-name key — the shape already committed to — so nothing is designed twice.

§3 items 1, 2 and 4 (scene handoff payload, `save_path`, settings API) are already
shipped and don't touch this.

---

## 7. Touch list

- `scripting/CompositeScript.{h,cpp}` — new; `YOPE_REGISTER_SCRIPT(CompositeScript)`.
- `CMakeLists.txt` — add to `ENGINE_SOURCES` (shared by `yope3d` + `yope_editor`).
- `scene/serialization/ComponentSerializers.cpp` — `"scripts": [...]` shape; loud
  overflow error.
- `editor/inspectors/ScriptComponentInspector.cpp` — stack UI (add/remove/reorder, one
  typed param block per child via the existing `BehaviorRegistry` path).
- `scripting/python/bindings_ecs.cpp` — `get_behavior(e, name)`; `attach_script`
  semantics for a second attach.
- `typings/yope3d/__init__.pyi` — **mandatory**, per the root `CLAUDE.md` invariant.
- `scripting/CLAUDE.md` — the "Model: per-entity scripts" bullet says "one class name,
  one params blob, one live instance"; update it.
- `limitations.md` §10 — mark superseded, point here.
