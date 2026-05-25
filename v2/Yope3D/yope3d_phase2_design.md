# Yope3D Phase 2 Design Document

**Status:** Draft v1
**Date:** May 2026
**Scope:** Architectural plan for Phase 2, with ECS as the foundation and the editor as the primary deliverable.

---

## 1. Purpose

Phase 1 produced a working engine — Vulkan rendering, custom physics, OpenAL audio, UI, compute raytracing — but built on OOP structures (`vector<unique_ptr<SceneObject>>`, `vector<Hull*>`, virtual collision dispatch) that won't scale to the 10–12k entity stress targets and won't support the editor's introspection cleanly. Phase 2 fixes the foundation, then builds the long-deferred editor on top of it.

The original roadmap had Editor (M12) before ECS (M13). This is inverted: **ECS → Editor → Python**. The editor's panels map directly onto ECS queries; building them against the existing `SceneObject` model would mean rewriting them weeks later.

This document is not a tutorial. It assumes familiarity with the Phase 1 codebase, the ECS pattern, and the conversation that produced these decisions.

---

## 2. Locked-in decisions

From the design Q&A, the following are fixed:

| Decision | Choice |
|----------|--------|
| Phase 2 milestone order | ECS → Editor → Python → Material → Animation → Physics expansion → … |
| ECS shape | Archetype-based |
| Component layout | AoS by default |
| Editor UI | Dear ImGui + ImGuizmo, heavily themed |
| Scene serialization | Full (JSON-based) |
| Play-mode preservation | Snapshot/restore (single shot) |
| Undo/redo | Command pattern, built in from day one |
| Asset hot-reload | Filesystem watch + manual reload button |
| Viewport picking | ID buffer (separate render pass) |
| Inspector reflection | Hand-written per-component drawer functions |
| Editor packaging | Separate CMake target, compile-time gated |
| Editor scene comfort target | 2–3k entities; up to 10–12k in stress tests |

---

## 3. Architectural foundations

### 3.1 The DOD rule for Phase 2

**Per-frame iteration over heterogeneous heap-allocated objects through virtual dispatch is banned.** Specifically:

- No `vector<BasePtr*>` for systems that iterate every frame.
- No virtual methods on data that's hot-iterated.
- Type variation expressed via type-segregated storage or tagged unions, not via inheritance.

Exceptions: `Script::update()` (called once per frame total) stays virtual. UI labels (low count, low frequency) stay virtual. The rule is about hot loops, not all polymorphism.

### 3.2 ECS shape: archetype, AoS

**Archetype-based registry.** An archetype is a unique set of component types. All entities with components `{Transform, RigidBody, SphereCollider}` share one archetype block. Iterating a query `(Transform, RigidBody, SphereCollider)` walks the archetype's three parallel arrays contiguously — no pointer chasing, no virtual dispatch, no random access.

Add/remove component migrates the entity to a different archetype (copy out of old arrays, copy into new arrays). At 10–12k entities this is fast; cost only matters if structural changes happen many times per frame, which they don't in practice.

**AoS by default.** `vector<Transform>` rather than three parallel arrays of `Vec3`/`Quat`/`Vec3`. Systems read whole components together more often than they read single fields together. SoA is reserved for components where profiling proves a system reads exactly one field across the array (e.g., a position-only culling pass on 10k entities might justify a separate `vector<Vec3>` of positions, but this is post-profiling, not upfront).

### 3.3 Editor / runtime separation

Two CMake targets share the engine source:

- `yope_runtime` — the standalone game runtime. No ImGui, no editor panels, no ID buffer pass. This is what ships to players.
- `yope_editor` — the editor build. Includes runtime + `editor/` directory + ImGui + ImGuizmo. Defines `YOPE_EDITOR`.

`#ifdef YOPE_EDITOR` blocks in engine code are kept to an absolute minimum — basically the render-to-texture viewport path and the ID buffer pass. Editor-specific logic lives entirely in `src/editor/`.

This pairs cleanly with the existing release/debug + embed/filesystem matrix:

| Target | Build | Assets |
|--------|-------|--------|
| `yope_runtime_debug` | debug | filesystem |
| `yope_runtime_release` | release | embedded |
| `yope_editor_debug` | debug | filesystem |
| `yope_editor_release` | debug-info release | filesystem |

The editor never embeds assets — it always reads from the filesystem so the asset browser is meaningful.

---

## 4. Revised milestone order

