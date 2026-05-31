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
    "${SHADER_SRC_DIR}/raytracer.comp"
    "${SHADER_SRC_DIR}/fullscreen.vert"
    "${SHADER_SRC_DIR}/fullscreen.frag"
    "${SHADER_SRC_DIR}/id_buffer.vert"
    "${SHADER_SRC_DIR}/id_buffer.frag"
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
    src/physics/ColliderDiscrete.cpp
    src/physics/IslandDetector.cpp
    src/physics/ThreadPool.cpp
    src/physics/Spring.cpp
    src/physics/Raycast.cpp
)

# ---------------------------------------------------------------------------
# Engine sources — shared between yope3d (runtime) and yope_editor
# ---------------------------------------------------------------------------

set(ENGINE_SOURCES
    src/ecs/Registry.cpp
    src/platform/Window.cpp
    src/platform/Input.cpp
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
    src/rendering/Camera.cpp
    src/rendering/Light.cpp
    src/gpu/StorageImage.cpp
    src/gpu/ComputePipeline.cpp
    src/world/World.cpp
    src/world/RenderMesh.cpp
    src/assets/AssetManager.cpp
    src/assets/ObjLoader.cpp
    src/assets/ImageLoader.cpp
    src/assets/AudioLoader.cpp
    src/assets/Primitives.cpp
    src/audio/AudioSystem.cpp
    src/audio/Source.cpp
    src/audio/Listener.cpp
    src/rendering/CameraController.cpp
    src/scripting/ScriptFactory.cpp
    src/scripting/Config.cpp
    src/scripting/SandboxScript.cpp
    src/scripting/PlatformerScript.cpp
    src/scripting/ArchitectScript.cpp
    src/debug/Profiler.cpp
    src/gpu/UIBuffer.cpp
    src/ui/Background.cpp
    src/ui/TexturedBackground.cpp
    src/ui/AnimatedBackground.cpp
    src/ui/CurvedBackground.cpp
    src/ui/TextAtlas.cpp
    src/ui/TextBox.cpp
    src/ui/UIManager.cpp
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
    src/editor/commands/ComponentSnapshot.cpp
    src/editor/commands/EntityLifecycleCommands.cpp
    src/editor/commands/AddColliderCommand.cpp
    src/editor/commands/RemoveColliderCommand.cpp
    src/editor/commands/AddSpringConstraintCommand.cpp
    src/editor/inspectors/InspectorRegistry.cpp
    src/editor/inspectors/TransformInspector.cpp
    src/editor/inspectors/HullInspector.cpp
    src/editor/inspectors/MeshRendererInspector.cpp
    src/editor/inspectors/ShapeFormInspector.cpp
    src/editor/inspectors/LightSourceInspector.cpp
    src/editor/inspectors/NameInspector.cpp
    src/editor/inspectors/AudioSourceInspector.cpp
    src/editor/inspectors/SpringConstraintInspector.cpp
    src/editor/picking/IdBufferPass.cpp
    src/editor/serialization/JsonWriter.cpp
    src/editor/serialization/JsonParser.cpp
    src/editor/serialization/ComponentSerializers.cpp
    src/editor/serialization/SceneSerializer.cpp
    src/platform/FileWatcher.cpp
    src/editor/panels/ViewportPanel.cpp
    src/editor/panels/HierarchyPanel.cpp
    src/editor/panels/InspectorPanel.cpp
    src/editor/panels/AssetBrowserPanel.cpp
    src/editor/panels/StatsPanel.cpp
    src/editor/panels/WorldSettingsPanel.cpp
    src/editor/panels/ConsolePanel.cpp
)

# ---------------------------------------------------------------------------
# Runtime executable (unchanged from before ENGINE_SOURCES refactor)
# ---------------------------------------------------------------------------

add_executable(yope3d src/main.cpp ${ENGINE_SOURCES})

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
    YOPE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    YOPE_SHADER_DIR="${CMAKE_SOURCE_DIR}/compiled_shaders"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:YOPE_EMBED_ASSETS>
)

target_link_libraries(yope3d PRIVATE
    glfw
    freetype
    OpenAL::OpenAL
    Vulkan::Vulkan
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:embedded_assets>
)

# ---------------------------------------------------------------------------
# Editor executable — same engine sources + editor overlay + ImGui
# Build: cmake --build build/mac-debug --target yope_editor
# Run:   ./build/mac-debug/yope_editor
# ---------------------------------------------------------------------------

add_executable(yope_editor src/main_editor.cpp ${ENGINE_SOURCES} ${EDITOR_SOURCES})

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
    YOPE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    YOPE_SHADER_DIR="${CMAKE_SOURCE_DIR}/compiled_shaders"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:YOPE_EMBED_ASSETS>
)

target_link_libraries(yope_editor PRIVATE
    glfw
    freetype
    OpenAL::OpenAL
    Vulkan::Vulkan
    imgui::imgui
    imguizmo::imguizmo
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:embedded_assets>
)
if(APPLE)
    find_library(CoreServices_FRAMEWORK CoreServices REQUIRED)
    target_link_libraries(yope_editor PRIVATE ${CoreServices_FRAMEWORK})
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
target_link_libraries(yope_physics_tests PRIVATE Catch2::Catch2WithMain)

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
