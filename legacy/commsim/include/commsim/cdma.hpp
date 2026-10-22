#pragma once
#include "common.hpp"
namespace commsim {
// Walsh-Hadamard codes of length n (n = power of 2): H[i][j] = (-1)^popcount(i&j)
inline std::vector<std::vector<double>> walsh(int n) {
    std::vector<std::vector<double>> H(n, std::vector<double>(n));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) H[i][j] = __builtin_parity(i & j) ? -1.0 : 1.0;
    return H;
}
inline std::vector<std::vector<double>> randomCodes(int users, int n, Rng& r) {
    std::vector<std::vector<double>> C(users, std::vector<double>(n));
    for (auto& c : C) for (auto& x : c) x = (r() & 1) ? 1.0 : -1.0;
    return C;
}
}  // namespace commsim