| Old # | New # | Milestone | Estimated hours |
|-------|-------|-----------|-----------------|
| 13 | 12 | **ECS Architecture** | 80–100 |
| 12 | 13 | **Editor Window** | 90–110 |
| 14 | 14 | Python Scripting | 50–60 |
| 15 | 15 | Material + Skybox | 30–35 (now lands as components) |
| 16 | 16 | Skeletal Animation | 70 |
| 17 | 17 | Physics Expansion | 100 |
| 18 | 18 | UI Expansion | 25 |
| 19 | 19 | Quality + Docs | ongoing |

ECS work grew from the roadmap's ~35h sketch to 80–100h because it's now a real migration of every system, not a thin wrapper around the existing `World`. Editor grew from ~65h to ~90–110h because handwritten inspectors, serialization, the command system, the ID buffer, and the filesystem watcher were either missing or undersized in the original plan. The investment compounds: Python, Material, Animation, and Physics Expansion are all easier once ECS lands.

---

## 5. Milestone 12 — ECS Architecture

### 5.1 Goal

Replace `World`'s `vector<unique_ptr<SceneObject>>` + flat hull/mesh caches with an archetype-based component registry. Every per-frame system (physics, rendering, audio, UI sync) iterates components directly from the registry. SceneObject ceases to exist as a class.

### 5.2 Registry types

```cpp
namespace ecs {

using EntityId    = uint32_t;
using TypeId      = uint32_t;   // compile-time stable index per component type
using ArchetypeId = uint32_t;
using Row         = uint32_t;

struct EntityRecord {
    ArchetypeId archetype;
    Row         row;
    uint32_t    generation;   // increments on destroy; detects stale handles
};

struct Entity {
    EntityId   id;
    uint32_t   generation;
    bool valid(const Registry&) const;
};

struct ComponentArray {
    TypeId       type;
    size_t       elementSize;
    size_t       elementAlign;
    void       (*destruct)(void*);
    void       (*moveConstruct)(void* dst, void* src);
    std::vector<std::byte> data;   // raw contiguous; cast to T* at access
};

struct Archetype {
    ArchetypeId             id;
    std::vector<TypeId>     types;     // sorted ascending; used as archetype key
    std::vector<ComponentArray> cols;  // parallel to types
    std::vector<Entity>     entities;  // entity at each row

    size_t size() const { return entities.size(); }
    int    colIndex(TypeId t) const;   // returns -1 if absent
};

class Registry {
public:
    Entity create();
    void   destroy(Entity);

    template<class T> T& add(Entity, T value);
    template<class T> void remove(Entity);
    template<class T> T*   get(Entity);
    template<class T> bool has(Entity) const;

    template<class... Ts>
    View<Ts...> view();

private:
    std::vector<Archetype>                   archetypes_;
    std::unordered_map<ArchetypeKey, ArchetypeId> archIndex_;
    std::vector<EntityRecord>                records_;
    std::vector<EntityId>                    freeIds_;
    EntityId                                 nextId_ = 0;
};

}
```

`Entity` is a 64-bit handle (id + generation). Generation prevents stale references after destroy + recycle.

`TypeId` is assigned at compile time per type via a template function returning a static counter. No RTTI, no `typeid`.

The `View<Ts...>` returned by `view()` is an iterable that walks every archetype containing all `Ts`, yielding tuples of references into the parallel arrays. Iteration is a nested loop: outer over matching archetypes, inner over rows within each archetype.

### 5.3 Component types

The initial component set (added in this milestone, used by editor and python in later milestones):

| Component | Purpose | Migrated from |
|-----------|---------|---------------|
| `Transform` | position (Vec3), rotation (Quat), scale (Vec3) | already exists |
| `RigidBody` | velocity, omega, mass, inverseMass, damping, materials, flags | currently embedded in Hull |
| `SphereCollider` | radius | from CSphere |
| `AABBCollider` | extent | from CAABB |
| `OBBCollider` | extent | from COBB |
| `BarrierCollider` | barrier set ref | from BarrierHull |
| `MeshRenderer` | vertex buffer ref, index buffer ref, material ref, draw flag | from RenderMesh |
| `LightSource` | type, color, intensity, range, etc. | from Light |
| `AudioEmitter` | source handle, gain, pitch, looping, etc. | from Source |
| `SpringJoint` | other entity, k, rest | from Spring |
| `Sleeping` | tag component, presence = sleeping | from Hull::sleeping |
| `Fixed` | tag, presence = static body | from Hull::fixed |
| `Name` | string (editor-only field, but stored uniformly) | new |
| `EditorSelectable` | tag, presence = visible in hierarchy | new |
| `EditorPickable` | tag, presence = ID buffer renders this entity | new |

