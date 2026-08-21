cmake_minimum_required(VERSION 3.22.1)
project(battle_squadron_native_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLATFORM Android CACHE STRING "" FORCE)
set(GRAPHICS GRAPHICS_API_OPENGL_ES2 CACHE STRING "" FORCE)

if(NOT EXISTS "${RAYLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set raylibDir/RAYLIB_DIR to a raylib source checkout")
endif()

add_subdirectory("${RAYLIB_DIR}" "${CMAKE_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

add_library(main SHARED
    "${REPO_ROOT}/src/viewer.c"
    "${REPO_ROOT}/src/render.c"
    "${REPO_ROOT}/src/audio.c"
    "${REPO_ROOT}/src/bsdata.c"
    "${REPO_ROOT}/src/overlay.c"
    "${REPO_ROOT}/src/bond.c"
    "${REPO_ROOT}/src/engine/engine.c"
    "${REPO_ROOT}/src/behaviours/hostiles.c"
    "${REPO_ROOT}/src/behaviours/objects.c"
)

target_include_directories(main PRIVATE
    "${REPO_ROOT}/src"
    "${REPO_ROOT}/src/engine"
    "${ANDROID_NDK}/sources/android/native_app_glue"
)

target_compile_definitions(main PRIVATE PLATFORM_ANDROID)
target_compile_options(main PRIVATE
    -O2
    -Wall
    -Wextra
    -Wno-unused-parameter
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
