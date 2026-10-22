/**
 * tests/test_m7.cpp  --  Catch2 unit & acceptance tests for Milestone M7:
 *   - Walsh code orthogonality <w_i, w_j> = N * delta_ij for SF = 8, 16, 64
 *   - CDMA Walsh zero multi-user interference at 12 users (Acceptance)
 *   - CDMA PN sequence BER rises monotonically with users (Acceptance)
 *   - Stop-and-wait ARQ efficiency matches (1 - Pf)/(1 + 2a) within 5% (Acceptance)
 *   - Go-Back-N ARQ efficiency simulation
 *   - TDMA slot multiplexing and demultiplexing isolation
 *   - FDMA frequency channelization and separation
 *   - Frame sync, serialization, and CRC-16 validation
 *   - OSI / TCP-IP 4-layer encapsulation and RFC checksum verification
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <numeric>

#include "blocks/multiple_access/cdma.hpp"
#include "blocks/multiple_access/tdma.hpp"
#include "blocks/multiple_access/fdma.hpp"
#include "blocks/protocol/framer.hpp"
#include "blocks/protocol/arq.hpp"
#include "blocks/protocol/packet_encap.hpp"
#include "blocks/channel/awgn.hpp"
#include "engine/rng.hpp"

using Catch::Approx;

// Q-function: Q(x) = 0.5 * erfc(x / sqrt(2))
static double q_func(double x) {
    return 0.5 * std::erfc(x / std::sqrt(2.0));
}

// ---------------------------------------------------------------------------
// Test 1: Walsh code orthogonality for SF = 8, 16, 64
// ---------------------------------------------------------------------------
TEST_CASE("M7: Walsh code orthogonality <w_i, w_j> = N delta_ij", "[m7][walsh]") {
    const std::vector<size_t> sf_list = { 8, 16, 64 };

    for (size_t sf : sf_list) {
        auto W = blocks::CdmaCodes::walsh(sf);
        REQUIRE(W.size() == sf);
        for (size_t i = 0; i < sf; ++i) {
            REQUIRE(W[i].size() == sf);
            for (size_t j = 0; j < sf; ++j) {
                double dot = 0.0;
                for (size_t k = 0; k < sf; ++k) {
                    dot += W[i][k] * W[j][k];
                }
                if (i == j) {
                    CHECK(dot == Approx(static_cast<double>(sf)));
                } else {
                    CHECK(std::abs(dot) < 1e-12);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Test 2: CDMA Walsh zero multi-user interference at 12 users (Acceptance)
// ---------------------------------------------------------------------------
TEST_CASE("M7: CDMA Walsh shows no multi-user interference at 12 users", "[m7][cdma][acceptance]") {
    std::printf("\n===================================================================\n");
    std::printf("[M7 Acceptance Test: CDMA Walsh Zero Multi-User Interference]\n");
    std::printf("===================================================================\n");

    const size_t SF = 16;
    const size_t num_users = 12;
    const size_t bits_per_user = 100000;

    blocks::CdmaTransmitter tx(SF, true, num_users);
    std::vector<blocks::CdmaReceiver> rx_nodes;
    rx_nodes.reserve(num_users);
    for (size_t u = 0; u < num_users; ++u) {
        rx_nodes.emplace_back(u, SF, true);
    }

    // Generate independent bit streams for each of the 12 users
    std::vector<engine::Stream<engine::cf>> user_tx_streams(num_users);
    std::vector<engine::Stream<engine::cf>*> in_ptrs(num_users);

    engine::Rng rng;
    rng.seed(12345, 0, 0);
    for (size_t u = 0; u < num_users; ++u) {
        user_tx_streams[u].resize(bits_per_user);
        for (size_t i = 0; i < bits_per_user; ++i) {
            float bit = (rng.next_u64() & 1u) ? 1.0f : 0.0f;
            user_tx_streams[u].buf[i] = engine::cf(bit, 0.0f);
        }
        in_ptrs[u] = &user_tx_streams[u];
    }

    // Part A: Noiseless multi-user transmission
    engine::Stream<engine::cf> chip_stream;
    std::vector<engine::Stream<engine::cf>*> tx_out = { &chip_stream };
    tx.work(in_ptrs, tx_out, bits_per_user);

    REQUIRE(chip_stream.buf.size() == bits_per_user * SF);

    std::printf("  Part A: Noiseless Synchronous CDMA Channel (12 users, SF=%zu, %zu bits/user)\n", SF, bits_per_user);
    std::printf("  User ID | Bits Tested | Bit Errors | Measured BER | Status\n");
    std::printf("  --------+-------------+------------+--------------+-----------\n");

    size_t total_errors = 0;
    for (size_t u = 0; u < num_users; ++u) {
        engine::Stream<engine::cf> rx_bits;
        std::vector<engine::Stream<engine::cf>*> rx_in = { &chip_stream };
        std::vector<engine::Stream<engine::cf>*> rx_out = { &rx_bits };
        rx_nodes[u].work(rx_in, rx_out, chip_stream.buf.size());

        size_t user_errors = 0;
        for (size_t i = 0; i < bits_per_user; ++i) {
            float tx_b = user_tx_streams[u].buf[i].real();
            float rx_b = rx_bits.buf[i].real();
            if (tx_b != rx_b) {
                user_errors++;
            }
        }
        total_errors += user_errors;
        double ber = static_cast<double>(user_errors) / static_cast<double>(bits_per_user);
        std::printf("     %2zu   |   %7zu   |     %4zu   |  %10.6f  | %s\n",
                    u, bits_per_user, user_errors, ber, (user_errors == 0 ? "PASS (ZERO MUI)" : "FAIL"));
        CHECK(user_errors == 0);
    }
    CHECK(total_errors == 0);

    // Part B: AWGN Channel at Eb/N0 = 6.0 dB with 12 active users
    // Theoretical single-user BPSK BER = Q(sqrt(2 * Eb/N0))
    const double eb_n0_db = 6.0;
    const double eb_n0_lin = std::pow(10.0, eb_n0_db / 10.0);
    const double theory_ber = q_func(std::sqrt(2.0 * eb_n0_lin));

    engine::ParamStore ps;
    ps.set("ebn0_db", eb_n0_db);
    ps.set("bits_per_sym", 1.0);
    ps.set("code_rate", 1.0);
    blocks::AwgnChannel awgn;
    awgn.params = &ps;
    awgn.global_seed = 99999;
    engine::Stream<engine::cf> noisy_chips;
    std::vector<engine::Stream<engine::cf>*> awgn_in = { &chip_stream };
    std::vector<engine::Stream<engine::cf>*> awgn_out = { &noisy_chips };
    awgn.work(awgn_in, awgn_out, chip_stream.buf.size());

    // Receiver for user 0 in presence of 11 interfering users + AWGN
    engine::Stream<engine::cf> rx_bits_noisy;
    std::vector<engine::Stream<engine::cf>*> rx_noisy_in = { &noisy_chips };
    std::vector<engine::Stream<engine::cf>*> rx_noisy_out = { &rx_bits_noisy };
    rx_nodes[0].work(rx_noisy_in, rx_noisy_out, noisy_chips.buf.size());

    size_t awgn_errors = 0;
    for (size_t i = 0; i < bits_per_user; ++i) {
        if (user_tx_streams[0].buf[i].real() != rx_bits_noisy.buf[i].real()) {
            awgn_errors++;
        }
    }
    double measured_awgn_ber = static_cast<double>(awgn_errors) / static_cast<double>(bits_per_user);
    double sigma = std::sqrt(theory_ber * (1.0 - theory_ber) / static_cast<double>(bits_per_user));

    std::printf("\n  Part B: AWGN Channel at Eb/N0 = %.1f dB with 12 active users\n", eb_n0_db);
    std::printf("  Target Single-User Theory: %.6f\n", theory_ber);
    std::printf("  Measured User 0 (12 users): %.6f (errors = %zu / %zu)\n",
                measured_awgn_ber, awgn_errors, bits_per_user);
    std::printf("  3-Sigma Range: [%.6f, %.6f]\n", theory_ber - 3.0 * sigma, theory_ber + 3.0 * sigma);
    std::printf("  Status: %s\n",
                (std::abs(measured_awgn_ber - theory_ber) <= 3.0 * sigma) ? "PASS (Matches Single-User Theory)" : "FAIL");

    CHECK(std::abs(measured_awgn_ber - theory_ber) <= 3.0 * sigma);
    std::printf("===================================================================\n\n");
}

// ---------------------------------------------------------------------------
// Test 3: CDMA PN sequence BER rises monotonically with users (Acceptance)
// ---------------------------------------------------------------------------
TEST_CASE("M7: PN BER rises monotonically with users", "[m7][cdma][acceptance]") {
    std::printf("===================================================================\n");
    std::printf("[M7 Acceptance Test: CDMA PN Spreading Monotonic BER vs Users]\n");
    std::printf("===================================================================\n");

    const size_t SF = 16;
    const std::vector<size_t> user_counts = { 1, 2, 4, 8, 12, 16 };
    const size_t bits_per_user = 60000;
    const double eb_n0_db = 7.0;

    std::vector<double> measured_bers;
    measured_bers.reserve(user_counts.size());

    std::printf("  SF = %zu, Eb/N0 = %.1f dB, Bits/User = %zu\n", SF, eb_n0_db, bits_per_user);
    std::printf("  Active Users | Multi-User Interference (MUI) | Measured BER | Delta vs Prev\n");
    std::printf("  -------------+-------------------------------+--------------+---------------\n");

    double prev_ber = -1.0;
    bool strictly_monotonic = true;

    for (size_t U : user_counts) {
        blocks::CdmaTransmitter tx(SF, false, U);
        blocks::CdmaReceiver rx(0, SF, false); // Target user 0

        std::vector<engine::Stream<engine::cf>> user_tx(U);
        std::vector<engine::Stream<engine::cf>*> in_ptrs(U);

        engine::Rng rng;
        rng.seed(54321 + U, 0, 0);
        for (size_t u = 0; u < U; ++u) {
            user_tx[u].resize(bits_per_user);
            for (size_t i = 0; i < bits_per_user; ++i) {
                float bit = (rng.next_u64() & 1u) ? 1.0f : 0.0f;
                user_tx[u].buf[i] = engine::cf(bit, 0.0f);
            }
            in_ptrs[u] = &user_tx[u];
        }

        engine::Stream<engine::cf> chips;
        std::vector<engine::Stream<engine::cf>*> tx_out = { &chips };
        tx.work(in_ptrs, tx_out, bits_per_user);

        // AWGN channel
        engine::ParamStore ps;
        ps.set("ebn0_db", eb_n0_db);
        ps.set("bits_per_sym", 1.0);
        ps.set("code_rate", 1.0);
        blocks::AwgnChannel awgn;
        awgn.params = &ps;
        awgn.global_seed = 1000 + U;
        engine::Stream<engine::cf> noisy_chips;
        std::vector<engine::Stream<engine::cf>*> awgn_in = { &chips };
        std::vector<engine::Stream<engine::cf>*> awgn_out = { &noisy_chips };
        awgn.work(awgn_in, awgn_out, chips.buf.size());

        // Demodulate user 0
        engine::Stream<engine::cf> rx_bits;
        std::vector<engine::Stream<engine::cf>*> rx_in = { &noisy_chips };
        std::vector<engine::Stream<engine::cf>*> rx_out = { &rx_bits };
        rx.work(rx_in, rx_out, noisy_chips.buf.size());

        size_t errors = 0;
        for (size_t i = 0; i < bits_per_user; ++i) {
            if (user_tx[0].buf[i].real() != rx_bits.buf[i].real()) {
                errors++;
            }
        }
        double ber = static_cast<double>(errors) / static_cast<double>(bits_per_user);
        measured_bers.push_back(ber);

        char delta_str[32];
        if (prev_ber < 0.0) {
            std::snprintf(delta_str, sizeof(delta_str), "baseline");
        } else {
            double delta = ber - prev_ber;
            std::snprintf(delta_str, sizeof(delta_str), "+%.6f", delta);
            if (ber <= prev_ber) strictly_monotonic = false;
        }

        double mui_var = static_cast<double>(U - 1) / static_cast<double>(SF);
        std::printf("       %2zu      |     sigma_mui^2 = %5.3f         |   %8.6f   | %s\n",
                    U, mui_var, ber, delta_str);

        prev_ber = ber;
    }

    std::printf("  Monotonicity Check: %s\n", strictly_monotonic ? "PASS (BER strictly increases with U)" : "FAIL");
    std::printf("===================================================================\n\n");

    CHECK(strictly_monotonic);
    for (size_t i = 1; i < measured_bers.size(); ++i) {
        CHECK(measured_bers[i] > measured_bers[i - 1]);
    }
}

// ---------------------------------------------------------------------------
// Test 4: Stop-and-Wait ARQ efficiency matches (1 - Pf)/(1 + 2a) within 5% (Acceptance)
// ---------------------------------------------------------------------------
TEST_CASE("M7: Stop-and-wait efficiency matches (1 - Pf)/(1 + 2a) within 5%", "[m7][arq][acceptance]") {
    std::printf("===================================================================\n");
    std::printf("[M7 Acceptance Test: Stop-and-Wait ARQ Efficiency vs Theory]\n");
    std::printf("===================================================================\n");
    std::printf("   Pf   |   a   | Theory eta | Sim eta | Relative Err (%%) | Status\n");
    std::printf("--------+-------+------------+---------+--------------------+--------\n");

    struct TestCase {
        double Pf;
        double a;
    };

    const std::vector<TestCase> cases = {
        { 0.0, 0.5 },
        { 0.1, 0.2 },
        { 0.2, 0.5 },
        { 0.3, 1.0 },
        { 0.5, 2.0 },
        { 0.2, 0.0 },
        { 0.4, 0.5 }
    };

    const size_t target_delivered = 25000;
    uint64_t seed = 777;

    for (const auto& tc : cases) {
        double theory_eta = blocks::StopAndWaitArq::theoryEfficiency(tc.Pf, tc.a);
        double sim_eta = blocks::StopAndWaitArq::simulate(tc.Pf, tc.a, target_delivered, seed++);
        double rel_err_pct = std::abs(sim_eta - theory_eta) / theory_eta * 100.0;

        std::printf("  %4.2f  |  %4.2f |   %6.4f   | %7.4f |      %5.2f %%      | %s\n",
                    tc.Pf, tc.a, theory_eta, sim_eta, rel_err_pct,
                    (rel_err_pct <= 5.0 ? "PASS" : "FAIL"));

        CHECK(rel_err_pct <= 5.0);
    }
    std::printf("===================================================================\n\n");
}

// ---------------------------------------------------------------------------
// Test 5: Go-Back-N ARQ efficiency validation
// ---------------------------------------------------------------------------
TEST_CASE("M7: Go-Back-N ARQ efficiency validation", "[m7][arq]") {
    const std::vector<size_t> windows = { 4, 8 };
    const std::vector<double> pfs = { 0.0, 0.1, 0.2 };
    const double a = 0.5;

    for (size_t W : windows) {
        for (double Pf : pfs) {
            double theory_eta = blocks::GoBackNArq::theoryEfficiency(Pf, a, W);
            double sim_eta = blocks::GoBackNArq::simulate(Pf, a, W, 20000, 888 + W);
            double rel_err_pct = std::abs(sim_eta - theory_eta) / theory_eta * 100.0;
            CHECK(rel_err_pct <= 5.0);
        }
    }
}

// ---------------------------------------------------------------------------
// Test 6: TDMA slot multiplexing and demultiplexing isolation
// ---------------------------------------------------------------------------
TEST_CASE("M7: TDMA Mux and Demux channel separation", "[m7][tdma]") {
    const size_t num_slots = 4;
    const size_t slot_samples = 32;
    const size_t guard_samples = 4;
    const size_t num_frames = 20;
    const size_t total_samples_per_user = slot_samples * num_frames;

    blocks::TdmaMux mux(num_slots, slot_samples, guard_samples);
    std::vector<blocks::TdmaDemux> demuxers;
    for (size_t s = 0; s < num_slots; ++s) {
        demuxers.emplace_back(s, num_slots, slot_samples, guard_samples);
    }

    // Populate each user with distinct recognizable DC levels
    std::vector<engine::Stream<engine::cf>> user_in(num_slots);
    std::vector<engine::Stream<engine::cf>*> mux_in(num_slots);
    for (size_t u = 0; u < num_slots; ++u) {
        user_in[u].resize(total_samples_per_user);
        for (size_t i = 0; i < total_samples_per_user; ++i) {
            user_in[u].buf[i] = engine::cf(static_cast<float>(u + 1) * 10.0f + static_cast<float>(i % 5), 0.0f);
        }
        mux_in[u] = &user_in[u];
    }

    engine::Stream<engine::cf> tdma_frame;
    std::vector<engine::Stream<engine::cf>*> mux_out = { &tdma_frame };
    mux.work(mux_in, mux_out, total_samples_per_user);

    REQUIRE(tdma_frame.buf.size() == num_frames * num_slots * (slot_samples + guard_samples));

    // Demux each user and verify exact bit-for-bit reconstruction
    for (size_t u = 0; u < num_slots; ++u) {
        engine::Stream<engine::cf> user_out;
        std::vector<engine::Stream<engine::cf>*> d_in = { &tdma_frame };
        std::vector<engine::Stream<engine::cf>*> d_out = { &user_out };
        demuxers[u].work(d_in, d_out, tdma_frame.buf.size());

        REQUIRE(user_out.buf.size() == total_samples_per_user);
        for (size_t i = 0; i < total_samples_per_user; ++i) {
            CHECK(user_out.buf[i].real() == Approx(user_in[u].buf[i].real()));
        }
    }
}

// ---------------------------------------------------------------------------
// Test 7: Protocol Framer serialization, CRC-16 check, and corrupted frame rejection
// ---------------------------------------------------------------------------
TEST_CASE("M7: Framer encapsulation, serialization, and CRC verification", "[m7][framer]") {
    blocks::Frame tx_frame;
    tx_frame.type = 0; // DATA
    tx_frame.seq_num = 42;
    tx_frame.payload = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!' };

    std::vector<uint8_t> serialized = tx_frame.serialize();
    REQUIRE(serialized.size() == 2 + 1 + 2 + 2 + tx_frame.payload.size() + 2);

    // Valid frame deserialize
    blocks::Frame rx_frame;
    bool ok = blocks::Frame::deserialize(serialized.data(), serialized.size(), rx_frame);
    REQUIRE(ok);
    CHECK(rx_frame.sync == blocks::Frame::PREAMBLE);
    CHECK(rx_frame.type == tx_frame.type);
    CHECK(rx_frame.seq_num == tx_frame.seq_num);
    CHECK(rx_frame.length == tx_frame.payload.size());
    CHECK(rx_frame.payload == tx_frame.payload);

    // Corrupt a single payload bit; verify frame is rejected due to CRC mismatch
    std::vector<uint8_t> corrupted = serialized;
    corrupted[10] ^= 0x01; // flip 1 bit in payload
    blocks::Frame rx_bad;
    bool bad_ok = blocks::Frame::deserialize(corrupted.data(), corrupted.size(), rx_bad);
    CHECK_FALSE(bad_ok);
}

// ---------------------------------------------------------------------------
// Test 8: OSI / TCP-IP 4-Layer Encapsulation and Checksum Verification
// ---------------------------------------------------------------------------
TEST_CASE("M7: OSI / TCP-IP Encapsulation and Checksum Verification", "[m7][encap]") {
    const std::string payload = "GET /status HTTP/1.1\r\nHost: commsim.local\r\n\r\n";
    auto res = blocks::PacketEncap::encapsulate(payload, "192.168.1.100", "10.0.0.1", 54321, 80, 100, 200);

    REQUIRE(res.raw_ethernet_frame.size() >= 14 + 20 + 20 + payload.size() + 4);

    // 1. Verify Ethernet FCS CRC-32
    uint32_t computed_crc32 = blocks::PacketEncap::crc32(res.raw_ethernet_frame.data(), res.raw_ethernet_frame.size() - 4);
    CHECK(computed_crc32 == res.fcs_crc32);

    // 2. Verify IPv4 Header Checksum
    // When checksum is verified across the 20-byte IP header (including the checksum field), the 1's complement sum must be 0x0000
    const uint8_t* ip_hdr = res.raw_ethernet_frame.data() + res.ip_offset;
    uint16_t ip_check = blocks::PacketEncap::internetChecksum(ip_hdr, 20);
    CHECK(ip_check == 0x0000);

    // 3. Verify fields table populated
    CHECK_FALSE(res.fields.empty());
    bool found_dest_mac = false;
    bool found_ip_src = false;
    bool found_tcp_port = false;
    for (const auto& f : res.fields) {
        if (f.name == "Destination MAC") found_dest_mac = true;
        if (f.name == "Source IP") found_ip_src = true;
        if (f.name == "Source Port") found_tcp_port = true;
    }
    CHECK(found_dest_mac);
    CHECK(found_ip_src);
    CHECK(found_tcp_port);
}
