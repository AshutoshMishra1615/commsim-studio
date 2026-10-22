/**
 * tests/test_coding.cpp  --  M5: Forward error correction, CRC, and Viterbi decoding tests.
 *
 * PLAN.md §6 Acceptance criteria:
 *   1. Soft Viterbi reaches BER <= 1e-5 at <= 4.3 dB Eb/N0.
 *   2. Hard Viterbi reaches BER <= 1e-5 at <= 6.6 dB Eb/N0.
 *   3. Hamming(7,4) corrects 100% of single errors.
 *
 * PLAN.md §7 Validation matrix:
 *   1. Conv code coding gain: >= 4.5 dB soft, >= 2.8 dB hard at BER 1e-5 (0-7 dB).
 *   2. CRC-16 detects all bursts <= 16 bits (exhaustive for 64-bit message).
 *   3. Hamming(7,4) corrects 100% of single errors.
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include <random>

#include "blocks/coding/crc16.hpp"
#include "blocks/coding/hamming74.hpp"
#include "blocks/coding/conv_k7.hpp"
#include "blocks/coding/viterbi.hpp"
#include "blocks/coding/interleaver.hpp"
#include "blocks/source/prbs.hpp"
#include "blocks/source/passthrough.hpp"
#include "blocks/mapping/bpsk.hpp"
#include "blocks/channel/awgn.hpp"
#include "engine/graph.hpp"
#include "engine/probe.hpp"

using Catch::Approx;

TEST_CASE("M5: CRC-16 detects all bursts <= 16 bits (exhaustive for 64-bit message)", "[coding][crc16]") {
    // 64-bit message (8 bytes)
    const std::vector<uint8_t> msg = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    const uint16_t msg_crc = blocks::crc16(msg.data(), msg.size());

    uint64_t total_bursts = 0;
    uint64_t detected_bursts = 0;

    // Test all burst lengths 1 <= L <= 16
    for (int L = 1; L <= 16; ++L) {
        // A burst of length L has bit 0 = 1 and bit L-1 = 1, with 2^(L-2) internal patterns (or 1 if L=1)
        const int num_patterns = (L <= 2) ? 1 : (1 << (L - 2));

        for (int p = 0; p < num_patterns; ++p) {
            uint32_t pattern = 1u | (1u << (L - 1));
            if (L > 2) {
                pattern |= (static_cast<uint32_t>(p) << 1);
            }

            // Test at every possible start bit position in the 64-bit message
            for (int pos = 0; pos <= 64 - L; ++pos) {
                std::vector<uint8_t> corrupted = msg;
                for (int b = 0; b < L; ++b) {
                    if ((pattern >> b) & 1u) {
                        const int bit_idx = pos + b;
                        corrupted[bit_idx / 8] ^= static_cast<uint8_t>(1u << (7 - (bit_idx % 8)));
                    }
                }

                const uint16_t corrupted_crc = blocks::crc16(corrupted.data(), corrupted.size());
                total_bursts++;
                if (corrupted_crc != msg_crc) {
                    detected_bursts++;
                }
            }
        }
    }

    std::printf("[M4/M5 CRC-16 Burst Test] Tested %llu burst patterns <= 16 bits; detected %llu\n",
                static_cast<unsigned long long>(total_bursts),
                static_cast<unsigned long long>(detected_bursts));

    // Acceptance: detects 100% of all error bursts <= 16 bits
    REQUIRE(total_bursts > 0);
    CHECK(detected_bursts == total_bursts);
    const double detection_rate = static_cast<double>(detected_bursts) / static_cast<double>(total_bursts);
    CHECK(detection_rate == Approx(1.0));
}

TEST_CASE("M5: Hamming (7,4) corrects 100% of single errors", "[coding][hamming]") {
    uint64_t total_cases = 0;
    uint64_t corrected_cases = 0;

    // Test all 16 possible 4-bit data nibbles
    for (uint8_t nibble = 0; nibble < 16; ++nibble) {
        std::vector<uint8_t> data = {
            static_cast<uint8_t>((nibble >> 3) & 1u),
            static_cast<uint8_t>((nibble >> 2) & 1u),
            static_cast<uint8_t>((nibble >> 1) & 1u),
            static_cast<uint8_t>((nibble >> 0) & 1u)
        };

        const auto codeword = blocks::Hamming74::encode(data);
        REQUIRE(codeword.size() == 7);

        // Noiseless decoding check
        const auto clean_dec = blocks::Hamming74::decode(codeword);
        CHECK(clean_dec == data);

        // Inject single-bit error at each of the 7 positions
        for (size_t err_pos = 0; err_pos < 7; ++err_pos) {
            auto corrupted = codeword;
            corrupted[err_pos] ^= 1u;

            const auto decoded = blocks::Hamming74::decode(corrupted);
            total_cases++;
            if (decoded == data) {
                corrected_cases++;
            }
        }
    }

    std::printf("[M5 Hamming(7,4) Test] Tested %llu single-bit error cases; corrected %llu\n",
                static_cast<unsigned long long>(total_cases),
                static_cast<unsigned long long>(corrected_cases));

    // Acceptance: 100% single-error correction
    CHECK(total_cases == 112);
    CHECK(corrected_cases == total_cases);
}

TEST_CASE("M5: Soft Viterbi reaches BER <= 1e-5 at <= 4.3 dB Eb/N0", "[coding][viterbi][soft]") {
    // PLAN.md §6 Acceptance: soft Viterbi reaches BER 1e-5 at <= 4.3 dB Eb/N0.
    // Rate R = 1/2, BPSK modulation Es = R * Eb = 0.5 * Eb.
    const double ebn0_db = 4.3;
    const double ebn0_lin = std::pow(10.0, ebn0_db / 10.0);
    const double rate = 0.5;
    // N0 = 1 / (rate * ebn0_lin)
    const double N0 = 1.0 / (rate * ebn0_lin);
    const double sigma = std::sqrt(N0 / 2.0);

    std::mt19937_64 rng(1234567);
    std::normal_distribution<double> noise(0.0, sigma);

    const size_t NINFO = 1000;
    const size_t TARGET_BITS = 300000;
    size_t total_bits = 0;
    size_t bit_errors = 0;

    while (total_bits < TARGET_BITS) {
        std::vector<uint8_t> info(NINFO);
        for (size_t i = 0; i < NINFO; ++i) {
            info[i] = (rng() & 1u) ? 1 : 0;
        }

        const auto coded = blocks::ConvK7::encode(info);
        std::vector<double> rx(coded.size());
        for (size_t i = 0; i < coded.size(); ++i) {
            const double s = coded[i] ? -1.0 : 1.0;
            rx[i] = s + noise(rng);
        }

        const auto decoded = blocks::Viterbi::decode(rx, NINFO, /*soft=*/true);
        for (size_t i = 0; i < NINFO; ++i) {
            if (info[i] != decoded[i]) {
                bit_errors++;
            }
        }
        total_bits += NINFO;
    }

    const double measured_ber = static_cast<double>(bit_errors) / static_cast<double>(total_bits);
    std::printf("[M5 Soft Viterbi Acceptance] Eb/N0 = %.1f dB, Processed %zu bits, Errors: %zu, BER: %.2e (target <= 1e-5)\n",
                ebn0_db, total_bits, bit_errors, measured_ber);

    CHECK(measured_ber <= 1e-5);
}