`Sleeping` and `Fixed` as tag components (zero-sized) is important: a system query `(Transform, RigidBody)` without `Sleeping` skips sleeping entities entirely at iteration time, instead of branching `if (hull->sleeping) continue;` inside the loop. This is one of the cleanest archetype wins.

### 5.4 The collision dispatch table

The current virtual visitor pattern (`Hull::detect(Hull&)` → vtable → `CSphere::detectCollision(CSphere&)` → vtable) becomes an explicit 3×3 (or however many shape types) function pointer table:

```cpp
using DetectFn = void(*)(Registry&, Entity a, Entity b, ContactManifold& out);

struct CollisionDispatch {
    // [shapeA_typeId][shapeB_typeId] -> function
    std::array<std::array<DetectFn, MAX_SHAPES>, MAX_SHAPES> detect;
    std::array<std::array<DetectFn, MAX_SHAPES>, MAX_SHAPES> resolve;
};
```

Direct call, no vtable, no double-dispatch ceremony. Same logic as `ColliderDiscrete` and `ColliderCCD` currently, just routed through an explicit table. Each broadphase pair tells you the two shape types (the archetype the entities live in encodes this); look up the function, call it.

### 5.5 BroadphaseSAP refactor

Signature changes from `(vector<Hull*>)` to:

```cpp
void collectPairs(
    std::span<const Vec3>     positions,    // packed from Transform array
    std::span<const Vec3>     extents,      // packed once per frame
    std::span<const uint32_t> layers,       // for filtering
    std::span<const Entity>   entityIds,    // back-references
    std::vector<std::pair<Entity, Entity>>& out);
```

Inputs are built by querying `(Transform, BroadphaseTag)` (where `BroadphaseTag` marks bodies that participate). Internal SAP logic is unchanged — Entry struct, sort, sweep, all already DOD.

### 5.6 Physics thread interface

The physics thread already owns the simulation. ECS doesn't change that — it just changes what the physics thread iterates. The double-buffered transform snapshot stays: physics thread writes `snapshotBack_`, main thread reads `snapshotFront_`. The snapshot format becomes:

```cpp
struct TransformSnapshot {
    Entity     entity;     // for lookup if needed
    Mat4       modelMatrix; // precomputed
    bool       active;     // false = mesh hidden this frame
};
```

The renderer queries `view<MeshRenderer>()` and looks up the snapshot by entity. If the snapshot list and the mesh view are kept in sync order (build snapshot at end of physics tick by iterating the same query), this becomes a parallel scan.

### 5.7 Migration strategy

Phased to keep the engine runnable throughout:

**Phase A: Build the registry.** New `src/ecs/` directory. Registry, Archetype, Entity, View. Unit tests. ~15 hours.

**Phase B: Add components in parallel.** Add `Transform`, `RigidBody`, `SphereCollider` etc. as ECS components, but keep the existing `Hull`/`SceneObject` classes alive. Migrate one factory method at a time. `World::addSphere` creates both a `SceneObject` and an ECS entity until cutover. ~10 hours.

**Phase C: Migrate systems.** PhysicsSystem (iterates ECS instead of `hullCache_`), RenderSystem (iterates ECS instead of `meshCache_`), etc. The legacy `World` becomes a thin wrapper that delegates to the Registry. ~25 hours.

**Phase D: Delete the old code.** Remove `SceneObject`, the Hull hierarchy, the flat caches. `World` either disappears entirely or becomes a thin convenience layer over `Registry` that holds non-entity state (gravity, lights config). ~10 hours.

**Phase E: Profile and optimize.** Stress test at 10–12k entities. Identify hot queries, possibly migrate specific components to SoA if a profile demands it. ~15 hours.

The original Hull hierarchy and SceneObject classes are gone at the end. CSphere, CAABB, COBB, Hull.h, Hull.cpp, SceneObject.h all deleted.

### 5.8 What stays

These Phase 1 components are reused without restructuring:

- `BroadphaseSAP` internals — just input/output signature changes.
- `ContactCache` — still keyed, but on `(Entity, Entity)` pairs instead of `(Hull*, Hull*)`.
- `IslandDetector` — operates on Entity IDs instead of Hull pointers.
- `ColliderDiscrete`, `ColliderCCD` — collision routines themselves don't change, just their inputs.
- `ThreadPool` — unchanged.
- The physics thread loop in `Engine` — unchanged.
- The double-buffered snapshot mechanism — unchanged in shape, just operates on entities.
- All math types, GPU abstraction, audio system, UI system — completely unchanged.

