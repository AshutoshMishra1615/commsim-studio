#pragma once
#include "common.hpp"
namespace commsim {
// Es = 1 convention. N0 for a given Eb/N0 (linear), k bits/symbol, code rate R.
inline double noiseN0(double ebn0_lin, int k, double R = 1.0) { return 1.0 / (k * R * ebn0_lin); }

// complex AWGN: variance N0/2 per dimension
inline void awgn(std::vector<cd>& x, double N0, Rng& r) {
    std::normal_distribution<double> g(0.0, std::sqrt(N0 / 2.0));
    for (auto& s : x) s += cd(g(r), g(r));
}
}  // namespace commsim
