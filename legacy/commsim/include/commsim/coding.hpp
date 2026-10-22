#pragma once
#include <limits>
#include "common.hpp"
namespace commsim {

// ---- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) ----
inline uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        c ^= uint16_t(d[i]) << 8;
        for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
    }
    return c;
}

// ---- Hamming (7,4): codeword positions p1 p2 d1 p3 d2 d3 d4 ----
struct Hamming74 {
    static Bits encode(const Bits& in) {  // in.size() multiple of 4
        Bits out;
        out.reserve(in.size() / 4 * 7);
        for (size_t i = 0; i < in.size(); i += 4) {
            uint8_t d1 = in[i], d2 = in[i + 1], d3 = in[i + 2], d4 = in[i + 3];
            out.insert(out.end(), {uint8_t(d1 ^ d2 ^ d4), uint8_t(d1 ^ d3 ^ d4), d1,
                                   uint8_t(d2 ^ d3 ^ d4), d2, d3, d4});
        }
        return out;
    }
    static Bits decode(const Bits& rx) {
        Bits out;
        out.reserve(rx.size() / 7 * 4);
        for (size_t i = 0; i < rx.size(); i += 7) {
            uint8_t c[7];
            for (int j = 0; j < 7; ++j) c[j] = rx[i + j];
            int s1 = c[0] ^ c[2] ^ c[4] ^ c[6];
            int s2 = c[1] ^ c[2] ^ c[5] ^ c[6];
            int s3 = c[3] ^ c[4] ^ c[5] ^ c[6];
            int syn = s1 + 2 * s2 + 4 * s3;
            if (syn) c[syn - 1] ^= 1;
            out.insert(out.end(), {c[2], c[4], c[5], c[6]});
        }
        return out;
    }
};

// ---- Rate-1/2, K=7 convolutional code (171,133 octal), Viterbi decoder ----
struct ConvK7 {
    static constexpr unsigned G0 = 0171, G1 = 0133;
    static int par(unsigned x) { return __builtin_parity(x); }

    // appends 6 zero tail bits -> output length 2*(n+6)
    static Bits encode(const Bits& in) {
        Bits out;
        out.reserve(2 * (in.size() + 6));
        unsigned st = 0;
        auto step = [&](unsigned b) {
            unsigned reg = (b << 6) | st;
            out.push_back(par(reg & G0));
            out.push_back(par(reg & G1));
            st = reg >> 1;
        };
        for (auto b : in) step(b);
        for (int i = 0; i < 6; ++i) step(0);
        return out;
    }
    // rx: antipodal values (bit 0 -> +1, bit 1 -> -1). Pass +-1 for hard decision,
    // raw received samples for soft decision. Squared-Euclidean branch metric.
    static Bits decode(const std::vector<double>& rx, size_t nInfo) {
        const size_t T = nInfo + 6;
        std::vector<uint8_t> prev(T * 64);
        double pm[64], npm[64];
        const double INF = std::numeric_limits<double>::infinity();
        for (int s = 0; s < 64; ++s) pm[s] = INF;
        pm[0] = 0;
        for (size_t t = 0; t < T; ++t) {
            double r0 = rx[2 * t], r1 = rx[2 * t + 1];
            for (int s = 0; s < 64; ++s) npm[s] = INF;
            for (int s = 0; s < 64; ++s) {
                if (pm[s] == INF) continue;
                for (unsigned b = 0; b < 2; ++b) {
                    unsigned reg = (b << 6) | s;
                    double x0 = par(reg & G0) ? -1.0 : 1.0;
                    double x1 = par(reg & G1) ? -1.0 : 1.0;
                    double m = pm[s] + (r0 - x0) * (r0 - x0) + (r1 - x1) * (r1 - x1);
                    unsigned ns = reg >> 1;
                    if (m < npm[ns]) { npm[ns] = m; prev[t * 64 + ns] = uint8_t(s); }
                }
            }
            for (int s = 0; s < 64; ++s) pm[s] = npm[s];
        }
        Bits out(T);
        unsigned cur = 0;  // terminated trellis ends in state 0
        for (size_t t = T; t-- > 0;) {
            out[t] = cur >> 5;
            cur = prev[t * 64 + cur];
        }
        out.resize(nInfo);
        return out;
    }
};
}  // namespace commsim
