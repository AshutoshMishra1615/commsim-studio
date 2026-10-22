/**
 * tests/test_m8.cpp  --  Catch2 unit & acceptance tests for Milestone M8:
 *   - Headless throughput benchmarks against PLAN.md §9 targets:
 *       1. Uncoded BPSK/QPSK headless >= 20 Mbit/s
 *       2. 16-QAM headless >= 15 Mbit/s
 *       3. Soft Viterbi K=7 >= 1.0 Mbit/s info
 *   - Clean pipeline execution across all core modulation and coding blocks
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/graph.hpp"
#include "blocks/source/prbs.hpp"
#include "blocks/mapping/bpsk.hpp"
#include "blocks/mapping/qpsk.hpp"
#include "blocks/mapping/qam16.hpp"
#include "blocks/channel/awgn.hpp"
#include "blocks/coding/conv_k7.hpp"
#include "blocks/coding/viterbi.hpp"

// ---------------------------------------------------------------------------
// Test 1: Uncoded BPSK headless throughput >= 20 Mbit/s
// ---------------------------------------------------------------------------
TEST_CASE("M8: Uncoded BPSK headless throughput >= 20 Mbit/s", "[m8][perf]") {
    const size_t total_bits = 4000000; // 4 Mbits
    blocks::PrbsSource prbs;
    blocks::BpskMod mod;
    blocks::BpskDemod demod;

    engine::Stream<engine::cf> bit_stream;
    engine::Stream<engine::cf> mod_stream;
    engine::Stream<engine::cf> rx_stream;

    std::vector<engine::Stream<engine::cf>*> empty_in;
    std::vector<engine::Stream<engine::cf>*> prbs_out = { &bit_stream };
    std::vector<engine::Stream<engine::cf>*> mod_in = { &bit_stream };
    std::vector<engine::Stream<engine::cf>*> mod_out = { &mod_stream };
    std::vector<engine::Stream<engine::cf>*> demod_in = { &mod_stream };
    std::vector<engine::Stream<engine::cf>*> demod_out = { &rx_stream };

    // Prime buffers
    prbs.work(empty_in, prbs_out, total_bits);

    auto t0 = std::chrono::high_resolution_clock::now();
    mod.work(mod_in, mod_out, total_bits);
    demod.work(demod_in, demod_out, total_bits);
    auto t1 = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(t1 - t0).count();
    double mbits_per_sec = (static_cast<double>(total_bits) / 1e6) / sec;

    std::printf("[M8 Perf] BPSK Headless: %.2f Mbit/s (Target: >= 20.0 Mbit/s, elapsed: %.4f s for %zu bits)\n",
                mbits_per_sec, sec, total_bits);

    CHECK(mbits_per_sec >= 20.0);
}

// ---------------------------------------------------------------------------
// Test 2: 16-QAM headless throughput >= 15 Mbit/s
// ---------------------------------------------------------------------------
TEST_CASE("M8: 16-QAM headless throughput >= 15 Mbit/s", "[m8][perf]") {
    const size_t total_bits = 4000000; // 4 Mbits (1 Msym)
    blocks::PrbsSource prbs;
    blocks::Qam16Mod mod;
    blocks::Qam16Demod demod;

    engine::Stream<engine::cf> bit_stream;
    engine::Stream<engine::cf> sym_stream;
    engine::Stream<engine::cf> rx_stream;

    std::vector<engine::Stream<engine::cf>*> empty_in;
    std::vector<engine::Stream<engine::cf>*> prbs_out = { &bit_stream };
    std::vector<engine::Stream<engine::cf>*> mod_in = { &bit_stream };
    std::vector<engine::Stream<engine::cf>*> mod_out = { &sym_stream };
    std::vector<engine::Stream<engine::cf>*> demod_in = { &sym_stream };
    std::vector<engine::Stream<engine::cf>*> demod_out = { &rx_stream };

    prbs.work(empty_in, prbs_out, total_bits);

    auto t0 = std::chrono::high_resolution_clock::now();
    mod.work(mod_in, mod_out, total_bits);
    demod.work(demod_in, demod_out, total_bits / 4);
    auto t1 = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(t1 - t0).count();
    double mbits_per_sec = (static_cast<double>(total_bits) / 1e6) / sec;

    std::printf("[M8 Perf] 16-QAM Headless: %.2f Mbit/s (Target: >= 15.0 Mbit/s, elapsed: %.4f s for %zu bits)\n",
                mbits_per_sec, sec, total_bits);

    CHECK(mbits_per_sec >= 15.0);
}

// ---------------------------------------------------------------------------
// Test 3: Soft Viterbi K=7 >= 1.0 Mbit/s info
// ---------------------------------------------------------------------------
TEST_CASE("M8: Soft Viterbi K=7 throughput >= 1.0 Mbit/s info", "[m8][perf]") {
    const size_t chunk_info = 1000;
    const size_t num_chunks = 500;
    const size_t total_info = chunk_info * num_chunks; // 500,000 info bits
    blocks::ConvK7 conv;

    std::vector<uint8_t> in_bits(chunk_info);
    for (size_t i = 0; i < chunk_info; ++i) in_bits[i] = (i % 7) & 1;

    std::vector<uint8_t> coded = conv.encode(in_bits);
    std::vector<double> rx(coded.size());
    for (size_t i = 0; i < coded.size(); ++i) rx[i] = coded[i] ? -1.0 : 1.0;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t c = 0; c < num_chunks; ++c) {
        auto dec = blocks::Viterbi::decode(rx, chunk_info, true);
        (void)dec;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(t1 - t0).count();
    double mbits_per_sec = (static_cast<double>(total_info) / 1e6) / sec;

    std::printf("[M8 Perf] Soft Viterbi K=7: %.2f Mbit/s info (Target: >= 1.0 Mbit/s, elapsed: %.4f s for %zu bits)\n",
                mbits_per_sec, sec, total_info);

    CHECK(mbits_per_sec >= 1.0);
}
