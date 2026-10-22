/**
 * tests/test_legacy.cpp
 *
 * M0 acceptance test: port all 9 legacy selftest checks to Catch2.
 * Each CHECK() from legacy/commsim/apps/selftest.cpp becomes a
 * separate TEST_CASE so they appear individually in ctest output.
 *
 * Theory references: PLAN.md section 7 (Validation matrix).
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "commsim/cdma.hpp"
#include "commsim/channel.hpp"
#include "commsim/coding.hpp"
#include "commsim/modem.hpp"
#include "commsim/pcm.hpp"

using namespace commsim;

// ---------------------------------------------------------------------------
// Test 1: All modems produce noiseless round-trip bit-exact output
// Theory: demodulate(modulate(bits)) == bits for noiseless AWGN (SNR = inf)
// ---------------------------------------------------------------------------
TEST_CASE("modems: noiseless round trip", "[legacy][modem]") {
    Rng rng(1);
    BPSK b; QPSK q; QAM16 m; BFSK f; OOK o;
    const Modem* ms[] = {&b, &q, &m, &f, &o};
    for (auto* p : ms) {
        Bits t = randomBits(4000, rng);
        INFO("modem: " << p->name());
        REQUIRE(p->demodulate(p->modulate(t)) == t);
    }
}

// ---------------------------------------------------------------------------
// Test 2: All modems normalised to average symbol energy Es = 1
// Theory: Es = E[|s_i|^2] = 1 (normalisation convention)
// ---------------------------------------------------------------------------
TEST_CASE("modems: average Es = 1", "[legacy][modem]") {
    Rng rng(1);
    BPSK b; QPSK q; QAM16 m; BFSK f; OOK o;
    const Modem* ms[] = {&b, &q, &m, &f, &o};
    for (auto* p : ms) {
        auto s = p->modulate(randomBits(400000, rng));
        double e = 0;
        for (auto x : s) e += std::norm(x);
        double avgEs = e / static_cast<double>(s.size());
        INFO("modem: " << p->name() << "  avgEs=" << avgEs);
        CHECK(std::fabs(avgEs - 1.0) < 0.01);
    }
}

// ---------------------------------------------------------------------------
// Test 3: BPSK BER at Eb/N0 = 6 dB within 10% of Q(sqrt(2*Eb/N0))
// Theory: BER_BPSK = Q(sqrt(2*Eb/N0))  [Proakis & Salehi, 4th ed., eq 5.2-57]
// ---------------------------------------------------------------------------
TEST_CASE("BPSK BER at 6 dB within 10% of theory", "[legacy][ber]") {
    Rng rng(1);
    BPSK b;
    const size_t n = 2'000'000;
    double ebn0_lin = db2lin(6.0);
    double N0 = noiseN0(ebn0_lin, 1);
    Bits t = randomBits(n, rng);
    auto s = b.modulate(t);
    awgn(s, N0, rng);
    size_t errs = countErrors(t, b.demodulate(s));
    double simBer   = static_cast<double>(errs) / static_cast<double>(n);
    double theoryBer = b.theoryBer(ebn0_lin);
    INFO("simBer=" << simBer << "  theoryBer=" << theoryBer
         << "  ratio=" << simBer / theoryBer);
    // At least 100 errors for statistical validity
    REQUIRE(errs >= 100);
    CHECK(std::fabs(simBer / theoryBer - 1.0) < 0.10);
}

// ---------------------------------------------------------------------------
// Test 4: CRC-16 detects all single-bit errors in a 64-bit message
// Theory: CRC-16/CCITT detects all single-bit and burst errors <= 16 bits
// ---------------------------------------------------------------------------
TEST_CASE("CRC-16 detects all single-bit errors in 64-bit message", "[legacy][coding]") {
    Rng rng(1);
    Bits msg = randomBits(64, rng);
    uint16_t c0 = crc16(msg.data(), msg.size());
    for (size_t i = 0; i < msg.size(); ++i) {
        Bits x = msg;
        x[i] ^= 1;
        INFO("flip bit " << i);
        CHECK(crc16(x.data(), x.size()) != c0);
    }
}

// ---------------------------------------------------------------------------
// Test 5: CRC-16 detects 9-bit burst errors in a 64-bit message
// Theory: CRC-16 polynomial detects all burst errors of length <= 16 bits
// ---------------------------------------------------------------------------
TEST_CASE("CRC-16 detects 9-bit burst errors in 64-bit message", "[legacy][coding]") {
    Rng rng(1);
    Bits msg = randomBits(64, rng);
    uint16_t c0 = crc16(msg.data(), msg.size());
    for (size_t i = 0; i + 9 < msg.size(); ++i) {
        Bits x = msg;
        for (int k = 0; k < 9; ++k) x[i + k] ^= 1;
        INFO("burst at offset " << i);
        CHECK(crc16(x.data(), x.size()) != c0);
    }
}

// ---------------------------------------------------------------------------
// Test 6: Hamming(7,4) corrects every single-bit error in every codeword position
// Theory: minimum distance d_min = 3; corrects t = floor((3-1)/2) = 1 error
// ---------------------------------------------------------------------------
TEST_CASE("Hamming(7,4) corrects every single-bit error", "[legacy][coding]") {
    Rng rng(1);
    Bits d = randomBits(400, rng);
    Bits c = Hamming74::encode(d);
    for (size_t blk = 0; blk < c.size() / 7; ++blk) {
        for (int j = 0; j < 7; ++j) {
            Bits x = c;
            x[blk * 7 + j] ^= 1;
            INFO("block=" << blk << " bit=" << j);
            CHECK(Hamming74::decode(x) == d);
        }
    }
}

// ---------------------------------------------------------------------------
// Test 7: K=7 convolutional Viterbi decoder: noiseless round trip
// Theory: rate-1/2, K=7 (171,133) code; Viterbi is ML optimal decoder
// ---------------------------------------------------------------------------
TEST_CASE("Conv/Viterbi noiseless round trip", "[legacy][coding]") {
    Rng rng(1);
    Bits d = randomBits(500, rng);
    Bits c = ConvK7::encode(d);
    std::vector<double> rx(c.size());
    for (size_t i = 0; i < c.size(); ++i) rx[i] = c[i] ? -1.0 : 1.0;
    CHECK(ConvK7::decode(rx, 500) == d);
}

// ---------------------------------------------------------------------------
// Test 8: K=7 convolutional Viterbi decoder: corrects 3 scattered hard errors
// Theory: free distance d_free = 10; corrects floor((10-1)/2) = 4 errors
// ---------------------------------------------------------------------------
TEST_CASE("Conv/Viterbi corrects 3 scattered bit errors", "[legacy][coding]") {
    Rng rng(1);
    Bits d = randomBits(500, rng);
    Bits c = ConvK7::encode(d);
    std::vector<double> rx(c.size());
    for (size_t i = 0; i < c.size(); ++i) rx[i] = c[i] ? -1.0 : 1.0;
    // flip 3 bits at scattered positions
    rx[50]  = -rx[50];
    rx[400] = -rx[400];
    rx[800] = -rx[800];
    CHECK(ConvK7::decode(rx, 500) == d);
}

// ---------------------------------------------------------------------------
// Test 9: PCM 8-bit SNR within 0.5 dB of 6.02*N + 1.76
// Theory: SNR_PCM = 6.02*N + 1.76 dB for uniform mid-rise quantiser [Haykin]
// ---------------------------------------------------------------------------
TEST_CASE("PCM 8-bit SNR within 0.5 dB of 6.02N+1.76", "[legacy][pcm]") {
    const int N = 8;
    double ps = 0, pn = 0;
    for (int n = 0; n < 100'000; ++n) {
        double x = std::sin(0.0123456 * 6.2831853 * n);
        double y = pcmDecode(pcmEncode(x, N), N);
        ps += x * x;
        pn += (x - y) * (x - y);
    }
    double snr_dB = 10.0 * std::log10(ps / pn);
    double theory = 6.02 * N + 1.76;
    INFO("SNR=" << snr_dB << " dB  theory=" << theory << " dB"
         << "  delta=" << std::fabs(snr_dB - theory) << " dB");
    CHECK(std::fabs(snr_dB - theory) < 0.5);
}

// ---------------------------------------------------------------------------
// Test (bonus, mirrors selftest structure): Walsh SF=16 orthogonality
// Theory: <w_i, w_j> = N * delta_ij  (Walsh codes are orthonormal after /N)
// ---------------------------------------------------------------------------
TEST_CASE("Walsh codes SF=16 are orthogonal", "[legacy][cdma]") {
    auto W = walsh(16);
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            double d = 0;
            for (int k = 0; k < 16; ++k) d += W[i][k] * W[j][k];
            INFO("i=" << i << " j=" << j << " dot=" << d);
            CHECK(d == (i == j ? 16.0 : 0.0));
        }
    }
}
