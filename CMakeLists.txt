cmake_minimum_required(VERSION 3.20)
project(commsim_studio CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------------------------------
# Compiler flags: wall / wextra on all targets
# --------------------------------------------------------------------------
add_compile_options(
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>
)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --------------------------------------------------------------------------
# Threads (required by legacy parallelFor)
# --------------------------------------------------------------------------
find_package(Threads REQUIRED)

# --------------------------------------------------------------------------
# OpenGL (headers + libGL already installed on the system)
# --------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# --------------------------------------------------------------------------
# SDL2 via FetchContent (pinned release 2.30.9)
# --------------------------------------------------------------------------
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED         OFF CACHE BOOL "" FORCE)
set(SDL_STATIC         ON  CACHE BOOL "" FORCE)
set(SDL_TEST           OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# --------------------------------------------------------------------------
# Dear ImGui (docking branch, pinned tag)
# --------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# ImGui is not a CMake project; build it as a static library manually.
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static OpenGL::GL)

# --------------------------------------------------------------------------
# ImPlot (pinned release v0.17)
# --------------------------------------------------------------------------
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.17
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(implot)

add_library(implot_lib STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot_lib PUBLIC
    ${implot_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)
target_link_libraries(implot_lib PUBLIC imgui_lib)

# --------------------------------------------------------------------------
# Catch2 v3 (pinned)
# --------------------------------------------------------------------------
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# --------------------------------------------------------------------------
# Legacy commsim prototype -- header-only INTERFACE library
# All logic lives in the headers; nothing to compile.
# --------------------------------------------------------------------------
add_library(commsim_legacy INTERFACE)
target_include_directories(commsim_legacy INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/commsim/include
)
target_link_libraries(commsim_legacy INTERFACE Threads::Threads)

# --------------------------------------------------------------------------
# Engine (M1): Block, Stream, Graph, Scheduler, RNG, ParamStore, Probe
# --------------------------------------------------------------------------
add_subdirectory(engine)

# --------------------------------------------------------------------------
# Render (M3): GPU Density Renderer, GL Context, Shaders
# --------------------------------------------------------------------------
add_subdirectory(render)

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --------------------------------------------------------------------------
# App -- empty SDL2 + ImGui window (M0 skeleton)
# --------------------------------------------------------------------------
add_subdirectory(app)
