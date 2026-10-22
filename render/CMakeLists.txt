cmake_minimum_required(VERSION 3.20)

add_library(render STATIC
    gl_context.cpp
    density_renderer.cpp
)
target_include_directories(render PUBLIC
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(render PUBLIC
    SDL2::SDL2-static
    OpenGL::GL
)
target_compile_options(render PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
