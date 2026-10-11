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

# GL tests: run the real executable under Xvfb (Mesa llvmpipe) and check its
# output. They are skipped when xvfb-run or Python is unavailable.
find_program(XVFB_RUN xvfb-run)
find_package(Python3 COMPONENTS Interpreter)
if(XVFB_RUN AND Python3_FOUND)
    set(MC_TEST_RUN ${CMAKE_BINARY_DIR}/test-run)
    set(MC_XVFB ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY __GLX_VENDOR_LIBRARY_NAME=mesa
        ${XVFB_RUN} -a -s "-screen 0 1280x720x24")

    # mc_gl_test(<name> <minecraft args...>)
    function(mc_gl_test name)
        add_test(NAME ${name}
            COMMAND ${MC_XVFB} $<TARGET_FILE:minecraft> --x11 --gamedir ${MC_TEST_RUN} ${ARGN}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(${name} PROPERTIES LABELS gl TIMEOUT 300 RESOURCE_LOCK xvfb)
    endfunction()
    # mc_py_test(<name> <depends> <script> <args...>)
    function(mc_py_test name depends script)
        add_test(NAME ${name}
            COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/${script} ${ARGN}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(${name} PROPERTIES LABELS gl DEPENDS "${depends}")
    endfunction()

    # M1: every block state renders (no missing textures, nothing unexpectedly empty)
    # and the tricky states match their golden tiles.
    mc_gl_test(gl_model_sheets --dump-model-sheets ${MC_TEST_RUN}/sheets --golden-tiles tests/golden/tiles.txt)
    mc_py_test(gl_golden_tiles gl_model_sheets compare_tiles.py ${MC_TEST_RUN}/sheets/tiles tests/golden/tiles)

    # M1: the 3D gallery draws with depth, culling and mipmaps; the corner is clear sky.
    mc_gl_test(gl_gallery_screenshot --gallery --size 1280x720 --frames 3 --pos 120,9,-4 --look -30,40
        --screenshot ${MC_TEST_RUN}/gallery.png --exit)
    mc_py_test(gl_gallery_check gl_gallery_screenshot check_image.py ${MC_TEST_RUN}/gallery.png
        --size 1280x720 --no-magenta --min-colors 500 --region 0,560,60,720 --mean-rgb 120,167,255 --tol 2)

    # M2: the debug world (every block state) through the chunk pipeline, mesher and MDI renderer.
    mc_gl_test(gl_debug_world --gamemode spectator --world-type debug --render-distance 6 --size 1280x720 --pos 10,76,10 --look -45,30
        --wait-chunks --screenshot ${MC_TEST_RUN}/debug_world.png --exit)
    mc_py_test(gl_debug_world_check gl_debug_world check_image.py ${MC_TEST_RUN}/debug_world.png
        --size 1280x720 --no-magenta --min-colors 1000)
    # M2/M3: --exec edits on a flat world (stone wall, glass, slab), lit at noon and midnight.
    set(MC_FLAT_SCENE --gamemode spectator --world-type flat --render-distance 8 --size 1280x720 --pos 0,-55,-12 --look 0,20
        --exec "/fill -3 -60 0 3 -55 6 stone" --exec "/fill -2 -59 1 2 -56 5 air"
        --exec "/setblock 0 -60 -3 glass" --exec "/setblock 2 -60 -3 oak_slab" --wait-chunks --freeze-time)
    mc_gl_test(gl_flat_noon ${MC_FLAT_SCENE} --time 6000 --screenshot ${MC_TEST_RUN}/flat_noon.png --exit)
    mc_gl_test(gl_flat_midnight ${MC_FLAT_SCENE} --time 18000 --screenshot ${MC_TEST_RUN}/flat_midnight.png --exit)
    mc_py_test(gl_flat_noon_wall gl_flat_noon check_image.py ${MC_TEST_RUN}/flat_noon.png
        --no-magenta --region 500,150,740,350 --mean-rgb 99,99,99 --tol 6)
    mc_py_test(gl_flat_noon_grass gl_flat_noon check_image.py ${MC_TEST_RUN}/flat_noon.png
        --region 100,550,400,700 --mean-rgb 83,108,51 --tol 8)
    mc_py_test(gl_day_night_ratio "gl_flat_noon;gl_flat_midnight" check_image.py ${MC_TEST_RUN}/flat_noon.png
        --region 100,450,1180,720 --luma-vs ${MC_TEST_RUN}/flat_midnight.png --min-ratio 2.8)
    # M3: the F3 screen draws text (light gray glyphs in the top-left corner).
    mc_gl_test(gl_debug_overlay ${MC_FLAT_SCENE} --time 6000 --debug-overlay
        --screenshot ${MC_TEST_RUN}/debug_overlay.png --exit)
    mc_py_test(gl_debug_overlay_text gl_debug_overlay check_image.py ${MC_TEST_RUN}/debug_overlay.png
        --region 0,0,640,240 --count-rgb 224,224,224 --tol 12 --min-count 2000)

    # M4: fixed-seed overworld terrain from the spawn point, and the coast with translucent water.
    mc_gl_test(gl_terrain --gamemode spectator --seed 12345 --render-distance 8 --size 1280x720 --look -45,15 --time 6000
        --freeze-time --wait-chunks --screenshot ${MC_TEST_RUN}/terrain.png --exit)
    mc_py_test(gl_terrain_check gl_terrain check_image.py ${MC_TEST_RUN}/terrain.png
        --size 1280x720 --no-magenta --min-colors 2000 --region 1180,0,1280,60 --mean-rgb 160,194,255 --tol 12)
    mc_gl_test(gl_terrain_coast --gamemode spectator --seed 12345 --render-distance 8 --size 1280x720 --pos 120,72,200 --look -60,35
        --time 6000 --freeze-time --wait-chunks --screenshot ${MC_TEST_RUN}/terrain_coast.png --exit)
    mc_py_test(gl_terrain_coast_water gl_terrain_coast check_image.py ${MC_TEST_RUN}/terrain_coast.png
        --no-magenta --region 200,500,1080,720 --mean-rgb 35,62,114 --tol 12)

    # M6: edits survive saving and loading the world.
    add_test(NAME gl_save_clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${MC_TEST_RUN}/saves/savetest)
    mc_gl_test(gl_save_create --world savetest --world-type flat --gamemode creative --render-distance 4 --size 640x360
        --exec "/setblock 3 -60 3 gold_block" --exec "/setblock 4 -60 3 oak_stairs[facing=east]" --exit)
    mc_gl_test(gl_save_reload --world savetest --render-distance 4 --size 640x360
        --exec "/assertblock 3 -60 3 gold_block"
        --exec "/assertblock 4 -60 3 oak_stairs[facing=east,half=bottom,shape=straight,waterlogged=false]"
        --exec "/assertblock 5 -60 3 air" --exit)
    set_tests_properties(gl_save_create PROPERTIES DEPENDS gl_save_clean)
    set_tests_properties(gl_save_reload PROPERTIES DEPENDS gl_save_create)

    # M7: every kind of block entity on a flat world: chests (single, double, trapped,
    # ender), a bed, signs (standing, wall, hanging), banners, the seven heads, shulker
    # boxes, the bell, a decorated pot and a conduit.
    set(MC_BE_COMMANDS
        "/fill -9 -60 6 9 -57 6 stone" "/fill -8 -60 5 8 -59 5 stone" "/fill -7 -60 4 6 -60 4 stone"
        "/setblock -8 -60 3 chest[facing=north]" "/setblock -6 -60 3 chest[facing=north,type=left]"
        "/setblock -5 -60 3 chest[facing=north,type=right]" "/setblock -3 -60 3 trapped_chest[facing=north]"
        "/setblock -2 -60 3 ender_chest[facing=north]" "/setblock 0 -60 3 red_bed[facing=west,part=foot]"
        "/setblock -1 -60 3 red_bed[facing=west,part=head]" "/setblock 2 -60 3 oak_sign[rotation=8]"
        "/setblock 4 -60 3 white_banner[rotation=8]" "/setblock 7 -60 3 shulker_box[facing=up]"
        "/setblock 8 -60 3 lime_shulker_box[facing=north]" "/setblock -7 -59 4 skeleton_skull[rotation=8]"
        "/setblock -6 -59 4 wither_skeleton_skull[rotation=8]" "/setblock -5 -59 4 zombie_head[rotation=8]"
        "/setblock -4 -59 4 creeper_head[rotation=8]" "/setblock -3 -59 4 player_head[rotation=8]"
        "/setblock -2 -59 4 piglin_head[rotation=8]" "/setblock 0 -59 4 dragon_head[rotation=8]"
        "/setblock 2 -59 4 stone" "/setblock 2 -58 4 bell[attachment=floor,facing=north]"
        "/setblock 4 -59 4 decorated_pot[facing=north]" "/setblock 6 -59 4 conduit"
        "/setblock 3 -58 5 cherry_wall_sign[facing=north]" "/setblock 5 -58 5 blue_wall_banner[facing=north]"
        "/setblock 7 -56 4 stone" "/setblock 7 -57 4 spruce_hanging_sign[rotation=8,attached=false]")
    set(MC_BE_ARGS)
    foreach(cmd IN LISTS MC_BE_COMMANDS)
        list(APPEND MC_BE_ARGS --exec "${cmd}")
    endforeach()
    mc_gl_test(gl_block_entities --gamemode spectator --world-type flat --render-distance 4 --size 1280x720
        --time 6000 --freeze-time --pos 0.5,-57.2,-5.5 --look 0,22 --wait-chunks ${MC_BE_ARGS}
        --screenshot ${MC_TEST_RUN}/block_entities.png --exit)
    mc_py_test(gl_block_entities_check gl_block_entities check_image.py ${MC_TEST_RUN}/block_entities.png
        --size 1280x720 --no-magenta --min-colors 800)
    mc_py_test(gl_block_entities_bed gl_block_entities check_image.py ${MC_TEST_RUN}/block_entities.png
        --region 615,284,690,302 --mean-rgb 146,37,32 --tol 12)
    mc_py_test(gl_block_entities_chest gl_block_entities check_image.py ${MC_TEST_RUN}/block_entities.png
        --region 900,290,1000,318 --mean-rgb 97,70,30 --tol 12)
    mc_py_test(gl_block_entities_shulker gl_block_entities check_image.py ${MC_TEST_RUN}/block_entities.png
        --region 140,270,190,315 --mean-rgb 77,131,18 --tol 12)

    # M7: the F5 view from behind: the player model (Steve's shirt) with its shadow.
    mc_gl_test(gl_third_person --world-type flat --gamemode creative --render-distance 4 --size 1280x720 --time 6000
        --freeze-time --pos 0.5,-58.38,0.5 --look 30,20 --perspective back --wait-chunks --ticks 5
        --screenshot ${MC_TEST_RUN}/third_person.png --exit)
    mc_py_test(gl_third_person_check gl_third_person check_image.py ${MC_TEST_RUN}/third_person.png
        --size 1280x720 --no-magenta --region 590,380,690,460 --mean-rgb 30,109,103 --tol 12)
endif()

message(STATUS "MinecraftCpp ${PROJECT_VERSION} -- build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Resource pack: ${MC_PACK_DIR}")
