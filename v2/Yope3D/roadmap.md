# Yope3D C++ Refactor — Master Roadmap

> **Development Model:** Solo, AI-assisted. Starting at ~5–6 hrs/week, ramping to 10–15+ hrs/week.
> Time estimates assume ~8 hrs/week average effective output across Phase 1 (accounting for ramp), and ~12 hrs/week for Phase 2. AI tooling is assumed to accelerate boilerplate by ~1.5–2×, but architectural decisions, Vulkan debugging, and physics math remain at human pace.

---

## Pre-Code Architecture Decisions

These must be decided and documented before any code is written. They constrain every downstream choice.

### Decision 1 — Build System: CMake 3.25+ with vcpkg

**CMake** is the only realistic cross-platform choice for a Mac+Windows C++20 project in 2024. Use **CMake Presets** (`CMakePresets.json`) to define Mac-Debug, Mac-Release, Windows-Debug, Windows-Release configs from day one — not as an afterthought. Use **vcpkg** (in manifest mode, `vcpkg.json` at repo root) for all external dependencies. This gives you a reproducible, one-command setup on both machines.

External packages managed by vcpkg: `glfw3`, `freetype`, `openal-soft`, `stb` (as a port), `python3` (for Phase 2 scripting). *Everything rendering and physics is written from scratch per your stated intent.*

All shaders are GLSL compiled to SPIR-V at **build time** via a CMake custom target invoking `glslc` (bundled in the Vulkan SDK). Compiled `.spv` files live in a `compiled_shaders/` directory that is gitignored.

### Decision 2 — Graphics API: Vulkan via MoltenVK on Mac

Vulkan is the correct long-term choice. The compute shader compute physics work you want requires it on Mac (OpenGL is frozen at 4.1 on Mac, no compute). On Mac, Vulkan runs via **MoltenVK**, which is included in the LunarG Vulkan SDK for Mac — it translates Vulkan calls to Metal at runtime. You do not write any Metal code. The behavior from your perspective is identical to Windows Vulkan.

**Critical setup note:** On Mac, the Vulkan SDK installs to a versioned path. Your CMake must locate it via the `VULKAN_SDK` environment variable set by `setup-env.sh` in the SDK, or via the `Vulkan::Vulkan` CMake find-package target. Add this to your shell profile as part of day-one setup.

Do **not** expose raw `VkDevice`, `VkPipeline`, `VkCommandBuffer` everywhere. Establish a thin abstraction layer (`GpuDevice`, `Pipeline`, `RenderPass`, `CommandBuffer`) in the first Vulkan milestone that hides the ceremony. The rest of the engine talks to that layer, never directly to Vulkan.

### Decision 3 — Math Library: Write a Thin One

Do not import GLM. Write `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`, `Quat` as header-only C++ structs in a `math/` module. This is ~600 lines total, will teach you operator overloading and template basics in C++, and gives you full control. Use GLM's source as reference for the formulas. Define `YOPE_MATH_IMPL` in exactly one translation unit if you want to separate declarations from definitions.

### Decision 4 — Engine Context: No Static Singleton

The `Launch` Java class (with its public static `window`, `world`, `renderer`, `game` fields) was a global mutable state grab-bag. In C++, replace it with an **`Engine` context struct** that is constructed in `main()` and passed by pointer or const-ref to subsystems. Scripts receive a `ScriptContext&` that exposes only the subset they need. This is the single most impactful structural improvement from the old codebase.

```
Engine
├── Window          (GLFW handle, input state)
├── GpuDevice       (Vulkan device/queue/swapchain)
├── Renderer        (pipelines, descriptor sets)
├── World           (scene graph / ECS registry in Phase 2)
├── AudioSystem     (OpenAL context, source/listener management)
└── AssetManager    (registry: meshes, textures, sounds — deduplication)
```

### Decision 5 — Component Separation

The Java `Mesh` class held rendering data, physics state, transform, scale, and color all in one object because `Hull` was bolted on late. In C++, separate these from day one:

- **`Transform`**: position (Vec3), rotation (Quat), scale (Vec3). Shared source of truth.
- **`RenderMesh`**: VAO/VBO equivalent (Vulkan vertex/index buffers), material reference, draw flag.
- **`Collider`**: collision hull type and parameters. References `Transform`.
- **`RigidBody`**: velocity, angular velocity, mass, inertia tensor. References `Transform` and `Collider`.

In Phase 1, these are separate structs held by a `SceneObject` container. In Phase 2, they become ECS components.

### Decision 6 — Asset Packaging: Dual Mode

**Small demos/single-file distribution:** A CMake custom target runs a Python script at build time that reads all assets in `assets/` and generates a `generated/embedded_assets.cpp` file containing `const unsigned char asset_<name>[] = {...}` arrays. The `AssetManager` checks this registry first before touching the filesystem. Result: a single statically-linked binary with no external dependencies.

**Larger projects/development mode:** Assets are loaded from a relative `assets/` directory alongside the binary. The `AssetManager` is toggled via a compile-time define `YOPE_EMBED_ASSETS`. Debug builds always use filesystem mode (faster iteration). Release builds of demos use embed mode.

### Decision 7 — Hot Reload Scope (Phase 1 Deferral)

Full C++ hot reload (DLL swapping) is complex and error-prone to implement before the architecture is stable. In Phase 1, accept a full recompile-and-relaunch cycle. In Phase 2, hot reload is achieved via **Python script reloading** (`importlib.reload`), which covers the highest-frequency editing use case (tweaking game logic). The editor window in Phase 2 further reduces the pain of relaunching.

---

## Phase 1: Foundation & Feature Parity

**Goal:** A C++ engine that can run ports of `SpringDemo` and `Platformer` with all current functionality, on both Mac and Windows, from a single repo build command.

**Estimated Duration:** ~26 weeks at the stated hours cadence.

---

### Milestone 1 — Project Infrastructure
**Weeks 1–2 | ~12 hrs**

This milestone has no visible output but is the foundation of everything. Rushing it causes pain for the entire project.

**Build System:**
- [ ] Initialize git repo with `.gitignore` (build dirs, `.spv` files, vcpkg cache, IDE project files)
- [ ] Write root `CMakeLists.txt` with `project(Yope3D VERSION 0.1.0 LANGUAGES CXX)`
- [ ] Set C++20 standard globally: `set(CMAKE_CXX_STANDARD 20)`
- [ ] Add `vcpkg.json` manifest declaring `glfw3`, `freetype`, `openal-soft` as initial dependencies
- [ ] Write `CMakePresets.json` with four presets: `mac-debug`, `mac-release`, `win-debug`, `win-release`. Each preset sets the vcpkg toolchain file, generator (Ninja on both), and build type.
- [ ] Verify a `cmake --preset mac-debug && cmake --build --preset mac-debug` cycle runs to an empty binary on the Mac machine
- [ ] Verify the same on Windows (clone repo, run one command, binary builds)

**Project Structure:**
```
yope3d/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── src/
│   ├── main.cpp
│   ├── math/          (Vec2, Vec3, Vec4, Mat3, Mat4, Quat)
│   ├── platform/      (Window, Input)
│   ├── gpu/           (Vulkan abstraction layer)
│   ├── rendering/     (Renderer, Mesh, Material, Lights)
│   ├── physics/       (Hull hierarchy, Collider, RigidBody)
│   ├── audio/         (AudioSystem, Source, Listener)
│   ├── world/         (World, SceneObject, Transform)
│   ├── scripting/     (Script base, C++ scripts)
│   ├── ui/            (Background, TextBox, Label)
│   └── assets/        (AssetManager, OBJ loader, Image loader)
├── shaders/           (GLSL source .vert .frag .comp)
├── compiled_shaders/  (gitignored, .spv output)
├── assets/            (models, textures, sounds, fonts)
├── tests/             (Catch2 unit tests — added progressively)
└── tools/             (embed_assets.py, any other build tools)
```

**Math Library:**
- [ ] `math/Vec2.h`, `math/Vec3.h`, `math/Vec4.h`: arithmetic operators, dot, cross, normalize, length, lerp
- [ ] `math/Mat3.h`, `math/Mat4.h`: construction, multiplication, transpose, inverse, `rotateXYZ`, `translate`, `scale`
- [ ] `math/Quat.h`: construction from axis-angle and Euler, multiplication, `toMat3`, `toMat4`, slerp
- [ ] `math/Math.h`: common scalar utilities (clamp, lerp, sign, PI constant, degrees↔radians)
- [ ] Write first tests in `tests/math_tests.cpp` using Catch2: verify matrix inverse, quaternion rotation round-trips, cross product. This establishes the test harness for the whole project.

