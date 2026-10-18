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
    # Client code that needs no GL context (screen layouts and behavior).
    file(GLOB_RECURSE MC_CLIENT_TESTS CONFIGURE_DEPENDS tests/client/*.cpp)
    add_executable(mc_client_tests ${MC_CLIENT_TESTS})
    target_link_libraries(mc_client_tests PRIVATE mc_client GTest::gtest_main)
    target_compile_options(mc_client_tests PRIVATE ${MC_WARNINGS})
    target_precompile_headers(mc_client_tests PRIVATE src/core/Pch.h)
    gtest_discover_tests(mc_client_tests PROPERTIES LABELS unit DISCOVERY_TIMEOUT 60)
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

    # M8: every item's GUI icon draws something and never the missing texture.
    mc_gl_test(gl_item_icons --dump-item-icons ${MC_TEST_RUN}/item_icons)
    # M8: the survival and creative inventory screens over the world (panel color C6C6C6).
    set(MC_SCREEN_SCENE --world-type flat --render-distance 4 --size 1280x720 --time 6000 --freeze-time
        --pos 0.5,-58.38,0.5 --look 30,20 --wait-chunks --ticks 3 --screen inventory)
    mc_gl_test(gl_inventory_screen ${MC_SCREEN_SCENE} --gamemode survival --exec "/give diamond 23"
        --exec "/give oak_planks 12" --screenshot ${MC_TEST_RUN}/inventory_screen.png --exit)
    mc_py_test(gl_inventory_screen_check gl_inventory_screen check_image.py ${MC_TEST_RUN}/inventory_screen.png
        --size 1280x720 --no-magenta --region 660,280,780,300 --mean-rgb 198,198,198 --tol 2)
    mc_gl_test(gl_creative_screen ${MC_SCREEN_SCENE} --gamemode creative
        --screenshot ${MC_TEST_RUN}/creative_screen.png --exit)
    mc_py_test(gl_creative_screen_check gl_creative_screen check_image.py ${MC_TEST_RUN}/creative_screen.png
        --size 1280x720 --no-magenta --min-colors 1000 --region 700,166,840,172 --mean-rgb 198,198,198 --tol 2)

    # M9: the title-side screens, in their own game directory (its panorama and saves).
    set(MC_TITLE_RUN ${MC_TEST_RUN}/title)
    set(MC_TITLE_ARGS --gamedir ${MC_TITLE_RUN} --size 1280x720 --splash Splash!)
    mc_gl_test(gl_panorama_bake --gamedir ${MC_TITLE_RUN} --bake-panorama)
    mc_py_test(gl_panorama_sky gl_panorama_bake check_image.py ${MC_TITLE_RUN}/panorama/panorama_4.png
        --size 1024x1024 --region 384,384,640,640 --mean-rgb 142,174,234 --tol 15)
    mc_py_test(gl_panorama_view gl_panorama_bake check_image.py ${MC_TITLE_RUN}/panorama/panorama_0.png
        --no-magenta --min-colors 5000)
    # The title over the panorama: the logo, and the disabled Realms button's left part.
    mc_gl_test(gl_title_screen ${MC_TITLE_ARGS} --screen title --frames 2 --screenshot ${MC_TEST_RUN}/title.png --exit)
    mc_py_test(gl_title_screen_check gl_title_screen check_image.py ${MC_TEST_RUN}/title.png
        --size 1280x720 --no-magenta --region 350,475,470,520 --mean-rgb 44,44,44 --tol 4)
    mc_py_test(gl_title_logo_check gl_title_screen check_image.py ${MC_TEST_RUN}/title.png
        --region 255,90,1023,222 --mean-rgb 112,108,100 --tol 10)
    # A world opened as the world list does: the loading screen's chunk map, then the game,
    # which saves the world's icon.
    mc_gl_test(gl_loading_screen ${MC_TITLE_ARGS} --play Loading --world-type flat --frames 1
        --screenshot ${MC_TEST_RUN}/loading.png --exit)
    mc_py_test(gl_loading_screen_check gl_loading_screen check_image.py ${MC_TEST_RUN}/loading.png
        --region 565,285,715,435 --count-rgb 84,84,84 --tol 2 --min-count 10000)
    mc_gl_test(gl_play_world ${MC_TITLE_ARGS} --play "Test World" --world-type flat --wait-chunks
        --screenshot ${MC_TEST_RUN}/play_world.png --exit)
    mc_py_test(gl_play_world_icon gl_play_world check_image.py "${MC_TITLE_RUN}/saves/Test World/icon.png"
        --size 64x64 --min-colors 50)
    # The world list (white names) and Create World (its footer button).
    mc_gl_test(gl_world_list ${MC_TITLE_ARGS} --screen worlds --frames 2 --screenshot ${MC_TEST_RUN}/world_list.png --exit)
    mc_py_test(gl_world_list_check gl_world_list check_image.py ${MC_TEST_RUN}/world_list.png
        --no-magenta --region 343,160,600,185 --count-rgb 255,255,255 --tol 2 --min-count 300)
    mc_gl_test(gl_create_world ${MC_TITLE_ARGS} --screen create --frames 2 --screenshot ${MC_TEST_RUN}/create_world.png --exit)
    mc_py_test(gl_create_world_check gl_create_world check_image.py ${MC_TEST_RUN}/create_world.png
        --no-magenta --region 180,645,250,690 --mean-rgb 113,113,113 --tol 6)
    set_tests_properties(gl_title_screen gl_loading_screen gl_play_world gl_world_list gl_create_world
        PROPERTIES DEPENDS gl_panorama_bake)
    set_tests_properties(gl_world_list PROPERTIES DEPENDS "gl_panorama_bake;gl_loading_screen;gl_play_world")

    # M9: the menus over a world (blurred behind them; a button's plain left part), and the
    # chat line, which darkens the world under it by half.
    set(MC_MENU_SCENE --world-type flat --render-distance 4 --size 1280x720 --time 6000 --freeze-time
        --pos 0.5,-58.38,0.5 --look 30,20 --wait-chunks --ticks 3)
    mc_gl_test(gl_pause_screen ${MC_MENU_SCENE} --screen pause --screenshot ${MC_TEST_RUN}/pause_screen.png --exit)
    mc_py_test(gl_pause_screen_check gl_pause_screen check_image.py ${MC_TEST_RUN}/pause_screen.png
        --size 1280x720 --no-magenta --region 345,230,520,270 --mean-rgb 111,111,111 --tol 4)
    mc_gl_test(gl_options_screen ${MC_MENU_SCENE} --screen options --screenshot ${MC_TEST_RUN}/options_screen.png --exit)
    mc_py_test(gl_options_screen_check gl_options_screen check_image.py ${MC_TEST_RUN}/options_screen.png
        --no-magenta --region 345,645,520,690 --mean-rgb 111,111,111 --tol 4)
    mc_gl_test(gl_video_screen ${MC_MENU_SCENE} --screen video --screenshot ${MC_TEST_RUN}/video_screen.png --exit)
    mc_py_test(gl_video_screen_check gl_video_screen check_image.py ${MC_TEST_RUN}/video_screen.png
        --no-magenta --region 345,645,520,690 --mean-rgb 111,111,111 --tol 4)
    mc_gl_test(gl_chat_screen ${MC_MENU_SCENE} --screen chat --screenshot ${MC_TEST_RUN}/chat_screen.png --exit)
    mc_py_test(gl_chat_screen_check gl_chat_screen check_image.py ${MC_TEST_RUN}/chat_screen.png
        --region 20,682,340,712 --mean-rgb 41,53,25 --tol 8)

    # M7: the F5 view from behind: the player model (Steve's shirt) with its shadow.
    mc_gl_test(gl_third_person --world-type flat --gamemode creative --render-distance 4 --size 1280x720 --time 6000
        --freeze-time --pos 0.5,-58.38,0.5 --look 30,20 --perspective back --wait-chunks --ticks 5
        --screenshot ${MC_TEST_RUN}/third_person.png --exit)
    mc_py_test(gl_third_person_check gl_third_person check_image.py ${MC_TEST_RUN}/third_person.png
        --size 1280x720 --no-magenta --region 590,380,690,460 --mean-rgb 30,109,103 --tol 12)

    # M10: sign text in the world (block entity data from /setblock): dye colors at 0.4,
    # glowing text full-bright with its outline (cream around black), wall and hanging signs.
    set(MC_SIGN_COMMANDS
        "/setblock 0 -60 0 oak_sign[rotation=0]{front_text:{messages:[\"Hello\",\"World!\",\"\",\"\"]}}"
        "/setblock 1 -60 0 birch_sign[rotation=0]{front_text:{messages:[\"Red\",\"text\",\"is here\",\"too long line of text for a sign\"],color:\"red\"}}"
        "/fill 2 -60 -1 2 -58 -1 stone"
        "/setblock 2 -59 0 oak_wall_sign[facing=south]{front_text:{messages:[\"Wall\",\"sign\",\"\",\"\"]}}"
        "/setblock 3 -58 0 stone"
        "/setblock 3 -59 0 oak_hanging_sign[rotation=0]{front_text:{messages:[\"Hang\",\"ing\",\"sign\",\"\"]}}"
        "/setblock 4 -60 0 spruce_sign[rotation=0]{front_text:{messages:[\"Glow\",\"lime\",\"\",\"\"],color:\"lime\",has_glowing_text:1b}}"
        "/setblock 5 -60 0 dark_oak_sign[rotation=0]{front_text:{messages:[\"Glow\",\"black\",\"\",\"\"],has_glowing_text:1b}}")
    set(MC_SIGN_ARGS)
    foreach(cmd IN LISTS MC_SIGN_COMMANDS)
        list(APPEND MC_SIGN_ARGS --exec "${cmd}")
    endforeach()
    mc_gl_test(gl_signs --gamemode spectator --world-type flat --render-distance 4 --size 1280x720 --time 6000
        --freeze-time --pos 3,-58.4,3.2 --look 180,15 --wait-chunks ${MC_SIGN_ARGS} --ticks 5
        --screenshot ${MC_TEST_RUN}/signs.png --exit)
    mc_py_test(gl_signs_black gl_signs check_image.py ${MC_TEST_RUN}/signs.png --no-magenta
        --region 70,320,265,410 --count-rgb 0,0,0 --tol 10 --min-count 200)
    mc_py_test(gl_signs_red gl_signs check_image.py ${MC_TEST_RUN}/signs.png
        --region 265,320,460,410 --count-rgb 102,0,0 --tol 20 --min-count 500)
    mc_py_test(gl_signs_glowing gl_signs check_image.py ${MC_TEST_RUN}/signs.png
        --region 820,320,1010,410 --count-rgb 191,255,0 --tol 25 --min-count 150)
    mc_py_test(gl_signs_outline gl_signs check_image.py ${MC_TEST_RUN}/signs.png
        --region 1010,320,1200,410 --count-rgb 240,235,204 --tol 20 --min-count 400)

    # M10: screens from using things: a chest (a diamond in slot 4), a furnace 120 ticks into
    # its first raw iron (the arrow three fifths white), the editor of a sign just placed, and
    # the death screen.
    set(MC_USE_SCENE --world-type flat --render-distance 4 --size 1280x720 --time 6000 --freeze-time
        --pos 0.5,-58.38,0.5 --wait-chunks --gamemode survival)
    mc_gl_test(gl_chest_screen ${MC_USE_SCENE} --look 0,29 --exec "/setblock 0 -60 2 chest"
        --exec "/item replace block 0 -60 2 container.4 with diamond 7"
        --exec "/item replace block 0 -60 2 container.13 with oak_log 32" --hold use --ticks 5
        --screenshot ${MC_TEST_RUN}/chest_screen.png --exit)
    mc_py_test(gl_chest_screen_panel gl_chest_screen check_image.py ${MC_TEST_RUN}/chest_screen.png
        --no-magenta --region 600,118,880,145 --mean-rgb 198,198,198 --tol 2)
    mc_py_test(gl_chest_screen_diamond gl_chest_screen check_image.py ${MC_TEST_RUN}/chest_screen.png
        --region 622,166,660,204 --mean-rgb 112,182,176 --tol 12)
    mc_gl_test(gl_furnace_screen ${MC_USE_SCENE} --look 0,29 --exec "/setblock 0 -60 2 furnace[facing=north]"
        --exec "/item replace block 0 -60 2 container.0 with raw_iron 4"
        --exec "/item replace block 0 -60 2 container.1 with coal 1" --hold use --ticks 120
        --screenshot ${MC_TEST_RUN}/furnace_screen.png --exit)
    mc_py_test(gl_furnace_screen_arrow gl_furnace_screen check_image.py ${MC_TEST_RUN}/furnace_screen.png
        --no-magenta --region 617,230,653,240 --mean-rgb 249,249,249 --tol 8)
    mc_gl_test(gl_sign_edit_screen ${MC_USE_SCENE} --look 0,40 --exec "/give oak_sign 2" --hold use --ticks 5
        --screenshot ${MC_TEST_RUN}/sign_edit_screen.png --exit)
    mc_py_test(gl_sign_edit_screen_board gl_sign_edit_screen check_image.py ${MC_TEST_RUN}/sign_edit_screen.png
        --no-magenta --region 500,240,780,320 --mean-rgb 165,135,81 --tol 12)
    mc_gl_test(gl_death_screen ${MC_USE_SCENE} --look 0,20 --exec "/kill" --ticks 30
        --screenshot ${MC_TEST_RUN}/death_screen.png --exit)
    mc_py_test(gl_death_screen_check gl_death_screen check_image.py ${MC_TEST_RUN}/death_screen.png
        --no-magenta --region 100,40,500,140 --mean-rgb 139,124,154 --tol 10)
    # Dying (in lava) with a chest open still shows the death screen (its Respawn button).
    mc_gl_test(gl_death_with_chest_open ${MC_USE_SCENE} --look 0,29 --exec "/setblock 0 -60 2 chest"
        --exec "/setblock 0 -60 0 lava" --hold use --ticks 200 --screenshot ${MC_TEST_RUN}/death_chest.png --exit)
    mc_py_test(gl_death_with_chest_open_check gl_death_with_chest_open check_image.py ${MC_TEST_RUN}/death_chest.png
        --no-magenta --region 350,404,420,446 --mean-rgb 111,111,111 --tol 6)

    # M11: the live world. Blocks falling (sand as bright as the placed sand below them,
    # red concrete powder).
    set(MC_SIM_SCENE --world-type flat --render-distance 4 --size 1280x720 --time 6000 --freeze-time
        --gamemode spectator --wait-chunks --gamerule doMobSpawning=false)
    mc_gl_test(gl_falling_blocks ${MC_SIM_SCENE} --pos 0.5,-57,-4 --look 0,-5
        --exec "/setblock 0 -59 3 stone" --exec "/setblock 0 -58 3 sand" --exec "/setblock -2 -52 3 sand"
        --exec "/setblock 2 -52 3 red_concrete_powder" --exec "/setblock 4 -52 3 anvil"
        --exec "/setblock -4 -52 3 gravel" --ticks 10 --screenshot ${MC_TEST_RUN}/falling_blocks.png --exit)
    mc_py_test(gl_falling_blocks_sand gl_falling_blocks check_image.py ${MC_TEST_RUN}/falling_blocks.png
        --no-magenta --region 745,90,805,140 --mean-rgb 172,162,127 --tol 6)
    mc_py_test(gl_falling_blocks_powder gl_falling_blocks check_image.py ${MC_TEST_RUN}/falling_blocks.png
        --region 470,90,540,140 --mean-rgb 130,42,40 --tol 8)
    # Weather: the sky greyed by rain at noon (clear is about 148,186,255), and nearly black
    # at midnight in the rain.
    mc_gl_test(gl_weather_rain ${MC_SIM_SCENE} --time 6000 --weather rain --pos 0.5,-58,0.5 --look 0,-30 --ticks 40
        --screenshot ${MC_TEST_RUN}/weather_rain.png --exit)
    mc_py_test(gl_weather_rain_sky gl_weather_rain check_image.py ${MC_TEST_RUN}/weather_rain.png
        --no-magenta --region 0,0,1280,200 --mean-rgb 92,104,139 --tol 10)
    mc_gl_test(gl_weather_night_rain ${MC_SIM_SCENE} --time 18000 --weather rain --pos 0.5,-58,0.5 --look 0,-30
        --ticks 40 --screenshot ${MC_TEST_RUN}/weather_night_rain.png --exit)
    mc_py_test(gl_weather_night_rain_sky gl_weather_night_rain check_image.py ${MC_TEST_RUN}/weather_night_rain.png
        --region 0,0,1280,200 --mean-rgb 4,5,12 --tol 6)
    # A lightning bolt in a night thunderstorm: its pale glow up the middle.
    mc_gl_test(gl_lightning ${MC_SIM_SCENE} --time 18000 --weather thunder --pos 0.5,-58,-20 --look 0,-20
        --exec "/summon lightning_bolt 0.5 -60 0.5" --ticks 1 --screenshot ${MC_TEST_RUN}/lightning.png --exit)
    mc_py_test(gl_lightning_bolt gl_lightning check_image.py ${MC_TEST_RUN}/lightning.png
        --region 300,0,1000,560 --count-rgb 160,160,180 --tol 50 --min-count 100)
    # Lit TNT 10 ticks from going off: flashing white.
    mc_gl_test(gl_tnt_flash ${MC_SIM_SCENE} --time 6000 --pos 0.5,-58.4,-3 --look 0,15
        --exec "/summon tnt 0.5 -60 0.5" --ticks 70 --screenshot ${MC_TEST_RUN}/tnt_flash.png --exit)
    mc_py_test(gl_tnt_flash_white gl_tnt_flash check_image.py ${MC_TEST_RUN}/tnt_flash.png
        --region 590,300,700,440 --mean-rgb 216,202,198 --tol 25)
    # Water from one source, 3 seconds on, seen from above: the 7-block diamond (its tip
    # blue, past it grass).
    mc_gl_test(gl_flowing_water ${MC_SIM_SCENE} --pos 0.5,-48,0.5 --look 0,90 --exec "/setblock 0 -60 0 water"
        --ticks 60 --screenshot ${MC_TEST_RUN}/flowing_water.png --exit)
    mc_py_test(gl_flowing_water_tip gl_flowing_water check_image.py ${MC_TEST_RUN}/flowing_water.png
        --no-magenta --region 322,345,355,380 --mean-rgb 51,84,120 --tol 12)
    mc_py_test(gl_flowing_water_dry gl_flowing_water check_image.py ${MC_TEST_RUN}/flowing_water.png
        --region 100,320,180,400 --mean-rgb 82,107,50 --tol 12)
    # A water bucket used on the ground in survival: a source where the player looks.
    mc_gl_test(gl_bucket_water --world-type flat --render-distance 4 --size 1280x720 --time 6000 --freeze-time
        --gamemode survival --pos 0.5,-58.38,0.5 --look 0,50 --wait-chunks --exec "/give water_bucket" --hold use
        --ticks 2 --screenshot ${MC_TEST_RUN}/bucket_water.png --exit)
    mc_py_test(gl_bucket_water_placed gl_bucket_water check_image.py ${MC_TEST_RUN}/bucket_water.png
        --no-magenta --region 520,400,760,520 --mean-rgb 50,80,109 --tol 12)
    # Under water: the water's dark blue fog closes in.
    mc_gl_test(gl_underwater_fog ${MC_SIM_SCENE} --time 6000 --pos 0.5,-59.5,0.5 --look 0,0
        --exec "/fill -20 -60 -20 20 -58 20 water" --ticks 5 --screenshot ${MC_TEST_RUN}/underwater.png --exit)
    mc_py_test(gl_underwater_fog_color gl_underwater_fog check_image.py ${MC_TEST_RUN}/underwater.png
        --region 0,0,1280,250 --mean-rgb 26,42,138 --tol 20)

    # M12: the eight mobs in the noon sun, facing the camera (the undead under a slab of stone,
    # out of the sun that would set them alight): spider, pig, cow, sheep, chicken, zombie,
    # skeleton, creeper.
    mc_gl_test(gl_mobs ${MC_SIM_SCENE} --pos 0.5,-58.4,8 --look 180,12 --exec [=[/fill 2 -57 0 5 -57 1 stone$<SEMICOLON>/summon spider -7.5 -60 0.5 {NoAI:1b}$<SEMICOLON>/summon pig -4.5 -60 0.5 {NoAI:1b}$<SEMICOLON>/summon cow -2.5 -60 0.5 {NoAI:1b}$<SEMICOLON>/summon sheep -0.5 -60 0.5 {NoAI:1b}$<SEMICOLON>/summon chicken 1.2 -60 0.5 {NoAI:1b}$<SEMICOLON>/summon zombie 2.7 -60 0.5 {NoAI:1b}$<SEMICOLON>/summon skeleton 4.4 -60 0.5 {NoAI:1b}$<SEMICOLON>/summon creeper 6 -60 0.5 {NoAI:1b}]=]
        --ticks 3 --screenshot ${MC_TEST_RUN}/mobs.png --exit)
    mc_py_test(gl_mobs_pig gl_mobs check_image.py ${MC_TEST_RUN}/mobs.png --no-magenta
        --region 255,305,315,345 --mean-rgb 135,80,77 --tol 25)
    mc_py_test(gl_mobs_sheep gl_mobs check_image.py ${MC_TEST_RUN}/mobs.png --region 545,290,590,340 --mean-rgb 156,153,153 --tol 20)
    mc_py_test(gl_mobs_zombie gl_mobs check_image.py ${MC_TEST_RUN}/mobs.png --region 780,250,815,350 --mean-rgb 34,73,86 --tol 25)
    mc_py_test(gl_mobs_creeper gl_mobs check_image.py ${MC_TEST_RUN}/mobs.png --region 990,260,1040,350 --mean-rgb 61,111,55 --tol 25)
    mc_py_test(gl_mobs_spider_eyes gl_mobs check_image.py ${MC_TEST_RUN}/mobs.png
        --region 10,300,80,340 --count-rgb 220,20,20 --tol 70 --min-count 100)
    # An ignited creeper 15 ticks into its fuse, flashing white and swollen; a cow just killed,
    # fallen on its side and red.
    mc_gl_test(gl_creeper_flash ${MC_SIM_SCENE} --pos 0.5,-58.4,-3 --look 0,12 --exec [=[/summon creeper -1 -60 2.5 {NoAI:1b,Rotation:[180f,0f],ignited:1b}$<SEMICOLON>/summon cow 2.5 -60 2.5 {NoAI:1b,Rotation:[180f,0f]}$<SEMICOLON>/kill @e[type=cow]]=]
        --ticks 15 --screenshot ${MC_TEST_RUN}/creeper_flash.png --exit)
    mc_py_test(gl_creeper_flash_white gl_creeper_flash check_image.py ${MC_TEST_RUN}/creeper_flash.png
        --region 755,250,810,400 --mean-rgb 128,162,124 --tol 25)
    mc_py_test(gl_mob_death_red gl_creeper_flash check_image.py ${MC_TEST_RUN}/creeper_flash.png
        --region 460,355,585,410 --mean-rgb 114,44,35 --tol 25)
    # At night: a charged creeper's swirl, a zombie in diamond armor, a spider's glowing eyes.
    mc_gl_test(gl_mobs_night ${MC_SIM_SCENE} --time 18000 --pos 0.5,-58.4,-3 --look 0,12 --exec [=[/summon spider -2.5 -60 2.5 {NoAI:1b,Rotation:[180f,0f]}$<SEMICOLON>/summon zombie 1 -60 2.5 {NoAI:1b,Rotation:[180f,0f],equipment:{head:{id:"minecraft:diamond_helmet"},chest:{id:"minecraft:diamond_chestplate"}}}$<SEMICOLON>/summon creeper 3.5 -60 2.5 {NoAI:1b,Rotation:[180f,0f],powered:1b}]=]
        --ticks 3 --screenshot ${MC_TEST_RUN}/mobs_night.png --exit)
    mc_py_test(gl_mobs_night_eyes gl_mobs_night check_image.py ${MC_TEST_RUN}/mobs_night.png
        --region 910,330,990,370 --count-rgb 220,20,20 --tol 70 --min-count 60)
    mc_py_test(gl_mobs_night_armor gl_mobs_night check_image.py ${MC_TEST_RUN}/mobs_night.png
        --region 545,205,640,330 --mean-rgb 18,37,48 --tol 12)
    mc_py_test(gl_mobs_night_swirl gl_mobs_night check_image.py ${MC_TEST_RUN}/mobs_night.png
        --region 300,240,410,400 --mean-rgb 31,57,66 --tol 15)
endif()

message(STATUS "MinecraftCpp ${PROJECT_VERSION} -- build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Resource pack: ${MC_PACK_DIR}")