### 5.9 ECS open decisions (deferred)

These can be decided during implementation without affecting upstream design:

- **Archetype chunking.** Single contiguous array per archetype, or chunked into fixed-size blocks? At 10–12k entities, single array is fine. Chunking matters at 100k+ where you want chunk size matched to cache.
- **Query caching.** Should `view<T...>` cache the list of matching archetypes? Yes, but invalidate when a new archetype is created. Implementation detail.
- **Parent/Hierarchy component.** Editor's scene tree benefits from a parent/child relationship between entities. Add a `Parent { Entity parent }` component and a hierarchy system that propagates transforms? Or stay flat? Decision: flat for v1; add hierarchy as a separate component if and when nested objects are needed.

---

## 6. Milestone 13 — Editor Window

The editor is the main visible deliverable of Phase 2. It's where the "engine project" starts to feel like a real tool rather than a hardcoded demo runner.

### 6.1 Build configuration

CMake-level:

```cmake
add_executable(yope_runtime ${ENGINE_SOURCES} src/main_runtime.cpp)

add_executable(yope_editor ${ENGINE_SOURCES} ${EDITOR_SOURCES} src/main_editor.cpp)
target_compile_definitions(yope_editor PRIVATE YOPE_EDITOR=1)
target_link_libraries(yope_editor PRIVATE imgui imguizmo)
```

`ENGINE_SOURCES` is the existing engine code. `EDITOR_SOURCES` is `src/editor/` only. The editor target gets ImGui and ImGuizmo via vcpkg. The runtime target stays minimal.

`#ifdef YOPE_EDITOR` blocks in engine code are limited to:
1. The render-to-texture viewport path (`Renderer::drawFrame` checks a mode bit).
2. The ID buffer render pass (compiled in only for editor).
3. Snapshot/restore hooks in `World::resetPhysics` for the play-stop flow.

Everything else editor-related lives in `src/editor/`.

### 6.2 Render-to-texture viewport

The runtime renders directly to the swapchain. The editor renders the game to an offscreen `VkImage` (color attachment, sampled in fragment shader), which is then displayed as an `ImGui::Image` widget inside the editor's "Viewport" panel.

```
src/rendering/ViewportTarget.{h,cpp}    — offscreen color + depth + framebuffer
src/editor/panels/ViewportPanel.{h,cpp} — owns ViewportTarget, displays it via ImGui
```

`ViewportTarget` exposes:
- `void resize(uint32_t w, uint32_t h)` — recreates image + framebuffer at new size.
- `VkImageView view()` — for sampling.
- `VkDescriptorSet imguiDescriptor()` — for `ImGui::Image`.

The editor calls `renderer->drawFrame(viewportTarget)` instead of `drawFrame(window)`. The editor itself is a second render pass into the swapchain after the game pass has populated the viewport texture.

When the viewport panel is resized by the user (drag the panel edge), `ViewportTarget::resize` is called and the camera's aspect ratio updates. Resize is throttled — only recreated after the user stops dragging for ~100ms — to avoid thrash.

### 6.3 Panel architecture

Each panel is a class with a fixed interface:

```cpp
class EditorPanel {
public:
    virtual ~EditorPanel() = default;
    virtual const char* name() const = 0;
    virtual void draw(EditorContext&) = 0;
    virtual bool wantsKeyboard() const { return false; }
};
```

`EditorContext` is the editor's analog of `ScriptContext`. It carries: `Registry&`, `Engine&` (for camera, renderer, asset access), the current `Selection`, the `CommandHistory`, and the `EditorTheme`.

The panels for v1:

| Panel | Responsibility |
|-------|----------------|
| `ViewportPanel` | Displays the game viewport. Play/stop button. Editor camera input when in edit mode. Gizmo rendering. |
| `HierarchyPanel` | Tree of all entities with `EditorSelectable`. Click to select. Right-click for context menu (delete, duplicate, add component). |
| `InspectorPanel` | Shows components of the selected entity. Per-component drawer functions render the editable fields. |
| `AssetBrowserPanel` | File-system tree of `assets/`. Hot-reload status. Drag-to-target support for material slots. |
| `ConsolePanel` | Captures stdout/stderr. Severity coloring. Filterable. Buffer cap (e.g., 10k lines). |
| `WorldSettingsPanel` | Gravity, fixed timestep, render mode (RASTER/RAYTRACE), light list (add/remove/edit). |
| `StatsPanel` | FPS, frame time graph, entity count, draw call count, physics island count. |