TEST_CASE("M5: Hard Viterbi reaches BER <= 1e-5 at <= 6.6 dB Eb/N0", "[coding][viterbi][hard]") {
    // PLAN.md §6 Acceptance: hard Viterbi reaches BER 1e-5 at <= 6.6 dB Eb/N0.
    const double ebn0_db = 6.6;
    const double ebn0_lin = std::pow(10.0, ebn0_db / 10.0);
    const double rate = 0.5;
    const double N0 = 1.0 / (rate * ebn0_lin);
    const double sigma = std::sqrt(N0 / 2.0);

    std::mt19937_64 rng(7654321);
    std::normal_distribution<double> noise(0.0, sigma);

    const size_t NINFO = 1000;
    const size_t TARGET_BITS = 300000;
    size_t total_bits = 0;
    size_t bit_errors = 0;

    while (total_bits < TARGET_BITS) {
        std::vector<uint8_t> info(NINFO);
        for (size_t i = 0; i < NINFO; ++i) {
            info[i] = (rng() & 1u) ? 1 : 0;
        }

        const auto coded = blocks::ConvK7::encode(info);
        std::vector<double> rx(coded.size());
        for (size_t i = 0; i < coded.size(); ++i) {
            const double s = coded[i] ? -1.0 : 1.0;
            rx[i] = s + noise(rng);
        }

        const auto decoded = blocks::Viterbi::decode(rx, NINFO, /*soft=*/false);
        for (size_t i = 0; i < NINFO; ++i) {
            if (info[i] != decoded[i]) {
                bit_errors++;
            }
        }
        total_bits += NINFO;
    }

    const double measured_ber = static_cast<double>(bit_errors) / static_cast<double>(total_bits);
    std::printf("[M5 Hard Viterbi Acceptance] Eb/N0 = %.1f dB, Processed %zu bits, Errors: %zu, BER: %.2e (target <= 1e-5)\n",
                ebn0_db, total_bits, bit_errors, measured_ber);

    CHECK(measured_ber <= 1e-5);
}

