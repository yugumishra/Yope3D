cmake_minimum_required(VERSION 3.25)
project(Yope3D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(glfw3    CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(OpenAL   CONFIG REQUIRED)
find_package(Catch2   CONFIG REQUIRED)
find_package(Vulkan   REQUIRED)
find_package(Stb      REQUIRED)         # header-only; provides Stb_INCLUDE_DIR
find_package(imgui    CONFIG REQUIRED)  # docking-experimental + vulkan-binding + glfw-binding
find_package(imguizmo CONFIG REQUIRED)
find_package(pybind11 CONFIG REQUIRED)  # embedded Python scripting
find_package(nfd     CONFIG REQUIRED)  # native file dialogs (editor only)

# ---------------------------------------------------------------------------
# Shader compilation — GLSL → SPIR-V via glslc (bundled in the Vulkan SDK)
# ---------------------------------------------------------------------------

find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)

set(SHADER_SRC_DIR      "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_COMPILED_DIR "${CMAKE_SOURCE_DIR}/compiled_shaders")
file(MAKE_DIRECTORY "${SHADER_COMPILED_DIR}")

set(SHADER_SOURCES
    "${SHADER_SRC_DIR}/triangle.vert"
    "${SHADER_SRC_DIR}/triangle.frag"
    "${SHADER_SRC_DIR}/ui.vert"
    "${SHADER_SRC_DIR}/ui.frag"
    "${SHADER_SRC_DIR}/text3d.vert"
    "${SHADER_SRC_DIR}/text3d.frag"
    "${SHADER_SRC_DIR}/line3d.vert"
    "${SHADER_SRC_DIR}/line3d.frag"
    "${SHADER_SRC_DIR}/stroke.vert"
    "${SHADER_SRC_DIR}/stroke.frag"
    "${SHADER_SRC_DIR}/skybox.vert"
    "${SHADER_SRC_DIR}/skybox.frag"
    "${SHADER_SRC_DIR}/raytracer.comp"
    "${SHADER_SRC_DIR}/fullscreen.vert"
    "${SHADER_SRC_DIR}/fullscreen.frag"
    "${SHADER_SRC_DIR}/id_buffer.vert"
    "${SHADER_SRC_DIR}/id_buffer.frag"
    "${SHADER_SRC_DIR}/id_buffer_ui.vert"
    "${SHADER_SRC_DIR}/id_buffer_ui.frag"
    "${SHADER_SRC_DIR}/shadow_depth.vert"
)

set(COMPILED_SHADERS "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV "${SHADER_COMPILED_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT  "${SPV}"
        COMMAND "${GLSLC}" "${SHADER}" -o "${SPV}"
        DEPENDS "${SHADER}"
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND COMPILED_SHADERS "${SPV}")
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADERS})

# ---------------------------------------------------------------------------
# MSDF font atlases — generated OFFLINE by the in-repo baker (tools/msdf_bake.cpp,
# uses the msdfgen library). The output PNG+JSON live in assets/fonts/generated/
# and are COMMITTED; the engine only loads them (see src/ui/TextAtlas). The bake
# is a developer-invoked regeneration step, opt-in via -DYOPE_BAKE_FONTS=ON:
#     cmake --preset mac-debug -DYOPE_BAKE_FONTS=ON
#     cmake --build build/mac-debug --target bake_fonts
# Nothing in the normal build depends on the baker, so msdfgen is never required
# for an ordinary compile (the committed atlases are picked up by GLOB embed).
# ---------------------------------------------------------------------------

option(YOPE_ENABLE_PROFILER "Enable Profiler instrumentation (writes timestamped CSVs each run)" OFF)

option(YOPE_BAKE_FONTS "Build the MSDF baker and regenerate font atlases" OFF)
set(YOPE_MSDF_PX_RANGE  4  CACHE STRING "MSDF distance range in atlas texels")
set(YOPE_MSDF_BAKE_SIZE 48 CACHE STRING "MSDF glyph bake size (pixels per em)")

if(YOPE_BAKE_FONTS)
    find_package(msdfgen CONFIG REQUIRED)

    add_executable(msdf_bake tools/msdf_bake.cpp)
    target_include_directories(msdf_bake PRIVATE "${Stb_INCLUDE_DIR}")
    target_link_libraries(msdf_bake PRIVATE msdfgen::msdfgen-core msdfgen::msdfgen-ext)

    set(FONT_BAKE_LIST monaco nunito_sans nunito_sans_bold)
    set(GEN_DIR "${CMAKE_SOURCE_DIR}/assets/fonts/generated")
    file(MAKE_DIRECTORY "${GEN_DIR}")

    set(BAKED_ATLASES "")
    foreach(F ${FONT_BAKE_LIST})
        add_custom_command(
            OUTPUT  "${GEN_DIR}/${F}.png" "${GEN_DIR}/${F}.json"
            COMMAND msdf_bake
                    "${CMAKE_SOURCE_DIR}/assets/fonts/${F}.ttf"
                    "${GEN_DIR}/${F}.png" "${GEN_DIR}/${F}.json"
                    ${YOPE_MSDF_BAKE_SIZE} ${YOPE_MSDF_PX_RANGE}
            DEPENDS msdf_bake "${CMAKE_SOURCE_DIR}/assets/fonts/${F}.ttf"
            COMMENT "Baking MSDF atlas for ${F}"
        )
        list(APPEND BAKED_ATLASES "${GEN_DIR}/${F}.png" "${GEN_DIR}/${F}.json")
    endforeach()

    add_custom_target(bake_fonts DEPENDS ${BAKED_ATLASES})
endif()

# ---------------------------------------------------------------------------
# Embed option — must be declared before the block that uses it
# ---------------------------------------------------------------------------

option(YOPE_EMBED_ASSETS "Embed assets into the binary" OFF)

# ---------------------------------------------------------------------------
# Embed mode: run tools/embed_assets.py to bake assets/ into a .cpp
# ---------------------------------------------------------------------------

if(YOPE_EMBED_ASSETS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
    set(EMBED_CPP     "${GENERATED_DIR}/embedded_assets.cpp")
    set(EMBED_H       "${GENERATED_DIR}/embedded_assets.h")

    file(GLOB_RECURSE ASSET_FILES "${CMAKE_SOURCE_DIR}/assets/*")

    add_custom_command(
        OUTPUT  "${EMBED_CPP}" "${EMBED_H}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/embed_assets.py"
                "${CMAKE_SOURCE_DIR}/assets"
                "${CMAKE_SOURCE_DIR}/compiled_shaders"
                "${GENERATED_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/embed_assets.py" ${ASSET_FILES} ${COMPILED_SHADERS}
        COMMENT "Embedding assets and shaders into binary"
    )

    add_library(embedded_assets STATIC "${EMBED_CPP}")
    target_include_directories(embedded_assets PUBLIC "${GENERATED_DIR}")
endif()

# ---------------------------------------------------------------------------
# Physics sources (also used by headless test targets)
# ---------------------------------------------------------------------------

set(PHYSICS_SOURCES
    src/ecs/Registry.cpp
    src/physics/BroadphaseSAP.cpp
    src/physics/ColliderSupports.cpp
    src/physics/ColliderAnalytical.cpp
    src/physics/ColliderGJK.cpp
    src/physics/ColliderDiscrete.cpp
    src/physics/CompoundShape.cpp
    src/physics/IslandDetector.cpp
    src/physics/ThreadPool.cpp
    src/physics/Spring.cpp
    src/physics/Raycast.cpp
    src/physics/KinematicQuery.cpp
)

# ---------------------------------------------------------------------------
# Engine sources — shared between yope3d (runtime) and yope_editor
# ---------------------------------------------------------------------------

set(ENGINE_SOURCES
    src/ecs/Registry.cpp
    src/platform/Window.cpp
    src/platform/Input.cpp
    src/platform/BundlePaths.cpp
    src/Engine.cpp
    src/math/MathImpl.cpp
    src/stb_impl.cpp
    src/gpu/VulkanInstance.cpp
    src/gpu/Surface.cpp
    src/gpu/PhysicalDevice.cpp
    src/gpu/LogicalDevice.cpp
    src/gpu/GpuDevice.cpp
    src/gpu/Swapchain.cpp
    src/gpu/DepthBuffer.cpp
    src/gpu/ShaderModule.cpp
    src/gpu/Buffer.cpp
    src/gpu/RenderPass.cpp
    src/gpu/DescriptorSetLayout.cpp
    src/gpu/DescriptorPool.cpp
    src/gpu/UniformBuffer.cpp
    src/gpu/StorageBuffer.cpp
    src/gpu/Texture.cpp
    src/rendering/Renderer.cpp
    src/rendering/Raytracer.cpp
    src/rendering/MeshBuild.cpp
    src/rendering/MaterialCache.cpp
    src/rendering/Skybox.cpp
    src/rendering/ShadowMap.cpp
    src/rendering/Camera.cpp
    src/rendering/Light.cpp
    src/gpu/StorageImage.cpp
    src/gpu/ComputePipeline.cpp
    src/world/World.cpp
    src/world/RenderMesh.cpp
    src/world/ColliderBaker.cpp
    src/assets/AssetManager.cpp
    src/assets/ObjLoader.cpp
    src/assets/GltfLoader.cpp
    src/assets/ImageLoader.cpp
    src/assets/TextureStreamer.cpp
    src/assets/AudioLoader.cpp
    src/assets/Primitives.cpp
    src/audio/AudioSystem.cpp
    src/audio/Source.cpp
    src/audio/Listener.cpp
    src/rendering/CameraController.cpp
    src/scripting/ScriptFactory.cpp
    src/scripting/Config.cpp
    src/scripting/EmptyScript.cpp
    src/scene/ComponentSnapshot.cpp
    src/scene/SceneManager.cpp
    src/scene/serialization/JsonWriter.cpp
    src/scene/serialization/JsonParser.cpp
    src/scene/serialization/ComponentSerializers.cpp
    src/scene/serialization/SceneSerializer.cpp
    src/debug/Profiler.cpp
    src/debug/Console.cpp
    src/debug/TaskProgress.cpp
    # Demo scripts (SandboxScript, PlatformerScript, ArchitectScript) used the
    # pre-M13 single-script model. Dropped from the build; their .cpp/.h files
    # remain on disk as reference for re-authoring scene-driven equivalents.
    src/gpu/UIBuffer.cpp
    src/gpu/Text3DBuffer.cpp
    src/gpu/LineBuffer.cpp
    src/ui/Background.cpp
    src/ui/TexturedBackground.cpp
    src/ui/AnimatedBackground.cpp
    src/ui/CurvedBackground.cpp
    src/ui/TextAtlas.cpp
    src/ui/TextBox.cpp
    src/ui/UIManager.cpp
    src/ui/UIInput.cpp
    ${PHYSICS_SOURCES}
)

# Editor-only sources — grows as src/editor/ is implemented
set(EDITOR_SOURCES
    src/editor/Selection.cpp
    src/editor/CommandHistory.cpp
    src/editor/EditorApp.cpp
    src/editor/ImGuiBackend.cpp
    src/editor/EditorTheme.cpp
    src/rendering/ViewportTarget.cpp
    src/editor/commands/EntityLifecycleCommands.cpp
    src/editor/commands/AddColliderCommand.cpp
    src/editor/commands/GenerateColliderCommand.cpp
    src/editor/commands/ReparentCommand.cpp
    src/editor/commands/SetUIParentCommand.cpp
    src/editor/commands/RemoveColliderCommand.cpp
    src/editor/commands/AddSpringConstraintCommand.cpp
    src/editor/commands/AddPointJointCommand.cpp
    src/editor/commands/AddHingeJointCommand.cpp
    src/editor/commands/AddConeTwistJointCommand.cpp
    src/editor/commands/SetAssetCommands.cpp
    src/editor/inspectors/InspectorRegistry.cpp
    src/editor/inspectors/TransformInspector.cpp
    src/editor/inspectors/HullInspector.cpp
    src/editor/inspectors/MeshRendererInspector.cpp
    src/editor/inspectors/MaterialInspector.cpp
    src/editor/inspectors/ShapeFormInspector.cpp
    src/editor/inspectors/CompoundColliderInspector.cpp
    src/editor/inspectors/LightSourceInspector.cpp
    src/editor/inspectors/NameInspector.cpp
    src/editor/inspectors/AudioSourceInspector.cpp
    src/editor/inspectors/SpringConstraintInspector.cpp
    src/editor/inspectors/PointJointInspector.cpp
    src/editor/inspectors/HingeJointInspector.cpp
    src/editor/inspectors/ConeTwistJointInspector.cpp
    src/editor/inspectors/ParentInspector.cpp
    src/editor/inspectors/UITransformInspector.cpp
    src/editor/inspectors/UIBackgroundInspector.cpp
    src/editor/inspectors/UIButtonInspector.cpp
    src/editor/picking/IdBufferPass.cpp
    src/platform/FileWatcher.cpp
    src/editor/panels/ViewportPanel.cpp
    src/editor/panels/HierarchyPanel.cpp
    src/editor/panels/InspectorPanel.cpp
    src/editor/panels/AssetBrowserPanel.cpp
    src/editor/panels/StatsPanel.cpp
    src/editor/panels/WorldSettingsPanel.cpp
    src/editor/panels/ConsolePanel.cpp
    src/editor/panels/SceneScriptPanel.cpp
    src/editor/panels/GJKTestPanel.cpp
    src/editor/panels/GJKStepperPanel.cpp
    src/editor/inspectors/ScriptComponentInspector.cpp
    src/editor/FileDialog.cpp
)

# Python scripting sources — compiled into both runtime and editor targets
set(PYTHON_SOURCES
    src/scripting/python/PyComponentTable.cpp
    src/scripting/python/PythonInterpreter.cpp
    src/scripting/python/PythonScript.cpp
    src/scripting/python/BehaviorRegistry.cpp
    src/scripting/python/Bindings.cpp
    src/scripting/python/bindings_math.cpp
    src/scripting/python/bindings_ecs.cpp
    src/scripting/python/bindings_world.cpp
)

# ---------------------------------------------------------------------------
# Runtime executable (unchanged from before ENGINE_SOURCES refactor)
# ---------------------------------------------------------------------------

add_executable(yope3d src/main.cpp ${ENGINE_SOURCES} ${PYTHON_SOURCES})

add_dependencies(yope3d compile_shaders)

target_include_directories(yope3d PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${Stb_INCLUDE_DIR}"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:${CMAKE_BINARY_DIR}>
)

if(YOPE_EMBED_ASSETS)
    add_dependencies(yope3d embedded_assets)
endif()

target_compile_definitions(yope3d PRIVATE
    YOPE_PYTHON=1
    YOPE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    YOPE_SHADER_DIR="${CMAKE_SOURCE_DIR}/compiled_shaders"
    YOPE_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/scripts"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:YOPE_EMBED_ASSETS>
    $<$<BOOL:${YOPE_ENABLE_PROFILER}>:YOPE_PROF_ENABLED>
)

target_link_libraries(yope3d PRIVATE
    glfw
    freetype
    OpenAL::OpenAL
    Vulkan::Vulkan
    pybind11::embed
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:embedded_assets>
)

# ---------------------------------------------------------------------------
# Editor executable — same engine sources + editor overlay + ImGui
# Excluded from release builds by default (set YOPE_BUILD_EDITOR=ON to override).
# Build: cmake --build build/mac-debug --target yope_editor
# Run:   ./build/mac-debug/yope_editor
# ---------------------------------------------------------------------------

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    option(YOPE_BUILD_EDITOR "Build the editor executable" OFF)
else()
    option(YOPE_BUILD_EDITOR "Build the editor executable" ON)
endif()

if(YOPE_BUILD_EDITOR)
add_executable(yope_editor src/main_editor.cpp ${ENGINE_SOURCES} ${EDITOR_SOURCES} ${PYTHON_SOURCES})

add_dependencies(yope_editor compile_shaders)

target_include_directories(yope_editor PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${Stb_INCLUDE_DIR}"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:${CMAKE_BINARY_DIR}>
)

if(YOPE_EMBED_ASSETS)
    add_dependencies(yope_editor embedded_assets)
endif()

target_compile_definitions(yope_editor PRIVATE
    YOPE_EDITOR=1
    YOPE_PYTHON=1
    YOPE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    YOPE_SHADER_DIR="${CMAKE_SOURCE_DIR}/compiled_shaders"
    YOPE_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/scripts"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:YOPE_EMBED_ASSETS>
    $<$<BOOL:${YOPE_ENABLE_PROFILER}>:YOPE_PROF_ENABLED>
)

target_link_libraries(yope_editor PRIVATE
    glfw
    freetype
    OpenAL::OpenAL
    Vulkan::Vulkan
    imgui::imgui
    imguizmo::imguizmo
    pybind11::embed
    nfd::nfd
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:embedded_assets>
)
if(APPLE)
    find_library(CoreServices_FRAMEWORK CoreServices REQUIRED)
    target_link_libraries(yope_editor PRIVATE ${CoreServices_FRAMEWORK})
endif()
endif() # YOPE_BUILD_EDITOR

# ---------------------------------------------------------------------------
# macOS app bundle
# Generates cmake/Info.plist at configure time and adds a 'bundle' target that
# runs tools/make_bundle.sh to assemble the standalone .app.
# Usage: cmake --build build/mac-release --target bundle
# ---------------------------------------------------------------------------

if(APPLE)
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        "${CMAKE_SOURCE_DIR}/cmake/Info.plist"
        @ONLY
    )

    add_custom_target(bundle
        COMMAND "${CMAKE_SOURCE_DIR}/tools/make_bundle.sh"
                "${CMAKE_BINARY_DIR}/yope3d"
                "${CMAKE_BINARY_DIR}"
        DEPENDS yope3d
        COMMENT "Assembling Yope3D.app"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------

add_executable(yope_tests
    tests/math_tests.cpp
    src/math/MathImpl.cpp
)

target_include_directories(yope_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(yope_tests PRIVATE Catch2::Catch2WithMain)

# ---------------------------------------------------------------------------
# Physics test executable — no Vulkan/GLFW/OpenAL dependency
# ---------------------------------------------------------------------------

add_executable(yope_physics_tests
    tests/physics_tests.cpp
    src/math/MathImpl.cpp
    src/debug/Profiler.cpp
    ${PHYSICS_SOURCES}
)

target_include_directories(yope_physics_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(yope_physics_tests PRIVATE
    $<$<BOOL:${YOPE_ENABLE_PROFILER}>:YOPE_PROF_ENABLED>
)
target_link_libraries(yope_physics_tests PRIVATE Catch2::Catch2WithMain)

# ---------------------------------------------------------------------------
# glTF loader test — headless (loader is decoupled from GPU via a callback).
# Needs Vulkan headers only (RenderMesh.h includes them), not the Vulkan lib.
# ---------------------------------------------------------------------------

add_executable(yope_gltf_tests
    tests/gltf_tests.cpp
    src/assets/GltfLoader.cpp
    src/scene/serialization/JsonParser.cpp
    src/math/MathImpl.cpp
)
target_include_directories(yope_gltf_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(yope_gltf_tests PRIVATE YOPE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_link_libraries(yope_gltf_tests PRIVATE Catch2::Catch2WithMain Vulkan::Vulkan)

# ---------------------------------------------------------------------------
# ECS test executable — no Vulkan/GLFW/OpenAL dependency
# ---------------------------------------------------------------------------

add_executable(yope_ecs_tests
    tests/ecs_tests.cpp
    src/ecs/Registry.cpp
    src/math/MathImpl.cpp
)

target_include_directories(yope_ecs_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(yope_ecs_tests PRIVATE Catch2::Catch2WithMain)

# ---------------------------------------------------------------------------
# Package version test — verifies external dependencies are linked correctly
# ---------------------------------------------------------------------------

add_executable(package_version_tests
    tests/package_version_tests.cpp
    src/stb_impl.cpp
)

target_include_directories(package_version_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${Stb_INCLUDE_DIR}"
)

target_link_libraries(package_version_tests PRIVATE
    glfw
    freetype
    OpenAL::OpenAL
)