Each panel is owned by `EditorApp`, which is the editor's top-level object. `EditorApp::draw()` iterates panels and calls `panel->draw(ctx)` inside an `ImGui::Begin`/`ImGui::End` per panel.

### 6.4 Command system (undo/redo)

Every editor mutation flows through the command system. No direct registry writes from panel code.

```cpp
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void redo(EditorContext&) = 0;
    virtual void undo(EditorContext&) = 0;
    virtual const char* label() const = 0;   // for the undo-history UI
};

class CommandHistory {
public:
    void execute(EditorContext&, std::unique_ptr<ICommand>);
    void undo(EditorContext&);
    void redo(EditorContext&);
    bool canUndo() const;
    bool canRedo() const;

private:
    std::vector<std::unique_ptr<ICommand>> stack_;
    size_t cursor_ = 0;   // index of next command to redo
};
```

`execute` calls `cmd->redo()` (the initial application), truncates anything past `cursor_` (we just diverged from the redo branch), then pushes. `undo` decrements cursor and calls `stack_[cursor]->undo()`. `redo` calls `stack_[cursor]->redo()` and increments.

Concrete command types (starter set):

```
TranslateEntityCommand    — old pos, new pos, entity
RotateEntityCommand       — old rot, new rot, entity
ScaleEntityCommand        — old scale, new scale, entity
RenameEntityCommand       — old name, new name, entity
CreateEntityCommand       — stored component blob to recreate on redo
DeleteEntityCommand       — stored component blob to restore on undo
AddComponentCommand       — entity, component blob
RemoveComponentCommand    — entity, component blob (for undo)
SetComponentFieldCommand  — entity, typeId, field offset, old value, new value
```

Critical: commands store **Entity (id + generation)**, never raw pointers. The command system survives entity destroy/recycle because stale handles are detected via generation mismatch.

Gizmo drag operations: while the user is dragging, no command is pushed — the registry is mutated live so the user sees feedback. When the user releases the mouse, a single command capturing the (start, end) state is pushed. This means undo undoes the whole drag, not every intermediate frame.

### 6.5 Selection and picking

`Selection` is owned by `EditorContext`:

```cpp
class Selection {
public:
    void set(Entity);
    void add(Entity);             // multi-select via shift/ctrl
    void clear();
    std::span<const Entity> get() const;
    Entity primary() const;       // first selected, for inspector display
};
```

Two ways to select an entity:

**Hierarchy click.** Trivial — the hierarchy panel knows which entity each tree row is and calls `selection.set(entity)`.

**Viewport click (ID buffer).** A separate render pass runs each frame (editor-only) into a `VK_FORMAT_R32_UINT` color attachment matching the viewport size. Each entity with `EditorPickable` renders its mesh with its `Entity.id` as the "color." On viewport click, the editor reads the pixel at the click coordinate via a staging buffer and resolves it to an entity.

Implementation notes:
- The ID buffer pass shares the depth buffer with the main game pass (occluded geometry doesn't get picked).
- ID write is per-fragment; the fragment shader writes `out_id = pushConstant.entityId`.
- Reading the pixel back requires an image-to-buffer copy + map. To avoid GPU stall, the readback uses the previous frame's ID buffer (1-frame latency on selection is fine — it's not a gameplay-critical input).
- For 2k–3k entities, the ID buffer adds ~1ms per frame. Acceptable; the editor is allowed to be heavier than runtime.

Decorative meshes without colliders are still selectable because picking is render-based, not physics-based. ✓ as required.

### 6.6 Scene serialization

JSON format. Each scene file is:

```json
{
    "version": 1,
    "scene_name": "demo_cloth",
    "settings": {
        "gravity": [0, -9.81, 0],
        "render_mode": "RASTER"
    },
    "entities": [
        {
            "id": 17,
            "components": {
                "Transform":    { "position": [0,5,0], "rotation": [0,0,0,1], "scale": [1,1,1] },
                "RigidBody":    { "mass": 1.0, "velocity": [0,0,0], "damping": 0.01, ... },
                "SphereCollider": { "radius": 0.5 },
                "MeshRenderer": { "mesh": "primitives/sphere.obj", "material": "materials/red.mtl" },
                "Name":         { "value": "Ball 1" }
            }
        }
    ]
}
```

