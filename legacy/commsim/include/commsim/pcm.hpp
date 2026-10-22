#pragma once
#include "common.hpp"
namespace commsim {
// Uniform mid-rise quantiser on [-1,1] with N bits.
inline int pcmEncode(double x, int N) {
    int L = 1 << N;
    int idx = (int)std::floor((x + 1.0) / (2.0 / L));
    return std::clamp(idx, 0, L - 1);
}
inline double pcmDecode(int idx, int N) {
    int L = 1 << N;
    return -1.0 + (idx + 0.5) * (2.0 / L);
}
// Linear delta modulation: 1 bit/sample, fixed step.
struct DeltaMod {
    double step, est = 0;
    explicit DeltaMod(double s) : step(s) {}
    bool encode(double x) { bool b = x >= est; est += b ? step : -step; return b; }
};
}  // namespace commsim