---

### Milestone 2 — Window & Input
**Weeks 2–3 | ~8 hrs**

**Window:**
- [ ] `platform/Window.h/cpp`: wraps `GLFWwindow*`. Constructor takes title, width, height.
- [ ] Expose: `shouldClose()`, `swapBuffers()`, `pollEvents()`, `getWidth()`, `getHeight()`, `setTitle()`
- [ ] Framebuffer resize callback registered internally; stores current width/height
- [ ] Window icon loading via STB image (same as current Java code)
- [ ] Pause/unpause: cursor hide/show via `glfwSetInputMode`

**Input:**
- [ ] `platform/Input.h/cpp`: polling-based, not callback-based for key state. Stores key state in a `std::array<bool, GLFW_KEY_LAST>`.
- [ ] Mouse button state: LMB, RMB, MMB, forward/backward buttons
- [ ] `MouseDelta`: accumulated per-frame delta computed from center-lock cursor, reset each frame
- [ ] Scroll accumulator: stores scroll offset since last query
- [ ] Register all callbacks in `Window::init()`, writing to the `Input` instance

**Engine Bootstrap:**
- [ ] `main.cpp`: constructs `Engine` context, calls `engine.init()`, runs the main loop, calls `engine.cleanup()`. The loop is: `input.beginFrame()` → `update()` → `render()` → `window.swapBuffers()` → `window.pollEvents()`.

---

### Milestone 3 — Vulkan Bootstrap
**Weeks 3–8 | ~40 hrs**

This is the largest single block in Phase 1. Expect the most debugging time here. The goal at the end of this milestone is a window with a solid clear color on both Mac and Windows. Every subsequent rendering milestone builds on this foundation, so correctness matters more than speed.

**Sub-milestone 3a — Instance & Device (Weeks 3–5):**
- [ ] `gpu/VulkanInstance.h/cpp`: create `VkInstance` with validation layers in debug builds (`VK_LAYER_KHRONOS_validation`). Enable `VK_KHR_portability_enumeration` extension on Mac (required for MoltenVK).
- [ ] `gpu/PhysicalDevice.h/cpp`: enumerate physical devices, score and select best (prefer discrete GPU, check for geometry shader support, required extensions). Store queue family indices (graphics, present, compute — all may be same family).
- [ ] `gpu/LogicalDevice.h/cpp`: create `VkDevice` with graphics + present + compute queues. Enable `VK_KHR_swapchain`, `VK_KHR_portability_subset` (Mac only via `#ifdef`).
- [ ] `gpu/Surface.h/cpp`: create `VkSurfaceKHR` from GLFW window via `glfwCreateWindowSurface`.

**Sub-milestone 3b — Swapchain & Render Pass (Weeks 5–6):**
- [ ] `gpu/Swapchain.h/cpp`: query surface capabilities, choose format (prefer `B8G8R8A8_SRGB` + `VK_COLOR_SPACE_SRGB_NONLINEAR`), choose present mode (prefer `MAILBOX`, fall back to `FIFO`). Create swapchain images and image views. Handle window resize by recreating swapchain.
- [ ] `gpu/RenderPass.h/cpp`: create a render pass with one color attachment (swapchain format) and one depth attachment (`VK_FORMAT_D32_SFLOAT`). Subpass dependency for layout transitions.
- [ ] `gpu/DepthBuffer.h/cpp`: create depth image, image memory, image view.
- [ ] `gpu/Framebuffer.h/cpp`: one framebuffer per swapchain image, binding color + depth views.

**Sub-milestone 3c — Command Buffers & Sync (Week 6–7):**
- [ ] `gpu/CommandPool.h/cpp`: one pool per thread (just one for now), reset per frame.
- [ ] `gpu/CommandBuffer.h/cpp`: wraps `VkCommandBuffer`. Exposes `begin()`, `end()`, `beginRenderPass()`, `endRenderPass()`, `bindPipeline()`, `draw()`, `drawIndexed()`.
- [ ] Synchronization: two frames in flight. Per-frame: one image-available semaphore, one render-finished semaphore, one in-flight fence. Properly wait on fence at frame start.
- [ ] Main render loop: `vkAcquireNextImageKHR` → record command buffer → `vkQueueSubmit` → `vkQueuePresentKHR`. Handle `VK_ERROR_OUT_OF_DATE_KHR` to trigger swapchain recreation.

**Sub-milestone 3d — Basic Pipeline & Clear Color (Week 7–8):**
- [ ] `gpu/ShaderModule.h/cpp`: load compiled `.spv` from disk (or embedded array), create `VkShaderModule`.
- [ ] CMake custom target: `add_custom_target(compile_shaders ...)` that runs `glslc` on every `.vert`/`.frag`/`.comp` in `shaders/`. Main target depends on `compile_shaders`.
- [ ] Write minimal `shaders/solid.vert` + `shaders/solid.frag` that output a hardcoded color.
- [ ] `gpu/Pipeline.h/cpp`: `GraphicsPipeline` class. Takes shader modules, render pass, vertex input description, descriptor set layout, push constant range. Creates `VkPipeline`.
- [ ] **Checkpoint**: clear color renders on screen on both Mac and Windows. Validation layers show zero errors.

---

### Milestone 4 — Core Rendering
**Weeks 8–13 | ~35 hrs**

**Sub-milestone 4a — Buffers & Mesh (Weeks 8–10):**
- [ ] `gpu/Buffer.h/cpp`: wraps `VkBuffer` + `VkDeviceMemory`. Exposes staging-buffer-based upload for vertex/index data. Helper: `createAndUpload(data, size, usage)`.
- [ ] `gpu/DescriptorPool.h/cpp` + `gpu/DescriptorSetLayout.h/cpp`: central pool, layout cache.
- [ ] `rendering/RenderMesh.h/cpp`: holds vertex buffer, index buffer, index count, material index. Created from raw `float[]` + `int[]` vertex/index data. `draw(VkCommandBuffer)` method.
- [ ] Vertex format struct: `struct Vertex { Vec3 position; Vec3 normal; Vec2 uv; }` — matches current 8-float-per-vertex layout.
- [ ] `rendering/Renderer.h/cpp`: main render orchestrator. Owns the graphics pipeline(s), issues draw calls for all `RenderMesh` objects in the world.

**Sub-milestone 4b — Camera & Uniforms (Weeks 10–11):**
- [ ] `gpu/UniformBuffer.h/cpp`: per-frame UBO backed by persistently-mapped host-visible memory. Avoids staging for small uniform updates.
- [ ] `rendering/Camera.h/cpp`: port from Java. Stores position (Vec3), rotation (Vec3 Euler), velocity (Vec3), FOV, aspect ratio. `genViewMatrix() → Mat4`, `genProjectionMatrix() → Mat4`. Mouse input directly updates rotation via `Input::getMouseDelta()`.
- [ ] UBO layout: `struct GlobalUBO { Mat4 view; Mat4 proj; Vec3 cameraPos; float padding; }`. Push constants for per-object model matrix (faster than per-object UBO for small data).
- [ ] Descriptor set: one set per frame-in-flight binding the global UBO to binding 0.
- [ ] Update vertex/fragment shaders to use these uniforms.
- [ ] **Checkpoint**: A colored cube rotating in 3D, camera controllable with mouse + WASD.

**Sub-milestone 4c — Lighting System (Weeks 11–13):**
- [ ] `rendering/Light.h`: `PointLight`, `SpotLight`, `DirectionalLight`, `FlashLight` structs. Capped at 64 lights (configurable compile-time constant `YOPE_MAX_LIGHTS`).
- [ ] Light SSBO layout: 48 bytes per light (3× Vec4) packed identically to current Java layout so shaders need minimal changes. Type discriminant via a sentinel float value in position.w (infinity = directional, etc.) — preserve existing convention but document it explicitly.
- [ ] `gpu/StorageBuffer.h/cpp`: similar to UniformBuffer but for SSBOs, bound at descriptor binding 1.
- [ ] `World::lightChanged()` equivalent: mark light buffer dirty; re-upload SSBO at frame start if dirty.
- [ ] Port `fragment.frag` from Java project (GLSL → SPIR-V via glslc, minimal changes needed).
- [ ] Port `vertex.vert`.
- [ ] **Checkpoint**: Lit scene with point lights and a spot light.