Per-component serialize/deserialize functions, hand-written, registered in a static table:

```cpp
struct ComponentSerializer {
    TypeId type;
    const char* name;
    void (*serialize)(const void* component, Json& out);
    void (*deserialize)(const Json& in, void* component);
};

extern std::vector<ComponentSerializer> g_serializers;
```

`YOPE_REGISTER_COMPONENT(Transform)` macro registers the type at static init. Each component's `Transform.h` declares its serializer functions.

Asset references serialize as paths relative to the assets directory (e.g., `"primitives/sphere.obj"`). On load, the `AssetManager` resolves the path. If the asset is missing, the entity is loaded with a placeholder mesh + a warning logged to the console.

Entity IDs are *renumbered* on save to be contiguous 0..N, decoupled from the runtime ID generator. On load, fresh IDs are allocated and the file's IDs are remapped (relevant for SpringJoint references between entities).

A scene file replaces the script's job of constructing the scene. The editor saves; the runtime loads. Scripts are downgraded to defining *behavior*, not *scene contents*. (A scene file can reference a behavior script that gets attached to the world on load.)

### 6.7 Asset browser + filesystem watcher

The asset browser displays `assets/` as a tree, with file type icons and a thumbnail for textures. On click, asset preview. On drag, the asset becomes droppable onto inspector slots (e.g., drag a `.obj` onto an entity's MeshRenderer slot).

The filesystem watcher is a separate cross-platform abstraction:

```
src/platform/FileWatcher.{h,cpp}
    macOS: FSEvents
    Windows: ReadDirectoryChangesW
    Linux: inotify (deferred — not a development platform yet)
```

`FileWatcher::watch(const fs::path&, callback)`. The editor watches `assets/` recursively. When a file changes, the asset manager is notified; if the asset is loaded, it's reloaded; if a MeshRenderer references it, the GPU buffer is rebuilt.

Manual reload remains as a button in the asset browser per the Q7 answer — when the watcher's auto-detection is suspect, the developer can force-reload.

### 6.8 Theming

The "looks generic with ImGui" concern is addressed entirely through styling, not by replacing ImGui. The editor at startup loads:

- **A custom font.** Pick one. Inter or IBM Plex Sans for UI text; JetBrains Mono for the console. Both free, distinctive. Loaded via `ImGui::GetIO().Fonts->AddFontFromFileTTF`.
- **A custom color palette.** Defined in `src/editor/EditorTheme.cpp`. Apply via `ImGui::GetStyle().Colors[...]`.
- **Custom widget styling.** Frame rounding, window rounding, padding, item spacing. Tighter than ImGui defaults.
- **Branded panels.** A status bar at the bottom showing engine version, current scene file, FPS. A custom title bar. A logo somewhere.

Implementation effort: ~5 hours total. Outcome: distinct enough that no one looking at the editor identifies it as a default ImGui application.

The ImGui docking branch is included as a dependency for panel docking/tabs/splits.

### 6.9 Play mode + snapshot

Two states: `EditMode` and `PlayMode`.

- **EditMode:** Time is paused. Physics doesn't advance. Editor camera is active. The script's `update()` does *not* run. Editor mutations (move, edit fields) apply immediately and pass through the command system.
- **PlayMode:** Time advances. Physics runs. Script `update()` runs. Game camera (driven by script) is active. Editor mutations are blocked or limited (debate later — start with blocked).

Transition `Edit → Play` (Play button pressed):
1. Take a snapshot of the entire registry into a `RegistrySnapshot`.
2. Take a snapshot of `World`-level state (gravity, render mode, light config).
3. Capture the editor camera's transform separately (we'll restore on stop).
4. Switch the active camera to the script-driven game camera.
5. Run the script's `init()` (the demo's setup code) — note: in PlayMode the script can mutate the registry freely.
6. Begin physics ticking.

Transition `Play → Edit` (Stop button pressed):
1. Halt physics.
2. Restore the registry from the snapshot.
3. Restore world-level state.
4. Restore the editor camera transform.

The registry snapshot is a memcpy of every archetype's component arrays (and the entity record table). At 10–12k entities with ~150 bytes per entity in components, this is ~1.5MB per snapshot. Memcpy is fast; no concern.

GPU resources (vertex buffers, textures, descriptor sets) are *not* snapshotted. They survive across play/stop unchanged. The snapshot only captures gameplay state.

Audio sources mid-playback are stopped on transition. Listener position resets to whatever the editor camera was at.

### 6.10 ImGuizmo integration

