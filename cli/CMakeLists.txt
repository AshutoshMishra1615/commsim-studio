cmake_minimum_required(VERSION 3.20)

add_executable(commsim_cli sweep.cpp)
target_link_libraries(commsim_cli
    PRIVATE
        engine
        Threads::Threads
)
target_include_directories(commsim_cli PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(commsim_cli PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
