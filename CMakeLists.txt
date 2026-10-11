cmake_minimum_required(VERSION 3.20)
project(MinecraftCpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a release build: an unset build type means no optimization at all,
# which makes world generation and meshing misleadingly slow.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The vanilla resource pack lives next to the code. The folder name contains
# spaces, so it is passed through quoted compile definitions below.
set(MC_PACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Java Default Textures 1.21.7 - 1.21.8"
    CACHE PATH "Vanilla resource pack root (the folder that contains assets/)")
set(MC_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(MC_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

# ---------------------------------------------------------------- dependencies

# All system packages. OpenGL entry points are resolved at runtime by
# src/gfx/GL.cpp, so neither a GL loader library nor libGL is linked.
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(PNG REQUIRED)
find_package(zstd REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest)

set(MC_WARNINGS
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers>)

# -------------------------------------------------------------- mc_common
# Everything that must not touch OpenGL: core, resources, world, entity, game.
# Unit tests link only this library, so simulation code stays testable headless.

file(GLOB_RECURSE MC_COMMON_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp src/resources/*.cpp src/world/*.cpp src/entity/*.cpp src/game/*.cpp)

add_library(mc_common STATIC ${MC_COMMON_SOURCES})
target_include_directories(mc_common PUBLIC src)
target_link_libraries(mc_common PUBLIC
    glm::glm nlohmann_json::nlohmann_json PNG::PNG zstd::libzstd Threads::Threads)
target_compile_definitions(mc_common PUBLIC
    "MC_DEFAULT_PACK_DIR=\"${MC_PACK_DIR}\""
    "MC_DEFAULT_DATA_DIR=\"${MC_DATA_DIR}\""
    "MC_DEFAULT_SHADER_DIR=\"${MC_SHADER_DIR}\""
    GLM_ENABLE_EXPERIMENTAL)
target_compile_options(mc_common PRIVATE ${MC_WARNINGS})
target_precompile_headers(mc_common PRIVATE src/core/Pch.h)

# -------------------------------------------------------------- mc_client
# Window, GL, rendering and UI on top of mc_common.

file(GLOB_RECURSE MC_CLIENT_SOURCES CONFIGURE_DEPENDS
    src/platform/*.cpp src/gfx/*.cpp src/render/*.cpp src/ui/*.cpp src/app/*.cpp)
list(REMOVE_ITEM MC_CLIENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp)

add_library(mc_client STATIC ${MC_CLIENT_SOURCES})
target_link_libraries(mc_client PUBLIC mc_common glfw)
# GL.h declares its own entry points; GLFW must not pull in a system GL header.
target_compile_definitions(mc_client PUBLIC GLFW_INCLUDE_NONE)
target_compile_options(mc_client PRIVATE ${MC_WARNINGS})
target_precompile_headers(mc_client PRIVATE src/core/Pch.h)

add_executable(minecraft src/app/main.cpp)
target_link_libraries(minecraft PRIVATE mc_client)
target_compile_options(minecraft PRIVATE ${MC_WARNINGS})

# ------------------------------------------------------------------ tests

enable_testing()

if(GTest_FOUND)
    file(GLOB_RECURSE MC_UNIT_TESTS CONFIGURE_DEPENDS tests/unit/*.cpp)
    add_executable(mc_tests ${MC_UNIT_TESTS})
    target_link_libraries(mc_tests PRIVATE mc_common GTest::gtest_main)
    target_compile_options(mc_tests PRIVATE ${MC_WARNINGS})
    target_precompile_headers(mc_tests PRIVATE src/core/Pch.h)
    include(GoogleTest)
    gtest_discover_tests(mc_tests PROPERTIES LABELS unit DISCOVERY_TIMEOUT 60)
else()
    message(WARNING "GTest not found: unit tests are disabled")
endif()

# GL smoke tests: run the real executable under Xvfb (Mesa llvmpipe) and check
# the screenshot. They are skipped when xvfb-run is unavailable.
find_program(XVFB_RUN xvfb-run)
find_package(Python3 COMPONENTS Interpreter)
if(XVFB_RUN AND Python3_FOUND)
    add_test(NAME gl_clear_screenshot
        COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY __GLX_VENDOR_LIBRARY_NAME=mesa
                ${XVFB_RUN} -a -s "-screen 0 1280x720x24"
                $<TARGET_FILE:minecraft> --x11 --gamedir ${CMAKE_BINARY_DIR}/test-run
                --size 320x240 --frames 3 --screenshot ${CMAKE_BINARY_DIR}/test-run/m0_clear.png --exit)
    set_tests_properties(gl_clear_screenshot PROPERTIES LABELS gl TIMEOUT 120)
    add_test(NAME gl_clear_screenshot_check
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_image.py
                ${CMAKE_BINARY_DIR}/test-run/m0_clear.png --size 320x240 --no-magenta
                --region 0,0,320,240 --mean-rgb 120,167,255 --tol 2)
    set_tests_properties(gl_clear_screenshot_check PROPERTIES LABELS gl DEPENDS gl_clear_screenshot)
endif()

message(STATUS "MinecraftCpp ${PROJECT_VERSION} -- build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Resource pack: ${MC_PACK_DIR}")
