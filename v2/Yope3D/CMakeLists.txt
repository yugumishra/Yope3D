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
# This block is what actually tells CMake the script exists and when to run it.
# ---------------------------------------------------------------------------

if(YOPE_EMBED_ASSETS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
    set(EMBED_CPP     "${GENERATED_DIR}/embedded_assets.cpp")
    set(EMBED_H       "${GENERATED_DIR}/embedded_assets.h")

    # Glob existing asset files so CMake reruns the script when any of them
    # change. Adding a brand-new file still requires a manual reconfigure
    # (CMake: Configure in VS Code) — standard CMake GLOB limitation.
    file(GLOB_RECURSE ASSET_FILES "${CMAKE_SOURCE_DIR}/assets/*")

    # add_custom_command ties the generated files to a build rule.
    # CMake will run the Python script whenever DEPENDS is out of date,
    # and treat EMBED_CPP/EMBED_H as real outputs that can be compiled.
    # Note: ${COMPILED_SHADERS} is the list of shader targets created earlier,
    # so the embed script will run after shaders are compiled.
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

    # A static library gives the generated .cpp its own compile step and
    # exposes the generated/ directory as an include path to dependents.
    add_library(embedded_assets STATIC "${EMBED_CPP}")
    target_include_directories(embedded_assets PUBLIC "${GENERATED_DIR}")
endif()

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------

set(PHYSICS_SOURCES
    src/physics/Hull.cpp
    src/physics/CSphere.cpp
    src/physics/CAABB.cpp
    src/physics/COBB.cpp
    src/physics/BarrierHull.cpp
    src/physics/BroadphaseSAP.cpp
    src/physics/ColliderCCD.cpp
    src/physics/ColliderDiscrete.cpp
    src/physics/IslandDetector.cpp
    src/physics/ThreadPool.cpp
    src/physics/Spring.cpp
    src/physics/Raycast.cpp
)

add_executable(yope3d
    src/main.cpp
    src/platform/Window.cpp
    src/platform/Input.cpp
    src/Engine.cpp
    src/math/MathImpl.cpp
    src/stb_impl.cpp               # owns STB_IMAGE_IMPLEMENTATION
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
    src/rendering/Camera.cpp
    src/rendering/Light.cpp
    src/world/World.cpp
    src/world/RenderMesh.cpp
    src/assets/AssetManager.cpp
    src/assets/ObjLoader.cpp
    src/assets/ImageLoader.cpp
    src/assets/AudioLoader.cpp
    src/assets/Primitives.cpp
    ${PHYSICS_SOURCES}
)

add_dependencies(yope3d compile_shaders)

target_include_directories(yope3d PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${Stb_INCLUDE_DIR}"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:${CMAKE_BINARY_DIR}>  # adds build root so
                                                          # "generated/embedded_assets.h"
                                                          # resolves correctly
)

# ensures the script runs and the header exists before Window.cpp compiles
if(YOPE_EMBED_ASSETS)
    add_dependencies(yope3d embedded_assets)
endif()

target_compile_definitions(yope3d PRIVATE
    YOPE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    YOPE_SHADER_DIR="${CMAKE_SOURCE_DIR}/compiled_shaders"
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:YOPE_EMBED_ASSETS>  # no space after colon
)

target_link_libraries(yope3d PRIVATE
    glfw
    freetype
    OpenAL::OpenAL
    Vulkan::Vulkan
    $<$<BOOL:${YOPE_EMBED_ASSETS}>:embedded_assets>    # only linked when embedding
)

# ---------------------------------------------------------------------------
# Test executable
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
    ${PHYSICS_SOURCES}
)

target_include_directories(yope_physics_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(yope_physics_tests PRIVATE Catch2::Catch2WithMain)

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