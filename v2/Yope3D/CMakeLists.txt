cmake_minimum_required(VERSION 3.25)
project(Yope3D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Dependencies
find_package(glfw3 CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

# Source files (Start small)
add_executable(yope3d 
    src/main.cpp
    src/math/MathImpl.cpp
)

# Link Libraries
target_link_libraries(yope3d PRIVATE 
    glfw 
    freetype 
    OpenAL::OpenAL
)

# Test Executable
add_executable(yope_tests 
    tests/math_tests.cpp
    src/math/MathImpl.cpp
)
target_link_libraries(yope_tests PRIVATE Catch2::Catch2WithMain)