**Sub-milestone 4d — Textures (Week 12–13 overlap):**
- [ ] `gpu/Texture.h/cpp`: `VkImage` + `VkImageView` + `VkSampler`. Created from raw pixel data (RGBA8). Mipmap generation via `vkCmdBlitImage` in a one-time command buffer.
- [ ] `assets/AssetManager.h/cpp`: `std::unordered_map<std::string, Texture*>` and equivalents for meshes and sounds. `loadTexture(path)` deduplicates — same as `Textures.java` pattern but generalized.
- [ ] Descriptor set slot 2: combined image sampler for the active texture.
- [ ] Render state integer (`state` uniform): port the `STATES` constants. Shader branches on state for solid-color vs. textured vs. UI vs. text vs. light modes.

---

### Milestone 5 — Asset Pipeline
**Weeks 12–14 | ~15 hrs** *(overlaps with Milestone 4)*

**OBJ Loader:**
- [ ] `assets/ObjLoader.h/cpp`: port `Util.readObjFile` to C++. Fix the index `/9` vs `/8` bug in the flat-shading branch. Use `std::unordered_map<std::string, uint32_t>` for vertex deduplication (same algorithm, cleaner in C++).
- [ ] Smooth normal averaging: same as Java — accumulate normals at shared positions, normalize at the end.
- [ ] MTL file awareness: parse `usemtl`, load associated `.mtl` file, extract `Kd` (diffuse color), `map_Kd` (diffuse texture path). Store result in a `MaterialData` struct to be consumed by the Material system (Milestone 13 expands this).
- [ ] Return a `LoadedMesh` struct: `{vector<Vertex> vertices, vector<uint32_t> indices, string materialHint}`.