ImGuizmo is a small library that draws translate/rotate/scale gizmos overlaid on the viewport. Integration:

1. Set ImGuizmo's view+projection matrices from the editor camera each frame.
2. After drawing the viewport image, in the same ImGui frame, call `ImGuizmo::Manipulate` with the selected entity's transform matrix.
3. The function returns true if the user is dragging; if so, decompose the modified matrix back into position/rotation/scale and apply to the selected entity's `Transform` component directly (no command yet — drag is in progress).
4. On mouse release, push a `TranslateEntityCommand` (or rotate/scale) with the captured start/end state.

Gizmo mode (translate/rotate/scale) is chosen via keyboard shortcuts (Q/W/E/R) — standard convention.

### 6.11 Editor source structure

```
src/editor/
    EditorApp.{h,cpp}          — top-level editor object, owns panels
    EditorContext.h            — passed to panels, contains Registry&, etc.
    EditorTheme.{h,cpp}        — fonts, colors, style application
    Selection.{h,cpp}
    CommandHistory.{h,cpp}
    commands/
        TransformCommand.{h,cpp}
        EntityLifecycleCommand.{h,cpp}
        ComponentCommand.{h,cpp}
    panels/
        ViewportPanel.{h,cpp}
        HierarchyPanel.{h,cpp}
        InspectorPanel.{h,cpp}
        AssetBrowserPanel.{h,cpp}
        ConsolePanel.{h,cpp}
        WorldSettingsPanel.{h,cpp}
        StatsPanel.{h,cpp}
    inspectors/
        TransformInspector.cpp     — drawComponent(Transform&)
        RigidBodyInspector.cpp
        MeshRendererInspector.cpp
        ... (one per component type)
    serialization/
        SceneSerializer.{h,cpp}
        ComponentSerializers.{h,cpp}  — registration of per-component fns
    picking/
        IdBufferPass.{h,cpp}
src/platform/
    FileWatcher.{h,cpp}        — outside editor since it's reusable
src/main_editor.cpp            — entry point: constructs Engine + EditorApp
```

### 6.12 Editor effort breakdown

| Sub-task | Hours |
|----------|-------|
| CMake split + editor target + ImGui/ImGuizmo integration | 6 |
| ViewportTarget render-to-texture | 8 |
| EditorApp + panel framework + theme | 8 |
| HierarchyPanel + Selection | 8 |
| InspectorPanel + first 3 inspectors (Transform, RigidBody, MeshRenderer) | 10 |
| Remaining inspectors (~8 more) | 12 |
| AssetBrowserPanel | 8 |
| ConsolePanel + stdout/stderr redirect | 4 |
| WorldSettingsPanel + StatsPanel | 4 |
| CommandHistory + base commands | 6 |
| Per-command implementations | 8 |
| Scene serialization (format + per-component serializers) | 10 |
| ID buffer pass + viewport picking | 8 |
| ImGuizmo integration | 4 |
| Play-mode snapshot/restore | 8 |
| FileWatcher (mac + win) + asset hot-reload | 8 |
| Polish, bugs, theme refinement | 10 |
| **Total** | **~130 hours** |

That's higher than my earlier 90–110 estimate; this is the honest breakdown. ~16 weeks at 8 hrs/week, ~10 weeks at 13 hrs/week.

---

## 7. Milestone 14 — Python Scripting

With ECS and the editor in place, Python scripting becomes:

### 7.1 ECS bindings

pybind11 binds `Registry`, `Entity`, component types, and view iteration:

```python
import yope

# Iterate all entities with Transform and RigidBody
for entity, transform, rb in yope.world.view(yope.Transform, yope.RigidBody):
    transform.position.y += 0.1
    rb.velocity = yope.Vec3(0, 0, 0)
```

Archetype iteration is exposed as a Python iterator that, under the hood, walks the matching archetypes. The Python iterator yields tuples; component references are returned as proxy objects that read/write the underlying memory directly (no copy).

### 7.2 Behavior scripts (not scene scripts)

A scene file can reference a behavior script:

```json
{ "scene_name": "platformer", "behaviors": ["platformer_logic.py"] }
```

The script defines `init(world)` (called when the scene loads) and `update(world, dt)` (called each frame). No more "the script constructs the scene" — the editor builds the scene, the script defines its behavior.

A script can also attach a per-entity callback:

```python
yope.world.on_update(entity, lambda dt: ...)
```

This is what enables the "clock-hand tick" pattern.

### 7.3 Editor integration

