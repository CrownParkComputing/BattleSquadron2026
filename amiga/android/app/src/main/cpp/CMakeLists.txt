cmake_minimum_required(VERSION 3.25)
project(battle_squadron_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLATFORM Android CACHE STRING "" FORCE)
set(GRAPHICS GRAPHICS_API_OPENGL_ES2 CACHE STRING "" FORCE)

if(NOT EXISTS "${RAYLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set RAYLIB_DIR to a raylib source checkout")
endif()

add_subdirectory("${RAYLIB_DIR}" "${CMAKE_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
# Musashi lives beside this tree; ../SWIV was a path from an older layout
set(MUSASHI_DIR "$ENV{HOME}/BattleSquadron/src/musashi")

add_library(main SHARED
    "${REPO_ROOT}/src/host/frontend.c"
    "${REPO_ROOT}/src/host/pad.c"
    "${REPO_ROOT}/src/host/amiga.c"
    "${REPO_ROOT}/src/host/whdload.c"
    "${MUSASHI_DIR}/m68kcpu.c"
    "${MUSASHI_DIR}/m68kops.c"
    "${MUSASHI_DIR}/m68kdasm.c"
    "${MUSASHI_DIR}/softfloat/softfloat.c"
)

target_include_directories(main PRIVATE
    "${REPO_ROOT}/src/host"
    "${MUSASHI_DIR}"
    "${ANDROID_NDK}/sources/android/native_app_glue"
)

target_compile_definitions(main PRIVATE
    PLATFORM_ANDROID
    M68K_INSTRUCTION_HOOK=M68K_OPT_SPECIFY_HANDLER
)
target_compile_options(main PRIVATE
    -O2
    -Wall
    -Wextra
    -include "${REPO_ROOT}/src/host/amiga.h"
)
target_link_options(main PRIVATE
    -u ANativeActivity_onCreate
    -Wl,--wrap=fopen
)
target_link_libraries(main PRIVATE
    raylib
    android
    nativewindow
    log
    EGL
    GLESv2
    OpenSLES
    atomic
    m
    dl
)