**Image Loader:**
- [ ] `assets/ImageLoader.h/cpp`: thin wrapper around `stb_image.h`. Returns `{vector<uint8_t> data, int width, int height, int channels}`.
- [ ] Flip vertically option (same as Java's `stbi_set_flip_vertically_on_load`).

**Audio Loader:**
- [ ] `assets/AudioLoader.h/cpp`: wrapper around `stb_vorbis.h`. Decodes OGG to `{vector<short> data, int sampleRate, int channels}`. Matches `Util.readOggFile` behavior.

**Embed Tool:**
- [ ] `tools/embed_assets.py`: walks `assets/`, generates `generated/embedded_assets.cpp` and `.h`. In embed mode, `AssetManager::open(path)` returns a `std::span<const uint8_t>` pointing to the embedded array instead of opening a file. Controlled by `YOPE_EMBED_ASSETS` CMake option.

---

### Milestone 6 — Physics Foundation ✅ COMPLETE
**Weeks 14–19 | ~35 hrs**

Physics is the project's emphasis. Phase 1 reproduces current behavior cleanly; Phase 2 expands it. The architecture here must be designed for extension.

> **Implementation note:** All items below are complete and exceeded. The planned octree (`CollisionTree`) was superseded by a Sweep-and-Prune broadphase (`BroadphaseSAP`) for better performance. Additional systems beyond the roadmap: CAABB shape, global island-partitioned parallel PGS solver with split-impulse position correction and warm-started Coulomb friction, `ThreadPool`, `IslandDetector`, body sleeping with island wake propagation, and named `CollisionLayers`.

**Hull Hierarchy:**
- [ ] `physics/Hull.h/cpp`: abstract base. Stores `Transform*` reference (shared with rendering). Fields: `Vec3 velocity`, `Vec3 omega`, `float mass`, `bool fixed`. Methods: `advance(float dt)` — gravity is now `World::gravity` (Vec3, default `{0, -9.80665f, 0}`). `addImpulse(Vec3)`, `addAngularImpulse(Vec3)`. Pure virtual `genInertiaTensor() → Mat3`.
- [ ] `physics/CSphere.h/cpp`: `float radius`. Port `genInertiaTensor`. Port `getModelMatrix`.
- [ ] `physics/COBB.h/cpp`: `Vec3 extent`. Port `genInertiaTensor`. Implement the OBB-OBB collision stub before Phase 2.
- [ ] `physics/Barrier.h/cpp`: `Vec3 normal`, `Vec3 position`.
- [ ] `physics/BoundedBarrier.h/cpp`: adds `float xScale`, `float yScale`, `Vec3 orientation`. Port `genRectangularBarriers` static factory.
- [ ] `physics/BarrierHull.h/cpp`: group of barriers with extent. Port `genInertiaTensor` (returns identity/null — note that barrier hulls are always fixed).

**CCD Collider:**
- [ ] `physics/ColliderCCD.h/cpp`: port all methods from Java. Fix: make `collideBarrier(Hull*, Barrier*)` dispatch to the correct sub-method cleanly (use `dynamic_cast` or a virtual dispatch on `Hull`).
- [ ] Fix `sphere_barrier`: remove the hardcoded `visual.Launch.world.getDT()` call — `dt` is now passed as a parameter to all collision methods.
- [ ] Fix `eval_dir`: same logic, cleaner variable naming.
- [ ] `physics/Collider.h/cpp`: static dispatch entry point (`CCD(Hull*, Hull*)`, `CCDBarrier(Hull*, Barrier*)`).

**Raycast:**
- [ ] `physics/Raycast.h/cpp`: port `raycastSphere`, `raycastAABB`. These are pure math, no state — make them `[[nodiscard]] static float` free functions. Add unit tests in `tests/physics_tests.cpp`: verify sphere and AABB hit/miss cases, edge cases (ray tangent to sphere, ray parallel to face).

**Collision Tree (Octree):**
- [ ] `physics/CollisionTree.h/cpp`: port the octree. **Fix the octree volume bug** (noted from code review): `vectorsToCheck` currently uses only 6 axis-aligned probe points from the hull's AABB corners. Replace with the full 8 AABB corner points for correct containment — a hull straddling a node boundary will no longer be missed. This is the bug you noticed yourself.
- [ ] Fix: `vectorsToCheck` must not mutate the hull's position. The Java version returns `new Vector3f` from `getPosition()` so it was accidentally safe; the C++ version must be explicit — `getPosition()` returns by value (copy).
- [ ] Add unit test: insert 3 hulls at known positions, query from a 4th hull, verify correct set returned.

**Spring:**
- [ ] `physics/Spring.h/cpp`: port `update(float dt)`. References two `Hull*` objects. `k` and `restLength` as before. Damping coefficient as a named field (not a magic `0.0075f` literal).

**World Integration:**
- [ ] `world/World.h/cpp`: `advance(float dt)` method. Iterate barriers × non-fixed hulls for `CCDBarrier`. Iterate tree objects for hull-hull CCD. Advance all hulls. Update all springs. Gravity is a `Vec3` field on `World`, defaulting to `{0, -9.80665f, 0}`.
- [ ] Fix `pauseAllMonoSources`/`playAllMonoSources` equivalent: each `Source` now has a `bool wasPaused` field. `AudioSystem::pauseAll()` sets field and pauses; `resumeAll()` only resumes sources where `wasPaused == false`.

---

### Milestone 7 — Audio
**Weeks 16–18 | ~14 hrs** *(overlaps with physics — audio has no rendering dependency)*

OpenAL port is the most direct 1:1 translation of any subsystem. OpenAL Soft (the C implementation) is the cross-platform drop-in.

- [ ] `audio/AudioSystem.h/cpp`: wraps OpenAL initialization. `alcOpenDevice`, `alcCreateContext`, `alcMakeContextCurrent`. Holds `std::vector<Source*> allSources`. Destructor calls `cleanup()`. Exposes `pauseAll()`, `resumeAll()` (with the per-source pause-state fix).
- [ ] `audio/Source.h/cpp`: wraps `ALuint sourceID` and `ALuint bufferID`. Constructor takes `SoundData*` and initial position/velocity. Port all methods: `play()`, `pause()`, `stop()`, `rewind()`, `setGain()`, `setPitch()`, `setPosition()`, `setVelocity()`, `enableLooping()`, `isPlaying()`, `cleanup()`. Add `bool pausedBySystem` field for the pause fix.
- [ ] `audio/Listener.h/cpp`: same static-field pattern is acceptable here (there is always exactly one listener). Port `init()`, `setPosition()`, `setVelocity()`, `setGain()`, `setOrientation()` and all getters.
- [ ] `audio/SoundData.h`: POD struct `{vector<short> data, int sampleRate, int channels}` — loaded by `AudioLoader`.
- [ ] Integration: `Engine` owns one `AudioSystem`. `AssetManager::loadSound(path)` returns `SoundData*` (cached).
- [ ] Test: manually verify spatial falloff of a mono source and that pause/resume cycle works correctly via a simple test script (before Python scripting exists, this is manual).

---

### Milestone 8 — World & C++ Script Architecture
**Weeks 18–21 | ~18 hrs**

**World:**
- [ ] `world/World.h/cpp`: full implementation. Holds `vector<SceneObject*> objects`, `vector<Barrier*> barriers`, `vector<Light*> lights`, `vector<Spring*> springs`, `CollisionTree* tree`. Methods: `addObject`, `removeObject`, `addBarrier`, `addLight`, `getLight`, `lightChanged`, `advance`, `getObjects(Hull*)`, `instantiateCollisionTree`.
- [ ] `world/SceneObject.h/cpp`: container for `Transform`, optional `RenderMesh*`, optional `Hull*` (as base pointer). Has `bool fixed`, `bool draw`. The `advance` loop calls `hull->advance(dt)` if hull is not null and not fixed.
- [ ] `world/Transform.h/cpp`: `Vec3 position`, `Quat rotation`, `Vec3 scale`. `getModelMatrix() → Mat4`. `getTranslationMatrix() → Mat4`. Replacing the `Hull::genTransform()` / `Hull::getModelMatrix()` pattern.

**Script System (Phase 1 — C++ only):**
- [ ] `scripting/Script.h`: abstract base with `virtual void init() = 0`, `virtual void update(float dt) = 0`, `virtual void onScroll(double x, double y) {}`. Receives a `ScriptContext&` in constructor — not raw `Engine*`. `ScriptContext` exposes: `World& world`, `Camera& camera`, `Input& input`, `AudioSystem& audio`, `AssetManager& assets`, `Window& window`.
- [ ] `scripting/ScriptContext.h`: the restricted-access wrapper. Exposes only what scripts need. Prevents direct access to renderer internals from script code.
- [ ] Script registration: `World::setScript(Script*)`. Engine calls `script->init()` once, then `script->update(dt)` each frame.
- [ ] `scripting/ScriptFactory.h`: `std::unordered_map<std::string, std::function<Script*(ScriptContext&)>>` registry. Registered via a macro `YOPE_REGISTER_SCRIPT(ClassName)` in each script's `.cpp`. `main.cpp` reads a script name from command-line argument or a config file, looks it up in the registry, instantiates it. This replaces the `Launch.toScript = ClassName.class` pattern.

**Config File:**
- [ ] Simple `yope3d.cfg` key-value text file at runtime: `script=SpringDemo`, `width=1280`, `height=720`. Parsed at startup. This replaces compile-time script selection and lets you switch demos without recompiling.

---

### Milestone 9 — UI Foundation
**Weeks 20–23 | ~20 hrs**

Rebuilt from scratch. Same conceptual model (`Label` → `Background` → `TextBox`) but integrated with Vulkan.

**Vulkan UI Pipeline:**
- [ ] Separate Vulkan graphics pipeline for UI rendering: depth test disabled, alpha blending enabled (same blend factors as current Java code). UI pass happens after the main 3D render pass — in Vulkan, this is a second subpass or a second render pass writing to the same swapchain image.
- [ ] UI vertex format: `{Vec2 position, Vec2 uv, Vec4 color}`. Smaller than 3D vertices.
- [ ] `gpu/UIBuffer.h/cpp`: host-visible, persistently-mapped vertex + index buffer for UI geometry. Rebuilt each frame (UI changes frequently). Suitable for dynamic text.

**Label Hierarchy:**
- [ ] `ui/Label.h`: abstract. `virtual void render(UICommandRecorder&) = 0`, `virtual void update(float dt) {}`, `virtual void cleanup() = 0`, `virtual bool draw() = 0`, `virtual int getDepth() = 0`, `virtual void onClicked(int x, int y, int button, int action) {}`, `virtual void onResize(int ow, int oh, int w, int h) {}`.
- [ ] `ui/Background.h/cpp`: solid color quad. Fields: `Vec2 min`, `Vec2 max`, `Vec3 color`, `bool visible`, `int depth`. `redefineMesh()` rebuilds the 4-vertex quad.
- [ ] `ui/TexturedBackground.h/cpp`: extends Background, adds a texture reference.
- [ ] `ui/CurvedBackground.h/cpp`: port the curved/pill shape geometry. Isolated so the complex mesh generation doesn't pollute Background.
- [ ] `ui/AnimatedBackground.h/cpp`: sequence of textures + frame counter.

**Text Rendering:**
- [ ] `ui/TextAtlas.h/cpp`: port from Java. Fix the blit alpha inversion bug (the `if(val < 1)` condition was inverted — non-zero values should be placed, not zero values). Use `FT_Load_Char` + `FT_RENDER_MODE_NORMAL`. Pack glyphs into a 512×512 R8 atlas texture. Store `GlyphInfo` structs for each character.
- [ ] `ui/TextBox.h/cpp`: port `generateTextMesh`. Word-wrap, newline handling, CENTERED vs DEFAULT alignment. References a `TextAtlas*` and a `Background*` for bounds.

**UI Manager:**
- [ ] `Window` or `Engine` owns `std::vector<std::vector<Label*>> uiLayers` (sorted by depth). `addLabel(Label*)`, `clearUI()`. Renderer calls `renderUI(UICommandRecorder&)` iterating depth-sorted layers, calling `label->render()` on each visible label.

---

### Milestone 10 — Compute Raytracer
**Weeks 22–25 | ~20 hrs**

**Vulkan Compute Pipeline:**
- [ ] `gpu/ComputePipeline.h/cpp`: wraps `VkPipeline` created with `vkCreateComputePipelines`. Takes a single compute shader module.
- [ ] Compute output texture: `VkImage` in `VK_IMAGE_LAYOUT_GENERAL`, bound as a storage image (`VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`). Same dimensions as swapchain.
- [ ] `gpu/StorageImage.h/cpp`: manages the compute output image and its descriptor.
- [ ] Compute dispatch in main render loop: before the graphics render pass. `vkCmdDispatch((width+7)/8, (height+3)/4, 1)` — same tile size as Java version. Pipeline barrier between compute write and fragment read.

**Raytracer Shaders:**
- [ ] Port `raytracer.comp` from Java project. Shader is GLSL compute — translates to SPIR-V with minimal changes. Key uniforms: camera-to-world matrix (replaces `genCamToWorldMatrix()`), FOV + aspect ratio, number of lights.
- [ ] Light data SSBO must be accessible to the compute shader: use the same SSBO binding index as the graphics pipeline (binding index 1) or bind it separately to the compute descriptor set.
- [ ] Full raytracer mode: render pass for raytracer only writes the compute texture to screen via two fullscreen triangles + a simple blit fragment shader. UI is rendered on top via the UI render pass.
- [ ] Toggle: `World` or `Engine` has a `RenderMode { RASTER, RAYTRACE }` enum. Mode switchable at runtime via a key press.
- [ ] **Checkpoint**: Raytracing mode works on Mac (via MoltenVK compute). This is the key validation that Vulkan was the right choice.

---

### Milestone 11 — Demo Scene Port
**Weeks 23–27 | ~20 hrs**

Translate `SpringDemo` and `Platformer` to the new C++ scripting system. This milestone is the integration test for everything built in Phase 1.

**SpringDemo Port:**
- [ ] `scripting/SpringDemoScript.cpp`: extends `Script`. Register via `YOPE_REGISTER_SCRIPT(SpringDemoScript)`.
- [ ] Init: create floor, camera sphere collider, world barriers, 4 point lights, init listener.
- [ ] Cloth creation (on forward MB): 30×30 grid of cube `SceneObject`s. Springs connecting horizontal, vertical, diagonal neighbors. Top row fixed.
- [ ] Raycast push (on RMB): `Raycast::raycastSphere` against each cloth node. Apply push + neighbor propagation.
- [ ] All magic number literals replaced with named constants at the top of the file.

**Platformer Port:**
- [ ] `scripting/PlatformerScript.cpp`: spiral course generation, player sphere, star object, dashing system, win/star conditions.
- [ ] `jumpValid()` raycast checks against course objects — verify octree query works correctly here.
- [ ] `winningVariable` → `public int winState` replaced by a proper `enum class WinState { NONE, WIN, NO_STAR, TOO_SLOW }`.
- [ ] UI death/win screens connected to `UIInit` equivalent.

**Integration Validation:**
- [ ] Both demos run on Mac (primary dev machine) without validation layer errors.
- [ ] Both demos build and run on Windows from the same CMake invocation.
- [ ] Spatial audio (Architect-style) tested manually in SpringDemo by adding a sound source.

---

## Phase 2: New Systems & Expansion

**Goal:** Editor window, Python ECS scripting, expanded physics (convex hull, dynamic BVH, constraints), material/normal map system, skeletal animation, skyboxes. No fixed end date — milestone-driven.

**Approximate Start: Week 28** (assuming Phase 1 finishes on schedule; adjust based on actual pace).

---

### Milestone 12 — ECS Architecture
**Weeks 32–37 | ~35 hrs** *(begins while editor is being refined)*

**Why ECS Now:**
The editor makes ECS more tractable — the inspector panel maps cleanly onto component iteration. The scripting system (Milestone 14) maps most naturally onto ECS entities as well.

**Approach — Sparse-Set ECS:**
- [ ] `world/Registry.h`: the ECS registry. Stores component arrays as `std::unordered_map<ComponentTypeId, ComponentArray*>`. `ComponentTypeId` is a compile-time `uint64_t` generated from `typeid(T).hash_code()`.
- [ ] `world/ComponentArray.h`: sparse-set backed dense array for each component type. O(1) add, remove, iteration.
- [ ] `world/Entity.h`: `using Entity = uint32_t`. `EntityManager` allocates/recycles IDs.
- [ ] Core component types: `Transform`, `RenderMesh`, `Hull` (base ptr), `RigidBody`, `AudioEmitter`, `ScriptComponent` (holds a `Script*`).
- [ ] Migration: `World` refactored to use `Registry` internally. `SceneObject` becomes a factory function that creates an entity with a standard set of components.
- [ ] `World::advance()` becomes a series of system calls: `PhysicsSystem::update(Registry&, float dt)`, `AudioSystem::updateSources(Registry&)`, `RenderSystem::collectDrawCalls(Registry&)`.
- [ ] Editor's Scene Hierarchy panel iterates `Registry::view<Transform>()` — shows all entities with a transform.


---

### Milestone 13 — Editor Window
**Weeks 28–36 | ~65 hrs**

The editor is the single most impactful quality-of-life improvement over the original project. This is a large milestone and must be designed carefully because it affects how the engine's main loop and rendering work.

**Architecture Decision — Editor vs Runtime:**
The engine has two modes determined at launch: `EditorMode` and `RuntimeMode`. In `RuntimeMode`, it behaves exactly as Phase 1 (runs a script, no editor UI). In `EditorMode`, the editor wraps the engine. **Dear ImGui is used exclusively for the editor UI** — it is a developer tool, not player-facing. The custom `ui/` system remains for in-game UI. This is not a contradiction of your "from scratch" philosophy because ImGui serves a different purpose and audience.

- [ ] Add `imgui` as a vcpkg dependency. Integrate the Vulkan+GLFW backend (`imgui_impl_vulkan.h`, `imgui_impl_glfw.h`). ImGui renders into a separate render pass (or as an additional subpass) after the game viewport.

**Viewport:**
- [ ] Game renders to a `VkFramebuffer` backed by a texture (not the swapchain). This is "render to texture."
- [ ] Editor displays this texture as an ImGui `Image` widget in a "Viewport" panel. The panel is resizable; when resized, the game's render texture is recreated at the new size and the camera's aspect ratio is updated.
- [ ] The editor window itself occupies the swapchain framebuffer. The game is one panel within it.

**Editor Panels:**
- [ ] **Viewport panel**: displays the game render texture. Shows FPS counter. "Play/Stop" button — play mode enables the script's `update()` loop and mouse capture; stop mode freezes the simulation and releases the mouse.
- [ ] **Scene Hierarchy panel**: tree view of all `SceneObject*` in `World`. Click to select. Shows object name, visibility toggle, fixed toggle.
- [ ] **Inspector panel**: shows selected object's properties. Transform (position, rotation, scale) editable as float input fields. Physics properties (mass, velocity). Material slot (texture path). Changes applied immediately.
- [ ] **Asset Browser panel**: reads `assets/` directory tree. Shows loaded vs. unloaded assets. Click mesh to preview (load and render a rotating preview in a small sub-viewport).
- [ ] **Console panel**: redirects `std::cout`/`std::cerr` to an in-editor log window. Color-coded by severity. Filterable.
- [ ] **World Settings panel**: gravity vector, global DT, collision tree bounds, render mode toggle (RASTER/RAYTRACE), number of active lights.
- [ ] **Light panel**: list of all lights in scene. Add/remove/modify (position, color, intensity, type). `lightChanged()` called on modification.

**Editor-Script Workflow:**
- [ ] In editor play mode, the registered script's `init()` and `update()` run normally.
- [ ] Stop mode: scene state resets to the pre-play snapshot. `World` state is deep-copied before play and restored on stop.
- [ ] Script is selected via a dropdown in editor (reads from `ScriptFactory` registry). Changing the selection and pressing play relaunches with the new script.

---

### Milestone 14 — Python Scripting Integration
**Weeks 36–43 | ~50 hrs**

**Embedding CPython:**
- [ ] Link against Python 3.x shared library (vcpkg `python3`). `#include <Python.h>`. Initialize with `Py_Initialize()` in `Engine::init()`.
- [ ] Use **pybind11** (vcpkg) to generate bindings. This is a binding generator, not a game library — it is the appropriate tool for this job. The engine's custom rendering/physics code is still all from scratch; pybind11 just eliminates writing PyObject* boilerplate by hand.
- [ ] Bind: `Vec3`, `Vec4`, `Mat4` (basic math ops), `World`, `SceneObject`, `Transform`, `Hull` (CSphere, COBB as subclasses), `Camera`, `Input`, `AudioSource`, `Listener`. Expose as a Python module named `yope3d`.

**Script Hot Reload:**
- [ ] Python scripts are `.py` files in `scripts/` loaded by name from `yope3d.cfg` or editor dropdown.
- [ ] `PythonScript.h/cpp`: C++ `Script` subclass. Its `init()` calls `module.attr("init")(ctx)` via pybind11. Its `update(dt)` calls `module.attr("update")(ctx, dt)`.
- [ ] Hot reload: on file change (poll mtime or use OS file-watcher), call `importlib.reload(module)` in the Python interpreter. The scene is NOT reset — only the script logic reloads. This means `init()` is not re-called; add a separate `onReload()` hook for scripts that need to reinitialize on reload.
- [ ] Editor has a "Reload Script" button that manually triggers reload.
- [ ] Error handling: if the Python script raises an exception, print to the editor console and continue running (do not crash the engine).

**Python ECS Integration:**
- [ ] Bind `Registry::view<T>()` to Python as an iterable that yields `(Entity, T&)` pairs. Scripts can iterate components: `for entity, transform in yope3d.world.view(yope3d.Transform): ...`
- [ ] Expose `Registry::add_component`, `remove_component`, `get_component` to Python.
- [ ] Allow Python scripts to attach anonymous update callbacks to entities: `yope3d.world.on_update(entity, lambda dt: ...)` — enables the clock-hand tick behavior you described.

**Porting Demos to Python:**
- [ ] Port `SpringDemoScript` to `spring_demo.py` as a reference implementation.
- [ ] Port `PlatformerScript` to `platformer.py`.
- [ ] Both C++ and Python versions kept in repo — the C++ versions serve as reference for correctness during Python port.

---

### Milestone 15 — Material System & Renderer Upgrade
**Weeks 38–43 | ~35 hrs** *(partially overlaps with Python scripting work)*

**Material System:**
- [ ] `rendering/Material.h/cpp`: `struct Material { Texture* albedo; Texture* normalMap; Texture* roughnessMap; Vec3 albedoColor; float roughness; float metallic; }`. Not full PBR but extensible toward it.
- [ ] OBJ loader extended: MTL parsing now loads `map_Kd` (albedo), `map_Bump`/`map_Kn` (normal), `map_Pr`/`map_Pm` (roughness/metallic if present). Falls back to solid color for missing maps.
- [ ] Expand fragment shader: add normal map sampling (TBN matrix transform from normal map R,G,B → world space normal). Add a roughness term to the Blinn-Phong specular calculation. This is "PBR-lite" — physically plausible without full PBR energy conservation.
- [ ] `rendering/MaterialCache.h/cpp`: owned by `AssetManager`. Deduplicates materials by their constituent texture paths.
- [ ] Descriptor set layout updated: albedo at binding 2, normal map at binding 3. These can be null-bound (a 1×1 white/flat-normal default texture).

**Skybox:**
- [ ] `rendering/Skybox.h/cpp`: loads a cubemap from 6 face images (or an equirectangular HDR map with conversion). Renders as a unit cube with depth testing disabled (always at infinity). Separate pipeline with front-face culling reversed.
- [ ] `assets/AssetManager::loadCubemap(paths[6]) → Skybox*`
- [ ] Exposed to Python scripts: `yope3d.world.set_skybox(yope3d.assets.load_cubemap(["px.png", "nx.png", ...]))`

---

### Milestone 16 — Skeletal Animation
**Weeks 42–52 | ~70 hrs**

Animation is architecturally significant because it touches the mesh vertex format, the asset loader, the GPU pipeline, and the update loop.

**Asset Format:**
- [ ] Add glTF 2.0 loading alongside OBJ. glTF is the standard interchange format for animated meshes. Write the loader from scratch using the glTF spec (JSON + binary buffer). This is a significant but instructive undertaking — glTF JSON is straightforward with C++20's `<format>` and a simple JSON parser (write a minimal one, ~300 lines, or use the spec as reference to understand the format).
- [ ] `assets/GltfLoader.h/cpp`: loads meshes, materials, skeletons (node hierarchy), animation clips (sampler + channel pairs). Returns `AnimatedMesh` structs.

**Skinning:**
- [ ] Vertex format extended for skinned meshes: `{Vec3 pos, Vec3 normal, Vec2 uv, Vec4i jointIndices, Vec4 jointWeights}`.
- [ ] `rendering/Skeleton.h/cpp`: bone hierarchy (parent indices array), bind pose inverse matrices (Vec3[] of joint transforms as Mats).
- [ ] `rendering/AnimationClip.h/cpp`: per-joint sampler (time → Vec3 translation, Quat rotation, Vec3 scale via linear or cubic spline interpolation). Duration, looping flag.
- [ ] `rendering/Animator.h/cpp`: current clip, current time, blend weight. `update(float dt)` advances time, samples all joint transforms, outputs `vector<Mat4> jointMatrices` (joint palette).
- [ ] GPU skinning via a **compute shader**: `shaders/skin.comp` takes the vertex buffer + joint palette + bind pose inverses and outputs a skinned vertex buffer. This is cheaper than skinning in the vertex shader for large meshes and gives you another compute shader use case.
- [ ] Joint palette sent to compute shader as an SSBO (one Mat4 per joint, max 256 joints).
- [ ] `Animator` component added to ECS; `AnimationSystem::update(Registry&, dt)` advances all animators and dispatches compute skinning.

---

### Milestone 17 — Physics Expansion
**Weeks 44–60 | ~100 hrs**

This is the project's deepest, most open-ended milestone. All items in this milestone are implementation from scratch.

**Dynamic BVH:**
- [ ] `physics/DynamicBVH.h/cpp`: axis-aligned bounding box BVH with incremental updates. Each `RigidBody` has a leaf node. On movement, the leaf's AABB is fattened by a velocity-proportional margin (avoids rebuilding every frame). Internal nodes refit bottom-up when leaves move outside their fattened AABB. Insertion: surface area heuristic (SAH) to choose sibling. Removal: collapse parent.
- [ ] `World::advance()` uses the BVH for broadphase: `bvh.queryOverlapping(aabb) → vector<Entity>`. Replaces the flat O(n²) dynamic-vs-dynamic loop and the static octree. The octree becomes purely optional for static geometry.
- [ ] Add unit tests: insert known hulls, verify query returns correct overlapping set. Test BVH stays valid across multiple frames of movement.

**Convex Hull Collision — GJK + EPA:**
- [ ] `physics/ConvexHull.h/cpp`: represent a convex hull as a `vector<Vec3>` of support vertices. `support(Vec3 dir) → Vec3` returns the furthest point in a direction (used by GJK).
- [ ] `physics/GJK.h/cpp`: Gilbert-Johnson-Keerthi distance algorithm. Returns `bool` (intersecting or not) and, if intersecting, a simplex for EPA. Implement the full 3D version with all simplex cases (point, line, triangle, tetrahedron). This is the hardest single algorithm in this milestone — implement it step by step with unit tests at each simplex case.
- [ ] `physics/EPA.h/cpp`: Expanding Polytope Algorithm. Takes the GJK simplex and refines the collision manifold to compute penetration depth and contact normal. Used when GJK returns intersection.
- [ ] `ColliderCCD.cpp`: add `convex_convex(ConvexHull*, ConvexHull*)` case using GJK+EPA. This enables arbitrary convex shape collision.
- [ ] `CSphere` becomes a special case with an analytical support function — faster than general GJK for spheres.
- [ ] `COBB` support function: `extent * sign(dir)` in local space, transformed to world space.

**Convex Hull Generation:**
- [ ] `physics/ConvexHullBuilder.h/cpp`: Quickhull algorithm. Takes a `vector<Vec3>` point cloud (e.g., mesh vertices) and returns the convex hull. This enables `SceneObject::generateHullFromMesh()` — automatic collision hull generation from arbitrary meshes.
- [ ] Python binding: `yope3d.ConvexHull.from_mesh(render_mesh)` generates a hull automatically.
- [ ] In the editor Inspector panel: "Generate Convex Hull" button on any object with a RenderMesh.

**Constraint System (Stretch):**
- [ ] `physics/Constraint.h`: abstract base. `virtual void preStep(float dt)` (compute Jacobian), `virtual void applyImpulse(float dt)`.
- [ ] `physics/DistanceConstraint.h/cpp`: maintains fixed distance between two points on two bodies. Enables rigid links.
- [ ] `physics/HingeConstraint.h/cpp`: allows rotation about one axis only.
- [ ] `physics/ConstraintSolver.h/cpp`: iterative impulse solver. Run N iterations (typically 10) per physics step over all constraints.
- [ ] Springs generalized: `Spring` becomes a `DistanceConstraint` with a stiffness/damping term rather than a hard constraint. Old Spring behavior preserved as a mode.

**Compound Bodies (Stretch):**
- [ ] `physics/CompoundHull.h/cpp`: a group of `ConvexHull*` with relative transforms. Used to approximate concave geometry. Collision tested against each child hull separately, impulses aggregated.

---

### Milestone 18 — UI Expansion
**Weeks 50–56 | ~35 hrs** *(overlaps with physics work)*

- [ ] **World-space UI**: `Label3D` — a `Label` variant that renders at a `Transform` in world space rather than screen space. Uses a billboard transformation (always faces camera). Enables floating health bars, name tags, interaction prompts.
- [ ] **UI scripting via Python**: expose `yope3d.ui.add_background(min, max, color, depth)`, `yope3d.ui.add_text(background, message, size)`, `yope3d.ui.clear()`. Scripts create and manage UI entirely from Python, no C++ recompilation needed.
- [ ] **Animated textures from Python**: `yope3d.ui.add_animated_background(folder, num_frames)` — drives `AnimatedBackground` from Python.
- [ ] **Cursor/click events to Python**: `Label.onClicked` hooked into Python via a registered callback per-label: `label.on_click = lambda x, y, button, action: ...`
- [ ] **Resolution-independent layout**: current `Background` uses hardcoded NDC coordinates. Add a layout system where coordinates can be specified as percentage of screen width/height, enabling adaptive layouts on different monitors.

---

### Milestone 19 — Quality, Testing & Documentation
**Ongoing from Week 1 — formalized from Week 55+**

Testing should be added incrementally throughout all milestones, not deferred entirely. The following represents the formalized test pass.

**Test Coverage Targets:**
- [ ] `tests/math_tests.cpp`: Vec3/Mat4/Quat operations, projection matrix edge cases.
- [ ] `tests/physics_tests.cpp`: Raycast sphere/AABB hit/miss, GJK all simplex cases, EPA depth/normal correctness, spring force calculation, BVH query correctness, octree volume containment (the corner-point bug regression test).
- [ ] `tests/asset_tests.cpp`: OBJ load roundtrip (load, check vertex count / face count against known file), AudioLoader sample rate/channel detection.
- [ ] `tests/collision_integration_tests.cpp`: sphere-sphere collision conserves momentum (within tolerance), sphere-barrier reflects correctly at normal incidence and 45°, spring-mass system reaches equilibrium.
- [ ] Use **Catch2** (vcpkg). CMake target: `add_executable(yope_tests tests/*.cpp)`. `ctest` integration for CI-like local validation.

**Documentation:**
- [ ] Every public header has a Doxygen block (`/** @brief ... @param ... @return ... */`).
- [ ] `docs/architecture.md`: description of the Engine context, the ECS registry, the Vulkan abstraction layer, and the scripting pipeline. One diagram per subsystem (ASCII or Mermaid).
- [ ] `docs/scripting_guide.md`: how to write a Python script, what `yope3d` module objects are available, ECS usage examples, hot reload behavior.
- [ ] `docs/build_guide.md`: one-command setup for Mac and Windows. Vulkan SDK installation. vcpkg bootstrap. CMake preset invocation.
- [ ] `CHANGELOG.md`: maintained from the start. Each milestone completion is an entry.

---

### Milestone 20 — Hybrid Render Mode (Rasterization + Ray-Traced Lighting)
**Phase 2 — estimate ~30–40 hrs**

A third `RenderMode::HYBRID` (joining `RASTER` and `RAYTRACE`) that uses rasterization for primary visibility and the compute raytracer exclusively for secondary effects: hard/soft shadows, ambient occlusion, and one-bounce indirect lighting. The core insight is that rasterization is fast and cache-friendly for primary rays (coherent, one-per-pixel), while raytracing earns its keep on secondary rays (incoherent, but few enough to be tractable). This split makes high-quality lighting feasible at interactive framerates without a full path-tracer for every pixel.

**G-Buffer Pass:**
- [ ] Add a new `RenderPass` configuration for a geometry pass (MRT — multiple render targets). Three output attachments: `position` (RGBA32F), `normal` (RGBA16F, world-space), `albedo` (RGBA8). Depth attachment reused from existing pass.
- [ ] Modify `triangle.vert`/`triangle.frag` to write position and normal to the extra attachments when `HYBRID_MODE` is active (push constant flag). Otherwise identical to the current rasterization pass.
- [ ] New `GBuffer` wrapper class in `src/gpu/` owns the three `StorageImage` objects and the associated `VkFramebuffer`. Created alongside the swapchain; recreated on resize.
- [ ] The rasterization pass writes position/normal/albedo to the G-buffer textures instead of (or in addition to) the swapchain image.

**Secondary-Ray Compute Pass:**
- [ ] New shader `shaders/hybrid_lighting.comp`. Reads G-buffer textures (as `sampler2D` rather than `image2D` — read-only); for each pixel with valid geometry (discard sky pixels via alpha = 0 sentinel in position attachment), casts secondary rays into the existing geometry SSBO:
  - Shadow rays: one per light, same `traceShadow` logic as `raytracer.comp`.
  - AO rays (optional, toggled): N=8 cosine-weighted hemisphere rays per pixel, averaged into an occlusion factor. Darkens indirect regions without needing a light source.
  - One-bounce indirect (optional, toggled): one random hemisphere ray per pixel, shaded at the bounce point and accumulated as indirect radiance.
- [ ] Writes a lighting contribution image (RGBA16F) that the composition pass blends over the rasterized albedo.
- [ ] BVH acceleration is **most important here**, since secondary rays are incoherent — each pixel dispatches in a different direction, thrashing the geometry cache. Adding BVH (Milestone 21) yields the biggest speedup in hybrid mode, not in the pure raytracer.

**Composition Pass:**
- [ ] A fullscreen quad pass (reuse `fullscreen.vert`) with a new `hybrid_composite.frag` that reads the rasterized color + the lighting contribution image and blends them. Gamma correction and tonemapping applied here.
- [ ] Toggle between RASTER / RAYTRACE / HYBRID via the existing key binding system (`R` cycles modes).

**C++ Side:**
- [ ] `RenderMode.h`: add `HYBRID` enumerant.
- [ ] `Renderer` dispatches the G-buffer pass when in HYBRID mode; `Raytracer` dispatches the secondary-ray compute; a new `HybridCompositor` issues the fullscreen blit.
- [ ] Shared geometry SSBO between `Raytracer` and the hybrid compute pass — no duplication needed.

**Risks:**
- MoltenVK input attachment sampling in compute: read G-buffer as regular `sampler2D` bindings (not input attachments) to stay compatible with compute stages on Metal.
- G-buffer memory bandwidth: three full-res floating-point textures is ~24 MB at 1080p. Keep `normal` as RGBA16F (not 32F); pack metallic/roughness into the albedo alpha channel if material system expands.

---

### Milestone 21 — Raytracer Optimizations (BVH + Temporal Accumulation)
**Phase 2 — estimate ~40–55 hrs**

Two independent optimizations that compound: BVH cuts per-ray cost from O(N) to O(log N), and temporal accumulation amortizes per-frame sample cost over many frames. Together they make full path tracing feasible at interactive framerates.

**Note on physics BVH:** The physics system uses `BroadphaseSAP` (sweep-and-prune), not a tree structure. There is no BVH in the physics code to reuse. A dedicated raytracing BVH must be built over the packed geometry SSBO.

**BVH — CPU Build, GPU Traverse:**
- [ ] CPU builder (`src/rendering/BVHBuilder.h/.cpp`): takes the packed geometry SSBO float array as input (or a parallel `std::vector<AABB>` of entry bounding boxes) and builds a flat binary BVH using surface-area heuristic (SAH) partitioning. Output: a flat array of `BVHNode` structs `{ vec3 aabbMin; uint leftOrPrimIdx; vec3 aabbMax; uint primCountOrRightIdx; }` (8 floats / 32 bytes per node, cacheline-friendly on GPU).
- [ ] New `BVHBuffer` SSBO (binding 4 in the raytracer descriptor set). Uploaded each frame when `packGeometry` runs (geometry changes every frame due to physics). CPU BVH rebuild time target: < 0.5 ms for 1,000 primitives (SAH sweep is O(N log N) with early-out).
- [ ] GLSL traversal in `raytracer.comp` and `hybrid_lighting.comp`: replace the linear scan in `traceScene`/`traceShadow` with a stack-based BVH descent. Fixed-size register stack (`vec4 stack[32]`) avoids dynamic allocation. Inner loop: test both child AABBs, push farther child, descend into nearer. Leaf nodes: test all contained primitives.
- [ ] Shadow ray optimization: `traceShadow` exits immediately on first hit — no need to find the closest intersection. BVH gives the biggest speedup here since shadow rays are incoherent and O(N) shadow tracing is the dominant cost in scenes with many lights.
- [ ] Fallback: if BVH is disabled (a `#define USE_BVH 0` specialization constant), `raytracer.comp` falls back to the existing linear scan for correctness testing.

**Temporal Accumulation:**
- [ ] New `StorageImage` at full resolution (`rgba32f`, persistent across frames) — the history buffer. Not recreated each frame; cleared on camera jump or scene change.
- [ ] Accumulation counter: a `uint` per-pixel storage buffer (or packed into the history image alpha channel). Incremented each frame.
- [ ] Each frame: `outputPixel = mix(historyPixel, newSample, 1.0 / (accumCount + 1.0))`. When `accumCount` is large, the new sample has negligible weight and noise averages out.
- [ ] Reprojection: before blending, reproject the previous frame's pixel using the previous view-projection matrix (stored in a small UBO field `prevViewProj`). Sample history at the reprojected UV rather than the current pixel coordinate. This allows the history to remain valid under camera motion.
- [ ] Disocclusion handling: if the reprojected position's depth differs from the current G-buffer depth by more than a threshold, reset the accumulation counter for that pixel (treat it as newly visible). Without this, ghosting artifacts appear when objects move in front of previously occluded surfaces.
- [ ] Toggle: temporal accumulation is disabled in `RAYTRACE` mode by default (single-frame output is already acceptable for the original demo); enabled in HYBRID mode where it amortizes the GI bounce cost.

---

### Milestone 22 — Vulkan RT Extensions (Hardware Ray Tracing)
**Phase 2 — estimate ~60–80 hrs**

Replace the compute raytracer with the dedicated Vulkan ray tracing pipeline (`VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure`). This is a complete rewrite of the raytracing backend — the compute shader disappears; in its place are five shader stages and hardware-managed BVH traversal. On Apple Silicon, MoltenVK translates these extensions to Metal 3's `raytracing` API, which maps cleanly to the hardware RT units in M2+.

**Acceleration Structure:**
- [ ] Bottom-level acceleration structure (BLAS): one BLAS per unique `RenderMesh` geometry. Built from vertex + index buffers directly (no CPU copy needed — the GPU reads them via `VkAccelerationStructureGeometryTrianglesDataKHR`). For analytical primitives (spheres, quads), either use triangle approximation or procedural AABBs (`VK_GEOMETRY_TYPE_AABBS_KHR`) with a custom intersection shader.
- [ ] Top-level acceleration structure (TLAS): one instance per active `RenderMesh` with its current `modelMatrix` as the `VkTransformMatrixKHR`. Rebuilt every frame (dynamic scene). TLAS rebuild is O(N) with compaction hints; for N < 10,000 this is GPU-side fast enough for per-frame updates.
- [ ] `src/gpu/AccelStructure.h/.cpp`: RAII wrappers for BLAS/TLAS. `build()` records the build commands into a command buffer and submits. `update()` does an in-place update (faster than rebuild when geometry doesn't change topology).

**RT Pipeline + Shader Stages:**
- [ ] Ray generation shader (`shaders/rt_raygen.rgen`): equivalent to `raytracer.comp`'s `main()`. Calls `traceRayEXT` for primary ray; on hit, calls `traceRayEXT` again for shadows and reflections. No manual intersection loop — the hardware BVH handles traversal.
- [ ] Closest-hit shader (`shaders/rt_chit.rchit`): invoked when a ray hits geometry. Reads vertex data from a storage buffer indexed by `gl_PrimitiveID` + `gl_InstanceID`. Computes shading (diffuse, specular) and writes to a payload struct. For reflections, recursively calls `traceRayEXT` up to a `gl_MaxRecursionDepth` limit (typically 4–8).
- [ ] Miss shader (`shaders/rt_miss.rmiss`): invoked when no geometry is hit. Returns sky color (same gradient as current `raytracer.comp`).
- [ ] Shadow miss shader (`shaders/rt_shadow.rmiss`): a second miss shader for shadow rays. Returns `shadowed = false`. The closest-hit for shadow rays can be a trivial any-hit shader that sets `shadowed = true` and calls `terminateRayEXT`.
- [ ] Any-hit shader (`shaders/rt_ahit.rahit`): used for alpha-tested geometry (future transparency). For now, a pass-through.

**C++ Pipeline:**
- [ ] Feature detection at startup: query `VkPhysicalDeviceRayTracingPipelinePropertiesKHR`. If the extension is absent (older GPU, MoltenVK < 3.2), fall back to the compute raytracer silently. Log the capability at startup.
- [ ] Shader binding table (SBT): `VkStridedDeviceAddressRegionKHR` for raygen, miss, hit, callable groups. The SBT is a `Buffer` with aligned entries for each shader group.
- [ ] `Raytracer` class gains a `bool hwRT_` flag. When true, `dispatch()` calls `vkCmdTraceRaysKHR` instead of `vkCmdDispatch`. Geometry SSBO for `shade()` is replaced by the TLAS; light SSBO is reused unchanged.
- [ ] Output image binding unchanged — the RT raygen shader writes to the same `StorageImage` that the fullscreen blit reads.

**MoltenVK Notes:**
- `VK_KHR_acceleration_structure` and `VK_KHR_ray_tracing_pipeline` are supported from MoltenVK 1.2.8+ on M2 and later. M1 supports BLAS/TLAS but the RT pipeline maps to Metal's `MPSAccelerationStructure`; performance is acceptable but not as optimized as M2+.
- Procedural AABB intersection shaders require `VK_KHR_ray_query` for inline queries, which MoltenVK supports via Metal's `intersection_query`. Prefer triangle BLASes over procedural AABBs to avoid this complexity.
- Recursive `traceRayEXT` calls require `maxRecursionDepth > 0` in the pipeline create info. Metal limits this; if the driver rejects the limit, replace recursion with an iterative bounce loop in the raygen shader (same approach as the current compute raytracer's `for (int bounce = 0; ...)` loop).

---

## Timeline Summary

| Phase | Milestone | Area | Estimated Weeks | Cumulative |
|-------|-----------|------|-----------------|------------|
| **1** | 1 | Infrastructure + Math | 1–2 | Wk 2 |
| **1** | 2 | Window + Input | 2–3 | Wk 3 |
| **1** | 3 | Vulkan Bootstrap | 3–8 | Wk 8 |
| **1** | 4 | Core Rendering | 8–13 | Wk 13 |
| **1** | 5 | Asset Pipeline | 12–14 | Wk 14 |
| **1** | 6 | Physics Foundation | 14–19 | Wk 19 |
| **1** | 7 | Audio | 16–18 | Wk 19 |
| **1** | 8 | World + C++ Scripts | 18–21 | Wk 21 |
| **1** | 9 | UI Foundation | 20–23 | Wk 23 |
| **1** | 10 | Compute Raytracer | 22–25 | Wk 25 |
| **1** | 11 | Demo Port | 23–27 | Wk 27 |
| **2** | 12 | Editor Window | 28–36 | Wk 36 |
| **2** | 13 | ECS Architecture | 32–37 | Wk 37 |
| **2** | 14 | Python Scripting | 36–43 | Wk 43 |
| **2** | 15 | Material + Skybox | 38–43 | Wk 43 |
| **2** | 16 | Animation | 42–52 | Wk 52 |
| **2** | 17 | Physics Expansion | 44–60 | Wk 60 |
| **2** | 18 | UI Expansion | 50–56 | Wk 56 |
| **2** | 19 | Quality + Docs | Ongoing | — |
| **2** | 20 | Hybrid Render Mode | TBD | — |
| **2** | 21 | Raytracer Optimizations | TBD | — |
| **2** | 22 | Vulkan RT Extensions | TBD | — |

*Phase 1 completes at approximately Week 27 (~7 months at initial pace).*
*Phase 2 key milestones (Editor + ECS + Python + Material) complete around Week 43 (~11 months total).*
*Physics expansion and animation extend well into the second year — both are deliberately open-ended.*
*Phase 2 raytracing milestones (20–22) are sequenced: Hybrid → Optimizations → Hardware RT. Each builds on the previous.*

---

## Key Cross-Cutting Notes

**On AI-Assisted Development:** Use AI tools most aggressively for: Vulkan ceremony (instance/device/swapchain boilerplate), OBJ/glTF parser structure, GLSL shader porting, CMake configuration, test case generation, and documentation. Use AI conservatively and verify carefully for: GJK/EPA math (subtle sign errors), BVH invariant maintenance, Vulkan synchronization (semaphore/fence/barrier ordering), and Python/C++ lifetime management (pybind11 reference vs. copy semantics). These are the areas where AI-generated code tends to be confidently wrong.

**On Vulkan Debugging:** Enable validation layers unconditionally in debug builds from day one. Set up `VK_EXT_debug_utils` with a message callback that prints to the editor console (and crashes in debug on `VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT`). Never proceed to the next Vulkan sub-milestone with outstanding validation errors. This discipline prevents compounding synchronization bugs that are nearly impossible to debug later.

**On MoltenVK:** A small number of Vulkan features are unsupported or have behavioral differences under MoltenVK. Check the MoltenVK feature set table in the SDK when implementing compute (all required compute features are supported). The key limitation is that certain pipeline barrier combinations need explicit barriers that Nvidia/AMD drivers elide. Always test on Mac after implementing a new render pass or compute dispatch — not just on Windows.

**On the Horror Game:** When you reach Phase 2 scripting, `Architect.java` is the best reference for the horror game demo — spatial audio, a flashlight, entity tracking, and death screen are all there. The Python scripting + ECS combination makes implementing these behaviors significantly cleaner than the original.