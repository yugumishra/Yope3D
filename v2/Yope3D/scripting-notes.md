# M14 Python Scripting — Implementation Notes

**Status:** Implemented, untested end-to-end. Compiles clean (both `yope3d` and `yope_editor`). Existing ECS/physics tests still pass.

---

## What was built

### Files added

```
src/scripting/python/
    PyComponentTable.h/cpp      — name→TypeId+wrap registry for ECS components
    PythonInterpreter.h/cpp     — owns scoped_interpreter, stdout/stderr redirect, bindContext
    Bindings.cpp                — PYBIND11_EMBEDDED_MODULE(yope3d) entry point
    bindings_math.cpp           — Vec2/Vec3/Vec4/Quat + math utilities
    bindings_ecs.cpp            — Entity, component classes, yope3d.view(), reg_get/has/valid
    bindings_world.cpp          — World, Camera, Input, AudioSystem, SceneManager, key constants
    PythonScript.h/cpp          — Script subclass; YOPE_REGISTER_SCRIPT(PythonScript)

src/editor/panels/
    SceneScriptPanel.h/cpp      — Run/Revert code editor panel

src/editor/inspectors/
    ScriptComponentInspector.cpp — draws ScriptComponent in inspector

src/debug/
    Console.h/cpp               — extracted from ConsolePanel; now in ENGINE_SOURCES
                                  so runtime Python output goes somewhere

scripts/
    behaviors/__init__.py
    behaviors/sandbox_gallery.py    — LEFT/RIGHT scene switching + SPACE spawn
    behaviors/platformer_logic.py   — WASD/jump/mouse-look/camera-follow
    setup/__init__.py
    setup/sandbox/__init__.py
    setup/sandbox/pyramid.py        — OBB pyramid builder
    setup/sandbox/spring_cloth.py   — 20×20 spring grid (3 variants × 3 shape types)
    setup/platformer/__init__.py
    setup/platformer/build_level.py — 51-platform spiral
```

### Files modified

- `vcpkg.json` — added `pybind11` (resolved to 3.0.1, Python 3.13.2 from miniconda)
- `CMakeLists.txt` — `find_package(pybind11)`, `PYTHON_SOURCES`, `YOPE_PYTHON=1` + `YOPE_SCRIPTS_DIR` on both targets, `pybind11::embed` linked to both
- `src/ecs/Registry.h/cpp` — added `entitiesWith(vector<TypeId>)` (runtime superset query); moved `componentTypes()` out of `#ifdef YOPE_EDITOR`
- `src/Engine.h/cpp` — owns `unique_ptr<PythonInterpreter> python`; init after sceneManager, bindContext after scriptCtx wired, shutdown after sceneManager->shutdown
- `src/world/World.h/cpp` — added `takeScriptSnapshot()` / `restoreScriptSnapshot()` (like snapshotForPlay/restoreFromPlay but does NOT change physics pause state)
- `src/editor/EditorApp.cpp` — registers `SceneScriptPanel`
- `src/editor/inspectors/InspectorRegistry.cpp` — registers `drawScriptComponent`
- `src/editor/panels/ConsolePanel.h/cpp` — now just includes `debug/Console.h`

---

## Architecture

### How PythonScript works

`ScriptComponent.scriptClass = "PythonScript"` — ScriptFactory creates a `PythonScript` C++ instance.

`ScriptComponent.paramsBlob` = JSON string, e.g.:
```json
{"module": "behaviors.sandbox_gallery", "class": "SandboxGallery", "scenes": [...]}
```

`PythonScript::deserializeParams` reads `module` and `class` from the blob.

`PythonScript::init(ctx, self)`:
1. `py::module_::import(module_)` → imports the Python module
2. `mod.attr(class_)()` → instantiates the class
3. Reads `paramsBlob` from the entity's own ScriptComponent
4. Parses blob via `json.loads(blob)` → Python dict
5. Calls `py_instance.init(world, entity, params_dict)`

`PythonScript::update(ctx, self, dt)` → `py_instance.update(world, entity, dt)`

Every call is in `try/catch(py::error_already_set)` → `Console::log(..., Error)`. A bad script never crashes the engine.

### Python module layout

`yope3d.*` module-level singletons (set by `PythonInterpreter::bindContext`):
- `yope3d.world` — bound `World*`
- `yope3d.camera` — bound `Camera*`
- `yope3d.input` — bound `Input*`
- `yope3d.audio` — bound `AudioSystem*`
- `yope3d.scene_manager` — bound `SceneManager*`

