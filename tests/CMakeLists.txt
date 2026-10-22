cmake_minimum_required(VERSION 3.20)

include(CTest)
include(Catch)

# ---- M0: legacy selftest ----
add_executable(test_legacy test_legacy.cpp)
target_link_libraries(test_legacy
    PRIVATE
        commsim_legacy
        Catch2::Catch2WithMain
)
target_compile_options(test_legacy PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_legacy)

# ---- M1: engine + block BER + determinism ----
add_executable(test_engine test_engine.cpp)
target_link_libraries(test_engine
    PRIVATE
        engine
        Catch2::Catch2WithMain
        Threads::Threads
)
target_include_directories(test_engine PRIVATE
    ${CMAKE_SOURCE_DIR}   # so #include "../engine/..." resolves from tests/
)
target_compile_options(test_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_engine)