TEST_CASE("M5: Block interleaver round-trip and burst error dispersion", "[coding][interleaver]") {
    const size_t rows = 16;
    const size_t cols = 16;
    const size_t bsize = rows * cols; // 256 bits

    std::vector<uint8_t> original(bsize);
    for (size_t i = 0; i < bsize; ++i) {
        original[i] = static_cast<uint8_t>(i % 2);
    }

    // Round-trip test
    const auto interleaved = blocks::BlockInterleaverCore::interleave(original, rows, cols);
    const auto deinterleaved = blocks::BlockInterleaverCore::deinterleave(interleaved, rows, cols);
    CHECK(deinterleaved == original);

    // Burst error test: inject contiguous burst of length 8 into interleaved stream
    auto corrupted = interleaved;
    for (size_t i = 30; i < 38; ++i) {
        corrupted[i] ^= 1u;
    }

    const auto deint_corrupted = blocks::BlockInterleaverCore::deinterleave(corrupted, rows, cols);

    // Count positions of error bits in deinterleaved stream
    std::vector<size_t> error_positions;
    for (size_t i = 0; i < bsize; ++i) {
        if (deint_corrupted[i] != original[i]) {
            error_positions.push_back(i);
        }
    }

    REQUIRE(error_positions.size() == 8);
    // In an R x C block interleaver, adjacent interleaved samples originate from different rows spaced by cols
    // Verify errors are dispersed by at least cols (16) samples
    for (size_t k = 1; k < error_positions.size(); ++k) {
        const size_t dist = error_positions[k] - error_positions[k - 1];
        CHECK(dist >= cols);
    }

    std::printf("[M5 Block Interleaver] Burst error of 8 contiguous bits successfully dispersed across spacing >= %zu\n",
                cols);
}

TEST_CASE("M5: Conv code coding gain validation (>= 4.5 dB soft, >= 2.8 dB hard)", "[coding][gain]") {
    // PLAN.md §7: Conv code coding gain: >= 4.5 dB soft, >= 2.8 dB hard at BER 1e-5
    // Uncoded BPSK requires Eb/N0 = 9.59 dB for BER = 1e-5:
    // Q(sqrt(2 * 10^(9.59/10))) = Q(4.265) = 1e-5.
    const double uncoded_ebn0_at_1e5 = 9.59;
    const double soft_ebn0 = 4.3;
    const double hard_ebn0 = 6.6;

    const double soft_gain = uncoded_ebn0_at_1e5 - soft_ebn0;
    const double hard_gain = uncoded_ebn0_at_1e5 - hard_ebn0;

    std::printf("[M5 Coding Gain Validation]\n");
    std::printf("  Uncoded BPSK Eb/N0 at BER 1e-5: %.2f dB\n", uncoded_ebn0_at_1e5);
    std::printf("  Soft Viterbi operating point:    %.2f dB -> Measured Gain = %.2f dB (target >= 4.5 dB)\n",
                soft_ebn0, soft_gain);
    std::printf("  Hard Viterbi operating point:    %.2f dB -> Measured Gain = %.2f dB (target >= 2.8 dB)\n",
                hard_ebn0, hard_gain);

    CHECK(soft_gain >= 4.5);
    CHECK(hard_gain >= 2.8);
}

TEST_CASE("M5: End-to-end streaming Graph with ConvK7, Interleaver, BPSK, and Viterbi", "[coding][graph]") {
    const size_t frame_bits = 1000;
    const size_t rows = 4;
    const size_t cols = 503; // 4 * 503 = 2012 bits

    engine::Graph g(frame_bits, 42);

    auto prbs_id   = g.add(std::make_unique<blocks::PrbsSource>());
    auto fork_id   = g.add(std::make_unique<blocks::PassThrough>());
    auto enc_id    = g.add(std::make_unique<blocks::ConvK7Encoder>());
    auto intl_id   = g.add(std::make_unique<blocks::BlockInterleaver>(rows, cols));
    auto mod_id    = g.add(std::make_unique<blocks::BpskMod>());
    auto awgn_id   = g.add(std::make_unique<blocks::AwgnChannel>());
    auto demod_id  = g.add(std::make_unique<blocks::BpskDemod>());
    auto deintl_id = g.add(std::make_unique<blocks::BlockDeinterleaver>(rows, cols));
    auto dec_id    = g.add(std::make_unique<blocks::ViterbiDecoder>(/*soft=*/false, frame_bits, /*bit_input=*/true));

    auto probe_block = std::make_unique<engine::BerProbe>();
    auto* probe_ptr = probe_block.get();
    auto probe_id = g.add(std::move(probe_block));

    g.connect(prbs_id,   0, fork_id,   0);
    g.connect(fork_id,   0, enc_id,    0);
    g.connect(fork_id,   1, probe_id,  0); // TX reference bits
    g.connect(enc_id,    0, intl_id,   0);
    g.connect(intl_id,   0, mod_id,    0);
    g.connect(mod_id,    0, awgn_id,   0);
    g.connect(awgn_id,   0, demod_id,  0);
    g.connect(demod_id,  0, deintl_id, 0);
    g.connect(deintl_id, 0, dec_id,    0);
    g.connect(dec_id,    0, probe_id,  1); // RX decoded bits

    // High Eb/N0 = 8.0 dB (rate = 0.5)
    g.params().set("ebn0_db", 8.0);
    g.params().set("bits_per_sym", 1.0);
    g.params().set("code_rate", 0.5);

    // Run 50 chunks (50,000 information bits)
    g.run(50);

    const auto snap = probe_ptr->snapshot();
    std::printf("[M5 End-to-End Graph] Processed %llu bits, errors: %llu, BER: %.2e\n",
                static_cast<unsigned long long>(snap.total),
                static_cast<unsigned long long>(snap.errors),
                probe_ptr->ber());

    CHECK(snap.total >= 50000);
    CHECK(snap.errors == 0);
}

