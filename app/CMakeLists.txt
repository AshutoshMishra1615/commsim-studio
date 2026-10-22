cmake_minimum_required(VERSION 3.20)

add_executable(commsim_studio main.cpp)
target_link_libraries(commsim_studio
    PRIVATE
        render
        engine
        implot_lib
        imgui_lib
        SDL2::SDL2-static
        OpenGL::GL
        Threads::Threads
)
target_include_directories(commsim_studio PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(commsim_studio PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
