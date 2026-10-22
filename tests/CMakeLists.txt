cmake_minimum_required(VERSION 3.20)

add_executable(test_legacy test_legacy.cpp)
target_link_libraries(test_legacy
    PRIVATE
        commsim_legacy
        Catch2::Catch2WithMain
)
target_compile_options(test_legacy PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

include(CTest)
include(Catch)
catch_discover_tests(test_legacy)
