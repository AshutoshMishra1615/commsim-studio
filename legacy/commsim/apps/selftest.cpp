#include <cstdio>
#include <cstdlib>
#include "commsim/cdma.hpp"
#include "commsim/channel.hpp"
#include "commsim/coding.hpp"
#include "commsim/modem.hpp"
#include "commsim/pcm.hpp"
using namespace commsim;
static int fails = 0;
#define CHECK(c, msg) do { bool _r = (c); printf("[%s] %s\n", _r ? "PASS" : "FAIL", msg); if (!_r) ++fails; } while (0)

int main() {
    Rng rng(1);
    // modem round trips (noiseless)
    BPSK b; QPSK q; QAM16 m; BFSK f; OOK o;
    const Modem* ms[] = {&b, &q, &m, &f, &o};
    bool rt = true;
    for (auto p : ms) { Bits t = randomBits(4000, rng); rt &= (p->demodulate(p->modulate(t)) == t); }
    CHECK(rt, "all modems: noiseless round trip");
    // unit symbol energy
    bool en = true;
    for (auto p : ms) { auto s = p->modulate(randomBits(400000, rng)); double e = 0; for (auto x : s) e += std::norm(x); en &= std::fabs(e / s.size() - 1) < 0.01; }
    CHECK(en, "all modems: average Es = 1");
    // BPSK BER at 6 dB vs theory
    { double N0 = noiseN0(db2lin(6), 1); size_t e = 0, n = 2000000; Bits t = randomBits(n, rng); auto s = b.modulate(t); awgn(s, N0, rng); e = countErrors(t, b.demodulate(s));
      double th = b.theoryBer(db2lin(6)); CHECK(std::fabs(e / (double)n / th - 1) < 0.1, "BPSK BER at 6 dB within 10% of Q(sqrt(2Eb/N0))"); }
    // CRC detects errors
    { Bits msg = randomBits(64, rng); uint16_t c0 = crc16(msg.data(), msg.size()); bool all = true;
      for (size_t i = 0; i < msg.size(); ++i) { Bits x = msg; x[i] ^= 1; all &= crc16(x.data(), x.size()) != c0; }
      for (size_t i = 0; i + 9 < msg.size(); ++i) { Bits x = msg; for (int k = 0; k < 9; ++k) x[i + k] ^= 1; all &= crc16(x.data(), x.size()) != c0; }
      CHECK(all, "CRC-16 detects all single-bit and 9-bit burst errors"); }
    // Hamming corrects any single error per block
    { Bits d = randomBits(400, rng); Bits c = Hamming74::encode(d); bool all = true;
      for (size_t blk = 0; blk < c.size() / 7; ++blk) for (int j = 0; j < 7; ++j) { Bits x = c; x[blk * 7 + j] ^= 1; all &= (Hamming74::decode(x) == d); }
      CHECK(all, "Hamming(7,4) corrects every single-bit error"); }
    // Conv: noiseless and 3 scattered hard errors
    { Bits d = randomBits(500, rng); Bits c = ConvK7::encode(d); std::vector<double> rx(c.size());
      for (size_t i = 0; i < c.size(); ++i) rx[i] = c[i] ? -1 : 1;
      CHECK(ConvK7::decode(rx, 500) == d, "Conv/Viterbi: noiseless round trip");
      rx[50] = -rx[50]; rx[400] = -rx[400]; rx[800] = -rx[800];
      CHECK(ConvK7::decode(rx, 500) == d, "Conv/Viterbi: corrects 3 scattered bit errors"); }
    // PCM SNR ~ 6.02N+1.76
    { int N = 8; double ps = 0, pn = 0; for (int n = 0; n < 100000; ++n) { double x = std::sin(0.0123456 * 6.2831853 * n); double y = pcmDecode(pcmEncode(x, N), N); ps += x * x; pn += (x - y) * (x - y); }
      CHECK(std::fabs(10 * std::log10(ps / pn) - (6.02 * N + 1.76)) < 0.5, "PCM 8-bit SNR within 0.5 dB of 6.02N+1.76"); }
    // Walsh orthogonality
    { auto W = walsh(16); bool ok = true; for (int i = 0; i < 16; ++i) for (int j = 0; j < 16; ++j) { double d = 0; for (int k = 0; k < 16; ++k) d += W[i][k] * W[j][k]; ok &= (d == (i == j ? 16 : 0)); }
      CHECK(ok, "Walsh codes (SF=16) are orthogonal"); }
    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails;
}