The editor's script dropdown reads from `scripts/`. Scripts are hot-reloadable via `importlib.reload`. The editor's "Reload Script" button triggers this manually; the filesystem watcher does it automatically on save.

Errors are caught — a script raising an exception prints to the editor console; the engine doesn't crash.

### 7.4 Effort

~50–60 hours, roughly as the original roadmap estimated. Archetype binding is slightly more work than sparse-set would have been (~5 extra hours), but the rest is unchanged.

---

## 8. Implications for later milestones

**Material system (M15):** `Material` becomes a component (or a reference held by `MeshRenderer`). Material parameter editing happens through the inspector. The roadmap's material work proceeds as written, just landing on ECS.

**Skeletal animation (M16):** `Skeleton`, `AnimationClip`, `Animator` all become components. Animation system iterates `(Animator, Skeleton)`. GPU skinning compute shader unchanged.

**Physics expansion (M17):** GJK/EPA implementations take entity-shape pairs as input. Dynamic BVH stores entity IDs as leaf data. Easier than the original plan because the input is already structured.

**Raytracing milestones (M20–22):** Hardware RT TLAS rebuild iterates `(Transform, MeshRenderer, RaytraceEnabled)`. The TLAS instance buffer is built directly from the component arrays — possibly the cleanest data-shape match in the entire engine.

---

## 9. Cross-cutting concerns

### 9.1 Thread safety

The physics thread owns the registry during simulation. The editor mutates the registry from the main thread, but only when physics is paused (`EditMode`). In `PlayMode`, the editor's mutations are blocked — only the script (running on the main thread, between physics ticks) can mutate. This avoids any locking on the registry hot path.

Snapshot publication remains as in Phase 1: physics thread writes `snapshotBack_`, swaps under mutex, main thread reads `snapshotFront_`.

### 9.2 Testing

Catch2 test suites for:

- ECS registry: create/destroy, add/remove/get, archetype migration, view iteration correctness, entity recycling, generation checks.
- Collision dispatch table: each function called for each pair type.
- Command system: do/undo idempotence, history truncation on diverge.
- Scene serialization: round-trip equivalence (save → load → save → identical).
- Snapshot/restore: equality after restore.

Tests are part of the runtime build, not editor-only.

### 9.3 What we are not doing in Phase 2

To prevent scope creep, the following are deferred:

- Multi-scene loading / scene streaming.
- Prefab / template system.
- Live edit during play.
- Scriptable rendering pipeline.
- Editor scripting (writing editor tools in Python).
- Component reflection via macros.
- Asset UUIDs (paths suffice until renames become a problem).

---

## 10. Sequencing summary

| Phase 2 weeks (cumulative) | Activity |
|---|---|
| 1–4 | ECS Phase A–B: registry + parallel component shadow |
| 5–9 | ECS Phase C: system migration |
| 10–11 | ECS Phase D–E: cleanup + profiling |
| 12–14 | Editor: build split + viewport + panel framework |
| 15–18 | Editor: panels + inspectors + commands |
| 19–21 | Editor: serialization + picking + gizmo |
| 22–24 | Editor: file watcher + play mode + polish |
| 25–29 | Python scripting integration |
| 30+ | Material, animation, physics expansion |

Total ECS + Editor + Python: roughly 30 weeks at 8 hrs/week, 20 weeks at 12 hrs/week. The investment is front-loaded; once the foundation lands, every later milestone proceeds against a clean, fast, introspectable substrate.

---

## 11. Open questions

These haven't blocked the design but should be resolved during implementation:

1. **Spring rendering.** Springs currently have an optional procedural mesh. As a component, where does that mesh live — alongside `SpringJoint`, or as a separate `SpringMesh` component?
2. **Barrier representation.** Static barriers (currently `Barrier` + `BoundedBarrier` in a variant) — keep as world-level data, or migrate to a `BarrierTag` + collider component?
3. **Light max count vs per-archetype storage.** Lights are currently capped at 64 and packed into an SSBO. As components, do they still pack into a single SSBO each frame from a view query, or do they get their own dedicated storage outside the registry?
4. **Editor input routing.** When the viewport panel has focus, WASD goes to the editor camera; when the scene tree has focus, WASD might mean something else (e.g., focus navigation). ImGui's IO routing handles most of this but some game-specific rules need to be defined.
5. **Asset thumbnail generation.** For mesh thumbnails in the asset browser, render the mesh into a small offscreen target. Reuse the main rendering pipeline or a simplified one?

---

*End of document.*
