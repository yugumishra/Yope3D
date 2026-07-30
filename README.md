# Yope3D

A C++20 Vulkan game engine from scratch — physics, 3D rendering, scripting, UI, and an in-house ImGui editor. Initially an old Java engine.

Solo project, in active development, not yet release-ready. 

## Features

**Physics**
- A 240 Hz fixed timestep rigid body simulation independent of rendering, on its own thread
- An acceleration structure with sweep-and-prune as the broadphase
- An SAT-based narrowphase with clipping for rich contact manifolds and feature ID'ing for solver warm starting
- A parallelizable (island splitting based) Projected Gauss Seidel solver with lambda warm starting and body sleeping
- Various constraint types with regular contacts as well as joints (supported: hinge, cone-twist, point-to-point, suspension, wheel friction) allowing for ragdolls and driven vehicles
- Hookean springs, enabling ropes, cloths, and soft body lite.
- Static and Dynamic Compound Colliders with baking to accelerate
- Collision layers support with masks (natural collision filtering) alongside collision enter/exit events carrying critical contact info
- Built-in query support for raycasts, capsule-casts, and overlaps
- GJK/EPA narrowphase pipeline for capsules/cylinders *(in development)*

**Entity, Component, System Architecture (The Data Oriented Design)**
- An Archetype-based registry with contiguous component storage, generational entity handles, tagged components: yielding extremely fast iteration over archetypes and wonderful data locality
- What's in the DOD Registry? everything (Physics, UI, Meshes, Scripts). Everything is fast and you don't chase ANY pointers

**Rendering Capabilities**
- Vulkan based backend, with raster and compute-raytrace modes
- Forward renderer with 32 Byte Vertices while supporting Cook-Torrance Physically Based Rendering (PBR) with full material maps
- Shadow mapping (directional/spot + point-light cubemaps); skyboxes
- Rigid animation clips; world-space debug-line pipeline

**Scripting**
- Per-entity Python behaviors via an embedded pybind11 module, with params loaded into the Editor GUI
- A fully typed stub so an IDE autocompletes for the entire engine surface
- Access to callbacks for collision & UI events, as well as object lifecycle

**Editor** (debug builds)
- Inspectors for every component; undo/redos
- glTF 2.0 and OBJ import alongside drop-in primitives and automatic collider fitters (primitives as well as compounds on glTF imports)
- An entire 3D viewport with a freely manipulatable camera for scene authoring and Play/Stop to demo scenes quickly
- Easy object selection via mouse picking in the viewport
- Easily parseable JSON scene format (reloadable) & entity templates
- Async scene loading with an animated splash; cross-scene handoff payloads

**Misc**
- UI with input routing (hover / press / click, text input), anchoring as well as pixel layouts, built-in fades
- MSDF text rendering in both 2D UI and 3D world space
- OpenAL 3D spatial audio with Doppler, streamed music with fades; Music/SFX/Voice buses

**Tooling & tests**
- In-engine profiler with CSV export for pandas analysis and a stage-by-stage analyzer
- Standalone macOS `.app` bundling
- 6 Catch2 test suites (math, physics, ECS, glTF, text, packaging), most fully headless

## Build

Requires Vulkan SDK (set `VULKAN_SDK` environment variable), vcpkg (set `VCPKG_ROOT` environment variable), Ninja, and CMake (3.25+).

First clone the repository:

```bash
git clone https://github.com/yugumishra/Yope3D.git
```

Then:

```bash
cd Yope3D/v2/Yope3D
```

Inside, run these to build for a set configuration:

```bash
cmake --preset mac-debug                              # set a configuration (options: win-release, win-debug, mac-release, mac-debug)
cmake --build build/mac-debug --config Debug          # build the project
```

To run the various executables built:

```bash
./build/mac-debug/yope3d              # main runtime
./build/mac-debug/yope_editor         # editor (dev side)
```

If using a different configuration than mac-debug, slot in that configuration (ex: `./build/win-release/yope3d`) — release presets also need `--config Release` in the build command.

To load a specific scene in scenes, use the `--scene` argument (or change the value in `yope3d.cfg`).

To load the ragdoll demonstration from `assets/scenes/`:

```bash
./build/mac-debug/yope3d --scene scenes/ragdollDemo.json
```

Once again, slot in your built configuration if not using mac-debug (ex: `./build/win-release/yope3d --scene scenes/ragdollDemo.json`).

Switching configurations requires a build/rebuild. Note `yope_editor` is included in debug configurations only.

## Repository layout

| Path | Contents |
|---|---|
| `v2/Yope3D/` | The current engine |
| `project/` | The original Java/LWJGL engine |

## Future Plans

Now: GJK/EPA narrowphase, headless execution, and ensuring determinism (scene replay).

## Development Notes
Yope3D was built by me, using AI coding tools. All changes: architectural, design, optics, etc, are reviewed and understood before they land. Any and all additions also run through the 6-test gauntlet and performance profiler, who really decide whether it stays or goes.

## License
See [LICENSE](LICENSE).
