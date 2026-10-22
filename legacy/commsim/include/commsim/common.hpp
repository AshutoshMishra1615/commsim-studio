#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

namespace commsim {
using cd   = std::complex<double>;
using Bits = std::vector<uint8_t>;
using Rng  = std::mt19937_64;

inline double Q(double x)          { return 0.5 * std::erfc(x / std::sqrt(2.0)); }
inline double db2lin(double db)    { return std::pow(10.0, db / 10.0); }

inline Bits randomBits(size_t n, Rng& r) {
    Bits b(n);
    size_t i = 0;
    while (i < n) {
        uint64_t w = r();
        for (int k = 0; k < 64 && i < n; ++k, ++i) b[i] = (w >> k) & 1;
    }
    return b;
}
inline size_t countErrors(const Bits& a, const Bits& b) {
    size_t e = 0;
    for (size_t i = 0; i < a.size(); ++i) e += (a[i] != b[i]);
    return e;
}
// run f(i) for i in [0,n) on all hardware threads
template <class F> void parallelFor(int n, F f) {
    unsigned T = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<int> next{0};
    std::vector<std::thread> th;
    for (unsigned t = 0; t < T; ++t)
        th.emplace_back([&] { for (int i; (i = next++) < n;) f(i); });
    for (auto& x : th) x.join();
}
}  // namespace commsim
