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
    add_custom_command(
        OUTPUT  "${EMBED_CPP}" "${EMBED_H}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/embed_assets.py"
                "${CMAKE_SOURCE_DIR}/assets"
                "${GENERATED_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/embed_assets.py" ${ASSET_FILES}
        COMMENT "Embedding assets into binary"
    )

    # A static library gives the generated .cpp its own compile step and
    # exposes the generated/ directory as an include path to dependents.
    add_library(embedded_assets STATIC "${EMBED_CPP}")
    target_include_directories(embedded_assets PUBLIC "${GENERATED_DIR}")
endif()

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------

add_executable(yope3d
    src/main.cpp
    src/platform/Window.cpp
    src/platform/Input.cpp
    src/Engine.cpp
    src/math/MathImpl.cpp
    src/stb_impl.cpp               # owns STB_IMAGE_IMPLEMENTATION
)

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