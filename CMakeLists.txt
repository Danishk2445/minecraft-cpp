cmake_minimum_required(VERSION 3.16)
project(minecraft_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release for single-config generators (Makefiles/Ninja).
# Multi-config generators (Visual Studio) pick the config at build time.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# MSVC's Release config already implies /O2; only set -O2 for GCC/Clang.
if(NOT MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "-O2")
endif()

# --- Dependencies ----------------------------------------------------------
# OpenGL, GLEW and PNG have reliable CMake modules/configs on every platform
# (Linux distros, vcpkg, MSYS2), so use them directly.
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)          # -> GLEW::GLEW
find_package(PNG REQUIRED)           # -> PNG::PNG (pulls in zlib)

# GLFW: prefer its CMake config (vcpkg, MSYS2, modern distros); fall back to
# pkg-config for older Linux setups that ship only glfw3.pc.
find_package(glfw3 CONFIG QUIET)
if(TARGET glfw)
  set(GLFW_LIB glfw)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
  set(GLFW_LIB PkgConfig::GLFW3)
endif()

# GLM is header-only: prefer its config target, else just locate the headers.
find_package(glm CONFIG QUIET)
if(TARGET glm::glm)
  set(GLM_LIB glm::glm)
else()
  find_path(GLM_INCLUDE_DIR glm/glm.hpp)
  if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM headers (glm/glm.hpp) not found. Install 'glm'.")
  endif()
  add_library(glm_headers INTERFACE)
  target_include_directories(glm_headers INTERFACE ${GLM_INCLUDE_DIR})
  set(GLM_LIB glm_headers)
endif()

# --- Target ----------------------------------------------------------------
add_executable(minecraft
  src/main.cpp
  src/texture.cpp
  src/blocks.cpp
  src/world.cpp
  src/mesher.cpp
  src/player.cpp
  src/mobs.cpp
)
target_link_libraries(minecraft PRIVATE
  ${GLFW_LIB}
  GLEW::GLEW
  PNG::PNG
  OpenGL::GL
  ${GLM_LIB}
)
