cmake_minimum_required(VERSION 3.16)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "Compiler launcher")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "Compiler launcher")
endif()

project(RocketLeagueLaptop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default matches the old makefile: -O2, asserts left on. -O3 changes float results.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug or Release" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2" CACHE STRING "Release flags" FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(FP_FLAGS -ffp-contract=off -fno-fast-math -fno-strict-aliasing)
set(WARN_FLAGS -Wall -Wextra)

file(GLOB_RECURSE ROCKETSIM_SRC CONFIGURE_DEPENDS
  third_party/RocketSim/src/*.cpp
  third_party/RocketSim/libsrc/*.cpp)
add_library(rocketsim STATIC ${ROCKETSIM_SRC})
target_include_directories(rocketsim PUBLIC third_party/RocketSim/src)
target_compile_options(rocketsim PRIVATE ${WARN_FLAGS} ${FP_FLAGS})
target_link_libraries(rocketsim PUBLIC Threads::Threads)

add_library(arena STATIC src/arena/ArenaGeometry.cpp)
target_include_directories(arena PUBLIC src)
target_compile_options(arena PRIVATE ${WARN_FLAGS} ${FP_FLAGS})

add_library(sim STATIC src/sim/Sim.cpp)
target_include_directories(sim PUBLIC src)
target_compile_options(sim PRIVATE ${WARN_FLAGS} ${FP_FLAGS})
target_link_libraries(sim PUBLIC arena rocketsim)

add_library(settings STATIC src/settings/Settings.cpp)
target_include_directories(settings PUBLIC src)
target_compile_options(settings PRIVATE ${WARN_FLAGS})

add_library(presentation STATIC
  src/render/Renderer.cpp
  src/render/PbrGl.cpp
  src/camera/Camera.cpp
  src/fx/FX.cpp
  src/fx/FlipReset.cpp
  src/fx/WheelEmbers.cpp
  src/models/Models.cpp
  src/models/Fennec.cpp
  src/models/ArenaShell.cpp
  src/models/ArenaTextures.cpp
  src/models/ArenaRip.cpp)
target_sources(presentation PRIVATE third_party/stb/stb_image_impl.cpp)
set_source_files_properties(third_party/stb/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(presentation PUBLIC src third_party/stb)
target_compile_options(presentation PRIVATE ${WARN_FLAGS})
target_link_libraries(presentation PUBLIC settings PkgConfig::SDL2 OpenGL::GL)

set(GAME_SRC
  src/game/main.cpp
  src/game/FreeplayBall.cpp
  src/fx/GoalBurst.cpp
  src/render/MatchHud.cpp
  src/render/PitchShadow.cpp
  src/input/Keyboard.cpp
  src/input/Gamepad.cpp
  src/menu/Menu.cpp
  src/audio/Audio.cpp)
add_executable(game ${GAME_SRC})
target_sources(game PRIVATE third_party/miniaudio/miniaudio.c)
set_source_files_properties(third_party/miniaudio/miniaudio.c PROPERTIES
  LANGUAGE CXX
  COMPILE_OPTIONS "-w")
target_include_directories(game PRIVATE third_party/miniaudio)
target_compile_options(game PRIVATE ${WARN_FLAGS} ${FP_FLAGS})
target_link_libraries(game PRIVATE presentation sim Threads::Threads ${CMAKE_DL_LIBS} m)

add_executable(headless_sim tools/headless_sim/main.cpp)
target_compile_options(headless_sim PRIVATE ${WARN_FLAGS} ${FP_FLAGS})
target_link_libraries(headless_sim PRIVATE sim)

add_executable(render_stub src/render/stub_main.cpp)
target_compile_options(render_stub PRIVATE ${WARN_FLAGS})
target_link_libraries(render_stub PRIVATE presentation)

add_executable(make_soccar_mesh tools/meshgen/make_soccar_mesh.cpp)
target_compile_options(make_soccar_mesh PRIVATE ${WARN_FLAGS})
# Writes soccar_cleanroom.cmf only when collision_meshes/soccar/ has no mesh,
# so a local dump already in that directory is left alone.
add_custom_target(meshgen
  COMMAND ${CMAKE_COMMAND}
    -DGEN=$<TARGET_FILE:make_soccar_mesh>
    -DDIR=${CMAKE_SOURCE_DIR}/collision_meshes/soccar
    -P ${CMAKE_SOURCE_DIR}/cmake/ensure_mesh.cmake
  DEPENDS make_soccar_mesh
  VERBATIM)
add_dependencies(game meshgen)

enable_testing()
set(RL_TESTS "")

function(rl_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_options(${name} PRIVATE ${WARN_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set(RL_TESTS ${RL_TESTS} ${name} PARENT_SCOPE)
endfunction()

rl_test(arena_test src/arena/arena_test.cpp)
target_link_libraries(arena_test PRIVATE arena)

rl_test(test_freeplay_ball tests/test_freeplay_ball.cpp src/game/FreeplayBall.cpp)
target_compile_options(test_freeplay_ball PRIVATE ${FP_FLAGS})
target_link_libraries(test_freeplay_ball PRIVATE sim)

rl_test(test_settings_roundtrip tests/test_settings_roundtrip.cpp)
target_link_libraries(test_settings_roundtrip PRIVATE settings)

rl_test(test_keyboard_menu tests/test_keyboard_menu.cpp src/input/Keyboard.cpp src/input/Gamepad.cpp src/menu/Menu.cpp)
target_link_libraries(test_keyboard_menu PRIVATE settings PkgConfig::SDL2)

rl_test(test_camera_dynamics tests/test_camera_dynamics.cpp src/camera/Camera.cpp)
target_link_libraries(test_camera_dynamics PRIVATE OpenGL::GL)

rl_test(test_gamepad tests/test_gamepad.cpp src/input/Gamepad.cpp)
target_link_libraries(test_gamepad PRIVATE settings PkgConfig::SDL2)

rl_test(test_arena_textures tests/test_arena_textures.cpp src/models/ArenaTextures.cpp)
target_link_libraries(test_arena_textures PRIVATE OpenGL::GL)

rl_test(test_arena_rip tests/test_arena_rip.cpp src/models/ArenaRip.cpp)

rl_test(test_arena_dir tests/test_arena_dir.cpp)
target_link_libraries(test_arena_dir PRIVATE PkgConfig::SDL2)

rl_test(test_last_touch tests/test_last_touch.cpp)
target_compile_options(test_last_touch PRIVATE ${FP_FLAGS})
target_link_libraries(test_last_touch PRIVATE sim)

rl_test(test_ball_trail tests/test_ball_trail.cpp)
target_link_libraries(test_ball_trail PRIVATE presentation)

rl_test(test_ball_spin tests/test_ball_spin.cpp)
target_compile_options(test_ball_spin PRIVATE ${FP_FLAGS})
target_link_libraries(test_ball_spin PRIVATE sim)

rl_test(test_contact_sparks tests/test_contact_sparks.cpp)
target_compile_options(test_contact_sparks PRIVATE ${FP_FLAGS})
target_link_libraries(test_contact_sparks PRIVATE sim)

add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/verify/measured.py
  COMMAND bash -c "python3 \"${CMAKE_SOURCE_DIR}/tools/verify/mesh_gates.py\" || true"
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/physics/verify_vs_rocketsim.py
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS ${RL_TESTS} game headless_sim render_stub meshgen
  USES_TERMINAL)