Convenience function: `yope3d.load_scene(path)` → `scene_manager.queueLoad(path)`

Key constants: `yope3d.KEY_W`, `KEY_A`, `KEY_S`, `KEY_D`, `KEY_SPACE`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_UP`, `KEY_DOWN`, `KEY_F`, `KEY_R`, `KEY_ESCAPE`, `KEY_LEFT_SHIFT`, `KEY_LEFT_CONTROL`

### ECS view from Python

```python
for (entity, hull, tf) in yope3d.view("Hull", "Transform"):
    tf.position.y += 0.1
```

Component proxies are `return_value_policy::reference` — they point directly into the archetype storage. **Do not cache a proxy across a structural change** (add/remove component, create/destroy entity) — the entity migrates to a new archetype and the pointer dangles.

Individual access: `yope3d.reg_get(entity, "Transform")`, `yope3d.reg_has(entity, "Hull")`, `yope3d.reg_valid(entity)`

### sys.path

`YOPE_SCRIPTS_DIR` (`<repo>/scripts/`) is inserted at index 0. Import paths are relative to `scripts/`:
- `import behaviors.sandbox_gallery` → `scripts/behaviors/sandbox_gallery.py`
- `import setup.sandbox.pyramid` → `scripts/setup/sandbox/pyramid.py`

### stdout/stderr redirect

Python `print()` and exceptions go to `Console::log()` (Info/Error severity). In the runtime build, `Console::log` also echoes to stderr. In the editor, they appear in the Console panel.

---

## Bound component types

These are usable in `yope3d.view()` and `yope3d.reg_get()`:

| Python name | C++ type | Key fields exposed |
|---|---|---|
| `"Transform"` | `Transform` | `position` (Vec3), `rotation` (Quat), `scale` (Vec3) |
| `"Hull"` | `ecs::Hull` | `velocity`, `omega`, `mass`, `linear_damping`, `angular_damping`, `friction`, `restitution`, `gravity`, `tangible` |
| `"SphereForm"` | `ecs::SphereForm` | `radius` |
| `"AABBForm"` | `ecs::AABBForm` | `extent` (Vec3) |
| `"OBBForm"` | `ecs::OBBForm` | `extent` (Vec3) |
| `"LightSource"` | `ecs::LightSource` | `type`, `intensity`, `color`, `position`, `direction` (all via property) |
| `"Name"` | `ecs::Name` | `value` (str) |
| `"SpringConstraint"` | `ecs::SpringConstraint` | `target` (Entity), `k`, `rest_length` |
| `"ScriptComponent"` | `ecs::ScriptComponent` | `script_class`, `params_blob` |

**Not bound:** `MeshRenderer` (holds `RenderMesh*`), `AudioSource` (holds `Source*`), `Fixed` (zero-size tag — use `world.fix_entity(e)` instead). `Sleeping` no longer exists: sleep state is the `Hull.asleep` field, reachable through the bound `Hull`.

---

## World methods exposed to Python

```python
world.add_sphere(mass, radius, pos)       → Entity
world.add_obb(extent, mass, pos)          → Entity
world.add_aabb(extent, mass, pos)         → Entity
world.add_static_aabb(pos, extent)        → Entity
world.remove_entity(entity)
world.reset_physics()
world.add_spring(a, b, k, rest)           → Spring* (opaque)
world.add_spring_with_proxies(a, b, k, rest, proxy_count, proxy_radius)
world.attach_sphere_mesh(entity, radius, r=1, g=1, b=1)
world.attach_box_mesh(entity, half_vec3, r=1, g=1, b=1)
world.fix_entity(entity)                  # sets inverseMass=0, adds Fixed tag
world.set_mesh_color(entity, r, g, b)
world.get_registry()                      → Registry (opaque, use yope3d.view/reg_get instead)
world.gravity                             # Vec3, read/write
world.get_hull_count()                    → int
world.get_island_count()                  → int
```

---

## Scene Script panel

Located in `src/editor/panels/SceneScriptPanel.cpp`.

**Run:** calls `world.takeScriptSnapshot()` (first time only, does NOT unpause physics), then `python->execString(code)`. New entities appear in Hierarchy immediately.

**Revert:** calls `world.restoreScriptSnapshot()`. Destroys GPU resources for script-created meshes, restores the registry snapshot, rebuilds `meshToEntity_`. No physics pause state change.

**Important:** the snapshot slot is SHARED with Play/Stop (`snapshotForPlay`/`restoreFromPlay`). If you Run a script then press Play, Play's snapshot overwrites the script's snapshot. The Revert button tracks `snapshotTaken_` locally but the underlying `prePlayMeshPoolSize_` and `playSnapshot_` are global to World. Don't mix script revert and Play in the same editing session without saving first.

---

## Known issues / things not yet tested

1. **PythonScript entities in scenes** — no scene JSON with `scriptClass=PythonScript` has been tested in Play mode. The serializer path through `ScriptComponent` is wired, but the round-trip (save → load → Play → init called) is unverified.

2. **`yope3d.view()` while physics is running** — safe in PythonScript::update (main thread, between physics ticks, inside `lockStructure()` via Engine::update). Safe in Scene Script panel (physics paused). NOT safe to call from a background thread.

3. **Component proxy lifetime** — proxies returned from `yope3d.view()` / `yope3d.reg_get()` are raw C++ references. Any `add`/`remove`/`create`/`destroy` call migrates archetypes and invalidates them. Current binding does not enforce this; undefined behavior if cached.

4. **`yope3d.load_scene()` during setup scripts** — the Scene Script panel runs synchronously. Calling `yope3d.load_scene()` queues a scene load but the flush happens at the next frame boundary (in `Engine::update`). So setup scripts should NOT call `load_scene` — only behavior scripts should.

5. **Hot reload** — `PythonInterpreter::reloadModule()` is implemented but nothing calls it automatically. The FileWatcher on `assets/` does NOT watch `scripts/`. Hot reload must be triggered manually (not wired to any UI button yet).

6. **Sandbox scene JSON files** — the setup scripts exist but none have been run yet. `assets/scenes/sandbox/` doesn't exist. Run each script in the Scene Script panel and Save Scene to create the JSON files.

7. **Platformer scene** — same as above. `build_level.py` builds the spiral but the player entity has no `Name` component assigned via Python (commented out because the ECS add for Name isn't bound). The `platformer_logic.py` script finds the player via the entity handle passed to `init(world, entity, params)`.

8. **`hull.inverseMass`** — not exposed in the Hull binding. `fix_entity()` handles zeroing it. If scripts need to read `inverseMass`, it needs to be added to the binding.

9. **assetManager removed from bindContext** — `yope3d.assets` is `None`. If future scripts need to load sounds/textures, `AssetManager` needs a binding.

---

## Debugging runbook

### Python error shows in Console but wrong format
Check `PyConsoleStream::write()` in `PythonInterpreter.cpp` — it flushes on `\n`. Multi-line tracebacks may appear as multiple Console entries.

### "module not found" on import
- Check `YOPE_SCRIPTS_DIR` is correct (printed at cmake configure time as a define, visible in build flags)
- Check `scripts/behaviors/__init__.py` and `scripts/setup/__init__.py` exist
- Run `python3 -c "import sys; sys.path.insert(0, '<scripts_dir>'); import behaviors.hello"` to test outside the engine

### Crash on script run (not revert)
- Build with ASAN: add `-DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"` to cmake preset, rebuild
- Run: `ASAN_OPTIONS=detect_leaks=0 ./build/mac-debug/yope_editor`
- Most likely cause: component proxy cached across an archetype migration

### Crash on revert (should be fixed, but if it recurs)
- Confirm `SceneScriptPanel` uses `takeScriptSnapshot()`/`restoreScriptSnapshot()`, NOT `snapshotForPlay()`/`restoreFromPlay()`
- Confirm `gpu_` is non-null in `World` at revert time (it's set in `World::init`)
- Check `RenderMesh::destroy()` nulls out handles after freeing (to prevent double-free in destructor)

### PythonScript not calling init
- Confirm `scriptClass` is exactly `"PythonScript"` (case-sensitive)
- Confirm `paramsBlob` is valid JSON with `"module"` and `"class"` keys
- In runtime: `PythonScript::deserializeParams` is called before `init`; if it returns false (empty module/class), init is skipped with a Warning log
- Press Play in editor — in edit mode, `init()` is NOT called (that's intentional: editor instantiates scripts only on Play)

### `yope3d.world` is None in a script
- `bindContext` is called after `scriptCtx_` is fully wired in `Engine::init`
- If a script runs before the startup scene loads, world is bound but may have no entities yet — that's fine
- If Python interpreter fails to init (check for errors at startup), `initialized_` is false and `bindContext` no-ops
