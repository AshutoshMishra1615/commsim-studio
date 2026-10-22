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

# ---- M2: viewer sim thread, triple buffer, latency, 60 FPS ----
add_executable(test_viewer test_viewer.cpp)
target_link_libraries(test_viewer
    PRIVATE
        render
        engine
        Catch2::Catch2WithMain
        Threads::Threads
)
target_include_directories(test_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(test_viewer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_viewer)

# ---- M3: GPU density renderer (1M pts/s at 60 FPS, 16-QAM at 10 dB) ----
add_executable(test_density test_density.cpp)
target_link_libraries(test_density
    PRIVATE
        render
        engine
        Catch2::Catch2WithMain
        Threads::Threads
)
target_include_directories(test_density PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(test_density PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_density)

# ---- M4: pulse shaping, matched filter, ISI FIR channel, eye diagram ----
add_executable(test_shaping test_shaping.cpp)
target_link_libraries(test_shaping
    PRIVATE
        engine
        Catch2::Catch2WithMain
        Threads::Threads
)
target_include_directories(test_shaping PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(test_shaping PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_shaping)

# ---- M5: coding blocks (CRC-16, Hamming74, Soft/Hard Viterbi, Interleaver) ----
add_executable(test_coding test_coding.cpp)
target_link_libraries(test_coding
    PRIVATE
        engine
        Catch2::Catch2WithMain
        Threads::Threads
)
target_include_directories(test_coding PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(test_coding PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_coding)

# ---- M6: source coding, analog (AM/FM/PM), M-ary (8-PSK, 16-PSK, 64-QAM), capacity ----
add_executable(test_m6 test_m6.cpp)
target_link_libraries(test_m6
    PRIVATE
        engine
        Catch2::Catch2WithMain
        Threads::Threads
)
target_include_directories(test_m6 PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(test_m6 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_m6)
# ---- M7: multiple access (CDMA, TDMA, FDMA), protocol (framer, ARQ, OSI/TCP-IP) ----
add_executable(test_m7 test_m7.cpp)
target_link_libraries(test_m7
    PRIVATE
        engine
        Catch2::Catch2WithMain
        Threads::Threads
)
target_include_directories(test_m7 PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(test_m7 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
catch_discover_tests(test_m7)
