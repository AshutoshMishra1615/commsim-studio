cmake_minimum_required(VERSION 3.20)

# engine: stream, block, graph/scheduler, rng, params, probe
# scheduler.cpp is the only .cpp; everything else is header-only.
add_library(engine STATIC
    scheduler.cpp
)
target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..   # so #include "engine/graph.hpp" works from root
)
target_compile_features(engine PUBLIC cxx_std_17)
target_compile_options(engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
target_link_libraries(engine PUBLIC Threads::Threads)
